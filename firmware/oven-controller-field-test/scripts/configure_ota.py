"""Configure espota authentication from the ignored local firmware config."""
from pathlib import Path
import re

Import("env")

config = Path(env.subst("$PROJECT_DIR")) / "include" / "ota_config.h"
if not config.is_file():
    raise RuntimeError("OTA: crea include/ota_config.h da ota_config.example.h e imposta la password")
match = re.search(
    r'^\s*#define\s+OVEN_OTA_PASSWORD\s+"([A-Za-z0-9_-]{16,64})"\s*(?://[^\n]*)?$',
    config.read_text(encoding="utf-8"), re.MULTILINE,
)
if not match:
    raise RuntimeError("OTA: password richiesta (16..64 lettere, numeri, _ o -) in include/ota_config.h")

# espota's debug option prints authentication credentials. Keep it disabled.
flags = [flag for flag in env.get("UPLOADERFLAGS", []) if flag not in ("--debug", "-d")]
env.Replace(UPLOADERFLAGS=flags + ["--auth=" + match.group(1), "--port=3232"])
