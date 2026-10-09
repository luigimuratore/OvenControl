import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import socket
import ssl
import tempfile
import threading
import unittest
from unittest.mock import MagicMock, patch
from types import SimpleNamespace
from datetime import datetime, timezone
from urllib.error import HTTPError, URLError

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = load("telegram_tool", ROOT / "scripts/telegram_tool.py")
preview = load("oven_preview", ROOT.parent / "local-preview/server.py")
TOKEN = "12345:abcdefghijklmnopqrstuvwxyz_123"


def update(ident, text="/status", chat=42, sender=42, kind="private", date=100):
    return dict(update_id=ident, message=dict(text=text, date=date,
                chat=dict(id=chat, type=kind), **{"from": dict(id=sender)}))


class CommandsTest(unittest.TestCase):
    def test_only_fresh_private_authorized_commands(self):
        commands = tool.Commands(42)
        updates = [update(1, chat=43, sender=43), update(2, sender=43),
                   update(3, kind="group"), update(4, date=1), update(5, date=221),
                   update(6, "/stop"), update(7, "/reset"), update(8, "/start prova"),
                   update(9, "/status extra"), update(10)]
        self.assertEqual(commands.consume(updates, 220), ["/status"])
        self.assertEqual(commands.offset, 11)
        self.assertEqual(commands.consume(updates, 220), [])

    def test_help_is_read_only(self):
        commands = tool.Commands(42)
        self.assertEqual(commands.consume([update(1, "/start"), update(2, "/help")], 100), ["/start", "/help"])

    def test_start_guide_is_silent_and_reuses_message_after_failed_pin(self):
        api = MagicMock()
        api.send.return_value = {"message_id": 123}
        api.call.side_effect = [tool.TelegramError(429, 5), True]
        guide = tool.GuideMessage()
        with self.assertRaises(tool.TelegramError):
            guide.send(api, 42)
        guide.send(api, 42)
        api.send.assert_called_once_with(42, tool.GUIDE_TEXT + "\n\nANTEPRIMA SIMULATA")
        self.assertEqual(api.call.call_count, 2)
        api.call.assert_called_with("pinChatMessage", chat_id=42, message_id=123, disable_notification=True)
        self.assertLess(len((tool.GUIDE_TEXT + "\n\nANTEPRIMA SIMULATA").encode()), 2399)

    def test_pairing_requires_nonce_and_new_private_message(self):
        self.assertEqual(tool.pairing_chat(update(1, "/start secret")["message"], "secret", 100), 42)
        for item in [update(1, "/start"), update(1, "/start wrong"),
                     update(1, "/start secret", kind="group"), update(1, "/start secret", sender=43),
                     update(1, "/start secret", date=99)]:
            self.assertIsNone(tool.pairing_chat(item["message"], "secret", 100))


class ConfigAndApiTest(unittest.TestCase):
    def test_tls_still_verifies_certificate_and_hostname(self):
        context = tool.telegram_tls_context()
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(context.check_hostname)

    def test_missing_portable_python_defaults_use_system_ca(self):
        context = MagicMock()
        with patch.dict(os.environ, {"SSL_CERT_FILE": "", "SSL_CERT_DIR": ""}), \
             patch.object(tool.ssl, "create_default_context", return_value=context), \
             patch.object(tool.ssl, "get_default_verify_paths", return_value=SimpleNamespace(cafile=None, capath=None)), \
             patch.object(tool.Path, "is_file", return_value=True):
            self.assertIs(tool.telegram_tls_context(), context)
        context.load_verify_locations.assert_called_once_with(cafile="/etc/ssl/cert.pem")

    def test_explicit_ca_configuration_is_preserved(self):
        context = MagicMock()
        with patch.dict(os.environ, {"SSL_CERT_FILE": "/private/custom-ca.pem"}), \
             patch.object(tool.ssl, "create_default_context", return_value=context):
            self.assertIs(tool.telegram_tls_context(), context)
        context.load_verify_locations.assert_not_called()

    def test_network_errors_are_specific_and_never_include_token(self):
        cases = [(ssl.SSLCertVerificationError(1, TOKEN), "Certificato"),
                 (socket.gaierror(8, TOKEN), "DNS"), (TimeoutError(TOKEN), "timeout"),
                 (OSError(TOKEN), "Connessione HTTPS")]
        for underlying, hint in cases:
            with patch.object(tool, "urlopen", side_effect=URLError(underlying)):
                with self.assertRaises(tool.TelegramError) as captured:
                    tool.TelegramApi(TOKEN).call("getMe")
            self.assertIn(hint, str(captured.exception))
            self.assertNotIn(TOKEN, str(captured.exception))

    def test_reads_retry_transient_timeouts_but_not_credentials_or_tls(self):
        api = tool.TelegramApi(TOKEN)
        with patch.object(api, "call", side_effect=[tool.TelegramError(), {"id": 42}]) as call, \
             patch.object(tool.time, "sleep") as sleeping, patch("builtins.print"):
            self.assertEqual(api.read("getMe"), {"id": 42})
        self.assertEqual(call.call_count, 2)
        sleeping.assert_called_once_with(2)
        for error in [tool.TelegramError(401), tool.TelegramError(retryable=False)]:
            with patch.object(api, "call", side_effect=error) as call, patch.object(tool.time, "sleep") as sleeping:
                with self.assertRaises(tool.TelegramError):
                    api.read("getMe")
            self.assertEqual(call.call_count, 1)
            sleeping.assert_not_called()
        with patch.object(api, "call") as call:
            with self.assertRaises(ValueError):
                api.read("sendMessage", chat_id=42, text="test")
            call.assert_not_called()

    def test_config_is_private_and_round_trips(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "telegram_config.h"
            tool.write_config(TOKEN, 42, path)
            self.assertEqual(tool.read_config(path), (TOKEN, 42))
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            tool.write_config(TOKEN, 43, path)
            self.assertEqual(tool.read_config(path)[1], 43)
            self.assertEqual(len(list(Path(directory).iterdir())), 1)

    def test_setup_preserves_configuration_if_confirmation_times_out(self):
        api = MagicMock()
        api.read.side_effect = [{"username": "OvenControl_bot"}, {},
                                [update(1, "/start fornopair_0102030405060708")], []]
        api.send.side_effect = tool.TelegramError()
        output = io.StringIO()
        with patch.object(tool, "TelegramApi", return_value=api), \
             patch.object(tool.getpass, "getpass", return_value=TOKEN), \
             patch.object(tool.secrets, "token_hex", return_value="0102030405060708"), \
             patch.object(tool.time, "time", return_value=100), \
             patch.object(tool.time, "monotonic", side_effect=[0, 1]), \
             patch.object(tool, "write_config") as saving, patch("sys.stdout", output):
            tool.setup()
        saving.assert_called_once_with(TOKEN, 42)
        self.assertIn("configurazione e salvata", output.getvalue())
        self.assertNotIn(TOKEN, output.getvalue())

    def test_invalid_configuration_cannot_inject_header(self):
        for token in ["", "123:short", TOKEN + '\"\n#define X 1']:
            with self.assertRaises(ValueError):
                tool.TelegramApi(token)
        for chat in [-1, True, 0, 2**63]:
            with self.assertRaises(ValueError):
                tool.write_config(TOKEN, chat)

    def test_http_error_does_not_reveal_token_and_honors_retry(self):
        error = HTTPError(f"https://api.telegram.org/bot{TOKEN}/sendMessage", 429,
                          "Too many requests", {}, io.BytesIO(b'{"parameters":{"retry_after":90}}'))
        with patch.object(tool, "urlopen", side_effect=error):
            with self.assertRaises(tool.TelegramError) as captured:
                tool.TelegramApi(TOKEN).send(42, "test")
        self.assertEqual(captured.exception.retry_after, 90)
        self.assertNotIn(TOKEN, str(captured.exception))

    def test_sending_is_bounded_valid_utf8_and_has_no_remote_commands(self):
        captured = []

        def opening(request, **kwargs):
            captured.append(json.loads(request.data))
            return io.BytesIO(b'{"ok":true,"result":{"message_id":1}}')

        with patch.object(tool, "urlopen", side_effect=opening):
            tool.TelegramApi(TOKEN).send(42, "🌡" * 2000)
        self.assertLess(len(captured[0]["text"].encode()), 2400)
        self.assertEqual(set(captured[0]), {"chat_id", "text", "disable_notification"})
        self.assertIs(captured[0]["disable_notification"], True)

    def test_only_critical_messages_request_normal_notification(self):
        captured = []
        def opening(request, **kwargs):
            captured.append(json.loads(request.data))
            return io.BytesIO(b'{"ok":true,"result":{"message_id":1}}')
        with patch.object(tool, "urlopen", side_effect=opening):
            api = tool.TelegramApi(TOKEN)
            for text in ("🚀 CICLO AVVIATO", "⚠️ STOP", "Stato: 🚨 EMERGENZA ATTIVA"):
                api.send(42, text)
            for text in ("🚨 EMERGENZA", "🚨 ALLARME SONDA", "⚠️ CICLO INTERROTTO DA RIAVVIO"):
                api.send(42, text, critical=True)
        self.assertEqual([p["disable_notification"] for p in captured], [True, True, True, False, False, False])
        self.assertEqual([p["text"] for p in captured][1], "⚠️ STOP")

    def test_pdf_upload_is_multipart_with_filename_and_private_chat(self):
        captured = []
        def opening(request, **kwargs):
            captured.append((request, kwargs))
            return io.BytesIO(b'{"ok":true,"result":{"message_id":1}}')
        with patch.object(tool, "urlopen", side_effect=opening):
            tool.TelegramApi(TOKEN).send_document(42, "Prova_2026-10-09_16-30-00.pdf", b"%PDF-1.4\nreport", "📄 Report")
        request, kwargs = captured[0]
        self.assertTrue(request.full_url.endswith("/sendDocument"))
        self.assertIn("multipart/form-data; boundary=", request.get_header("Content-type"))
        self.assertIn(b'name="chat_id"\r\n\r\n42\r\n', request.data)
        self.assertIn(b'filename="Prova_2026-10-09_16-30-00.pdf"', request.data)
        self.assertIn(b'name="disable_notification"\r\n\r\ntrue\r\n', request.data)
        self.assertIn(b"Content-Type: application/pdf\r\n\r\n%PDF-1.4\nreport", request.data)
        self.assertEqual(kwargs["timeout"], 30)
        for filename, pdf in [('bad"\r\nheader.pdf', b"%PDF-1.4"), ("valid.pdf", b"wrong"),
                              ("huge.pdf", b"%PDF-" + b"x" * 1048576)]:
            with self.assertRaises(ValueError):
                tool.TelegramApi(TOKEN).send_document(42, filename, pdf)


class PreviewTest(unittest.TestCase):
    def setUp(self):
        self.device = preview.DemoDevice("full", speed=1)
        self.events = tool.PreviewEvents(dict(events=copy.deepcopy(self.device.events)))

    def consume(self):
        return self.events.consume(dict(events=copy.deepcopy(self.device.events)), self.device.status())

    def command(self, action):
        return self.device.request("POST", "command", body=dict(action=action, hardwareReady=True,
            recipeId=self.device.recipes[0]["id"]))

    def test_cycle_start_pause_resume_stop_and_no_duplicate_events(self):
        for action, title in [("start", "CICLO AVVIATO"), ("pause", "CICLO IN PAUSA"),
                              ("resume", "CICLO RIPRESO"), ("stop", "STOP")]:
            self.command(action)
            notices = self.consume()
            self.assertEqual(len(notices), 1)
            self.assertEqual(notices[0][0], title)
            self.assertFalse(notices[0][1])
            self.assertEqual(self.consume(), [])

    def test_emergency_and_probe_failure_are_alarms(self):
        self.command("start"); self.consume()
        self.command("emergency")
        self.assertEqual(self.consume()[0][:2], ("EMERGENZA", True))
        text = tool.format_status(self.device.status())
        self.assertIn("EMERGENZA SIMULATA ATTIVA", text)
        self.assertIn("Comando GPIO simulato: OFF", text)
        self.command("reset"); self.consume(); self.command("start"); self.consume()
        self.device.request("POST", "preview-sensor", body={"missing": True})
        self.device.advance(1)
        self.assertEqual(self.consume()[0][:2], ("ALLARME", True))
        self.assertIn("PT100 #2: NON VALIDA", tool.format_status(self.device.status()))

    def test_completed_program_and_preview_restart(self):
        self.command("start"); self.consume()
        # Run a valid one-minute hold in the simulator, independent of wall clock.
        self.device.active["steps"] = [{"type": "hold", "target": 24, "duration": 1}]
        self.device.step_index = 0; self.device.begin_step()
        self.device.advance(65)
        self.assertEqual(self.consume()[0][:2], ("CICLO COMPLETATO", False))
        reset = preview.DemoDevice("full")
        self.assertEqual(self.events.consume(dict(events=reset.events))[0][0], "ANTEPRIMA RIAVVIATA")

    def test_start_and_stop_between_polls_are_both_reported(self):
        self.command("start"); self.command("stop")
        self.assertEqual([n[0] for n in self.consume()], ["CICLO AVVIATO", "STOP"])

    def test_status_has_program_temperatures_and_remaining_time(self):
        self.command("start")
        status = self.device.status()
        text = tool.format_status(status)
        for wanted in [status["recipeName"], "Step: 1/", "PT100 #1:",
                       "PT100 #2:", "Media:", "Target istantaneo:", "Rimanente step (stima):", "PID: Kp", "Nessun ESP collegato"]:
            self.assertIn(wanted, text)
        self.assertEqual(tool.temperature(None), "non disponibile")
        self.assertEqual(tool.duration(None), "non disponibile")

    def test_inactive_status_hides_selected_and_previous_program(self):
        selected = self.device.status()
        selected["recipeName"] = "Programma selezionato in dashboard"
        self.assertNotIn(selected["recipeName"], tool.format_status(selected))
        self.command("start")
        previous = self.device.status()
        for phase in ("idle", "complete", "fault", "interrupted"):
            with self.subTest(phase=phase):
                status = dict(previous, phase=phase)
                text = tool.format_status(status)
                for hidden in (previous["recipeName"], "Programma:", "Step:", "Target finale:",
                               "Target istantaneo:", "Tempo ciclo totale:", "Rimanente step", "📋 CICLO ATTIVO"):
                    self.assertNotIn(hidden, text)
                self.assertIn("🌡 TEMPERATURE", text)
                self.assertIn("⚡ USCITA", text)

    def test_running_and_paused_status_keep_active_program_and_sections(self):
        for action, icon in (("start", "✅"), ("pause", "⏸️")):
            self.command(action)
            status = self.device.status()
            text = tool.format_status(status)
            self.assertTrue(text.startswith("Stato: " + icon))
            self.assertIn("Programma: " + status["recipeName"], text)
            sections = ("📋 CICLO ATTIVO", "⏱ TEMPI", "🌡 TEMPERATURE", "⚡ USCITA", "⚙️ REGOLAZIONE", "🧠 SISTEMA")
            for section in sections:
                self.assertIn("\n\n" + section + "\n", text)
            positions = [text.index(section) for section in sections]
            self.assertEqual(positions, sorted(positions))

    def test_status_icons_distinguish_normal_stop_emergency_and_probe_warnings(self):
        status = self.device.status()
        self.assertEqual(tool.status_style(status), ("💤", "IDLE · NESSUN CICLO ATTIVO"))
        self.command("stop")
        self.assertEqual(tool.status_style(self.device.status()), ("💤", "IDLE · NESSUN CICLO ATTIVO"))
        self.command("start"); self.command("stop")
        self.assertEqual(tool.status_style(self.device.status()), ("⚠️", "STOP · CICLO FERMATO"))
        self.command("emergency")
        self.assertTrue(tool.format_status(self.device.status()).startswith("Stato: 🚨 EMERGENZA ATTIVA"))
        self.assertEqual(tool.status_style(dict(status, phase="fault")), ("🚨", "IN ALLARME"))
        for faulty in (dict(status, sampleAgeMs=3000), dict(status, sensors=[])):
            self.assertEqual(tool.status_style(faulty), ("⚠️", "SONDE NON PRONTE"))

    def test_notice_icons_and_terminal_event_program_context(self):
        for title, critical, icon in (("CICLO AVVIATO", False, "🚀"), ("CICLO COMPLETATO", False, "✅"),
                                     ("STOP", False, "⚠️"), ("CICLO IN PAUSA", False, "⏸️"),
                                     ("CICLO RIPRESO", False, "⏯️"),
                                     ("CICLO INTERROTTO", True, "⚠️"), ("EMERGENZA", True, "🚨"),
                                     ("ALLARME FORNO", True, "🚨")):
            with self.subTest(title=title):
                self.assertTrue(tool.format_notice(title, critical=critical).startswith(icon + " " + title))
        self.command("start"); self.command("stop")
        status = self.device.status()
        text = tool.format_notice("STOP", "Arresto richiesto", status)
        self.assertIn("Programma: " + status["recipeName"], text)
        self.assertNotIn("STATO AL RILEVAMENTO", text)
        self.assertNotIn("Motivo:", text)
        self.assertNotIn("📋 CICLO ATTIVO", text)
        self.assertNotIn(status["recipeName"], tool.format_status(status))

    def test_routine_notices_only_contain_event_program_and_italian_event_time(self):
        self.command("start")
        started = self.consume()[0]
        # The event time survives delayed polling/delivery and uses Italian DST.
        self.assertEqual(started[3], self.device.events[-1]["utc"])
        utc = int(datetime(2026, 10, 9, 12, 30, 0, tzinfo=timezone.utc).timestamp())
        for title in ("CICLO AVVIATO", "STOP", "CICLO COMPLETATO", "CICLO IN PAUSA", "CICLO RIPRESO"):
            with self.subTest(title=title):
                text = tool.format_notice(title, "Dettagli non richiesti", self.device.status(), event_utc=utc)
                self.assertEqual(text.splitlines(), [tool.notice_icon(title) + " " + title,
                    "Programma: " + self.device.status()["recipeName"],
                    "📅 09/10/2026 14:30:00 (Italia)", "", "ANTEPRIMA SIMULATA"])
        winter = int(datetime(2026, 12, 9, 12, 30, 0, tzinfo=timezone.utc).timestamp())
        self.assertIn("09/12/2026 13:30:00", tool.format_notice("STOP", status=self.device.status(), event_utc=winter))
        self.command("stop"); self.consume()
        self.command("stop")
        self.assertEqual(self.consume(), [])

    def test_critical_notices_keep_cause_temperatures_and_output_details(self):
        self.command("start")
        self.command("emergency")
        status = self.device.status()
        for title in ("EMERGENZA", "ALLARME FORNO", "BLACKOUT · ALIMENTAZIONE RIPRISTINATA"):
            with self.subTest(title=title):
                text = tool.format_notice(title, "Uscite bloccate; riconoscimento locale richiesto.", status, True)
                for detail in ("🚨 " + title, "Programma dell'evento: " + status["recipeName"],
                               "Motivo: Uscite bloccate", "STATO AL RILEVAMENTO", "PT100 #1:",
                               "PT100 #2:", "Comando GPIO simulato: OFF", "PID: Kp"):
                    self.assertIn(detail, text)

    def test_blackout_status_has_program_estimate_and_persistent_block(self):
        status = self.device.status()
        status.update(phase="interrupted", lastInterruption=dict(recorded=True, pending=True,
            powerRelated=True, resetReason=1, programName="Programma interrotto", estimateKnown=True, upperBoundSec=300))
        text = tool.format_status(status)
        self.assertTrue(text.startswith("Stato: 🚨 BLACKOUT · CICLO BLOCCATO"))
        self.assertIn("Programma interrotto: Programma interrotto", text)
        self.assertIn("00:05:00", text)
        self.assertIn("riconoscimento e nuovo avvio manuale", text)
        self.assertEqual(tool.notice_icon("BLACKOUT · ALIMENTAZIONE RIPRISTINATA", True), "🚨")
        status["lastInterruption"].update(powerRelated=False, resetReason=3, estimateKnown=False)
        text = tool.format_status(status)
        self.assertTrue(text.startswith("Stato: 🚨 RIAVVIO · CICLO BLOCCATO"))
        self.assertNotIn("BLACKOUT", text)
        self.assertIn("durata sconosciuta", text)
        status["lastInterruption"].update(estimateKnown=True, upperBoundSec=0)
        self.assertIn("max stimato): 00:00:00", tool.format_status(status))

    def test_status_starts_with_state_without_redundant_header(self):
        for action in (None, "start", "pause", "stop", "emergency"):
            with self.subTest(action=action):
                if action:
                    self.command(action)
                text = tool.format_status(self.device.status())
                self.assertTrue(text.startswith("Stato: "))
                self.assertNotIn("FORNO · STATO", text)
                self.assertNotIn("ANTEPRIMA SIMULATA", text)
                self.assertIn("Nessun ESP collegato; nessuna uscita fisica.", text)

    def test_bridge_rejects_physical_devices_or_nonlocal_urls(self):
        self.assertEqual(tool.local_base("http://localhost:8080/full/"), "http://localhost:8080/full")
        for url in ["http://192.168.4.1/full/", "http://oven-full.local/full/", "https://example.com/full/",
                    "http://localhost/full/?token=secret", "http://user:pass@localhost/full/", "http://localhost/field-test/"]:
            with self.assertRaises(ValueError):
                tool.local_base(url)

    def test_actual_local_http_endpoints(self):
        server = preview.PreviewServer(("127.0.0.1", 0), speed=1)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_port}/full"
            self.assertTrue(tool.format_status(tool.local_json(base, "status")).startswith("Stato: 💤 IDLE"))
            self.assertIn("events", tool.local_json(base, "logs"))
        finally:
            server.shutdown(); server.server_close(); thread.join()

    def test_pdf_filename_uses_program_and_end_time_in_italy(self):
        report = dict(recipeName='Prova / 50 "C"', outcome='completed', endUtc=datetime(2026, 10, 9, 14, 30, tzinfo=timezone.utc).timestamp())
        self.assertEqual(tool.report_filename(report), "Completed_Prova_50_C_2026-10-09_16-30-00.pdf")
        report["endUtc"] = datetime(2026, 12, 9, 14, 30, tzinfo=timezone.utc).timestamp()
        for outcome, prefix in (("completed", "Completed"), ("stopped", "Stopped"), ("fault", "Error")):
            report["outcome"] = outcome
            self.assertEqual(tool.report_filename(report), prefix + "_Prova_50_C_2026-12-09_15-30-00.pdf")

    def test_reports_skip_old_and_empty_stop_capture_once_and_freeze_pdf(self):
        watcher = tool.PreviewReports(self.device.status())
        self.command("stop")
        self.assertIsNone(watcher.capture("unused", self.device.status()))
        self.command("start"); self.device.advance(20); self.command("stop")
        report = copy.deepcopy(self.device.last_report)
        report["curveAvailable"] = True
        curve = dict(key=report["key"], samples=[[1, report["startUptimeMs"], 24, 24.2, 25, 10, 11, 0]], more=False)
        calls = []
        def local(base, endpoint):
            calls.append(endpoint)
            return copy.deepcopy(curve if endpoint.startswith("report/history?") else report)
        with patch.object(tool, "local_json", side_effect=local), patch.object(tool, "preview_pdf", return_value=b"%PDF-frozen") as rendering:
            key, document = watcher.capture("http://localhost/full", self.device.status())
            self.assertEqual(key, report["key"])
            self.assertEqual(document["pdf"], b"%PDF-frozen")
            points = rendering.call_args.args[1]
            self.assertAlmostEqual(points[0]["actual"], 24.1)
            watcher.last = key
            self.assertIsNone(watcher.capture("unused", self.device.status()))
            rendering.assert_called_once()
        self.assertIsNone(tool.PreviewReports(self.device.status()).capture("unused", self.device.status()))
        self.command("start")
        self.assertEqual(document["pdf"], b"%PDF-frozen")
        self.assertEqual(len(calls), 3)

    def test_reports_reject_curve_from_another_cycle(self):
        watcher = tool.PreviewReports(self.device.status())
        self.command("start"); self.command("stop")
        report = copy.deepcopy(self.device.last_report)
        report["curveAvailable"] = True
        with patch.object(tool, "local_json", side_effect=[report, dict(key="other", samples=[], more=False)]):
            with self.assertRaises(ValueError):
                watcher.capture("unused", self.device.status())
        self.assertEqual(watcher.last, "")

    def test_preview_pdf_reuses_dashboard_export(self):
        self.command("start"); self.device.advance(20); self.command("stop")
        report = copy.deepcopy(self.device.last_report)
        pdf = tool.preview_pdf(report, [dict(seq=1, ms=report["startUptimeMs"], t1=24, t2=24.2,
            actual=24.1, target=25, duty=10, step=0, marker=16)])
        self.assertTrue(pdf.startswith(b"%PDF-"))
        self.assertLess(len(pdf), 1048576)

    def test_pdf_capture_from_real_preview_http_includes_only_ended_cycle_curve(self):
        server = preview.PreviewServer(("127.0.0.1", 0), speed=1)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            d = server.devices["full"]
            watcher = tool.PreviewReports(d.status())
            body = dict(action="start", hardwareReady=True, recipeId=d.recipes[0]["id"])
            with server.lock:
                d.request("POST", "command", body=body); d.advance(120)
                d.request("POST", "command", body=dict(action="stop"))
                ended = d.status()
                start, end = d.last_report["startUptimeMs"], d.last_report["endUptimeMs"]
                d.advance(30); d.request("POST", "command", body=body); d.advance(30)
            with patch.object(tool, "preview_pdf", return_value=b"%PDF-frozen") as rendering:
                captured = watcher.capture(f"http://127.0.0.1:{server.server_port}/full", ended)
                self.assertIsNotNone(captured)
                report, points = rendering.call_args.args
                self.assertGreater(len(points), 5)
                self.assertTrue(all(start <= p["ms"] <= end for p in points))
                self.assertEqual(report["curveSamples"], len(points))
                self.assertFalse(report["curvePartial"])
        finally:
            server.shutdown(); server.server_close(); thread.join()


if __name__ == "__main__":
    unittest.main()
