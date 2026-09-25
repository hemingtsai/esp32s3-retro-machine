#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent
    source = root / "assembler.c"
    binary = root / "assembler"

    if binary.is_file() and os.access(binary, os.X_OK):
        executable = binary
        return subprocess.run([str(executable), *sys.argv[1:]]).returncode

    with tempfile.TemporaryDirectory(prefix="retro-assembler-") as temporary_directory:
        executable = Path(temporary_directory) / "assembler"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wpedantic",
                str(source),
                "-o",
                str(executable),
            ],
            check=True,
        )
        return subprocess.run([str(executable), *sys.argv[1:]]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
