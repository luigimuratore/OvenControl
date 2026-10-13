import unittest
from test_telegram_tool import preview, tool


class PausePreviewTest(unittest.TestCase):
    def setUp(self):
        self.device = preview.DemoDevice("full", speed=1)

    def post(self, action, **data):
        return self.device.request("POST", "command", body=dict(action=action, **data))

    def start(self, kind="ramp", target=175, rate=2, actual=100):
        self.device.temperature = actual
        self.device.recipes = [dict(id="test", name="Programma test", steps=[
            dict(type=kind, target=target, rate=rate, duration=10)])]
        return self.post("start", recipeId="test", hardwareReady=True)

    def test_pause_captures_mean_and_freezes_step_and_hold_while_regulating(self):
        self.start()
        self.device.advance(30)
        captured = sum(p["c"] for p in self.device.sensors()) / 2
        elapsed = self.device.step_elapsed
        self.post("pause")
        self.assertEqual(self.device.target, captured)
        self.device.advance(60)
        status = self.device.status()
        self.assertEqual(status["phase"], "paused")
        self.assertTrue(status["pauseMaintainsTemperature"])
        self.assertEqual(status["pauseTargetC"], captured)
        self.assertEqual(self.device.step_elapsed, elapsed)
        self.assertEqual(self.device.hold, 0)
        self.assertGreater(status["duty"], 0)
        self.assertAlmostEqual(self.device.temperature, captured, delta=1)
        self.assertEqual(status["cycleElapsedSec"], 90)
        self.assertIn("PID mantiene", tool.format_status(status))
        self.assertIn("MANTENIMENTO", tool.status_style(status)[1])

    def test_resume_ramp_uses_held_temperature_and_original_rate_without_lost_time(self):
        self.start()
        for _ in range(2):
            self.device.advance(30)
            elapsed = self.device.step_elapsed
            self.post("pause")
            held = self.device.target
            self.device.advance(20)
            self.post("resume")
            self.assertEqual(self.device.target, held)
            self.device.advance(30)
            self.assertAlmostEqual(self.device.target, held + 1)
            self.assertEqual(self.device.step_elapsed, elapsed + 30)
        self.post("stop")
        report = self.device.request("GET", "report")
        self.assertEqual(report["pauseCount"], 2)
        self.assertEqual(report["pausedMs"], 40000)
        self.assertEqual(report["activeMs"], 120000)

    def test_hold_keeps_earned_time_and_returns_to_original_target(self):
        self.start(kind="hold", target=175, actual=174)
        self.device.advance(3)
        earned = self.device.hold
        self.post("pause")
        self.device.advance(20)
        self.assertEqual(self.device.hold, earned)
        self.post("resume")
        self.assertEqual(self.device.target, 175)
        self.device.advance(1)
        self.assertEqual(self.device.hold, earned)
        self.device.advance(1)
        self.assertEqual(self.device.hold, earned + 1)

    def test_cooldown_pause_regulates_then_resumes_passively_from_held_temperature(self):
        self.start(kind="cooldown", target=25, rate=1, actual=175)
        self.device.advance(10)
        self.post("pause")
        held = self.device.target
        self.device.advance(20)
        self.assertGreater(self.device.duty, 0)
        self.post("resume")
        self.device.advance(30)
        self.assertAlmostEqual(self.device.target, held - .5)
        self.assertEqual(self.device.duty, 0)
        self.assertFalse(self.device.relay)

    def test_stop_emergency_and_probe_fault_end_pause_and_leave_outputs_off(self):
        for event in ("stop", "emergency", "probe"):
            self.setUp(); self.start(); self.post("pause")
            if event == "probe":
                self.device.request("POST", "preview-sensor", body=dict(missing=True))
            else:
                self.post(event)
            self.assertFalse(self.device.relay)
            self.assertEqual(self.device.duty, 0)
            self.assertFalse(self.device.status()["pauseMaintainsTemperature"])
            self.assertEqual(self.device.last_report["outcome"], "stopped" if event == "stop" else "fault")
            with self.assertRaises(preview.ApiError):
                self.post("resume")


if __name__ == "__main__":
    unittest.main()
