import unittest
from unittest.mock import patch
from test_telegram_tool import preview


class ThermalPreviewTest(unittest.TestCase):
    def device(self, mode, target=175):
        d = preview.DemoDevice("full", speed=1)
        d.temperature = target
        if mode == "manual":
            d.request("POST", "manual", body=dict(action="start", targetC=target,
                rateCMin=1, hardwareReady=True))
        else:
            d.recipes = [dict(id="thermal", name="Test temperatura", steps=[
                dict(type="hold", target=target, duration=5)])]
            d.request("POST", "command", body=dict(action="start", recipeId="thermal", hardwareReady=True))
            if mode == "pause":
                d.request("POST", "command", body=dict(action="pause"))
        d.target = target
        return d

    def test_mean_threshold_in_program_manual_and_pause(self):
        for mode in ("program", "manual", "pause"):
            for first, second, alarm in ((183.02, 171.01, False),
                    (196, 174, False), (196.02, 174, True), (185.01, 185.01, True)):
                with self.subTest(mode=mode, first=first, second=second):
                    d = self.device(mode)
                    pair = d.sensors()
                    pair[0]["c"], pair[1]["c"] = first, second
                    with patch.object(d, "sensors", return_value=pair):
                        d.advance_full(0)
                    self.assertEqual(d.phase, "fault" if alarm else "paused" if mode == "pause" else "running")
                    if alarm:
                        self.assertFalse(d.relay)
                        self.assertEqual(d.duty, 0)
                        for text in ("media PT100", "setpoint 175.00", "limite >10"):
                            self.assertIn(text, d.fault)

    def test_absolute_limit_is_individual_even_when_mean_is_below_target(self):
        for mode in ("program", "manual", "pause"):
            with self.subTest(mode=mode):
                d = self.device(mode, 300)
                pair = d.sensors()
                pair[0]["c"], pair[1]["c"] = 415, 100
                with patch.object(d, "sensors", return_value=pair):
                    d.advance_full(0)
                self.assertEqual(d.phase, "fault")
                self.assertFalse(d.relay)
                self.assertEqual(d.duty, 0)
                self.assertIn("Temperatura >= 415", d.fault)


if __name__ == "__main__":
    unittest.main()
