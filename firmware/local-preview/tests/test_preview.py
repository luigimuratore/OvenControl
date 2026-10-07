"""Exercise demo workflows and real HTTP parsing without requiring a listening port."""
import importlib.util
import io
import json
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("preview", Path(__file__).resolve().parents[1] / "server.py")
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


class DemoTests(unittest.TestCase):
    def setUp(self):
        self.full = preview.DemoDevice("full", speed=1)
        self.field = preview.DemoDevice("field-test", speed=60)

    def post(self, device, endpoint="command", **body):
        return device.request("POST", endpoint, body=body)

    def start(self):
        return self.post(self.full, action="start", recipeId="prova-50", hardwareReady=True)

    def test_full_pause_resume_and_stop(self):
        with self.assertRaises(preview.ApiError):
            self.post(self.full, action="start", recipeId="prova-50")
        self.start()
        self.full.advance(10)
        self.assertGreater(self.full.target, 24)
        self.post(self.full, action="pause")
        target, elapsed = self.full.target, self.full.step_elapsed
        self.full.advance(20)
        self.assertEqual(self.full.target, target)
        self.assertEqual(self.full.step_elapsed, elapsed)
        self.assertFalse(self.full.relay)
        self.assertEqual(self.full.status()["cycleElapsedSec"], 30)
        with self.assertRaises(preview.ApiError):
            self.post(self.full, "control", actuator="ssr", windowSec=10, maxPower=30)
        self.post(self.full, action="resume")
        self.full.advance(1)
        self.assertGreater(self.full.step_elapsed, elapsed)
        self.post(self.full, action="stop")
        self.assertFalse(self.full.relay)
        self.assertEqual(self.full.phase, "idle")
        self.assertTrue(any(r[6] & 32 for r in self.full.samples))

    def test_fault_requires_restore_and_acknowledgment(self):
        self.start()
        self.post(self.full, "preview-sensor", missing=True)
        self.assertEqual(self.full.phase, "fault")
        self.assertFalse(self.full.relay)
        self.assertIsNone(self.full.status()["sensors"][1]["c"])
        with self.assertRaises(preview.ApiError):
            self.post(self.full, action="reset")
        self.post(self.full, action="stop")
        self.assertEqual(self.full.phase, "fault")
        self.post(self.full, "preview-sensor", missing=False)
        with self.assertRaises(preview.ApiError):
            self.start()
        self.post(self.full, action="reset")
        self.assertEqual(self.start()["phase"], "running")

    def test_all_recipe_steps_finish(self):
        recipe = [{"id": "short", "name": "Demo", "steps": [
            {"type": "ramp", "target": 26, "rate": 60},
            {"type": "hold", "target": 26, "duration": 1},
            {"type": "cooldown", "target": 24.5, "rate": 60}]}]
        self.post(self.full, "recipes", recipes=recipe)
        self.post(self.full, action="start", recipeId="short", hardwareReady=True)
        self.full.advance(1500)
        self.assertEqual(self.full.phase, "complete")
        self.assertFalse(self.full.relay)
        self.assertEqual(self.full.step_index, 3)
        self.assertTrue(any(r[6] & 64 for r in self.full.samples))

    def test_control_pid_and_recipe_validation(self):
        self.post(self.full, "control", actuator="ssr", windowSec=10, maxPower=30)
        self.assertEqual(self.full.status()["actuator"], "ssr")
        self.post(self.full, "pid", kp=10, ki=.02, kd=30)
        self.assertEqual(self.full.status()["pid"]["kp"], 10)
        for endpoint, body in [("control", dict(actuator="relay", windowSec=10, maxPower=30)),
                               ("pid", dict(kp=float("nan"), ki=.1, kd=30)),
                               ("recipes", dict(recipes=[]))]:
            with self.assertRaises(preview.ApiError):
                self.post(self.full, endpoint, **body)

    def test_history_pagination_exports_every_row_once(self):
        self.full.advance(2500)
        for device in (self.full, self.field):
            exported, after = [], 0
            while True:
                page = device.request("GET", "history", {"after": [str(after)]})
                exported.extend(page["samples"])
                after = exported[-1][0]
                if not page["more"]:
                    break
            self.assertEqual(exported, device.samples)
            self.assertEqual(len({r[0] for r in exported}), len(exported))
            tail = device.request("GET", "history", {"tail": ["1"]})
            older = device.request("GET", "history", {"before": [str(tail["samples"][0][0])]})
            self.assertTrue(all(r[0] < tail["samples"][0][0] for r in older["samples"]))
        self.assertEqual(len(self.full.samples[0]), 8)
        self.assertEqual(len(self.field.samples[0]), 9)

    def test_field_pulse_duration_lease_and_stop(self):
        token = self.post(self.field, action="arm", loadsDisconnected=True)["token"]
        for bad in (99, 5001, True, 1.5):
            with self.assertRaises(preview.ApiError):
                self.post(self.field, action="relayPulse", token=token, durationMs=bad)
        self.post(self.field, action="relayPulse", token=token, durationMs=1000)
        self.assertTrue(self.field.status()["relay"])
        self.field.advance(1.1)
        self.assertFalse(self.field.status()["relay"])
        self.assertTrue(self.field.armed)
        self.post(self.field, "heartbeat", token=token)
        self.field.advance(2.6)
        self.assertFalse(self.field.armed)
        with self.assertRaises(preview.ApiError):
            self.post(self.field, "heartbeat", token=token)
        token = self.post(self.field, action="arm", loadsDisconnected=True)["token"]
        self.post(self.field, action="relaySequence", token=token)
        self.post(self.field, action="stop")
        self.assertFalse(self.field.status()["relay"])

    def test_field_arm_expires_despite_heartbeats(self):
        token = self.post(self.field, action="arm", loadsDisconnected=True)["token"]
        for _ in range(59):
            self.field.advance(1)
            self.post(self.field, "heartbeat", token=token)
        self.field.advance(1.1)
        self.assertFalse(self.field.armed)
        self.assertEqual(self.field.speed, 1)

    def test_led_sequence_and_isolated_devices(self):
        token = self.post(self.field, action="arm", loadsDisconnected=True)["token"]
        self.post(self.field, action="ledSequence", token=token)
        for expected in [(False, False), (False, True), (True, False), (True, True)]:
            s = self.field.status()
            self.assertEqual((s["red"], s["green"]), expected)
            self.assertFalse(s["relay"])
            self.field.advance(2)
            self.post(self.field, "heartbeat", token=token)
        self.assertFalse(self.field.status()["running"])
        self.assertEqual(self.full.phase, "idle")


class FakeConnection:
    def __init__(self, request):
        self.input, self.output = io.BytesIO(request), bytearray()

    def makefile(self, *_):
        return self.input

    def sendall(self, data):
        self.output.extend(data)


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.server = SimpleNamespace(devices={m: preview.DemoDevice(m) for m in preview.PROJECTS},
                                      lock=threading.Lock(), update=lambda: None)

    def request(self, path, body=None, raw=None):
        method = "GET" if body is None and raw is None else "POST"
        data = (json.dumps(body).encode() if raw is None else raw) if method == "POST" else b""
        request = f"{method} {path} HTTP/1.0\r\nHost: localhost\r\nContent-Length: {len(data)}\r\n\r\n".encode() + data
        connection = FakeConnection(request)
        preview.Handler(connection, ("127.0.0.1", 1234), self.server)
        headers, payload = bytes(connection.output).split(b"\r\n\r\n", 1)
        return int(headers.split(b" ")[1]), headers.decode(), payload

    def test_assets_and_api_both_dashboards(self):
        for mode in preview.PROJECTS:
            code, _, html = self.request(f"/{mode}/")
            self.assertEqual(code, 200)
            self.assertIn(f'/{mode}/preview.js'.encode(), html)
            self.assertLess(html.index(b"preview.js"), html.index(b"/app.js"))
            for file in ("app.js", "style.css", "preview.js"):
                self.assertEqual(self.request(f"/{mode}/{file}")[0], 200)
            code, _, body = self.request(f"/{mode}/api/status")
            self.assertEqual(code, 200)
            self.assertTrue(json.loads(body)["preview"])
        self.assertEqual(self.request("/")[0], 302)

    def test_no_project_files_or_unprefixed_commands(self):
        for path in ("/wifi_config.h", "/full/wifi_config.h", "/full/../include/wifi_config.h",
                     "/full/.pio/firmware.bin", "/api/status", "/site/"):
            self.assertEqual(self.request(path)[0], 404)
        self.assertEqual(self.request("/full/app.js", body={})[0], 405)

    def test_json_errors_and_beacon_stop(self):
        self.assertEqual(self.request("/full/api/command", raw=b"{")[0], 400)
        self.assertEqual(self.request("/field-test/api/command", body={"action": []})[0], 400)
        code, _, payload = self.request("/field-test/api/command", body={"action": "arm", "loadsDisconnected": True})
        self.assertEqual(code, 200)
        token = json.loads(payload)["token"]
        self.assertEqual(self.request("/field-test/api/heartbeat", body={"token": token})[0], 204)
        self.assertEqual(self.request("/field-test/api/command", raw=b'{"action":"stop"}')[0], 200)
        self.assertFalse(self.server.devices["field-test"].armed)


if __name__ == "__main__":
    unittest.main()
