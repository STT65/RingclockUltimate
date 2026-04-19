# scripts/

Build and release helper scripts for RingclockUltimate.

---

## release_data.py

PlatformIO pre-build script, automatically invoked for the `release` environment.

Patches the `data/` directory before packing the LittleFS image:

| File | Change |
|------|--------|
| `script.js` | Removes hardcoded dev WebSocket/fetch URLs; uncomments production URLs |
| `mqtt.json` | Sets `mqttEnabled` to `false` |

The original source files are never modified. A patched copy is written to
`.pio/build/release/release_data/` and packed into `littlefs.bin` via a
`mklittlefs` post-action.

---

## inspect_littlefs.py

Inspect the contents of a LittleFS image. Useful for verifying a release build
before uploading.

**Requirements:** run `pio run` at least once to install PlatformIO packages
(mklittlefs must be present under `~/.platformio/packages/tool-mklittlefs/`).

### List files

```bash
# Default release image (.pio/build/release/littlefs.bin)
python scripts/inspect_littlefs.py

# Specific image
python scripts/inspect_littlefs.py .pio/build/d1_mini/littlefs.bin
```

### Extract files

```bash
python scripts/inspect_littlefs.py --extract C:/temp/lfs_out
```

### Typical release check

```bash
python scripts/inspect_littlefs.py
# Verify: mqttEnabled=false in mqtt.json
# Verify: no hardcoded IP in script.js

python scripts/inspect_littlefs.py --extract C:/temp/lfs_out
cat C:/temp/lfs_out/mqtt.json
```
