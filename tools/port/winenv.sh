# Source from Git Bash on Windows: puts LLVM clang, the CMake bundled with the
# Visual Studio Build Tools, and ninja on PATH for the PC build.
#   . tools/port/winenv.sh
_vs="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake"
export PATH="/c/Program Files/LLVM/bin:$_vs/CMake/bin:$_vs/Ninja:$PATH"
command -v ninja >/dev/null 2>&1 || export PATH="$(dirname "$(python -c 'import sys;print(sys.executable)')")/Scripts:$PATH"
# CMake's FindPython: the real interpreter, not the Microsoft Store alias or the
# Python install manager's launchers, which a build can fail to run.
export Python3_ROOT_DIR="$(dirname "$(python -c 'import sys;print(sys.executable)')")"
unset _vs
