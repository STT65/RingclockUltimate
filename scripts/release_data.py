"""
release_data.py — PlatformIO pre-build script for the 'release' environment.

Processes data/script.js before the LittleFS image is built:
  - WS URL:    removes hardcoded dev line (ws://x.x.x.x/ws)
               uncomments production line  (location.host)
  - fetch URL: removes hardcoded dev line (http://x.x.x.x/settings)
               uncomments production line  (/settings)

The original source file is never modified. A temporary copy of the entire
data/ directory is written to .pio/release_data/ and the filesystem builder
is pointed there instead.
"""

import re
import os
import shutil

Import("env")  # noqa: F821  (PlatformIO SCons built-in)


def _process_release_data(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")

    src_data  = os.path.join(project_dir, "data")
    dest_data = os.path.join(build_dir, "release_data")

    # Fresh copy of the whole data directory
    if os.path.exists(dest_data):
        shutil.rmtree(dest_data)
    shutil.copytree(src_data, dest_data)

    # Patch script.js
    js_path = os.path.join(dest_data, "script.js")
    if not os.path.exists(js_path):
        print("[release_data] WARNING: data/script.js not found — skipping patch")
        return

    with open(js_path, "r", encoding="utf-8") as f:
        content = f.read()

    # --- WebSocket URL ---
    # Remove the hardcoded dev URL line (entire line, any leading whitespace)
    content = re.sub(
        r'[ \t]*//[ \t]*ws\s*=\s*new\s+WebSocket\(`ws://\d+\.\d+\.\d+\.\d+/ws`\);[ \t]*\r?\n',
        "",
        content,
    )
    # Uncomment the location.host line
    content = re.sub(
        r'//[ \t]*(ws\s*=\s*new\s+WebSocket\(`ws://\$\{location\.host\}/ws`\);)',
        r'\1',
        content,
    )

    # --- fetch URL ---
    # Remove the hardcoded dev fetch line (entire line, any leading whitespace)
    content = re.sub(
        r'[ \t]*//[ \t]*fetch\(`?["\']?http://\d+\.\d+\.\d+\.\d+/settings["\']?`?\)[^\r\n]*\r?\n',
        "",
        content,
    )
    # Uncomment the production fetch line
    content = re.sub(
        r'//[ \t]*(fetch\(["\']\/settings["\'][^\r\n]*)',
        r'\1',
        content,
    )

    with open(js_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[release_data] Patched script.js → {dest_data}")

    # Point the filesystem builder at the processed directory
    env.Replace(PROJECTDATA_DIR=dest_data)


env.AddPreAction("$BUILD_DIR/littlefs.bin", _process_release_data)  # noqa: F821
