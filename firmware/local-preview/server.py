#!/usr/bin/env python3
"""Serve the original dashboards with isolated, in-memory simulated devices."""
import argparse
import copy
import json
import math
import re
import secrets
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PROJECTS = {"full": "oven-controller-full", "field-test": "oven-controller-field-test"}


class ApiError(Exception):
    def __init__(self, code, message):
        self.code, self.message = code, message


def require(condition, message, code=400):
    if not condition:
        raise ApiError(code, message)


def number(value, low, high):
    return type(value) in (int, float) and math.isfinite(value) and low <= value <= high


def validate_recipes(recipes):
    require(isinstance(recipes, list) and 1 <= len(recipes) <= 8, "Servono 1–8 ricette")
    ids = set()
    for r in recipes:
        require(isinstance(r, dict), "Ricetta non valida")
        ident, name, steps = r.get("id"), r.get("name"), r.get("steps")
        require(isinstance(ident, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,32}", ident) and ident not in ids, "ID ricetta non valido o duplicato")
        require(isinstance(name, str) and bool(name.strip()) and len(name.encode("utf-8")) <= 48, "Nome ricetta non valido")
        require(isinstance(steps, list) and 1 <= len(steps) <= 12, "Servono 1–12 step")
        ids.add(ident)
        previous = None
        for s in steps:
            require(isinstance(s, dict), "Step non valido")
            kind, target = s.get("type"), s.get("target")
            require(kind in ("ramp", "hold", "cooldown") and number(target, 0, 190), "Tipo o target non valido")
            if kind == "hold":
                require(type(s.get("duration")) is int and 1 <= s["duration"] <= 360, "Hold: 1–360 minuti interi")
            else:
                require(number(s.get("rate"), .1, 1e30), "Rate minimo: 0,1 °C/min")
            if previous is not None:
                require((kind == "ramp" and target > previous) or (kind == "cooldown" and target < previous) or
                        (kind == "hold" and abs(target - previous) <= .1), "Sequenza target non coerente")
            previous = target


class DemoDevice:
    """Interface preview, not an emulator of the ESP PID or the physical oven."""
    def __init__(self, mode, speed=10):
        self.mode, self.speed, self.now = mode, speed if mode == "full" else 1, 600.0
        self.phase, self.fault, self.temperature, self.missing = "idle", "", 24.0, False
        self.control = {"actuator": "relay", "windowSec": 60, "maxPower": 30}
        self.pid = {"kp": 12, "ki": .015, "kd": 50}
        self.recipes = json.loads((ROOT / "oven-controller-full/examples/recipes.json").read_text())
        self.active, self.step_index = None, 0
        self.cycle_at = self.step_at = self.now
        self.step_elapsed = self.hold = 0.0
        self.was_in_band = False
        self.target, self.duty, self.relay = None, 0.0, False
        self.armed, self.token, self.action = False, 0, ""
        self.arm_at = self.heartbeat_at = self.test_at = self.now
        self.test_duration = 0.0
        self.samples, self.events, self.seq, self.event_seq = [], [], 0, 0
        self.log("ANTEPRIMA", "Dati simulati: nessun ESP collegato e nessuna uscita fisica.")
        for at in range(0, 601, 10 if mode == "full" else 1):
            self.record(at)
        self.last_sample = self.now

    def log(self, level, message):
        self.event_seq += 1
        self.events.append(dict(seq=self.event_seq, atMs=int(self.now * 1000), utc=int(time.time()),
                                level=level, message=message, saved=False))
        self.events = self.events[-100:]

    def sensors(self, at=None):
        at = self.now if at is None else at
        result = []
        for i, c in enumerate([self.temperature + .08 * math.sin(at / 13), self.temperature + .2]):
            bad = self.missing and i == 1
            ohms = 100 * (1 + 3.9083e-3 * c - 5.775e-7 * c * c)
            result.append(dict(c=None if bad else round(c, 3), valid=not bad, ohms=470 if bad else round(ohms, 3),
                               raw=32767 if bad else round(ohms / 470 * 32768), faultCode=144 if bad else 0, rref=470,
                               message="Fault simulato: circuito aperto" if bad else "Lettura simulata"))
        return result

    def outputs(self):
        elapsed = self.now - self.test_at
        relay = self.action == "relayPulse" or self.action == "relaySequence" and elapsed % 3 < 1
        red = relay or self.action in ("ledRed", "ledBoth") or self.action == "ledSequence" and int(elapsed // 2) in (2, 3)
        green = self.action not in ("ledRed", "ledOff", "ledSequence") or self.action == "ledSequence" and int(elapsed // 2) in (1, 3)
        return bool(relay), bool(red), bool(green)

    def record(self, at=None, marker=0):
        at = self.now if at is None else at
        self.seq += 1
        a, b = self.sensors(at)
        flags = int(a["valid"]) | (int(b["valid"]) << 1)
        if self.mode == "full":
            flags |= (4 if self.relay else 0) | (8 if self.target is not None else 0) | marker
            row = [self.seq, int(at * 1000), a["c"], b["c"], self.target, self.duty, flags, self.step_index]
        else:
            relay, red, green = self.outputs()
            flags |= (4 if relay else 0) | (8 if red else 0) | (16 if green else 0)
            row = [self.seq, int(at * 1000), a["c"], b["c"], a["raw"], b["raw"], a["faultCode"], b["faultCode"], flags]
        self.samples.append(row)
        self.samples = self.samples[-(4320 if self.mode == "full" else 3600):]

    def begin_step(self):
        self.step_at, self.step_elapsed, self.hold, self.was_in_band = self.now, 0.0, 0.0, False
        step = self.active["steps"][self.step_index]
        self.target = step["target"] if step["type"] == "hold" else self.temperature + .1
        self.log("STEP", f"Step simulato {self.step_index + 1}: {step['type']}")

    def stop_field(self):
        self.armed, self.token, self.action = False, 0, ""

    def advance(self, elapsed):
        remaining = elapsed * self.speed
        while remaining > 1e-9:
            dt = min(remaining, 1.0)
            remaining -= dt
            self.now += dt
            if self.mode == "field-test":
                if self.armed and (self.now - self.heartbeat_at >= 2.5 or self.now - self.arm_at >= 60):
                    self.stop_field()
                    self.log("STOP", "Abilitazione simulata scaduta: uscite spente.")
                if self.action and self.now - self.test_at >= self.test_duration:
                    self.action = ""
                    self.log("TEST", "Prova simulata terminata.")
            else:
                self.advance_full(dt)
            if self.now - self.last_sample >= (10 if self.mode == "full" else 1):
                self.record()
                self.last_sample = self.now

    def advance_full(self, dt):
        self.duty, self.relay = 0.0, False
        if self.phase in ("running", "paused") and self.now - self.cycle_at >= 43200:
            self.phase, self.fault = "fault", "Durata massima simulata: 12 ore"
            self.log("ALLARME", self.fault)
            self.record(marker=128)
        if self.phase != "running":
            self.temperature += (24 - self.temperature) / 1800 * dt
            return
        self.step_elapsed += dt
        step = self.active["steps"][self.step_index]
        if step["type"] == "ramp":
            self.target = min(step["target"], self.target + step["rate"] * dt / 60)
        elif step["type"] == "cooldown":
            self.target = max(step["target"], self.target - step["rate"] * dt / 60)
        if step["type"] != "cooldown":
            # Illustrative response: the ESP's actual PID is not reproduced here.
            self.duty = min(self.control["maxPower"], max(0, (self.target - self.temperature) * self.pid["kp"]))
            self.relay = self.duty > 0 and (self.now - self.cycle_at) % self.control["windowSec"] < self.control["windowSec"] * self.duty / 100
        self.temperature += (self.duty * .014 - (self.temperature - 24) / 1800) * dt
        in_band = abs(self.temperature - step["target"]) <= 1 and abs(self.temperature + .2 - step["target"]) <= 1
        if step["type"] == "hold" and in_band and self.was_in_band:
            self.hold += dt
        self.was_in_band = in_band
        done = (step["type"] == "ramp" and self.target >= step["target"] and self.temperature >= step["target"] - 1 or
                step["type"] == "hold" and self.hold >= step["duration"] * 60 or
                step["type"] == "cooldown" and self.target <= step["target"] and self.temperature + .2 <= step["target"] + 1)
        if done:
            self.step_index += 1
            if self.step_index < len(self.active["steps"]):
                self.begin_step()
            else:
                self.phase, self.target, self.duty, self.relay = "complete", None, 0.0, False
                self.log("CICLO", "Ciclo simulato terminato.")
                self.record(marker=64)

    def status(self):
        base = dict(firmware="LOCAL-PREVIEW", preview=True, bootCount=1, resetReason="1", uptimeMs=int(self.now * 1000),
                    sampleAtMs=int(self.now * 1000), sensors=self.sensors(), freeHeap=160000, minFreeHeap=150000,
                    chipTemperatureC=40.0, historyLatestSeq=self.seq, interrupted=False, workshopConfigured=False,
                    workshopConnected=False, stationIp="", apIp="", localName="", rssi=0)
        if self.mode == "field-test":
            relay, red, green = self.outputs()
            names = {"relayPulse": "Impulso relè", "relaySequence": "Relè: 3 impulsi", "ledGreen": "LED verde",
                     "ledRed": "LED rosso", "ledBoth": "Entrambi i LED", "ledOff": "LED spenti", "ledSequence": "Sequenza LED"}
            base.update(armed=self.armed, armRemainingMs=max(0, int((60 - self.now + self.arm_at) * 1000)) if self.armed else 0,
                        running=bool(self.action), test=names.get(self.action, "Lettura sonde simulate"),
                        remainingMs=max(0, int((self.test_duration - self.now + self.test_at) * 1000)) if self.action else 0,
                        relay=relay, red=red, green=green, leaseMs=2500)
        else:
            active = self.phase in ("running", "paused")
            step = self.active["steps"][self.step_index] if self.active and self.step_index < len(self.active["steps"]) else {}
            base.update(self.control)
            base.update(mode="FULL", phase=self.phase, fault=self.fault, relay=self.relay, duty=self.duty,
                        ready=not self.missing and self.phase not in ("fault", "interrupted"), pid=dict(self.pid),
                        cycleElapsedSec=int(self.now - self.cycle_at) if active else 0, stepElapsedSec=int(self.step_elapsed) if active else 0,
                        target=self.target, stepIndex=self.step_index, stepCount=len(self.active["steps"]) if self.active else 0,
                        recipeName=self.active["name"] if self.active else "", stepType=step.get("type", ""),
                        stepFinalTarget=step.get("target"), stepRate=step.get("rate", 0), holdInBandSec=int(self.hold),
                        holdRequiredSec=step.get("duration", 0) * 60, sampleAgeMs=0, ntpSynced=False, utcSec=0, outageUpperBoundSec=0)
        return base

    def request(self, method, endpoint, query=None, body=None):
        query = query or {}
        if method == "GET":
            if endpoint == "status":
                return self.status()
            if endpoint == "recipes" and self.mode == "full":
                return {"recipes": copy.deepcopy(self.recipes)}
            if endpoint == "logs":
                return dict(bootCount=1, capacity=100, savedCapacity=0, events=copy.deepcopy(self.events))
            if endpoint == "history":
                try:
                    before = int(query["before"][0]) if "before" in query else None
                    after = int(query.get("after", ["0"])[0])
                except (ValueError, IndexError):
                    raise ApiError(400, "Indice storico non valido")
                if before is not None:
                    eligible = [r for r in self.samples if r[0] < before]
                    rows, more, more_before = eligible[-180:], False, len(eligible) > 180
                elif "tail" in query:
                    rows, more, more_before = self.samples[-180:], False, len(self.samples) > 180
                else:
                    eligible = [r for r in self.samples if r[0] > after]
                    rows, more = eligible[:180], len(eligible) > 180
                    more_before = bool(rows and rows[0][0] > self.samples[0][0])
                return dict(bootCount=1, capacity=4320 if self.mode == "full" else 3600,
                            intervalMs=10000 if self.mode == "full" else 1000, oldestSeq=self.samples[0][0], latestSeq=self.seq,
                            samples=copy.deepcopy(rows), more=more, moreBefore=more_before)
            raise ApiError(404, "API non trovata")
        require(isinstance(body, dict), "Oggetto JSON richiesto")
        if endpoint == "preview-sensor":
            require(type(body.get("missing")) is bool, "Stato sonda non valido")
            self.missing = body["missing"]
            if self.mode == "full" and self.missing and self.phase in ("running", "paused"):
                self.phase, self.fault, self.duty, self.relay = "fault", "Fault PT100 simulato", 0.0, False
                self.record(marker=128)
            self.log("SONDA", "Sonda #2 scollegata nella simulazione" if self.missing else "Sonda #2 ripristinata nella simulazione")
            return self.status()
        if self.mode == "field-test":
            return self.field_command(endpoint, body)
        if endpoint in ("recipes", "pid", "control"):
            require(self.phase not in ("running", "paused"), "Ferma il ciclo simulato prima di modificare", 409)
            if endpoint == "recipes":
                validate_recipes(body.get("recipes"))
                self.recipes = copy.deepcopy(body["recipes"])
                self.log("RICETTE", "Ricette salvate in RAM nell'anteprima")
                return {"recipes": copy.deepcopy(self.recipes)}
            if endpoint == "pid":
                require(all(number(body.get(k), 0, limit) for k, limit in (("kp", 50), ("ki", .5), ("kd", 200))), "Coefficienti PID fuori limite")
                self.pid = {k: body[k] for k in ("kp", "ki", "kd")}
            else:
                require(body.get("actuator") in ("relay", "ssr"), "Attuatore non valido")
                require(type(body.get("windowSec")) is int and (1 if body["actuator"] == "ssr" else 60) <= body["windowSec"] <= 300
                        and number(body.get("maxPower"), 1, 100), "Finestra o limite comando fuori intervallo")
                self.control = {k: body[k] for k in ("actuator", "windowSec", "maxPower")}
            self.log(endpoint.upper(), "Impostazioni salvate in RAM nell'anteprima")
            return self.status()
        require(endpoint == "command", "API non trovata", 404)
        action = body.get("action")
        if action == "stop":
            self.duty, self.relay = 0.0, False
            if self.phase in ("running", "paused"):
                self.phase, self.target = "idle", None
                self.record(marker=32)
        elif action == "reset":
            require(self.phase not in ("running", "paused") and not self.missing, "Ferma il ciclo e ripristina le sonde", 409)
            self.phase, self.fault, self.target = "idle", "", None
        elif action == "start":
            require(self.phase not in ("running", "paused", "fault", "interrupted") and not self.missing, "Ciclo o sonde non pronti", 409)
            require(body.get("hardwareReady") is True, "Conferma richiesta (solo nella simulazione)")
            recipe = next((r for r in self.recipes if r["id"] == body.get("recipeId")), None)
            require(recipe is not None, "Ricetta non trovata", 404)
            first = recipe["steps"][0]
            require(first["type"] != "ramp" or self.temperature + .1 < first["target"] - 1, "Temperatura già vicina al primo target", 409)
            require(first["type"] != "cooldown" or self.temperature + .1 > first["target"] + 1, "Temperatura già sotto il cooldown", 409)
            self.active, self.phase, self.step_index, self.cycle_at = copy.deepcopy(recipe), "running", 0, self.now
            self.begin_step()
            self.record(marker=16)
        elif action == "pause":
            require(self.phase == "running", "Nessun ciclo in corso", 409)
            self.phase, self.duty, self.relay, self.was_in_band = "paused", 0.0, False, False
        elif action == "resume":
            require(self.phase == "paused" and not self.missing, "Nessun ciclo in pausa o sonda non valida", 409)
            self.phase = "running"
        else:
            raise ApiError(400, "Azione sconosciuta")
        self.log("STOP" if action == "stop" else "CICLO", f"Comando simulato: {action}")
        return self.status()

    def field_command(self, endpoint, body):
        if endpoint == "heartbeat":
            require(self.armed and type(body.get("token")) is int and body["token"] == self.token, "Abilitazione simulata scaduta", 403)
            self.heartbeat_at = self.now
            return None
        require(endpoint == "command", "API non trovata", 404)
        action = body.get("action")
        if action == "stop":
            self.stop_field()
            self.log("STOP", "STOP simulato: uscite spente")
            return self.status()
        if action == "arm":
            require(body.get("loadsDisconnected") is True, "Conferma richiesta (solo nella simulazione)")
            require(not self.armed, "Test già abilitati", 409)
            self.armed, self.token = True, secrets.randbelow(2**31 - 1) + 1
            self.arm_at = self.heartbeat_at = self.now
            self.log("TEST", "Test simulati abilitati per 60 s")
            return {"token": self.token}
        durations = {"relayPulse": 1, "relaySequence": 9, "ledSequence": 8,
                     "ledGreen": 3, "ledRed": 3, "ledBoth": 3, "ledOff": 3}
        require(isinstance(action, str) and action in durations, "Azione sconosciuta")
        require(self.armed and type(body.get("token")) is int and body["token"] == self.token, "Abilita prima i test simulati", 403)
        require(not self.action, "Test già in corso", 409)
        duration = durations[action]
        if action == "relayPulse":
            require(type(body.get("durationMs")) is int and 100 <= body["durationMs"] <= 5000, "Durata: 100–5000 ms interi")
            duration = body["durationMs"] / 1000
        self.action, self.test_at, self.test_duration = action, self.now, duration
        self.log("TEST", f"Prova simulata: {action}")
        return self.status()


class PreviewServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, speed=10):
        super().__init__(address, Handler)
        self.devices = {name: DemoDevice(name, speed) for name in PROJECTS}
        self.lock = threading.Lock()
        self.tick_at = time.monotonic()

    def update(self):
        now = time.monotonic()
        for device in self.devices.values():
            device.advance(now - self.tick_at)
        self.tick_at = now

    def service_actions(self):
        # Keep clocks/history/leases advancing even when no browser polls.
        with self.lock:
            self.update()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send(self, payload, mime="application/json; charset=utf-8", code=200):
        data = payload if isinstance(payload, bytes) else json.dumps(payload, allow_nan=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.handle_request("GET")

    def do_POST(self):
        self.handle_request("POST")

    def handle_request(self, method):
        parsed = urlsplit(self.path)
        if parsed.path == "/" and method == "GET":
            self.send_response(302)
            self.send_header("Location", "/full/")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        pieces = parsed.path.strip("/").split("/")
        mode = pieces[0]
        if mode not in PROJECTS:
            return self.send({"error": "Percorso non trovato"}, code=404)
        try:
            if len(pieces) == 3 and pieces[1] == "api":
                body = None
                if method == "POST":
                    size = int(self.headers.get("Content-Length", "0"))
                    require(0 < size <= 20000, "Dimensione richiesta non valida", 413)
                    try:
                        body = json.loads(self.rfile.read(size))
                    except (ValueError, UnicodeError):
                        raise ApiError(400, "JSON non valido")
                with self.server.lock:
                    self.server.update()
                    result = self.server.devices[mode].request(method, pieces[2], parse_qs(parsed.query), body)
                if result is None:
                    self.send_response(204)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                else:
                    self.send(result)
                return
            require(method == "GET", "Metodo non consentito", 405)
            name = "index.html" if len(pieces) == 1 else pieces[1] if len(pieces) == 2 else ""
            require(name in ("index.html", "app.js", "style.css", "preview.js"), "File non trovato", 404)
            path = Path(__file__).with_name("preview.js") if name == "preview.js" else ROOT / PROJECTS[mode] / "data" / name
            content = path.read_text(encoding="utf-8")
            if name == "index.html":
                content = content.replace('href="/style.css"', f'href="/{mode}/style.css"')
                content = content.replace('<script src="/app.js"></script>', f'<script src="/{mode}/preview.js"></script><script src="/{mode}/app.js"></script>')
                content = content.replace("sull'ESP", "nell'anteprima").replace("sul controllore", "nell'anteprima")
                content = content.replace("PT100 reali", "PT100 simulate").replace("Temperature reali", "Temperature simulate")
            elif name == "app.js":
                content = content.replace("Due PT100 reali · nessuna simulazione", "Due PT100 simulate · anteprima locale")
                content = content.replace("nella memoria dell'ESP.", "nell'anteprima fino al riavvio del server.")
            mime = {"index.html": "text/html", "style.css": "text/css", "app.js": "application/javascript", "preview.js": "application/javascript"}[name]
            self.send(content.encode(), mime + "; charset=utf-8")
        except ApiError as error:
            self.send({"error": error.message}, code=error.code)
        except (ValueError, OverflowError):
            self.send({"error": "Richiesta non valida"}, code=400)


def main():
    parser = argparse.ArgumentParser(description="Dashboard locali con dati simulati, senza ESP o dipendenze esterne.")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--speed", type=float, default=10, help="Velocità termica: default 10x; collaudo sempre 1x")
    parser.add_argument("--open", action="store_true", help="Apri il browser")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not number(args.speed, 1, 60):
        parser.error("Porta: 1–65535; velocità: 1–60")
    try:
        server = PreviewServer(("127.0.0.1", args.port), args.speed)
    except OSError as error:
        parser.exit(1, f"Impossibile avviare il server: {error}. Prova --port 8081.\n")
    print(f"Anteprima locale: http://localhost:{args.port}/full/", flush=True)
    print(f"Collaudo:         http://localhost:{args.port}/field-test/", flush=True)
    print(f"Dati simulati. Cicli termici: {args.speed:g}x; test uscite: 1x. Ctrl+C per chiudere.", flush=True)
    if args.open:
        webbrowser.open(f"http://localhost:{args.port}/full/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
