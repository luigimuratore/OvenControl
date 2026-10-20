"""Exercise manual workflows through the preview API, without physical outputs."""
import unittest
from test_telegram_tool import preview, tool


class ManualPreviewTest(unittest.TestCase):
    def setUp(self):
        self.device = preview.DemoDevice("full", speed=1)

    def manual(self, action, **data):
        data.setdefault("rateCMin", 60)
        return self.device.request("POST", "manual", body=dict(action=action, **data))

    def command(self, action, **data):
        return self.device.request("POST", "command", body=dict(action=action, **data))

    def start(self, target=100):
        return self.manual("start", targetC=target, hardwareReady=True)

    def test_edit_target_keeps_session_history_and_does_not_create_program(self):
        status = self.start()
        started = status["manual"]["startedAtMs"]
        self.device.now += 50
        updated = self.manual("target", targetC=150)
        self.assertEqual(updated["manual"]["startedAtMs"], started)
        self.assertEqual(updated["manual"]["elapsedSec"], 50)
        self.assertEqual(updated["manual"]["targetC"], 150)
        self.assertEqual(updated["stepCount"], 0)
        self.assertEqual(updated["recipeName"], "")
        self.assertEqual(updated["cycleElapsedSec"], 0)
        self.assertEqual(self.device.samples[-1][-1], 255)
        self.assertEqual(len(self.device.recipes), len(preview.DemoDevice("full").recipes))

    def test_lower_target_passively_cools_then_regulates(self):
        self.start()
        self.device.temperature = 90
        self.device.target = 90  # The simulated oven has reached this reference before lowering it.
        self.manual("target", targetC=60)
        self.device.advance_full(0)
        self.assertTrue(self.device.status()["manual"]["coolingToTarget"])
        self.assertFalse(self.device.relay)
        self.assertEqual(self.device.duty, 0)
        self.device.temperature = 59
        self.device.advance_full(40)
        self.assertFalse(self.device.status()["manual"]["coolingToTarget"])
        self.assertGreater(self.device.duty, 0)

    def test_rate_controls_reference_and_edits_keep_it_continuous(self):
        self.manual("start", targetC=100, rateCMin=2, hardwareReady=True)
        initial = self.device.target
        for _ in range(60):
            self.device.now += 1
            self.device.advance_full(1)
        self.assertAlmostEqual(self.device.target, initial + 2)
        old_reference = self.device.target
        self.manual("target", targetC=150, rateCMin=4)
        self.assertEqual(self.device.target, old_reference)
        self.assertEqual(self.device.status()["manual"]["rateCMin"], 4)
        for _ in range(60):
            self.device.now += 1
            self.device.advance_full(1)
        self.assertAlmostEqual(self.device.target, old_reference + 4)
        for rate in (None, True, 0, .09, "2", float("nan")):
            with self.assertRaises(preview.ApiError):
                self.manual("target", targetC=150, rateCMin=rate)

    def test_cooldown_is_indefinite_and_explicit_target_restarts_regulation(self):
        self.start()
        self.manual("cooldown", targetC=20)
        self.device.now += 13 * 3600
        self.device.advance_full(0)
        status = self.device.status()
        self.assertEqual(status["phase"], "running")
        self.assertEqual(status["manual"]["mode"], "cooldown")
        self.assertEqual(status["manual"]["targetC"], 20)
        self.assertFalse(status["relay"])
        self.manual("target", targetC=70)
        self.assertEqual(self.device.status()["manual"]["mode"], "regulating")
        self.device.advance_full(1)
        self.assertGreater(self.device.duty, 0)

    def test_manual_stop_preserves_previous_program_report(self):
        previous = {"key": "previous", "durationMs": 12300}
        self.device.last_report = previous
        self.start()
        self.command("stop")
        self.assertFalse(self.device.status()["manual"]["active"])
        self.assertFalse(self.device.relay)
        self.assertIs(self.device.last_report, previous)
        self.assertIsNone(self.device.report_work)

    def test_program_test_and_settings_cannot_overlap_manual(self):
        self.start()
        recipe_id = self.device.recipes[0]["id"]
        for endpoint, body in (("command", dict(action="start", recipeId=recipe_id, hardwareReady=True)),
                               ("command", dict(action="pause")),
                               ("test/command", dict(action="enter")),
                               ("pid", dict(kp=12, ki=.02, kd=50))):
            with self.assertRaises(preview.ApiError):
                self.device.request("POST", endpoint, body=body)
        self.command("stop")
        self.device.request("POST", "test/command", body=dict(action="enter"))
        with self.assertRaises(preview.ApiError):
            self.start()

    def test_fault_and_emergency_stop_manual_without_report_or_auto_resume(self):
        for event in ("emergency", "probe", "overtemperature"):
            self.setUp()
            self.start()
            if event == "emergency":
                self.command("emergency")
            elif event == "probe":
                self.device.request("POST", "preview-sensor", body=dict(missing=True))
            else:
                self.device.temperature = 415
                self.device.advance_full(0)
            self.assertEqual(self.device.phase, "fault")
            self.assertFalse(self.device.status()["manual"]["active"])
            self.assertFalse(self.device.relay)
            self.assertIsNone(self.device.last_report)
            with self.assertRaises(preview.ApiError):
                self.manual("target", targetC=50)
        self.assertFalse(preview.DemoDevice("full").status()["manual"]["active"])

    def test_invalid_targets_and_missing_hardware_confirmation_do_not_start(self):
        for target in (-1, 401, True, None, "100", float("nan"), float("inf")):
            with self.assertRaises(preview.ApiError):
                self.start(target)
        with self.assertRaises(preview.ApiError):
            self.manual("start", targetC=50)
        self.assertEqual(self.device.phase, "idle")
        self.assertFalse(self.device.relay)

    def test_telegram_manual_state_and_events_never_show_selected_program(self):
        events = tool.PreviewEvents(dict(events=self.device.events), self.device.status())
        status = self.start()
        notices = events.consume(dict(events=self.device.events), status)
        self.assertEqual([n[0] for n in notices], ["MANUALE AVVIATO"])
        status["recipeName"] = "Programma selezionato sulla dashboard"
        text = tool.format_status(status)
        self.assertIn("CONTROLLO MANUALE", text)
        self.assertIn("Target finale: 100.00", text)
        self.assertNotIn(status["recipeName"], text)
        self.assertNotIn("CICLO ATTIVO", text)
        self.assertIn("MANUALE AVVIATO", tool.format_notice(notices[0][0], notices[0][2], status, mode="essenziale"))
        brief = tool.format_notice(notices[0][0], notices[0][2], status)
        self.assertTrue(brief.startswith("🚀"))
        self.assertIn("100", brief)
        self.manual("target", targetC=150)
        self.manual("cooldown", targetC=20)
        notices = events.consume(dict(events=self.device.events), self.device.status())
        self.assertEqual([n[0] for n in notices], ["MANUALE · TARGET MODIFICATO", "MANUALE · COOLDOWN"])
        self.assertEqual(tool.status_style(self.device.status())[0], "❄️")
        self.command("stop")
        notices = events.consume(dict(events=self.device.events), self.device.status())
        self.assertEqual([n[0] for n in notices], ["MANUALE FERMATO · STOP"])


if __name__ == "__main__":
    unittest.main()
