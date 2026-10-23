#!/usr/bin/env python3
"""One-time USB install with full flash backup; never erase the original NVS.

Nothing runs on import. All serial writes require an explicit --port and a
fresh, stopped oven status matching the serial device's MAC address.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
from urllib.request import ProxyHandler, build_opener

ROOT = Path(__file__).resolve().parents[1]
FLASH_BYTES = 0x1000000
NVS_OFFSET, NVS_BYTES = 0x610000, 0x40000
VERSION = "full-1.29"
LEGACY = [
    ("nvs", 1, 2, 0x9000, 0x5000, 0),
    ("otadata", 1, 0, 0xE000, 0x2000, 0),
    ("app0", 0, 16, 0x10000, 0x640000, 0),
    ("app1", 0, 17, 0x650000, 0x640000, 0),
    ("spiffs", 1, 130, 0xC90000, 0x360000, 0),
    ("coredump", 1, 3, 0xFF0000, 0x10000, 0),
]
DEDICATED = LEGACY[:2] + [("app0", 0, 16, 0x10000, 0x600000, 0),
    ("oven_nvs", 1, 2, NVS_OFFSET, NVS_BYTES, 0)] + LEGACY[3:]


def partitions(raw):
    """Validate the Espressif partition table including its MD5 record."""
    result = []
    for offset in range(0, min(len(raw), 0xC00), 32):
        row = raw[offset:offset + 32]
        if len(row) != 32:
            break
        magic, = struct.unpack_from("<H", row)
        if magic == 0xEBEB:
            if row[2:16] != b"\xff" * 14 or hashlib.md5(raw[:offset]).digest() != row[16:32]:
                raise RuntimeError("Checksum della tabella partizioni non valido: nessuna scrittura.")
            return result
        if magic != 0x50AA:
            break
        _, kind, subtype, address, size, label, flags = struct.unpack("<HBBII16sI", row)
        result.append((label.rstrip(b"\0").decode("ascii"), kind, subtype, address, size, flags))
    raise RuntimeError("Tabella partizioni sconosciuta o senza checksum: nessuna scrittura.")


def safe(state):
    return (isinstance(state, dict) and state.get("phase") in ("idle", "complete", "fault", "interrupted")
            and state.get("relay") is False and state.get("otaUpdating") is False
            and isinstance(state.get("manual"), dict) and state["manual"].get("active") is False
            and isinstance(state.get("test"), dict) and state["test"].get("running") is False)


def get(ip, route):
    opener = build_opener(ProxyHandler({}))
    with opener.open(f"http://{ip}{route}", timeout=10) as reply:
        return json.load(reply)


def stopped(ip):
    state = get(ip, "/api/status")
    if not safe(state):
        raise RuntimeError("Forno attivo o stato incompleto: ferma Programmi, Manuale e TEST prima della migrazione.")
    if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", state.get("stationMac", "")):
        raise RuntimeError("Identità MAC dell'ESP assente: nessuna scrittura.")
    return state


def artifacts():
    build = ROOT / ".pio/build/esp32-s3-n16r8"
    files = {"partitions": build / "partitions.bin", "firmware": build / "firmware.bin",
             "selector": Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"}
    for path in files.values():
        if not path.is_file():
            raise RuntimeError(f"Manca {path.name}: esegui prima la pipeline di compilazione.")
    if partitions(files["partitions"].read_bytes()) != DEDICATED:
        raise RuntimeError("La compilazione non contiene la tabella NVS dedicata attesa.")
    image = files["firmware"].read_bytes()
    if len(image) > 0x600000 or VERSION.encode() + b"\0" not in image:
        raise RuntimeError("Firmware incompatibile con questa migrazione.")
    return files


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def usb_port(value):
    if value != "auto":
        return value
    candidates = sorted({str(path) for pattern in ("cu.usbmodem*", "cu.usbserial*") for path in Path("/dev").glob(pattern)})
    if len(candidates) != 1:
        raise RuntimeError("Porta USB non univoca: collega l'ESP al Mac e indica --port con la porta corretta. Trovate: " + (", ".join(candidates) or "nessuna"))
    return candidates[0]


def users(data):
    return sorted(data["users"], key=lambda user: str(user["chatId"]))


def flash_commands(layout, files):
    if layout not in (LEGACY, DEDICATED):
        raise RuntimeError("Layout flash diverso da quelli supportati: backup conservato, nessuna scrittura.")
    commands = []
    if layout == LEGACY:
        commands.append(["erase_region", hex(NVS_OFFSET), hex(NVS_BYTES)])
    commands.append(["write_flash", "0x8000", str(files["partitions"]), "0xe000", str(files["selector"]),
                     "0x10000", str(files["firmware"])])
    return commands


def verify(before, recipes, authorized, after, applied, restored):
    if not safe(after) or after.get("firmware") != VERSION or not after.get("storage", {}).get("dedicated"):
        raise RuntimeError("Migrazione non confermata: non avviare il forno; consulta storage in /api/status.")
    for key in ("pid", "actuator", "windowSec", "maxPower", "protections"):
        if before.get(key) != after.get(key):
            raise RuntimeError(f"Configurazione {key} diversa dopo la migrazione: non avviare il forno.")
    if recipes["recipes"] != applied["recipes"] or users(authorized) != users(restored):
        raise RuntimeError("Programmi/utenti diversi dal backup: non avviare il forno.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Porta USB dell'ESP oppure auto sul Mac con una sola scheda collegata")
    parser.add_argument("--ip", default="192.168.1.190")
    args = parser.parse_args(argv)
    args.port = usb_port(args.port)
    files = artifacts()
    hashes = {name: sha(path) for name, path in files.items()}
    pio = Path.home() / ".platformio"
    python = pio / "penv/bin/python"
    tool = pio / "packages/tool-esptoolpy/esptool.py"
    if not python.is_file() or not tool.is_file():
        raise RuntimeError("Serve PlatformIO locale con il tool esptool installato.")
    before = stopped(args.ip)
    recipes = get(args.ip, "/api/recipes")
    authorized = get(args.ip, "/api/telegram/users")
    directory = ROOT / "output/usb-backups" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    directory.mkdir(parents=True, mode=0o700)
    backup = directory / "flash-originale-16MB.bin"
    backup.touch(mode=0o600)
    snapshot = directory / "configurazione-prima.json"
    snapshot.touch(mode=0o600)
    snapshot.write_text(json.dumps(dict(status=before, programs=recipes, telegram=authorized), ensure_ascii=False, indent=2))
    print(f"Backup privato: {directory}", flush=True)

    def serial(command, *, first=False, final=False, capture=False):
        call = [str(python), str(tool), "--chip", "esp32s3", "--port", args.port, "--baud", "460800",
                "--before", "default_reset" if first else "no_reset", "--after", "hard_reset" if final else "no_reset"] + command
        return subprocess.run(call, check=True, text=True, capture_output=capture)

    fresh = stopped(args.ip)
    if fresh["stationMac"].lower() != before["stationMac"].lower():
        raise RuntimeError("L'ESP raggiunto in rete è cambiato: nessuna scrittura.")
    identity = serial(["read_mac"], first=True, capture=True)
    addresses = re.findall(r"MAC:\s*((?:[0-9a-f]{2}:){5}[0-9a-f]{2})", identity.stdout, re.I)
    if not addresses or any(value.lower() != before["stationMac"].lower() for value in addresses):
        raise RuntimeError("L'ESP USB non corrisponde alla dashboard: nessuna scrittura.")
    serial(["read_flash", "0x0", hex(FLASH_BYTES), str(backup)])
    if backup.stat().st_size != FLASH_BYTES:
        raise RuntimeError("Backup flash incompleto: nessuna scrittura.")
    with backup.open("rb") as handle:
        handle.seek(0x8000)
        layout = partitions(handle.read(0x1000))
    commands = flash_commands(layout, files)
    manifest = directory / "manifest.json"
    manifest.touch(mode=0o600)
    manifest.write_text(json.dumps(dict(backupSha256=sha(backup), version=VERSION, artifacts=hashes, previousLayout=layout), indent=2))
    if any(sha(files[name]) != digest for name, digest in hashes.items()):
        raise RuntimeError("I file di compilazione sono cambiati durante il backup: nessuna scrittura.")
    print("Backup completo e tabella verificati. NVS originale, SPIFFS e coredump restano invariati.", flush=True)
    for index, command in enumerate(commands):
        serial(command, final=index == len(commands) - 1)
    print("Attendo riavvio e verifica di Programmi, utenti e impostazioni...", flush=True)
    deadline = time.monotonic() + 120
    last_error = "ESP non raggiungibile"
    while time.monotonic() < deadline:
        try:
            after = get(args.ip, "/api/status")
            if after.get("firmware") == VERSION:
                verify(before, recipes, authorized, after, get(args.ip, "/api/recipes"), get(args.ip, "/api/telegram/users"))
                print(f"Verificato: {VERSION}, NVS dedicata attiva, Programmi/utenti/impostazioni conservati, uscita spenta.")
                print(f"Conserva il backup: {directory}")
                return 0
        except (OSError, ValueError) as error:
            last_error = str(error)
        time.sleep(2)
    raise RuntimeError(f"Verifica finale non conclusa: {last_error}. Backup disponibile in {directory}.")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERRORE: {error}", file=sys.stderr)
        raise SystemExit(1)
