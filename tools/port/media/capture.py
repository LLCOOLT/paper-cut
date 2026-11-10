#!/usr/bin/env python3
"""README and trailer media from a scenario: run it, collect its burst of player screenshots, encode, clean up.

  (F: a game frame, or @NAME: from the frame the scenario's input script says `shot NAME`)
  capture.py gif   SCENARIO OUT.gif --start F --count N [--step 5] [--size 640x360] [--window 1280x720]
  capture.py mp4   SCENARIO OUT.mp4 --start F --count N [--step 1] [--fps 60] [--window 1920x1080] [--crf 18]
  capture.py still SCENARIO OUT.jpg --frame F [--window 1280x720] [--quality 88]
  capture.py encode-gif DIR OUT.gif ...   /   encode-mp4 DIR OUT.mp4 ...   (a burst already on disk; kept)

Each capture runs SCENARIO (tests/port/testpass) once on the US Aurora build, offscreen, through testpass.py with
  --clean-frame                 nothing over the picture the shot doesn't ask for (TTYD_CLEAN_FRAME, section 1.4)
  TTYD_USER_SHOT=F:N:S          the burst: N frames, one every S game frames from F, the whole window as shown
                                (post-effects and ImGui included) at the window's size (port/src/aurora/shot_aurora.c)
  window_w, window_h            --window; widescreen=1 and the shot plan's README look unless --set says otherwise
then encodes the burst (QOI files, lossless) and deletes it. More settings or game environment: --set K=V, --env K=V
(repeatable). The burst goes to --scratch (default local-docs/media_burst/<name>), one folder at a time.

Encodings (local-docs/release/SHOT_PLAN_0.5.0.md, section 1.1):
  gif    Pillow, one palette for the clip (made from frames across it), no dithering (--dither none, menus) or
         Floyd-Steinberg (--dither light, 3D scenes); 80 ms a frame (12.5 fps: one capture every 5 game frames);
         then gifsicle -O3 --lossy=40 (--lossy N)
  mp4    ffmpeg, libx264 High, CRF (--crf; 24-26 for the README tour, 18 for the trailer, 14 for its
         intermediates), yuv420p, +faststart, no sound track; --size scales (Lanczos)
  still  JPEG quality 88 (--quality), scaled to --size when given
Tools: ffmpeg ($PAPERCUT_FFMPEG, else on PATH, else WinGet's link), gifsicle ($PAPERCUT_GIFSICLE, else on PATH, else
C:/Users/LT/tools/gifsicle); Pillow in the Python that runs this one.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
TESTPASS = os.path.join(REPO, "tools", "port", "testpass.py")
OUT_ROOT = os.path.join(REPO, "local-docs", "testpass")
# the README look (shot plan 1.2) under every capture; a scenario's settings and --set go over it
README_LOOK = {"widescreen": 1, "resolution": 3, "msaa": 0, "aniso": 4, "bloom": 0, "rt_lighting": 0,
               "paper_shadows": 1}


def tool(name, env, fallbacks):
    p = os.environ.get(env) or shutil.which(name)
    for f in fallbacks:
        if not p and os.path.exists(f):
            p = f
    if not p:
        sys.exit(f"capture.py: no {name} (set ${env})")
    return p


def ffmpeg():
    return tool("ffmpeg", "PAPERCUT_FFMPEG",
                [os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Links\ffmpeg.exe")])


def gifsicle():
    return tool("gifsicle", "PAPERCUT_GIFSICLE", [r"C:\Users\LT\tools\gifsicle\gifsicle.exe"])


def size_of(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def burst_files(folder):
    return sorted(glob.glob(os.path.join(folder, "burst_*.qoi")) + glob.glob(os.path.join(folder, "burst_*.bmp")) +
                  glob.glob(os.path.join(folder, "burst_*.png")))


def run_burst(a, spec, count, folder):
    """the scenario played with the burst asked for; its frames' files (in order) and the burst's own report"""
    if os.path.isdir(folder):
        for f in burst_files(folder):
            os.remove(f)
    os.makedirs(folder, exist_ok=True)
    run = "media_" + re.sub(r"[^a-z0-9_]+", "_", os.path.splitext(os.path.basename(a.out))[0].lower())
    w, h = size_of(a.window)
    sets = {**README_LOOK, "window_w": w, "window_h": h}
    sets.update(dict(kv.split("=", 1) for kv in (a.set or [])))
    cmd = [sys.executable, TESTPASS, "run", a.scenario, "--run", run, "--versions", a.version, "--jobs", "1",
           "--clean-frame", "--env", f"TTYD_USER_SHOT={spec}", "--env", f"TTYD_USER_SHOT_DIR={folder}",
           "--env", f"TTYD_USER_SHOT_THREADS={a.threads}"]
    for k, v in sets.items():
        cmd += ["--set", f"{k}={v}"]
    for kv in a.env or []:
        cmd += ["--env", kv]
    if a.tree:
        cmd += ["--tree", a.tree]
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    log = os.path.join(OUT_ROOT, run, a.scenario, a.version, "log.txt")
    report = ""
    if os.path.exists(log):
        for line in open(log, encoding="utf-8", errors="replace"):
            if line.startswith("[burst]") and ("done" in line or "cannot" in line or "no picture" in line):
                report += line
    files = burst_files(folder)
    print(f"capture.py: {a.scenario}: {len(files)} of {count} frames in {time.time() - t0:.0f} s "
          f"(testpass exit {r.returncode}; log {log})")
    if report:
        print(report.rstrip())
    if len(files) != count:
        print((r.stdout + r.stderr)[-2000:])
        sys.exit(f"capture.py: the burst has {len(files)} frames, not {count}")
    return files


def load(path):
    from PIL import Image
    im = Image.open(path)
    im.load()
    return im.convert("RGB")


def encode_gif(files, out, a):
    from PIL import Image
    w, h = size_of(a.size)
    frames = [load(f).resize((w, h), Image.LANCZOS) for f in files]
    # one palette for the whole clip: from up to 16 frames across it, side by side
    picks = frames[:: max(1, len(frames) // 16)][:16]
    sheet = Image.new("RGB", (w * len(picks), h))
    for i, f in enumerate(picks):
        sheet.paste(f, (i * w, 0))
    palette = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    dither = Image.Dither.NONE if a.dither == "none" else Image.Dither.FLOYDSTEINBERG
    q = [f.quantize(palette=palette, dither=dither) for f in frames]
    raw = out + ".pillow.gif"
    q[0].save(raw, save_all=True, append_images=q[1:], duration=a.delay, loop=0, optimize=False, disposal=1)
    subprocess.run([gifsicle(), "-O3", f"--lossy={a.lossy}", "--no-warnings", "-o", out, raw], check=True)
    os.remove(raw)
    print(f"capture.py: {out}: {len(frames)} frames, {w}x{h}, {a.delay} ms, {os.path.getsize(out) / 1e6:.2f} MB")


def encode_mp4(files, out, a):
    first = os.path.basename(files[0])
    ext = os.path.splitext(first)[1]
    start = int(re.search(r"(\d+)", first).group(1))
    vf = []
    if a.size:
        w, h = size_of(a.size)
        vf = ["-vf", f"scale={w}:{h}:flags=lanczos"]
    cmd = [ffmpeg(), "-hide_banner", "-loglevel", "error", "-y", "-framerate", str(a.fps), "-start_number", str(start),
           "-i", os.path.join(os.path.dirname(files[0]), "burst_%05d" + ext), "-frames:v", str(len(files))] + vf + [
           "-c:v", "libx264", "-profile:v", "high", "-preset", a.preset, "-crf", str(a.crf), "-pix_fmt", "yuv420p",
           "-movflags", "+faststart", "-an", out]
    subprocess.run(cmd, check=True)
    print(f"capture.py: {out}: {len(files)} frames at {a.fps} fps, CRF {a.crf}, {os.path.getsize(out) / 1e6:.2f} MB")


def encode_still(files, out, a):
    from PIL import Image
    im = load(files[0])
    if a.size:
        im = im.resize(size_of(a.size), Image.LANCZOS)
    im.save(out, quality=a.quality, optimize=True, progressive=True, subsampling=0)
    print(f"capture.py: {out}: {im.size[0]}x{im.size[1]}, {os.path.getsize(out) / 1e3:.0f} KB")


def drop(folder, files):
    for f in files:
        os.remove(f)
    try:
        os.rmdir(folder)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("gif", "mp4", "still", "encode-gif", "encode-mp4"):
        p = sub.add_parser(name)
        p.add_argument("scenario", help="the scenario (or, for encode-*, the burst's folder)")
        p.add_argument("out")
        if name in ("gif", "mp4"):
            p.add_argument("--start", required=True,
                           help="the first game frame (retrace count), or @NAME: the frame the input script's "
                                "`shot NAME` comes")
            p.add_argument("--count", type=int, required=True, help="frames captured")
            p.add_argument("--step", type=int, default=5 if name == "gif" else 1)
        if name == "still":
            p.add_argument("--frame", required=True, help="a game frame, or @NAME (as --start)")
            p.add_argument("--quality", type=int, default=88)
        if name in ("gif", "encode-gif"):
            p.add_argument("--size", default="640x360")
            p.add_argument("--delay", type=int, default=80, help="ms a frame")
            p.add_argument("--lossy", type=int, default=40)
            p.add_argument("--dither", choices=["none", "light"], default="light")
        else:
            p.add_argument("--size", help="scaled to WxH (default: the window's)")
        if name in ("mp4", "encode-mp4"):
            p.add_argument("--fps", type=int, default=60)
            p.add_argument("--crf", type=int, default=18)
            p.add_argument("--preset", default="slow")
        p.add_argument("--window", default="1920x1080" if name == "mp4" else "1280x720")
        p.add_argument("--version", default="us")
        p.add_argument("--tree", help="the checkout whose build is run (default: this one)")
        p.add_argument("--threads", type=int, default=4, help="the game's burst writers")
        p.add_argument("--set", action="append", metavar="K=V")
        p.add_argument("--env", action="append", metavar="K=V")
        p.add_argument("--scratch", help="the burst's folder (default local-docs/media_burst/<out's name>)")
        p.add_argument("--keep", action="store_true", help="keep the burst")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    if a.cmd.startswith("encode-"):
        files = burst_files(a.scenario)
        if not files:
            sys.exit(f"capture.py: no burst_* files in {a.scenario}")
        (encode_gif if a.cmd == "encode-gif" else encode_mp4)(files, a.out, a)
        return 0
    # (the tools first: a missing one stops before the run, not after)
    if a.cmd == "gif":
        gifsicle()
    elif a.cmd == "mp4":
        ffmpeg()
    folder = os.path.abspath(a.scratch or os.path.join(REPO, "local-docs", "media_burst",
                                                       os.path.splitext(os.path.basename(a.out))[0]))
    if a.cmd == "still":
        spec, count = f"{a.frame}:1:1", 1
    else:
        spec, count = f"{a.start}:{a.count}:{a.step}", a.count
    files = run_burst(a, spec, count, folder)
    try:
        {"gif": encode_gif, "mp4": encode_mp4, "still": encode_still}[a.cmd](files, a.out, a)
    finally:
        if not a.keep:
            drop(folder, files)
    return 0


if __name__ == "__main__":
    sys.exit(main())
