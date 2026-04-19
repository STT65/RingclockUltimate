"""
inspect_littlefs.py — List or extract the contents of a LittleFS image.

Usage:
    python scripts/inspect_littlefs.py [--extract <dir>] [image]

Arguments:
    image       Path to the .bin file (default: .pio/build/release/littlefs.bin)
    --extract   Unpack all files into <dir> instead of just listing them

Examples:
    # List files in the default release image
    python scripts/inspect_littlefs.py

    # List files in a specific image
    python scripts/inspect_littlefs.py .pio/build/d1_mini/littlefs.bin

    # Extract to a temporary folder
    python scripts/inspect_littlefs.py --extract C:/temp/lfs_out
"""

import argparse
import os
import subprocess
import sys
import tempfile

MKLITTLEFS = os.path.expanduser(
    r"~/.platformio/packages/tool-mklittlefs/mklittlefs.exe"
)
DEFAULT_IMAGE = os.path.join(".pio", "build", "release", "littlefs.bin")

# D1 Mini LittleFS partition parameters
FS_SIZE  = 1024000
FS_BLOCK = 8192
FS_PAGE  = 256


def run(args: list[str]) -> int:
    result = subprocess.run(args, text=True, capture_output=True)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(
        description="List or extract a RingclockUltimate LittleFS image."
    )
    parser.add_argument(
        "image", nargs="?", default=DEFAULT_IMAGE,
        help=f"Path to littlefs.bin (default: {DEFAULT_IMAGE})"
    )
    parser.add_argument(
        "--extract", metavar="DIR",
        help="Extract all files into DIR instead of listing"
    )
    opts = parser.parse_args()

    if not os.path.exists(MKLITTLEFS):
        sys.exit(f"ERROR: mklittlefs not found at {MKLITTLEFS}\n"
                 "Run 'pio run' once to install PlatformIO packages.")

    if not os.path.exists(opts.image):
        sys.exit(f"ERROR: image not found: {opts.image}")

    common = [MKLITTLEFS, "-s", str(FS_SIZE), "-b", str(FS_BLOCK), "-p", str(FS_PAGE)]

    if opts.extract:
        os.makedirs(opts.extract, exist_ok=True)
        print(f"Extracting {opts.image} -> {opts.extract}")
        sys.exit(run(common + ["-u", opts.extract, opts.image]))
    else:
        print(f"Contents of {opts.image}:")
        sys.exit(run(common + ["-l", opts.image]))


if __name__ == "__main__":
    main()
