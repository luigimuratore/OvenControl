#!/usr/bin/env python3
"""Local regression pipeline. --build compiles USB/OTA; never uploads firmware."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="Compile both firmware environments without upload")
    args = parser.parse_args()
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    run([sys.executable, "scripts/embed_web.py"])
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"])
    run([sys.executable, "-m", "unittest", "discover", "-s", ROOT.parent / "local-preview/tests", "-p", "test_*.py"])
    tests = sorted((ROOT / "tests").glob("*_test.cpp"))
    with tempfile.TemporaryDirectory(prefix="oven-native-") as directory:
        def native(source):
            binary = Path(directory) / source.stem
            run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", source, "-o", binary], capture_output=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            return source.stem, result.stdout.strip()

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for name, result in pool.map(native, tests):
                print(f"OK {name}" + (f": {result}" if result else ""), flush=True)
    node = shutil.which("node")
    if not node:
        fallback = Path("/Applications/ChatGPT.app/Contents/Resources/cua_node/bin/node")
        node = str(fallback) if fallback.exists() else None
    if not node:
        raise RuntimeError("Node.js richiesto per i test dashboard/report")
    for suite in ("dashboard_test.cjs", "report_export_test.cjs"):
        run([node, ROOT / "tests" / suite])
    if args.build:
        pio = shutil.which("pio") or str(Path.home() / ".platformio/penv/bin/pio")
        run([pio, "run", "-e", "esp32-s3-n16r8", "-e", "esp32-s3-n16r8-ota"])
    run(["git", "-c", "core.fsmonitor=false", "diff", "--check"])
    print(f"Pipeline OK: Python, {len(tests)} suite C++ con sanitizzatori, dashboard/report JavaScript" + (", build USB/OTA." if args.build else "."))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout, file=sys.stderr)
        if error.stderr:
            print(error.stderr, file=sys.stderr)
        raise SystemExit(error.returncode)
