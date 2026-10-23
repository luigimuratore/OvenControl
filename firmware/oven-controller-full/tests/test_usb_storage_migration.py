import contextlib
import copy
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/upload_usb_migration.py"
spec = importlib.util.spec_from_file_location("usb_storage_migration", SCRIPT)
usb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(usb)


def table(layout):
    raw = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, start, size, name.encode(), flags)
                   for name, kind, subtype, start, size, flags in layout)
    return (raw + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()).ljust(0x1000, b"\xff")


class UsbStorageMigrationTest(unittest.TestCase):
    def test_partition_table_integrity_and_known_layout(self):
        for layout in (usb.LEGACY, usb.DEDICATED):
            self.assertEqual(usb.partitions(table(layout)), layout)
        bad = bytearray(table(usb.LEGACY)); bad[10] ^= 1
        for raw in (bad, b"\xff" * 4096, table(usb.LEGACY)[:80]):
            with self.assertRaises(RuntimeError): usb.partitions(raw)
        self.assertEqual([p for p in usb.LEGACY if p[0] not in ("app0",)],
                         [p for p in usb.DEDICATED if p[0] not in ("app0", "oven_nvs")])
        rows = sorted(usb.DEDICATED, key=lambda p: p[3])
        for left, right in zip(rows, rows[1:]): self.assertLessEqual(left[3] + left[4], right[3])
        self.assertEqual(rows[-1][3] + rows[-1][4], usb.FLASH_BYTES)

    def test_write_ranges_and_repeated_install_never_erase_new_data(self):
        files = dict(partitions="p.bin", firmware="f.bin", selector="s.bin")
        initial = usb.flash_commands(usb.LEGACY, files)
        self.assertEqual(initial[0], ["erase_region", "0x610000", "0x40000"])
        self.assertEqual(initial[1][1::2], ["0x8000", "0xe000", "0x10000"])
        repeated = usb.flash_commands(usb.DEDICATED, files)
        self.assertEqual(repeated, [initial[1]])
        with self.assertRaises(RuntimeError): usb.flash_commands([], files)

    def test_live_or_incomplete_state_is_rejected(self):
        state = self.state()
        self.assertTrue(usb.safe(state))
        for changes in (dict(phase="running"), dict(phase="paused"), dict(relay=True),
                        dict(manual=dict(active=True)), dict(test=dict(running=True)),
                        dict(otaUpdating=True), dict(test={})):
            self.assertFalse(usb.safe(dict(state, **changes)))
        self.assertFalse(usb.safe({})); self.assertFalse(usb.safe(None))

    def test_auto_port_requires_one_candidate(self):
        self.assertEqual(usb.usb_port("/dev/cu.explicit"), "/dev/cu.explicit")
        with patch.object(Path, "glob", return_value=[]):
            with self.assertRaises(RuntimeError): usb.usb_port("auto")
        with patch.object(Path, "glob", return_value=[Path("/dev/cu.usbmodem1")]):
            self.assertEqual(usb.usb_port("auto"), "/dev/cu.usbmodem1")
        with patch.object(Path, "glob", return_value=[Path("/dev/cu.usbmodem1"), Path("/dev/cu.usbmodem2")]):
            with self.assertRaises(RuntimeError): usb.usb_port("auto")

    @staticmethod
    def state():
        return dict(phase="idle", relay=False, otaUpdating=False, manual=dict(active=False), test=dict(running=False),
                    stationMac="14:c1:9f:22:4e:58", firmware="full-1.27", pid=dict(kp=12, ki=.02, kd=50),
                    actuator="ssr", windowSec=5, maxPower=60, protections=dict(probeDeltaEnabled=True))

    def simulated_install(self, *, layout=None, mismatch=False, incomplete=False, active=False, change_binary=False):
        with tempfile.TemporaryDirectory(prefix="oven-usb-test-") as directory:
            root = Path(directory)
            home = root / "home"
            for name in ("penv/bin/python", "packages/tool-esptoolpy/esptool.py"):
                path = home / ".platformio" / name; path.parent.mkdir(parents=True, exist_ok=True); path.touch()
            build = root / ".pio/build/esp32-s3-n16r8"; build.mkdir(parents=True)
            files = {"partitions": build / "partitions.bin", "firmware": build / "firmware.bin", "selector": build / "boot_app0.bin"}
            files["partitions"].write_bytes(table(usb.DEDICATED)); files["firmware"].write_bytes(b"\xe9" + usb.VERSION.encode() + b"\0"); files["selector"].write_bytes(b"selector")
            state = self.state()
            if active: state["phase"] = "running"
            after = dict(state, firmware=usb.VERSION, storage=dict(dedicated=True))
            programs = dict(recipes=[dict(id="prova-50", name="Carbonio", steps=[dict(type="ramp", target=80, rate=1)])])
            authorized = dict(users=[dict(chatId="42", owner=True, mode="essenziale"), dict(chatId="77", owner=False, mode="dettagliata")])
            calls = []
            writing = False
            def get(ip, path):
                if path == "/api/status": return copy.deepcopy(after if writing else state)
                return copy.deepcopy(programs if path == "/api/recipes" else authorized)
            def serial(call, **kwargs):
                nonlocal writing
                calls.append(call)
                if "read_mac" in call:
                    mac = "00:00:00:00:00:00" if mismatch else state["stationMac"]
                    return subprocess.CompletedProcess(call, 0, stdout=f"MAC: {mac}\n")
                if "read_flash" in call:
                    image = bytearray(b"\xff" * (1024 if incomplete else usb.FLASH_BYTES))
                    if not incomplete: image[0x8000:0x9000] = table(usb.LEGACY if layout is None else layout)
                    Path(call[-1]).write_bytes(image)
                    if change_binary: files["firmware"].write_bytes(b"changed")
                if "write_flash" in call: writing = True
                return subprocess.CompletedProcess(call, 0)
            with patch.object(usb, "ROOT", root), patch.object(Path, "home", return_value=home), \
                 patch.object(usb, "artifacts", return_value=files), patch.object(usb, "get", side_effect=get), \
                 patch.object(usb.subprocess, "run", side_effect=serial), contextlib.redirect_stdout(io.StringIO()):
                failed = False
                try: self.assertEqual(usb.main(["--port", "/dev/cu.test"]), 0)
                except RuntimeError: failed = True
            backups = list((root / "output/usb-backups").glob("*/flash-originale-16MB.bin")) if (root / "output").exists() else []
            if writing:
                self.assertEqual(len(backups), 1); self.assertEqual(backups[0].stat().st_size, usb.FLASH_BYTES)
                self.assertEqual(backups[0].stat().st_mode & 0o777, 0o600)
                self.assertTrue((backups[0].parent / "manifest.json").is_file())
            return calls, failed

    def test_first_install_backups_before_all_writes(self):
        calls, failed = self.simulated_install()
        self.assertFalse(failed)
        operations = [next(op for op in ("read_mac", "read_flash", "erase_region", "write_flash") if op in call) for call in calls]
        self.assertEqual(operations, ["read_mac", "read_flash", "erase_region", "write_flash"])

    def test_reinstall_preserves_authoritative_partition(self):
        calls, failed = self.simulated_install(layout=usb.DEDICATED)
        self.assertFalse(failed); self.assertFalse(any("erase_region" in call for call in calls))

    def test_wrong_device_incomplete_backup_unknown_layout_and_changed_binary_never_write(self):
        for scenario in (dict(mismatch=True), dict(incomplete=True), dict(layout=usb.LEGACY[:-1]),
                         dict(active=True), dict(change_binary=True)):
            with self.subTest(scenario=scenario):
                calls, failed = self.simulated_install(**scenario)
                self.assertTrue(failed)
                self.assertFalse(any("erase_region" in call or "write_flash" in call for call in calls))

    def test_post_migration_checks_configuration_users_and_programs(self):
        before = self.state(); after = dict(before, firmware=usb.VERSION, storage=dict(dedicated=True))
        recipes = dict(recipes=[dict(id="one", target=80)])
        authorized = dict(users=[dict(chatId="42", mode="essenziale")])
        usb.verify(before, recipes, authorized, after, recipes, authorized)
        for changed in (dict(after, relay=True), dict(after, pid={}), dict(after, storage=dict(dedicated=False))):
            with self.assertRaises(RuntimeError): usb.verify(before, recipes, authorized, changed, recipes, authorized)
        with self.assertRaises(RuntimeError): usb.verify(before, recipes, authorized, after, dict(recipes=[]), authorized)
        with self.assertRaises(RuntimeError): usb.verify(before, recipes, authorized, after, recipes, dict(users=[]))


if __name__ == "__main__": unittest.main()
