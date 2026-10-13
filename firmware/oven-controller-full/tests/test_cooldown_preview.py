import unittest
from test_telegram_tool import preview, tool


class CooldownPreviewTest(unittest.TestCase):
    def device(self, manual, rate=1, target=50):
        d = preview.DemoDevice("full", speed=1)
        d.temperature = 175
        d.control.update(actuator="ssr", windowSec=5, maxPower=60)
        if manual:
            d.request("POST", "manual", body=dict(action="start", targetC=175, rateCMin=1, hardwareReady=True))
            d.request("POST", "manual", body=dict(action="cooldown", targetC=target, rateCMin=rate))
        else:
            d.recipes = [dict(id="cool", name="Discesa", steps=[dict(type="cooldown", target=target, rate=rate)])]
            d.request("POST", "command", body=dict(action="start", recipeId="cool", hardwareReady=True))
        return d

    def test_slow_cooldown_uses_heat_and_keeps_close_to_reference(self):
        for manual in (False, True):
            with self.subTest(manual=manual):
                d = self.device(manual)
                commands = []
                for _ in range(120):
                    d.advance(1); commands.append(d.duty)
                    self.assertLessEqual(d.duty, 60)
                self.assertTrue(any(value > 0 for value in commands))
                self.assertAlmostEqual(d.temperature, d.target, delta=1)
                self.assertEqual(d.phase, "running")
                self.assertTrue(d.status()["cooldownRegulated"])

    def test_equal_or_above_reference_is_always_off_and_large_natural_lag_is_not_fault(self):
        for manual in (False, True):
            with self.subTest(manual=manual):
                d = self.device(manual, rate=30)
                for offset in (0, 1, 10, 40):
                    d.temperature = d.target + offset
                    d.advance_full(0)
                    self.assertEqual(d.duty, 0)
                    self.assertFalse(d.relay)
                    self.assertEqual(d.phase, "running")
                d = self.device(manual, rate=30)
                d.advance(300)
                self.assertGreater(d.temperature, d.target + 8)
                self.assertEqual(d.duty, 0)
                self.assertFalse(d.relay)
                self.assertEqual(d.phase, "running")

    def test_curve_endpoint_and_probe_temperature_are_both_required(self):
        d = self.device(True, rate=30)
        d.temperature = 49
        d.advance_full(0)
        self.assertFalse(d.status()["manual"]["cooldownComplete"])
        d.target = 50
        d.temperature = 70
        d.advance_full(0)
        self.assertFalse(d.status()["manual"]["cooldownComplete"])
        d.temperature = 50
        d.advance_full(0)
        self.assertTrue(d.status()["manual"]["cooldownComplete"])
        d.temperature = 40
        d.advance(10)
        self.assertEqual(d.duty, 0)
        self.assertFalse(d.relay)

    def test_final_program_step_completes_and_outputs_stay_off(self):
        d = self.device(False, rate=30)
        d.advance(3800)
        self.assertEqual(d.phase, "complete")
        self.assertEqual(d.duty, 0)
        self.assertFalse(d.relay)
        self.assertEqual(d.last_report["outcome"], "completed")

    def test_stop_emergency_sensor_fault_and_hard_limit_still_stop_cooling(self):
        for manual in (False, True):
            for event in ("stop", "emergency", "probe", "temperature"):
                with self.subTest(manual=manual, event=event):
                    d = self.device(manual)
                    d.advance(20)
                    if event == "probe":
                        d.request("POST", "preview-sensor", body=dict(missing=True))
                    elif event == "temperature":
                        d.temperature = 415
                        d.advance_full(0)
                    else:
                        d.request("POST", "command", body=dict(action=event))
                    self.assertEqual(d.duty, 0)
                    self.assertFalse(d.relay)
                    self.assertEqual(d.phase, "idle" if event == "stop" else "fault")

    def test_telegram_describes_regulated_cooldown_and_final_monitoring(self):
        d = self.device(True)
        text = tool.format_status(d.status())
        self.assertIn("COOLDOWN REGOLATO", text)
        self.assertNotIn("Cooldown passivo", text)
        d.target = 50; d.temperature = 50; d.advance_full(0)
        text = tool.format_status(d.status())
        self.assertIn("COOLDOWN COMPLETATO", text)
        self.assertIn("riscaldamento SPENTO", text)


if __name__ == "__main__":
    unittest.main()
