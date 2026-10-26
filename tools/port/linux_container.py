#!/usr/bin/env python3
"""Run a command in the Linux x86-64 build container over this checkout
(tools/port/linux-release.Dockerfile; Docker or OrbStack, emulated on Apple
silicon): the release's Linux packages (release.py) and local CI's Linux
stages (ci.yml's Linux job) both run there.

  linux_container.py [--env NAME=VALUE ...] -- COMMAND...

The checkout is mounted at /src (the working directory). $TTYD_LOCAL_DATA,
the disc images and extracted discs (tools/port/ci_link_data.py), is mounted
at its own path, so the links into it work in and out of the container.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IMAGE = "ttyd-port-linux"


def docker():
    found = shutil.which("docker")
    if found is None:
        sys.exit("linux_container: no docker (install Docker or OrbStack)")
    return found


def build_image():
    """The image (Ubuntu's packages: apt downloads, retried as local CI's
    downloading steps are)."""
    import local_ci
    cmd = [docker(), "build", "--platform", "linux/amd64", "-t", IMAGE,
           "-f", os.path.join(ROOT, "tools", "port", "linux-release.Dockerfile"), os.path.join(ROOT, "tools", "port")]
    if not local_ci.retrying(lambda: subprocess.run(cmd).returncode == 0, "docker build"):
        sys.exit("linux_container: the image's build failed")


def run(command, env=()):
    """The command's exit status, run in the container (built if need be)."""
    build_image()
    args = [docker(), "run", "--rm", "--platform", "linux/amd64", "-v", f"{ROOT}:/src", "-w", "/src"]
    data = os.environ.get("TTYD_LOCAL_DATA")
    if data and os.path.isdir(data):
        args += ["-v", f"{data}:{data}", "-e", f"TTYD_LOCAL_DATA={data}"]
    for e in env:
        args += ["-e", e]
    print("$", " ".join(args + [IMAGE] + list(command)), flush=True)
    return subprocess.run(args + [IMAGE] + list(command)).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE in the container")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    command = a.command[1:] if a.command[:1] == ["--"] else a.command
    if not command:
        ap.error("no command")
    return run(command, a.env)


if __name__ == "__main__":
    sys.exit(main())
