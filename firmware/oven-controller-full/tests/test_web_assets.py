"""Validate the actual embedded bytes and lossless native plot output."""
import gzip
import re
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class WebAssetsTests(unittest.TestCase):
    def test_deflate_run_boundaries_and_all_literal_codes(self):
        with tempfile.TemporaryDirectory(prefix="oven-deflate-") as directory:
            binary, records = Path(directory) / "deflate", Path(directory) / "records"
            subprocess.run(["c++", "-std=c++17", str(ROOT / "tests/deflate_runs_test.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(records)], check=True)
            data = records.read_bytes()
        pos = 0
        while pos < len(data):
            raw_size, packed_size = struct.unpack_from("=II", data, pos)
            pos += 8
            expected = data[pos:pos+raw_size]
            pos += raw_size
            self.assertEqual(zlib.decompress(data[pos:pos+packed_size]), expected)
            pos += packed_size
        self.assertEqual(pos, len(data))

    def test_embedded_assets_are_current_and_lossless(self):
        source = (ROOT / "src/web_assets.h").read_text()
        for filename, symbol, compressed in (
            ("index.html", "WEB_HTML", True), ("style.css", "WEB_CSS", True),
            ("app.js", "WEB_JS", True), ("report-export.js", "WEB_EXPORT_JS", True),
            ("vendor/jspdf.umd.min.js", "WEB_PDF_JS", True), ("favicon.jpg", "WEB_FAVICON", False),
        ):
            with self.subTest(filename=filename):
                match = re.search(rf"const uint8_t {symbol}\[\] PROGMEM = \{{(.*?)\}};", source, re.S)
                self.assertIsNotNone(match)
                embedded = bytes(int(value, 16) for value in re.findall(r"0x([a-f0-9]{2})", match[1]))
                self.assertEqual(gzip.decompress(embedded) if compressed else embedded, (ROOT / "data" / filename).read_bytes())
                if compressed:
                    self.assertLess(len(embedded), (ROOT / "data" / filename).stat().st_size)

    def test_native_png_crc_pixels_and_compression(self):
        with tempfile.TemporaryDirectory(prefix="oven-png-") as directory:
            binary, image, original = Path(directory) / "plot", Path(directory) / "plot.png", Path(directory) / "plot.raw"
            subprocess.run(["c++", "-std=c++17", str(ROOT / "tests/plot_png_test.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(image), str(original)], check=True)
            content = image.read_bytes()
            pixels = original.read_bytes()
        self.assertEqual(content[:8], b"\x89PNG\r\n\x1a\n")
        pos, chunks = 8, {}
        while pos < len(content):
            length = struct.unpack_from(">I", content, pos)[0]
            kind, data = content[pos+4:pos+8], content[pos+8:pos+8+length]
            self.assertEqual(zlib.crc32(kind + data), struct.unpack_from(">I", content, pos+8+length)[0])
            chunks.setdefault(kind, bytearray()).extend(data)
            pos += length + 12
        self.assertEqual(struct.unpack_from(">II", chunks[b"IHDR"]), (960, 480))
        raster = zlib.decompress(chunks[b"IDAT"])
        self.assertEqual(len(raster), 961 * 480)
        self.assertEqual(raster, pixels)
        self.assertTrue(all(raster[y * 961] == 0 for y in range(480)))
        self.assertEqual(set(raster), set(range(6)))
        self.assertLess(len(content), len(raster) // 10)


if __name__ == "__main__":
    unittest.main()
