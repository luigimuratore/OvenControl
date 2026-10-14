import unittest
from test_telegram_tool import preview


class ResetErrorsPreviewTest(unittest.TestCase):
    def device(self):
        return preview.DemoDevice("full", speed=1)

    def test_fault_blocks_test_until_reset_and_reset_never_heats(self):
        d = self.device()
        d.phase, d.fault = "fault", "Allarme sonda"
        self.assertTrue(d.status()["resetRequired"])
        with self.assertRaises(preview.ApiError):
            d.request("POST", "test/command", body=dict(action="enter"))
        d.request("POST", "command", body=dict(action="reset"))
        s = d.status()
        self.assertFalse(s["resetRequired"])
        self.assertEqual(s["phase"], "idle")
        self.assertFalse(s["relay"])
        self.assertIsNone(s["target"])

    def test_reset_leaves_test_even_if_probe_is_still_invalid(self):
        d = self.device()
        d.request("POST", "test/command", body=dict(action="relayPulse", durationMs=1000))
        d.phase, d.missing = "fault", True
        with self.assertRaises(preview.ApiError):
            d.request("POST", "command", body=dict(action="reset"))
        self.assertTrue(d.status()["resetRequired"])
        self.assertFalse(d.test_enabled)
        self.assertFalse(d.status()["relay"])
        d.missing = False
        d.request("POST", "command", body=dict(action="reset"))
        self.assertFalse(d.status()["resetRequired"])
        self.assertFalse(d.status()["relay"])

    def test_reset_cannot_interrupt_an_active_control(self):
        d = self.device()
        d.request("POST", "manual", body=dict(action="start", targetC=50, rateCMin=1, hardwareReady=True))
        with self.assertRaises(preview.ApiError):
            d.request("POST", "command", body=dict(action="reset"))
        self.assertTrue(d.status()["manual"]["active"])


if __name__ == "__main__":
    unittest.main()
