#!/usr/bin/env python3
"""Build the local mgba Python package and run a single pytest file.

This avoids the deprecated setup.py pytest / pytest-runner path.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--test-file", required=True)
    args = parser.parse_args()

    env = os.environ.copy()

    # Ensure the extension module is rebuilt for the active interpreter.
    subprocess.check_call(
        [sys.executable, "setup.py", "build", "-b", args.build_dir],
        cwd=args.source_dir,
        env=env,
    )

    lib_dirs = sorted(
        path for path in glob.glob(os.path.join(args.build_dir, "lib.*"))
        if os.path.isdir(path)
    )
    if not lib_dirs:
        raise RuntimeError("No built Python package directory (lib.*) found")

    pythonpath = os.pathsep.join(lib_dirs)
    if env.get("PYTHONPATH"):
        pythonpath = pythonpath + os.pathsep + env["PYTHONPATH"]
    env["PYTHONPATH"] = pythonpath

    # Prefer the built package over the source tree when importing "mgba".
    source_dir = os.path.abspath(args.source_dir)
    old_path = list(sys.path)
    sys.path = []
    for lib_dir in lib_dirs:
        if lib_dir not in sys.path:
            sys.path.append(lib_dir)
    for entry in old_path:
        resolved = os.path.abspath(entry or os.getcwd())
        if resolved == source_dir:
            continue
        if entry not in sys.path:
            sys.path.append(entry)

    os.chdir(lib_dirs[0])

    import pytest  # Imported late so sys.path has already been adjusted.

    return int(pytest.main(["-q", "--import-mode=importlib", args.test_file]))


if __name__ == "__main__":
    raise SystemExit(main())
