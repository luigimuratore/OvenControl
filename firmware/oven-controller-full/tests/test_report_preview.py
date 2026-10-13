import unittest
from unittest.mock import patch
from test_telegram_tool import preview


class ReportPreviewTest(unittest.TestCase):
    def test_overshoot_tracks_mean_with_asymmetric_probes(self):
        d = preview.DemoDevice("full", speed=1)
        d.request("POST", "command", body=dict(action="start", recipeId=d.recipes[0]["id"], hardwareReady=True))
        d.target = 175
        def probes(a, b):
            return [dict(c=a, valid=True), dict(c=b, valid=True)]
        with patch.object(d, "sensors", return_value=probes(179.22, 170.78)):
            d.begin_report()  # Isolate these observations from the initial startup sample.
        self.assertEqual(d.report_work["version"], 3)
        self.assertEqual(d.report_work["stats"]["maxOvershootC"], 0)
        with patch.object(d, "sensors", return_value=probes(179.22, 174.78)):
            d.observe_report()
        for stats in (d.report_work["stats"], d.report_work["steps"][0]["stats"]):
            self.assertAlmostEqual(stats["maxOvershootC"], 2)


if __name__ == "__main__":
    unittest.main()
