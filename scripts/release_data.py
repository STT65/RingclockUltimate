"""
release_data.py — PlatformIO pre-build script for the 'release' environment.

Processes data/ files before the LittleFS image is built:
  - script.js:
      WS URL:    removes hardcoded dev line (ws://x.x.x.x/ws)
                 uncomments production line  (location.host)
      fetch URL: removes hardcoded dev line (http://x.x.x.x/settings)
                 uncomments production line  (/settings)
  - mqtt.json:   sets mqttEnabled to false (dev default may differ)

The original source files are never modified. A temporary copy of the entire
data/ directory is written to .pio/release_data/, which is then packed into
littlefs.bin via a post-action using mklittlefs directly.
"""

import json
import re
import os
import shutil
import subprocess

Import("env")  # noqa: F821  (PlatformIO SCons built-in)

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
    print("[release_data] WARNING: data/script.js not found -- skipping patch")
else:
    with open(js_path, "r", encoding="utf-8") as f:
        content = f.read()

    # --- WebSocket URL ---
    content = re.sub(
        r'[ \t]*//[ \t]*ws\s*=\s*new\s+WebSocket\(`ws://\d+\.\d+\.\d+\.\d+/ws`\);[ \t]*\r?\n',
        "",
        content,
    )
    content = re.sub(
        r'//[ \t]*(ws\s*=\s*new\s+WebSocket\(`ws://\$\{location\.host\}/ws`\);)',
        r'\1',
        content,
    )

    # --- fetch URL ---
    content = re.sub(
        r'[ \t]*//[ \t]*fetch\(`?["\']?http://\d+\.\d+\.\d+\.\d+/settings["\']?`?\)[^\r\n]*\r?\n',
        "",
        content,
    )
    content = re.sub(
        r'//[ \t]*(fetch\(["\']\/settings["\'][^\r\n]*)',
        r'\1',
        content,
    )

    with open(js_path, "w", encoding="utf-8") as f:
        f.write(content)

    print("[release_data] Patched script.js")

# Patch mqtt.json — disable MQTT in release image
mqtt_path = os.path.join(dest_data, "mqtt.json")
if os.path.exists(mqtt_path):
    with open(mqtt_path, "r", encoding="utf-8") as f:
        mqtt = json.load(f)
    mqtt["mqttEnabled"] = False
    with open(mqtt_path, "w", encoding="utf-8") as f:
        json.dump(mqtt, f)
    print("[release_data] Patched mqtt.json (mqttEnabled=false)")
else:
    print("[release_data] WARNING: data/mqtt.json not found -- skipping patch")


def _repack_littlefs(source, target, env):
    """Rebuild littlefs.bin from the patched release_data directory.

    PlatformIO's ESP8266 platform hardcodes 'data' as the source directory
    and ignores PROJECTDATA_DIR, so we rebuild the image ourselves after
    the regular build using the same mklittlefs parameters.
    """
    mklittlefs = os.path.join(
        os.path.expanduser("~"), ".platformio", "packages",
        "tool-mklittlefs", "mklittlefs.exe"
    )
    lfs_bin  = os.path.join(build_dir, "littlefs.bin")
    lfs_size = env.BoardConfig().get("upload.maximum_size", 1024000)

    # Derive filesystem size from board flash layout
    flash_size  = int(env.BoardConfig().get("upload.maximum_size",  4 * 1024 * 1024))
    fs_start    = int(env.BoardConfig().get("upload.maximum_data_size", 1 * 1024 * 1024))
    fs_size     = flash_size - fs_start
    # D1 Mini (4 MB): LittleFS occupies the last 1 000 KB
    fs_size = 1024000  # matches board partition

    cmd = [mklittlefs, "-c", dest_data, "-p", "256", "-b", "8192",
           "-s", str(fs_size), lfs_bin]
    print(f"[release_data] Repacking littlefs.bin from release_data ...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[release_data] ERROR: {result.stderr}")
        raise Exception("mklittlefs failed")
    print("[release_data] littlefs.bin repacked successfully")


env.AddPostAction("buildfs", _repack_littlefs)  # noqa: F821
