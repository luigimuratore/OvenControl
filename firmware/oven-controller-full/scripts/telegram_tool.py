#!/usr/bin/env python3
"""Pair a private bot, send a test, or bridge the local simulated dashboard.

Python standard library only. Token is read privately, never printed or passed
on the command line. The preview bridge performs only GETs on localhost.
"""
import argparse
from collections import deque
from datetime import datetime, timezone
import getpass
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "include/telegram_config.h"
TOKEN_PATTERN = r"[0-9]+:[A-Za-z0-9_-]{20,150}"
# Share the firmware's guide verbatim; edit kGuideText in telegram_policy.h.
GUIDE_TEXT = re.search(r'R"guide\((.*?)\)guide"',
    (ROOT / "src/telegram_policy.h").read_text(), re.S)[1]


class TelegramError(Exception):
    def __init__(self, code=0, retry_after=0, reason=None, retryable=None):
        self.code, self.retry_after = code, retry_after
        self.retryable = (code == 0 or code == 429 or code >= 500) if retryable is None else retryable
        super().__init__(reason or f"Telegram non disponibile (codice {code}); verifica rete/configurazione.")


def telegram_tls_context():
    """Keep TLS verification and repair portable Python's absent default CA path."""
    try:
        context = ssl.create_default_context()
        # Preserve an explicitly configured trust store, including private CAs.
        if os.environ.get("SSL_CERT_FILE") or os.environ.get("SSL_CERT_DIR"):
            return context
        paths = ssl.get_default_verify_paths()
        if not paths.cafile and not paths.capath:
            for name in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt"):
                if Path(name).is_file():
                    context.load_verify_locations(cafile=name)
                    return context
            # Optional fallback if already installed; no dependency installation.
            try:
                import certifi
            except ImportError:
                pass
            else:
                context.load_verify_locations(cafile=certifi.where())
        return context
    except OSError:
        raise TelegramError(reason="Impossibile caricare i certificati HTTPS di Python. Verifica SSL_CERT_FILE/SSL_CERT_DIR.", retryable=False) from None


def connection_error(error):
    """Classify errors without printing URLs, request data or the bot token."""
    reason = error.reason if isinstance(error, URLError) else error
    if isinstance(reason, ssl.SSLCertVerificationError):
        return TelegramError(reason="Certificato HTTPS di Telegram non verificabile: controlla i certificati CA di Python o della rete/proxy.", retryable=False)
    if isinstance(reason, socket.gaierror):
        return TelegramError(reason="Indirizzo api.telegram.org non risolto: controlla Internet e DNS.")
    if isinstance(reason, TimeoutError):
        return TelegramError(reason="Telegram non risponde entro il timeout: controlla Internet, firewall e proxy.")
    return TelegramError(reason="Connessione HTTPS a Telegram non riuscita: controlla Internet, firewall e proxy.")


class TelegramApi:
    def __init__(self, token):
        if not re.fullmatch(TOKEN_PATTERN, token) or len(token) > 160:
            raise ValueError("Formato del token Telegram non valido.")
        self._token = token
        self._tls = telegram_tls_context()

    def call(self, method, **payload):
        return self._request(method, json.dumps(payload).encode(), "application/json")

    def _request(self, method, data, content_type, timeout=8):
        request = Request(f"https://api.telegram.org/bot{self._token}/{method}",
                          data=data, headers={"Content-Type": content_type})
        try:
            with urlopen(request, timeout=timeout, context=self._tls) as response:
                raw = response.read(131073)
        except HTTPError as error:
            retry = 0
            try:
                retry = json.loads(error.read(8192)).get("parameters", {}).get("retry_after", 0)
            except (ValueError, OSError):
                pass
            raise TelegramError(error.code, retry) from None
        except (URLError, OSError) as error:
            raise connection_error(error) from None
        try:
            if len(raw) > 131072:
                raise ValueError()
            data = json.loads(raw)
            if not data.get("ok"):
                raise TelegramError(data.get("error_code", 0), data.get("parameters", {}).get("retry_after", 0))
            return data["result"]
        except (ValueError, KeyError):
            raise TelegramError(reason="Telegram ha restituito una risposta non valida.", retryable=False) from None

    def read(self, method, **payload):
        """Retry read operations; never automatically repeat a sendMessage here."""
        if method not in ("getMe", "getWebhookInfo", "getUpdates"):
            raise ValueError("Metodo non disponibile per lettura con ritentativi.")
        for attempt in range(3):
            try:
                return self.call(method, **payload)
            except TelegramError as error:
                if not error.retryable or attempt == 2:
                    raise
                delay = max(error.retry_after, 2 * (attempt + 1))
                print(f"{error} Nuovo tentativo tra {delay} s.", flush=True)
                time.sleep(delay)

    def send(self, chat, text, critical=False):
        # Native messages are bounded at 2400 UTF-8 bytes; use the same limit here.
        bounded = text.encode()[:2399].decode(errors="ignore")
        return self.call("sendMessage", chat_id=chat, text=bounded, disable_notification=not critical)

    def send_document(self, chat, filename, pdf, caption=""):
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,120}\.pdf", filename) or not pdf.startswith(b"%PDF-") or len(pdf) > 1048576:
            raise ValueError("Allegato PDF non valido o troppo grande.")
        boundary = "OvenPdf" + secrets.token_hex(16)
        prefix = (f'--{boundary}\r\nContent-Disposition: form-data; name="chat_id"\r\n\r\n{chat}\r\n'
                  f'--{boundary}\r\nContent-Disposition: form-data; name="disable_notification"\r\n\r\ntrue\r\n'
                  f'--{boundary}\r\nContent-Disposition: form-data; name="caption"\r\n\r\n{caption[:1024]}\r\n'
                  f'--{boundary}\r\nContent-Disposition: form-data; name="document"; filename="{filename}"\r\n'
                  'Content-Type: application/pdf\r\n\r\n').encode()
        return self._request("sendDocument", prefix + pdf + f"\r\n--{boundary}--\r\n".encode(),
                             "multipart/form-data; boundary=" + boundary, timeout=30)


def read_config(path=CONFIG):
    try:
        contents = Path(path).read_text()
    except OSError:
        raise ValueError("Configurazione mancante: esegui python3 scripts/telegram_tool.py --setup") from None
    token = re.search(r'^#define OVEN_TELEGRAM_TOKEN "([^"]*)"$', contents, re.M)
    chat = re.search(r'^#define OVEN_TELEGRAM_CHAT_ID "([0-9]+)"$', contents, re.M)
    if not token or not re.fullmatch(TOKEN_PATTERN, token[1]) or len(token[1]) > 160 or not chat or not 0 < int(chat[1]) <= 2**63 - 1:
        raise ValueError("Configurazione Telegram non valida: riesegui --setup.")
    return token[1], int(chat[1])


def write_config(token, chat, path=CONFIG):
    if not re.fullmatch(TOKEN_PATTERN, token) or len(token) > 160 or type(chat) is not int or not 0 < chat <= 2**63 - 1:
        raise ValueError("Token o chat privata non validi.")
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix=".telegram-", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w") as output:
            output.write('#pragma once\n\n// Private file, excluded from Git.\n'
                         f'#define OVEN_TELEGRAM_TOKEN "{token}"\n'
                         f'#define OVEN_TELEGRAM_CHAT_ID "{chat}"\n')
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def private_message(message, chat=None):
    identity = message.get("chat", {}).get("id")
    return (type(identity) is int and identity > 0 and message.get("chat", {}).get("type") == "private"
            and message.get("from", {}).get("id") == identity and (chat is None or identity == chat))


def pairing_chat(message, nonce, started):
    if (private_message(message) and message.get("date", 0) >= started
            and message.get("text") == f"/start {nonce}"):
        return message["chat"]["id"]
    return None


def setup():
    print("Crea un bot dedicato con /newbot nella chat ufficiale @BotFather.")
    token = getpass.getpass("Token (nascosto mentre scrivi): ").strip()
    api = TelegramApi(token)
    bot = api.read("getMe")
    if api.read("getWebhookInfo").get("url"):
        raise ValueError("Questo bot ha un webhook attivo. Usa un nuovo bot dedicato al forno.")
    nonce = "fornopair_" + secrets.token_hex(8)
    print(f"Apri https://t.me/{bot['username']}?start={nonce} e premi AVVIA nella tua chat privata.")
    print(f"Oppure invia al bot: /start {nonce}")
    started, offset = int(time.time()), -1
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        try:
            updates = api.read("getUpdates", offset=offset, timeout=3, limit=10, allowed_updates=["message"])
        except TelegramError as error:
            if not error.retryable:
                raise
            print(f"{error} Associazione ancora in attesa; riprovo.", flush=True)
            time.sleep(min(5, max(0, deadline - time.monotonic())))
            continue
        for update in updates:
            offset = update["update_id"] + 1
            message = update.get("message", {})
            chat = pairing_chat(message, nonce, started)
            if chat is not None:
                write_config(token, chat)
                print("Chat privata collegata. Salvato include/telegram_config.h (escluso da Git).", flush=True)
                # Confirm updates so the ESP/preview starts from an empty backlog.
                try:
                    api.read("getUpdates", offset=offset, timeout=0, limit=1, allowed_updates=["message"])
                    api.send(chat, "✅ FORNO · Chat collegata\nConfigurazione completata sul Mac.\n/status rispondera quando avvii l'anteprima Telegram o installi il firmware configurato.")
                except TelegramError as error:
                    print(f"{error} La configurazione e salvata; verifica la ricezione con --check.")
                return
        time.sleep(.5)
    raise ValueError("Associazione scaduta. Riesegui --setup e premi AVVIA dal link mostrato.")


def local_base(url):
    parsed = urlsplit(url)
    if (parsed.scheme != "http" or parsed.hostname not in ("localhost", "127.0.0.1", "::1")
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or parsed.path.rstrip("/") != "/full"):
        raise ValueError("--preview richiede l'anteprima locale, per esempio http://localhost:8080/full/")
    return url.rstrip("/")


def local_json(base, endpoint):
    try:
        with urlopen(base + "/api/" + endpoint, timeout=3) as response:
            return json.load(response)
    except (URLError, OSError, ValueError):
        raise ValueError("Anteprima locale non raggiungibile: avvia ../local-preview/server.py") from None


def report_filename(report, ended_at=None):
    name = re.sub(r"[^A-Za-z0-9-]+", "_", report.get("recipeName", "")).strip("_")[:48] or "Programma"
    ended = report.get("endUtc")
    date = datetime.fromtimestamp(ended, timezone.utc) if ended else ended_at or datetime.now(timezone.utc)
    prefix = {"completed": "Completed", "stopped": "Stopped"}.get(report.get("outcome"), "Error")
    return prefix + "_" + name + "_" + date.astimezone(ZoneInfo("Europe/Rome")).strftime("%Y-%m-%d_%H-%M-%S") + ".pdf"


def preview_pdf(report, points):
    node = shutil.which("node")
    if not node:
        bundled = Path("/Applications/ChatGPT.app/Contents/Resources/cua_node/bin/node")
        node = str(bundled) if bundled.is_file() else None
    if not node:
        raise ValueError("PDF Telegram dell'anteprima: serve Node.js (il firmware ESP non lo richiede).")
    try:
        result = subprocess.run([node, str(ROOT / "scripts/report_pdf.cjs")],
            input=json.dumps(dict(report=report, points=points), allow_nan=False).encode(),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20, check=True)
        if not result.stdout.startswith(b"%PDF-") or len(result.stdout) > 1048576:
            raise ValueError()
        return result.stdout
    except (OSError, subprocess.SubprocessError, ValueError):
        raise ValueError("Generazione PDF dell'anteprima non riuscita; report disponibile dalla dashboard.") from None


class PreviewReports:
    def __init__(self, status):
        # Existing reports predate this connection; do not resend on every restart.
        self.last = status.get("reportKey", "")

    def capture(self, base, status):
        key = status.get("reportKey", "")
        if not status.get("reportAvailable") or not key or key == self.last:
            return None
        report = local_json(base, "report")
        if report.get("key") != key:
            raise ValueError("Il report è cambiato durante l'acquisizione; nuovo tentativo alla prossima lettura.")
        points, after = [], 0
        # Older local preview servers expose only the dashboard history ring.
        # Filter by this report's boot/time span and recheck identity afterwards.
        legacy_curve = "curveAvailable" not in report
        if report.get("curveAvailable") or legacy_curve:
            for _ in range(30):
                endpoint = f"history?after={after}" if legacy_curve else f"report/history?key={key}&after={after}"
                page = local_json(base, endpoint)
                if (legacy_curve and page.get("bootCount") != report.get("bootCount")) or (not legacy_curve and page.get("key") != key):
                    raise ValueError("La curva non corrisponde al report.")
                for row in page.get("samples", []):
                    seq, ms, t1, t2, target, duty, flags, step = row
                    if seq <= after or len(points) >= 4321:
                        raise ValueError("Paginazione curva report non valida.")
                    after = seq
                    if legacy_curve and ((ms - report["startUptimeMs"]) & 0xffffffff) > ((report["endUptimeMs"] - report["startUptimeMs"]) & 0xffffffff):
                        continue
                    first, second = t1 if flags & 1 else None, t2 if flags & 2 else None
                    points.append(dict(seq=seq, ms=ms, t1=first, t2=second,
                        actual=(first + second) / 2 if first is not None and second is not None else None,
                        target=target if flags & 8 else None, duty=duty, step=step, marker=flags & 0xf0))
                if not page.get("more"):
                    break
                if not page.get("samples"):
                    raise ValueError("Paginazione curva report non valida.")
            else:
                raise ValueError("Curva report troppo grande.")
            if legacy_curve:
                report.update(curveAvailable=bool(points), curveSamples=len(points), curveIntervalMs=10000,
                              curvePartial=not any(p["marker"] & 16 for p in points))
        if local_json(base, "report").get("key") != key:
            raise ValueError("Il report è cambiato durante l'acquisizione.")
        document = dict(filename=report_filename(report), pdf=preview_pdf(report, points),
                        caption="📄 Report di fine ciclo · ANTEPRIMA SIMULATA")
        # Retried sends own this immutable PDF; a subsequent cycle cannot change it.
        return key, document


def duration(seconds):
    if not isinstance(seconds, (float, int)) or not math.isfinite(seconds):
        return "non disponibile"
    seconds = max(0, int(seconds))
    return f"{seconds // 3600:02d}:{seconds // 60 % 60:02d}:{seconds % 60:02d}"


def temperature(value):
    return f"{value:.2f} °C" if isinstance(value, (float, int)) and math.isfinite(value) else "non disponibile"


def status_style(status):
    phase = status.get("phase", "idle")
    if status.get("emergency"):
        return "🚨", "EMERGENZA ATTIVA"
    if phase == "fault" or status.get("fault"):
        return "🚨", "IN ALLARME"
    if phase == "interrupted":
        interrupted = status.get("lastInterruption", {})
        power = interrupted.get("powerRelated", interrupted.get("resetReason") in (1, 9))
        return "🚨", "BLACKOUT · CICLO BLOCCATO" if power else "RIAVVIO · CICLO BLOCCATO"
    if status.get("otaUpdating"):
        return "⚠️", "AGGIORNAMENTO IN CORSO"
    if status.get("test", {}).get("enabled"):
        return "⚠️", "MODALITÀ TEST"
    sensors = status.get("sensors", [])
    valid = [sensor.get("c") for sensor in sensors if sensor.get("valid")
             and isinstance(sensor.get("c"), (int, float)) and math.isfinite(sensor["c"])]
    if len(valid) != 2 or abs(valid[0] - valid[1]) > 10 or max(valid) >= 415 or status.get("sampleAgeMs", 0) >= 3000:
        return "⚠️", "SONDE NON PRONTE"
    if phase == "paused":
        return "⏸️", "CICLO IN PAUSA"
    if phase == "running":
        return "✅", "CICLO IN CORSO"
    if phase == "complete":
        return "✅", "CICLO COMPLETATO"
    if phase == "idle":
        if status.get("cycleStartedAtMs") is not None:
            return "⚠️", "STOP · CICLO FERMATO"
        return "💤", "IDLE · NESSUN CICLO ATTIVO"
    return "⚠️", "STATO NON DISPONIBILE"


def notice_icon(title, critical=False):
    if any(word in title for word in ("EMERGENZA", "ALLARME", "BLACKOUT")):
        return "🚨"
    cycle_icons = {"CICLO AVVIATO": "🚀", "CICLO IN PAUSA": "⏸️", "CICLO RIPRESO": "⏯️"}
    if title in cycle_icons:
        return cycle_icons[title]
    if any(word in title for word in ("STOP", "FERMATO", "PAUSA", "INTERROT", "INTERRUZ", "RIAVVI", "AVVISO", "NON RAGGIUNGIBILE")):
        return "⚠️"
    return "🚨" if critical else "✅"


def format_status(status):
    icon, state = status_style(status)
    lines = [f"Stato: {icon} {state}", "Aggiornato: " + datetime.now(timezone.utc).strftime("%d/%m/%Y %H:%M:%S UTC")]
    if status.get("emergency"):
        lines += ["", "🚨 EMERGENZA SIMULATA ATTIVA", "Riconoscimento dalla dashboard richiesto."]
    if status.get("fault"):
        lines.append("Motivo: " + status["fault"][:300])
    if status.get("test", {}).get("enabled"):
        lines += ["", "🧪 TEST", status["test"].get("name", "")]
    # recipeName can retain a selected/previous program after STOP or completion.
    # Only a running/paused cycle owns an active program and step.
    if status.get("phase") in ("running", "paused"):
        lines += ["", "📋 CICLO ATTIVO", "Programma: " + (status.get("recipeName") or "non disponibile")]
        if status.get("stepIndex", 0) < status.get("stepCount", 0):
            kind = {"ramp": "Rampa", "hold": "Mantenimento", "cooldown": "Cooldown passivo"}.get(status.get("stepType"), "non disponibile")
            lines += [f"Step: {status.get('stepIndex', 0) + 1}/{status.get('stepCount')} · {kind}",
                      "Target finale: " + temperature(status.get("stepFinalTarget"))]
            if status.get("stepType") == "hold":
                lines.append(f"Hold ±5 °C: {duration(status.get('holdInBandSec'))} / {duration(status.get('holdRequiredSec'))}")
            else:
                lines.append(f"Rate: {status.get('stepRate')} °C/min")
        lines += ["Target istantaneo: " + temperature(status.get("target")), "", "⏱ TEMPI",
                  "Tempo ciclo totale: " + duration(status.get("cycleElapsedSec")),
                  "Rimanente step" + (" (stima)" if status.get("stepRemainingEstimated") else "") + ": " + duration(status.get("stepRemainingSec"))]
    lines += ["", "🌡 TEMPERATURE"]
    valid = []
    for i, sensor in enumerate(status.get("sensors", []), 1):
        lines.append(f"PT100 #{i}: " + (temperature(sensor.get("c")) if sensor.get("valid") else f"NON VALIDA · fault 0x{sensor.get('faultCode', 0):x} · RAW {sensor.get('raw', 0)}"))
        if sensor.get("valid") and isinstance(sensor.get("c"), (float, int)) and math.isfinite(sensor["c"]):
            valid.append(sensor["c"])
    if len(valid) == 2:
        lines += ["Media: " + temperature(sum(valid) / 2), f"Differenza sonde: {abs(valid[0] - valid[1]):.2f} °C"]
    age = status.get("sampleAgeMs", 0)
    lines += [f"Età letture: {age} ms" + (" · ⚠️ SCADUTE" if age >= 3000 else ""), "", "⚡ USCITA",
              f"Comando GPIO simulato: {'ON' if status.get('relay') else 'OFF'}",
              f"Comando PID: {status.get('duty', 0):.1f}%", "", "⚙️ REGOLAZIONE",
              f"Attuatore: {'SSR' if status.get('actuator') == 'ssr' else 'Relè'}",
              f"Finestra: {status.get('windowSec')} s · limite: {status.get('maxPower')}%"]
    pid = status.get("pid", {})
    lines.append(f"PID: Kp {pid.get('kp')} · Ki {pid.get('ki')} · Kd {pid.get('kd')}")
    interruption = status.get("lastInterruption", {})
    historical_error = status.get("lastCycleError")
    if (historical_error and historical_error != status.get("fault")) or interruption.get("recorded"):
        lines += ["", "⚠️ ERRORI REGISTRATI"]
        if historical_error and historical_error != status.get("fault"):
            lines.append("Ultimo errore di ciclo: " + historical_error[:240])
        if interruption.get("recorded"):
            power = interruption.get("powerRelated", interruption.get("resetReason") in (1, 9))
            lines.append(f"🚨 {'BLACKOUT / alimentazione ESP' if power else 'Interruzione da riavvio'} · reset {interruption.get('resetReason')}")
            if interruption.get("programName"):
                lines.append("Programma interrotto: " + interruption["programName"])
            bound = interruption.get("upperBoundSec", 0)
            known = interruption.get("estimateKnown", bool(bound))
            lines.append("Intervallo fino al riavvio (max stimato): " + (duration(bound) if known else "durata sconosciuta"))
            if interruption.get("pending"):
                lines.append("Uscite bloccate; riconoscimento e nuovo avvio manuale richiesti.")
    lines += ["", "🧠 SISTEMA", "Uptime simulato: " + duration(status.get("uptimeMs", 0) / 1000),
              f"Avvio: #{status.get('bootCount', 0)}",
              f"RAM libera/min: {status.get('freeHeap', 0)} / {status.get('minFreeHeap', 0)} B",
              "", "Nessun ESP collegato; nessuna uscita fisica."]
    return "\n".join(lines)


def format_notice(title, reason="", status=None, critical=False, event_utc=None):
    date = datetime.fromtimestamp(event_utc, timezone.utc) if event_utc else datetime.now(timezone.utc)
    stamp = date.astimezone(ZoneInfo("Europe/Rome")).strftime("%d/%m/%Y %H:%M:%S (Italia)")
    lines = [f"{notice_icon(title, critical)} {title}"]
    if not critical:
        cycle_event = title in ("CICLO AVVIATO", "STOP", "CICLO COMPLETATO", "CICLO IN PAUSA", "CICLO RIPRESO")
        if cycle_event and status and status.get("recipeName") and (
                status.get("phase") in ("running", "paused") or status.get("cycleStartedAtMs") is not None):
            lines.append("Programma: " + status["recipeName"])
        return "\n".join(lines + ["📅 " + stamp, "", "ANTEPRIMA SIMULATA"])
    lines += ["📅 " + stamp, "ANTEPRIMA SIMULATA"]
    if status and status.get("recipeName") and status.get("cycleStartedAtMs") is not None and status.get("phase") not in ("running", "paused"):
        lines += ["", "Programma dell'evento: " + status["recipeName"]]
    if reason:
        lines.append("Motivo: " + reason)
    if status is not None:
        lines += ["", "📍 STATO AL RILEVAMENTO", format_status(status)]
    return "\n".join(lines)


class GuideMessage:
    """Keep the sent message ID when pinning needs a retry, avoiding duplicates."""
    def __init__(self):
        self.message_id = None

    def send(self, api, chat):
        if self.message_id is None:
            self.message_id = api.send(chat, GUIDE_TEXT + "\n\nANTEPRIMA SIMULATA")["message_id"]
        api.call("pinChatMessage", chat_id=chat, message_id=self.message_id, disable_notification=True)


class Commands:
    def __init__(self, chat):
        self.chat, self.offset = chat, -1

    def consume(self, updates, now):
        commands = []
        for update in updates:
            ident = update.get("update_id", -1)
            if ident < 0 or ident < self.offset:
                continue
            self.offset = ident + 1
            message = update.get("message", {})
            date = message.get("date", 0)
            if not private_message(message, self.chat) or not (0 < date <= now and now - date <= 120):
                continue
            text = message.get("text")
            if text in ("/status", "/start", "/help"):
                commands.append(text)
        return commands


class PreviewEvents:
    def __init__(self, logs, status=None):
        self.last = max((e["seq"] for e in logs.get("events", [])), default=0)
        self.phase = (status or {}).get("phase")

    def consume(self, logs, status=None):
        result = []
        mapping = {"start": "CICLO AVVIATO", "stop": "STOP", "pause": "CICLO IN PAUSA",
                   "resume": "CICLO RIPRESO", "emergency": "EMERGENZA"}
        latest = max((e["seq"] for e in logs.get("events", [])), default=0)
        if latest < self.last:  # Preview server restarted: ignore its old baseline.
            self.last = latest
            self.phase = (status or {}).get("phase")
            return [("ANTEPRIMA RIAVVIATA", False, "Stato simulato azzerato.", None)]
        for event in logs.get("events", []):
            if event["seq"] <= self.last:
                continue
            self.last = event["seq"]
            message = event.get("message", "")
            action = message.removeprefix("Comando simulato: ")
            event_utc = event.get("utc")
            if action in mapping:
                # Like the ESP, STOP without an active cycle is not a cycle-stop event.
                if action != "stop" or self.phase in ("running", "paused"):
                    result.append((mapping[action], action == "emergency", message, event_utc))
                self.phase = {"start": "running", "stop": "idle", "pause": "paused",
                              "resume": "running", "emergency": "fault"}[action]
            elif event.get("level") == "ALLARME":
                result.append(("ALLARME", True, message, event_utc))
            elif message == "Ciclo simulato terminato.":
                result.append(("CICLO COMPLETATO", False, message, event_utc))
                self.phase = "complete"
        if status is not None:
            # The preview records probe faults as SONDA, without an ALLARME log.
            if status.get("phase") == "fault" and self.phase != "fault" and not any(n[1] for n in result):
                result.append(("ALLARME", True, status.get("fault", "Allarme simulato"), None))
            self.phase = status.get("phase")
        return result


def preview(api, chat, url):
    base = local_base(url)
    if api.read("getWebhookInfo").get("url"):
        raise ValueError("Bot con webhook attivo: usa un bot dedicato.")
    commands = Commands(chat)
    events = PreviewEvents(local_json(base, "logs"), local_json(base, "status"))
    reports = PreviewReports(local_json(base, "status"))
    pending = deque(maxlen=32)

    def enqueue(text, priority=0):
        if len(pending) == pending.maxlen:
            # Preserve queued alarms when dropping a routine preview message.
            expendable = next((item for item in pending if item[0] < 2), None)
            if expendable is None:
                print("Coda anteprima piena: messaggio non accodato.", flush=True)
                return
            pending.remove(expendable)
        pending.append((priority, time.monotonic(), text))

    enqueue(format_notice("COLLEGAMENTO ATTIVO", status=local_json(base, "status")))
    print("Bot collegato all'anteprima. Invia /status; usa la dashboard per avvio/STOP/emergenza. Ctrl+C per chiudere.", flush=True)
    next_local = next_poll = next_send = retry_until = 0
    unavailable, failures = False, 0
    while True:
        now = time.monotonic()
        if now >= next_local:
            next_local = now + 1
            try:
                logs, status = local_json(base, "logs"), local_json(base, "status")
                for title, critical, reason, event_utc in events.consume(logs, status):
                    enqueue(format_notice(title, reason, status, critical, event_utc), 2 if critical else 0)
                try:
                    captured = reports.capture(base, status)
                    if captured:
                        key, document = captured
                        if sum(isinstance(p[2], dict) for p in pending) < 3:
                            enqueue(document, -1)
                            reports.last = key
                        else:
                            print("Coda PDF anteprima piena; report disponibile dalla dashboard.", flush=True)
                            reports.last = key
                except ValueError as error:
                    print(str(error), flush=True)
                if unavailable:
                    enqueue(format_notice("DASHBOARD NUOVAMENTE RAGGIUNGIBILE"))
                unavailable = False
            except ValueError:
                if not unavailable:
                    enqueue(format_notice("DASHBOARD LOCALE NON RAGGIUNGIBILE"), 2)
                unavailable = True
        if now >= retry_until:
            try:
                if now >= next_poll:
                    next_poll = now + 2
                    updates = api.call("getUpdates", offset=commands.offset, limit=10, timeout=0, allowed_updates=["message"])
                    for command in commands.consume(updates, int(time.time())):
                        if command == "/status":
                            try:
                                text = format_status(local_json(base, "status"))
                            except ValueError:
                                text = format_notice("DASHBOARD LOCALE NON RAGGIUNGIBILE", "Nessuno stato aggiornato disponibile.")
                            enqueue(text, 1)
                        elif command == "/start":
                            enqueue(GuideMessage())
                        else:
                            enqueue(GUIDE_TEXT + "\n\nANTEPRIMA SIMULATA", 1)
                if now >= next_send and pending:
                    # Replies expire after 10 s; notices after 1 h; PDFs after 24 h.
                    for item in list(pending):
                        if item[0] < 2 and now - item[1] > (10 if item[0] == 1 else 86400 if item[0] == -1 else 3600):
                            pending.remove(item)
                    if pending:
                        item = max(pending, key=lambda p: p[0])
                        if isinstance(item[2], dict):
                            api.send_document(chat, **item[2])
                        elif isinstance(item[2], GuideMessage):
                            item[2].send(api, chat)
                        else:
                            api.send(chat, item[2], critical=item[0] == 2)
                        pending.remove(item)
                        next_send = time.monotonic() + 1.1
                failures = 0
            except TelegramError as error:
                failures += 1
                delay = max(error.retry_after, (5, 15, 30, 60)[min(failures - 1, 3)])
                retry_until = time.monotonic() + delay
                print(f"{error} Nuovo tentativo tra {delay} s.", flush=True)
        time.sleep(.1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--setup", action="store_true", help="Associa il bot alla tua chat privata e salva la configurazione")
    mode.add_argument("--check", action="store_true", help="Invia un messaggio di prova dal Mac")
    mode.add_argument("--preview", metavar="URL", help="Collega il bot alla dashboard simulata su localhost")
    args = parser.parse_args()
    try:
        if args.setup:
            setup()
        else:
            token, chat = read_config()
            api = TelegramApi(token)
            if args.check:
                api.read("getMe")
                api.send(chat, "✅ PROVA TELEGRAM DAL MAC\nRicezione notifiche verificata.\nQuesto messaggio non proviene dall'ESP.")
                print("Messaggio di prova inviato alla tua chat.")
            else:
                preview(api, chat, args.preview)
    except (ValueError, TelegramError) as error:
        parser.exit(1, str(error) + "\n")
    except KeyboardInterrupt:
        print("\nProva Telegram terminata.")


if __name__ == "__main__":
    main()
