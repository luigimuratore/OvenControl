"""Embed the site dashboard in firmware flash; also runnable with Python."""
from pathlib import Path
import gzip

try:
    Import("env")
    project = Path(env.subst("$PROJECT_DIR"))
except NameError:
    project = Path(__file__).resolve().parents[1]
data = project / "data"
out = project / "src" / "web_assets.h"
parts = ["#pragma once", "#include <Arduino.h>", "// Generated from data/ by scripts/embed_web.py.\n"]
for filename, symbol, compressed in (
    ("index.html", "WEB_HTML", True), ("style.css", "WEB_CSS", True),
    ("app.js", "WEB_JS", True), ("report-export.js", "WEB_EXPORT_JS", True),
    ("vendor/jspdf.umd.min.js", "WEB_PDF_JS", True), ("favicon.jpg", "WEB_FAVICON", False),
):
    content = (data / filename).read_bytes()
    if compressed:
        content = gzip.compress(content, compresslevel=9, mtime=0)
    parts.append(f"const uint8_t {symbol}[] PROGMEM = {{\n" + "\n".join(
        ",".join(f"0x{b:02x}" for b in content[i:i+24]) + "," for i in range(0, len(content), 24)) + "\n};\n")
new_content = "\n".join(parts)
if not out.exists() or out.read_text(encoding="utf-8") != new_content:
    out.write_text(new_content, encoding="utf-8")
