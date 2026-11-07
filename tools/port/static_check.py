#!/usr/bin/env python3
"""Static analysis for Paper Cut: clang's static analyzer, clang-tidy and
the compiler's memory-safety warnings, diffed against a stored baseline so CI can flag only new warnings.

Needs a configured build directory with compile_commands.json and the
pre-compile generated sources (the sjis/ copies and generated/ headers):

  cmake -S . -B build-pc-us-headless -G Ninja \
      --toolchain cmake/toolchains/windows-clang.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTTYD_VERSION=G8ME01 \
      -DTTYD_BACKEND=headless -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  python tools/port/static_check.py --build build-pc-us-headless --gen

Then:
  python tools/port/static_check.py --build build-pc-us-headless \
      [--build build-pc-us-aurora]
      runs the three passes over port/, src/, rels/*/src and libs/, writes the
      full findings to <build>/static_check.json and prints the warnings
      that are not in the baseline (exit 1 when there are any).
  --update-baseline   rewrite tools/port/static_check_baseline.txt
  --only analyzer|tidy|warn, --files <substring>..., -j N
  --cache DIR         keep per-file results to resume a stopped run; it is
                      keyed by the compile command, not the sources, so
                      don't reuse it across edits
  --build A --build B the first build dir is the main one; later ones only
                      add files it lacks (port/src/aurora from an Aurora
                      configure)

Findings are keyed by file, check, message and the text of the flagged
source line (not the line number), so unrelated edits don't churn the
baseline. Fixed warnings that are still in the baseline are listed as
"gone" so the baseline can be tightened.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shlex
import subprocess
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE = os.path.join(ROOT, 'tools', 'port', 'static_check_baseline.txt')

# The analyzer: default checkers plus everything under these packages.
ANALYZER_CHECKERS = ['core', 'unix', 'deadcode', 'security']
# security.insecureAPI.* flags every memcpy/sprintf/strcpy by name; the
# fixed-buffer cases are found by bugprone-not-null-terminated-result and
# the analyzer's own bounds checks instead.
ANALYZER_DISABLED = [
    'security.insecureAPI.DeprecatedOrUnsafeBufferHandling',
    'security.insecureAPI.strcpy',
    'security.insecureAPI.rand',
]

TIDY_CHECKS = ','.join([
    '-*',
    'bugprone-*',
    'cert-err34-c',
    'misc-redundant-expression',
    'readability-misleading-indentation',
    # Noise on decompiled C: style, naming, C++-only, or flagged on
    # nearly every function of the matching code.
    '-bugprone-easily-swappable-parameters',
    '-bugprone-reserved-identifier',
    '-bugprone-narrowing-conversions',
    '-bugprone-implicit-widening-of-multiplication-result',
    '-bugprone-macro-parentheses',
    '-bugprone-branch-clone',
    '-bugprone-assignment-in-if-condition',
    '-bugprone-assignment-in-selection-statement',
    '-bugprone-casting-through-void',
    '-bugprone-multi-level-implicit-pointer-conversion',
    '-bugprone-switch-missing-default-case',
    '-bugprone-signed-char-misuse',
    '-bugprone-signed-bitwise',
    '-bugprone-integer-division',
    '-bugprone-suspicious-include',
    '-bugprone-tagged-union-member-count',
    '-bugprone-unsafe-functions',
    '-bugprone-inc-dec-in-conditions',
    '-bugprone-suspicious-string-compare',
    '-bugprone-unhandled-code-paths',
    '-bugprone-multiple-statement-macro',
    '-bugprone-unused-return-value',
    '-bugprone-invalid-enum-default-initialization',
    # clang-analyzer-* is the same engine as the dedicated analyzer run
    # (core, unix, deadcode, security and the defaults); running it twice
    # per file only doubles the time.
    '-clang-analyzer-*',
])

# The compiler's own memory-safety warnings, which the build silences on
# decompiled code (-w): fixed-buffer sprintf/strcpy overflows, constant
# out-of-bounds indexes, uninitialized reads, missing returns, format
# mismatches (pointers through %d/%x are a 64-bit hazard).
WARNINGS = [
    '-Wfortify-source', '-Warray-bounds', '-Wsizeof-pointer-memaccess',
    '-Wsizeof-array-argument', '-Wsizeof-pointer-div', '-Wstring-plus-int',
    '-Wformat', '-Wformat-insufficient-args', '-Wuninitialized',
    '-Wsometimes-uninitialized', '-Wdivision-by-zero', '-Wreturn-type',
    '-Wshift-count-overflow', '-Wshift-count-negative', '-Wmemset-transposed-args',
    '-Wsuspicious-memaccess', '-Wunsequenced', '-Wself-assign',
    '-Wtautological-overlap-compare', '-Wstrlcpy-strlcat-size', '-Wstrncat-size',
    '-Wvoid-pointer-to-int-cast', '-Wpointer-to-int-cast', '-Wint-to-pointer-cast',
    '-Warray-parameter',
]

SCOPE = re.compile(r'^(port/|src/|libs/|rels/[^/]+/src/)')
DIAG = re.compile(r'^(?P<file>(?:[A-Za-z]:)?[^:]+):(?P<line>\d+):(?P<col>\d+): '
                  r'(?P<sev>warning|error|fatal error): (?P<msg>.*?)(?: \[(?P<check>[^\]]+)\])?$')


def norm_path(path, build):
    p = os.path.abspath(path).replace('\\', '/')
    root = ROOT.replace('\\', '/')
    b = os.path.abspath(build).replace('\\', '/')
    # The build compiles UTF-8 copies of Shift-JIS sources from <build>/sjis/.
    for pre in (b + '/sjis/', b + '/statics/sjis/', b + '/statics/'):
        if p.lower().startswith(pre.lower()):
            return p[len(pre):]
    if p.lower().startswith(root.lower() + '/'):
        return p[len(root) + 1:]
    return p


def load_db(build, seen_rel=None):
    with open(os.path.join(build, 'compile_commands.json'), encoding='utf-8') as f:
        db = json.load(f)
    seen, out = set(), []
    for e in db:
        rel = norm_path(os.path.join(e['directory'], e['file']), build)
        if not SCOPE.match(rel) or not rel.endswith(('.c', '.cpp', '.cc')):
            continue
        key = e.get('command') or ' '.join(e.get('arguments', []))
        if (rel, key) in seen or (seen_rel is not None and rel in seen_rel):
            continue
        seen.add((rel, key))
        e['rel'] = rel
        e['build'] = build
        out.append(e)
    if seen_rel is not None:
        seen_rel.update(e['rel'] for e in out)
    return out


def split_cmd(e):
    if 'arguments' in e:
        return list(e['arguments'])
    if os.name == 'nt':
        return windows_split(e['command'])
    return shlex.split(e['command'])


def windows_split(cmd):
    """CommandLineToArgvW rules: backslashes are literal unless before a quote."""
    args, cur, inq, i, have = [], [], False, 0, False
    while i < len(cmd):
        c = cmd[i]
        if c == '\\':
            n = 0
            while i < len(cmd) and cmd[i] == '\\':
                n += 1
                i += 1
            if i < len(cmd) and cmd[i] == '"':
                cur.append('\\' * (n // 2))
                if n % 2:
                    cur.append('"')
                    i += 1
                have = True
            else:
                cur.append('\\' * n)
                have = True
            continue
        if c == '"':
            inq = not inq
            have = True
        elif c in ' \t' and not inq:
            if have:
                args.append(''.join(cur))
                cur, have = [], False
        else:
            cur.append(c)
            have = True
        i += 1
    if have:
        args.append(''.join(cur))
    return args


def strip_output(args):
    """Drop -o <obj>, -c, warning suppression and the source path."""
    out, skip = [], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a in ('-c', '-w') or a.startswith('-Wno-everything') or a.startswith('-Werror'):
            continue
        out.append(a)
    return args[0], out


def run_analyzer(e, build):
    cc, args = strip_output(split_cmd(e))
    src = os.path.join(e['directory'], e['file'])
    args = [a for a in args if os.path.normcase(os.path.abspath(os.path.join(e['directory'], a)))
            != os.path.normcase(os.path.abspath(src))]
    flags = ['--analyze', '--analyzer-output', 'text', '-Wno-everything',
             '-Xclang', '-analyzer-checker=' + ','.join(ANALYZER_CHECKERS)]
    for d in ANALYZER_DISABLED:
        flags += ['-Xclang', '-analyzer-disable-checker=' + d]
    flags += ['-Xclang', '-analyzer-config', '-Xclang', 'aggressive-binary-operation-simplification=true']
    cmd = [cc] + args + flags + ['-o', os.devnull, src]
    p = run(cmd, e)
    return parse(p.stdout + p.stderr, 'analyzer', build, e)


def run_warn(e, build):
    cc, args = strip_output(split_cmd(e))
    src = os.path.join(e['directory'], e['file'])
    args = [a for a in args if os.path.normcase(os.path.abspath(os.path.join(e['directory'], a)))
            != os.path.normcase(os.path.abspath(src))]
    cmd = [cc] + args + ['-fsyntax-only', '-Wno-everything'] + WARNINGS + [src]
    p = run(cmd, e)
    return parse(p.stdout + p.stderr, 'warn', build, e)


def run_tidy(e, build, tidy):
    src = os.path.join(e['directory'], e['file'])
    cmd = [tidy, '-p', build, '--quiet', '--checks=' + TIDY_CHECKS,
           '--header-filter=.*', '--system-headers=false',
           '--extra-arg=-Wno-everything', src]
    p = run(cmd, e)
    return parse(p.stdout + p.stderr, 'tidy', build, e)


def run(cmd, e):
    try:
        return subprocess.run(cmd, cwd=e['directory'], capture_output=True, text=True,
                              encoding='utf-8', errors='replace', timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        print(f"timeout: {cmd[0]} {e['rel']}", file=sys.stderr)
        return subprocess.CompletedProcess(cmd, 1, '', '')


TIMEOUT = 1200
_lines = {}


def line_text(path, n):
    if path not in _lines:
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                _lines[path] = f.read().split('\n')
        except OSError:
            _lines[path] = []
    ls = _lines[path]
    return ' '.join(ls[n - 1].split()) if 0 < n <= len(ls) else ''


def parse(text, tool, build, e):
    out = []
    for raw in text.splitlines():
        m = DIAG.match(raw.strip())
        if not m:
            continue
        check = m.group('check') or ''
        msg = m.group('msg')
        if m.group('sev') != 'warning':
            # A TU that doesn't compile: reported once, by the warn pass.
            if tool != 'warn':
                continue
            check = 'error'
        if tool == 'tidy' and check.startswith('clang-diagnostic'):
            continue  # compiler warnings: the warn pass's job
        if tool == 'analyzer':
            check = 'analyzer:' + (check or 'core')
        elif tool == 'warn':
            check = 'warn:' + (check.split(',')[0] or 'error')
        if not check.startswith(('analyzer:', 'warn:')) and m.group('sev') != 'warning':
            check = 'warn:error'
        path = os.path.join(e['directory'], m.group('file'))
        rel = norm_path(path, build)
        if not SCOPE.match(rel):
            continue
        ln = int(m.group('line'))
        src = path if os.path.exists(path) else os.path.join(ROOT, rel)
        out.append({'file': rel, 'line': ln, 'col': int(m.group('col')), 'check': check,
                    'msg': msg, 'text': line_text(src, ln), 'tu': e['rel']})
    return out


def key(f):
    # (clang-tidy names C++ lambdas by absolute path and line: machine- and edit-specific)
    msg = re.sub(r'\(lambda at [^)]*\)', '(lambda)', f['msg'])
    return '\t'.join([f['file'], f['check'], msg, f['text']])


def dedupe(findings):
    seen, out = set(), []
    for f in findings:
        k = (f['file'], f['line'], f['col'], f['check'], f['msg'])
        if k in seen:
            continue
        seen.add(k)
        out.append(f)
    out.sort(key=lambda f: (f['file'], f['line'], f['col'], f['check']))
    return out


def gen(build):
    with open(os.path.join(build, 'build.ninja'), encoding='utf-8', errors='replace') as f:
        targets = re.findall(r'^build (cmake_object_order_depends_target_\S+?):', f.read(), re.M)
    subprocess.check_call(['ninja', '-C', build, '-j', str(os.cpu_count() // 2 or 1)] + targets)


def cached(cache, tool, e, fn, *args):
    if not cache:
        return fn(*args)
    import hashlib
    h = hashlib.sha1('|'.join([tool, e['rel'], e.get('command') or ' '.join(e.get('arguments', [])),
                                  TIDY_CHECKS, ' '.join(WARNINGS + ANALYZER_CHECKERS)]).encode()).hexdigest()
    path = os.path.join(cache, h + '.json')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    r = fn(*args)
    os.makedirs(cache, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(r, f)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', action='append',
                    help='build dir(s); later ones add only the files the first lacks '
                         '(e.g. headless, then aurora for port/src/aurora)')
    ap.add_argument('--cache', help='reuse per-file results from this dir (resume a stopped run)')
    ap.add_argument('--gen', action='store_true', help='build the generated sources first')
    ap.add_argument('--only', choices=['analyzer', 'tidy', 'warn'])
    ap.add_argument('--files', nargs='*', help='only TUs whose path contains one of these')
    ap.add_argument('-j', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--clang-tidy', default='clang-tidy')
    ap.add_argument('--baseline', default=BASELINE)
    ap.add_argument('--update-baseline', action='store_true')
    ap.add_argument('--out', help='findings JSON (default <build>/static_check.json)')
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    builds = [os.path.abspath(b) for b in (a.build or [os.path.join(ROOT, 'build-pc-us-headless')])]
    build = builds[0]
    if a.gen:
        gen(build)
    db, seen_rel = [], set()
    for b in builds:
        db += load_db(b, seen_rel)
    if a.files:
        db = [e for e in db if any(s in e['rel'] for s in a.files)]
    jobs = []
    with cf.ThreadPoolExecutor(a.j) as ex:
        for e in db:
            if a.only in (None, 'analyzer'):
                jobs.append(ex.submit(cached, a.cache, 'analyzer', e, run_analyzer, e, e['build']))
            if a.only in (None, 'tidy'):
                jobs.append(ex.submit(cached, a.cache, 'tidy', e, run_tidy, e, e['build'], a.clang_tidy))
            if a.only in (None, 'warn'):
                jobs.append(ex.submit(cached, a.cache, 'warn', e, run_warn, e, e['build']))
        findings, done = [], 0
        for j in cf.as_completed(jobs):
            findings += j.result()
            done += 1
            if done % 100 == 0:
                print(f'  {done}/{len(jobs)}', file=sys.stderr, flush=True)
    findings = dedupe(findings)
    out = a.out or os.path.join(build, 'static_check.json')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(findings, f, indent=1)
    byc = Counter(f['check'] for f in findings)
    print(f'{len(findings)} warnings in {len(db)} translation units ({out})')
    for c, n in byc.most_common():
        print(f'  {n:6} {c}')

    cur = Counter(key(f) for f in findings)
    if a.update_baseline:
        with open(a.baseline, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# static_check.py baseline: file<TAB>check<TAB>message<TAB>source line [<TAB>count]\n')
            for k in sorted(cur):
                f.write(k + ('\t#%d' % cur[k] if cur[k] > 1 else '') + '\n')
        print(f'baseline written: {len(cur)} entries')
        return 0
    base = Counter()
    if os.path.exists(a.baseline):
        with open(a.baseline, encoding='utf-8') as f:
            for ln in f:
                ln = ln.rstrip('\n')
                if not ln or ln.startswith('#'):
                    continue
                n = 1
                m = re.search(r'\t#(\d+)$', ln)
                if m:
                    n, ln = int(m.group(1)), ln[:m.start()]
                base[ln] += n
    tus = {e['rel'] for e in db}

    def scoped(k):
        # A partial run (--files/--only) only reports "gone" for what it ran.
        f, check = k.split('\t')[:2]
        tool = check.split(':')[0] if check.startswith(('analyzer:', 'warn:')) else 'tidy'
        if a.only and tool != a.only:
            return False
        return not a.files or f in tus or any(s in f for s in a.files)
    new = cur - base
    gone = Counter({k: v for k, v in (base - cur).items() if scoped(k)})
    for k in sorted(gone):
        print('gone: ' + k.replace('\t', ' | '))
    for f in findings:
        k = key(f)
        if new[k] > 0:
            new[k] -= 1
            print(f"NEW: {f['file']}:{f['line']}:{f['col']}: {f['msg']} [{f['check']}]")
    n_new = sum((cur - base).values())
    print(f'{n_new} new, {sum(gone.values())} gone vs baseline')
    return 1 if n_new else 0


if __name__ == '__main__':
    sys.exit(main())
