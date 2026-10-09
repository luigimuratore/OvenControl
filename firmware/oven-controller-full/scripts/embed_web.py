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
for filename, symbol in (("index.html", "WEB_HTML"), ("style.css", "WEB_CSS"), ("app.js", "WEB_JS"), ("report-export.js", "WEB_EXPORT_JS")):
    content = (data / filename).read_text(encoding="utf-8")
    delimiter = "OVEN_ASSET_2026"
    if f"){delimiter}\"" in content:
        raise ValueError("Web asset contains the raw-string delimiter")
    parts.append(f'const char {symbol}[] PROGMEM = R"{delimiter}({content}){delimiter}";\n')
pdf_js = gzip.compress((data / "vendor/jspdf.umd.min.js").read_bytes(), mtime=0)
parts.append("const uint8_t WEB_PDF_JS[] PROGMEM = {\n" + "\n".join(
    ",".join(f"0x{b:02x}" for b in pdf_js[i:i+24]) + "," for i in range(0, len(pdf_js), 24)) + "\n};\n")
new_content = "\n".join(parts)
if not out.exists() or out.read_text(encoding="utf-8") != new_content:
    out.write_text(new_content, encoding="utf-8")
