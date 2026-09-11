#!/usr/bin/env python3
"""
Development server for the web portal.

It serves components/aos_web/portal.html and the same API as the firmware, but
against a local folder. It is for working on the page without the board: if it
works here, the contract with the firmware is the same.

    python3 tools/portal_dev_server.py [--root sim/sim_fs] [--port 8088]

It also serves /remoto and its API. Since it writes into the SAME folder and
the SAME prefs.txt the simulator uses, you can have the page open in the
browser and the simulator beside it: the profile is saved and the app reloads
it by itself, just as on the board.
"""
import argparse
import json
import os
import posixpath
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs, unquote
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, "components", "aos_web", "portal.html")
INICIO_PAGE = os.path.join(ROOT, "components", "aos_web", "inicio.html")
AJUSTES_PAGE = os.path.join(ROOT, "components", "aos_web", "ajustes.html")
PANTALLA_PAGE = os.path.join(ROOT, "components", "aos_web", "pantalla.html")
REGISTRO_PAGE = os.path.join(ROOT, "components", "aos_web", "registro.html")
ALARMAS_PAGE = os.path.join(ROOT, "components", "aos_web", "alarmas.html")
CAPTURA_FAKE = os.path.join(ROOT, "docs", "img", "launcher-list.png")
REMOTO_PAGE = os.path.join(ROOT, "components", "aos_web", "remoto.html")
RED_PAGE    = os.path.join(ROOT, "components", "aos_web", "red.html")
WIFI_PAGE   = os.path.join(ROOT, "components", "aos_web", "wifi.html")
AP_PAGE     = os.path.join(ROOT, "components", "aos_web", "ap.html")
CLIMA_PAGE  = os.path.join(ROOT, "components", "aos_web", "clima.html")
COTIZ_PAGE  = os.path.join(ROOT, "components", "aos_web", "cotiz.html")
PIXEL_PAGE  = os.path.join(ROOT, "components", "aos_web", "pixel.html")
SENSO_PAGE  = os.path.join(ROOT, "components", "aos_web", "sensores.html")
CSS_FILE    = os.path.join(ROOT, "components", "aos_web", "aos.css")
JS_FILE     = os.path.join(ROOT, "components", "aos_web", "aos.js")

# The AP's automatic name: on the board it comes from the MAC, here it is
# fixed, just as in sim/hal_sim.c. The prefs KEYS are the same ones the
# simulator's HAL uses, so the page and the simulator really do share state.
AP_SSID_AUTO = "AmoledOS-5IM"
AP_PASS_FABRICA = "amoledos"
AP_ALFABETO = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"
DIRS = ("apps", "photos", "music", "recordings", "redes", "pixel")

CONTENT_TYPES = {
    ".wav": "audio/wav", ".mp3": "audio/mpeg",
    ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
    ".png": "image/png", ".bmp": "image/bmp", ".gif": "image/gif",
}


def prefs_path(base):
    return os.path.join(base, "prefs.txt")


def prefs_leer(base):
    """The same key=value file sim/hal_sim.c uses."""
    datos = {}
    try:
        with open(prefs_path(base)) as f:
            for linea in f:
                if "=" in linea:
                    k, v = linea.rstrip("\n").split("=", 1)
                    datos[k] = v
    except FileNotFoundError:
        pass
    return datos


def prefs_escribir(base, cambios):
    datos = prefs_leer(base)
    datos.update(cambios)
    os.makedirs(base, exist_ok=True)
    with open(prefs_path(base), "w") as f:
        for k, v in datos.items():
            f.write(f"{k}={v}\n")


def ap_clave_nueva():
    """The same alphabet as the board: no 0/O and no 1/l/I."""
    import random
    return "".join(random.choice(AP_ALFABETO) for _ in range(10))


def ap_estado(datos):
    """What GET /api/ap answers, with the HAL's rules."""
    rotativa = str(datos.get("ap_pmode", "0")) == "1"
    clave = datos.get("ap_pass") or ""
    if not clave:
        clave = ap_clave_nueva() if rotativa else AP_PASS_FABRICA
    return {
        "ssid": datos.get("ap_ssid") or AP_SSID_AUTO,
        "clave": clave,
        "modo": "rotativa" if rotativa else "fija",
        "ssid_auto": AP_SSID_AUTO,
        "activo": str(datos.get("ap_activo", "1")) == "1",
        "ip": "127.0.0.1",
    }


def ha_pedir(base, camino, cuerpo=None):
    """Asks the configured Home Assistant, the way the firmware does."""
    datos = prefs_leer(base)
    url   = (datos.get("rc_url") or "").rstrip("/")
    token = datos.get("rc_token") or ""
    if not url.startswith("http://") or not token:
        return -100, ""

    req = Request(url + camino,
                  data=cuerpo.encode() if cuerpo else None,
                  headers={"Authorization": "Bearer " + token,
                           "Content-Type": "application/json"})
    try:
        with urlopen(req, timeout=10) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except HTTPError as e:
        return e.code, ""
    except (URLError, OSError):
        return -2, ""


class Handler(BaseHTTPRequestHandler):
    base = ""
    app_abierta = ""
    log_buf = ""
    log_total = 0
    log_ms = 0

    @classmethod
    def log_linea(cls, nivel, tag, msg):
        color = {"I": "32", "W": "33", "E": "31"}.get(nivel, "0")
        cls.log_ms += 137
        linea = f"\x1b[0;{color}m{nivel} ({cls.log_ms}) {tag}: {msg}\x1b[0m\n"
        cls.log_buf = (cls.log_buf + linea)[-16384:]
        cls.log_total += len(linea)

    @classmethod
    def log_alimentar(cls):
        import random
        if cls.log_total == 0:
            cls.log_linea("I", "main", "AmoledOS v0.2.0-dev starting up")
            cls.log_linea("I", "main", "boot reason: normal power-on")
            cls.log_linea("W", "aos_hal", "touch controller went quiet, re-arming")
        for _ in range(random.randint(0, 2)):
            cls.log_linea(random.choice("IIIIWE"), random.choice(["main", "aos_hal", "aos_ui", "aos_ble"]),
                          random.choice(["heartbeat: display=active touch reads=812 fingers=3 heap_int=41919",
                                         "light sleep armed", "notification 42 from Mensajes",
                                         "sntp: time synced", "portal request /api/status"]))

    def _perfil(self):
        return os.path.join(self.base, "data", "remoto.json")

    def _remoto_get(self, camino):
        if camino == "/remoto":
            with open(REMOTO_PAGE, "rb") as page:
                return self._send(200, page.read(), "text/html; charset=utf-8")

        if camino == "/api/remoto/config":
            datos = prefs_leer(self.base)
            token = datos.get("rc_token", "")
            return self._send(200, json.dumps({
                "url":   datos.get("rc_url", ""),
                "token": bool(token),
                "cola":  token[-4:] if len(token) >= 4 else "",
                "gen":   int(datos.get("rc_gen", 0) or 0),
            }))

        if camino == "/api/remoto/perfil":
            try:
                with open(self._perfil(), "rb") as f:
                    return self._send(200, f.read())
            except FileNotFoundError:
                return self._send(200, "null")

        if camino == "/api/remoto/probar":
            code, _ = ha_pedir(self.base, "/api/")
            if code == -100:
                msg, ok = "falta la direccion o el token", False
            elif code == 200:
                msg, ok = "Home Assistant contesta y el token sirve", True
            elif code in (401, 403):
                msg, ok = f"llegue pero rechazo el token ({code})", False
            elif code < 0:
                msg, ok = "no me pude conectar", False
            else:
                msg, ok = f"contesto {code}", False
            print(f"  probar -> {code}  {msg}")
            return self._send(200, json.dumps({"ok": ok, "msg": msg}))

        if camino == "/api/remoto/entidades":
            code, body = ha_pedir(
                self.base, "/api/template",
                '{"template":"{{ states | map(attribute=\'entity_id\') '
                '| join(\',\') }}"}')
            if code != 200:
                return self._send(503, "", "text/plain; charset=utf-8")
            return self._send(200, body, "text/plain; charset=utf-8")

        return None

    def _remoto_post(self, camino, cuerpo):
        if camino == "/api/remoto/config":
            campos = parse_qs(cuerpo.decode("utf-8", "replace"))
            url = (campos.get("url") or [""])[0].rstrip("/")
            tok = (campos.get("token") or [""])[0]
            if not url.startswith("http://"):
                return self._send(200, json.dumps({
                    "ok": False,
                    "error": "tiene que empezar con http:// -- el firmware no "
                             "hace TLS a proposito"}))
            cambios = {"rc_url": url}
            if tok:
                cambios["rc_token"] = tok
            datos = prefs_leer(self.base)
            cambios["rc_gen"] = int(datos.get("rc_gen", 0) or 0) + 1
            prefs_escribir(self.base, cambios)
            print(f"  conexion guardada: {url}  token {'nuevo' if tok else 'igual'}")
            return self._send(200, '{"ok":true}')

        if camino == "/api/remoto/perfil":
            os.makedirs(os.path.dirname(self._perfil()), exist_ok=True)
            tmp = self._perfil() + ".tmp"
            with open(tmp, "wb") as f:
                f.write(cuerpo)
            os.replace(tmp, self._perfil())
            datos = prefs_leer(self.base)
            prefs_escribir(self.base,
                           {"rc_gen": int(datos.get("rc_gen", 0) or 0) + 1})
            print(f"  perfil guardado, {len(cuerpo)} bytes")
            return self._send(200, '{"ok":true}')

        return None

    def _safe_dir(self, query):
        name = (parse_qs(query).get("dir") or ["apps"])[0]
        if name == "sd" or name.startswith("sd/"):
            # The explorer: any folder of the card, validated piece by piece
            # the way the firmware does it.
            rel = name[3:] if len(name) > 2 else ""
            for piece in rel.split("/") if rel else []:
                if not piece or piece.startswith(".") or any(c in piece for c in '\\:*?"<>|'):
                    return None
            path = os.path.join(self.base, rel) if rel else self.base
            return path if os.path.isdir(path) else None
        if name not in DIRS:
            return None
        path = os.path.join(self.base, name)
        os.makedirs(path, exist_ok=True)
        return path

    def _safe_name(self, query):
        raw = (parse_qs(query).get("name") or [""])[0]
        name = posixpath.basename(unquote(raw))
        return name if name and not name.startswith(".") else None

    def _send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)

        if url.path == "/remoto" or url.path.startswith("/api/remoto/"):
            if self._remoto_get(url.path) is not None:
                return
            return self._send(404, '{"error":"no existe"}')

        if url.path == "/red":
            with open(RED_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/wifi":
            with open(WIFI_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/ap":
            with open(AP_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/clima":
            with open(CLIMA_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/cotiz":
            with open(COTIZ_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/pixel":
            with open(PIXEL_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/sensores":
            with open(SENSO_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        # The two shared ones. Deliberately no caching here: in development you
        # edit the css and reload, and an hour of max-age drives you mad.
        elif url.path == "/aos.css":
            with open(CSS_FILE, "rb") as f:
                self._send(200, f.read(), "text/css; charset=utf-8")

        elif url.path == "/aos.js":
            with open(JS_FILE, "rb") as f:
                self._send(200, f.read(), "application/javascript; charset=utf-8")

        elif url.path == "/api/lang":
            # The language comes from the SAME prefs.txt the simulator reads,
            # so changing it on the screen shows on the page and vice versa.
            actual = prefs_leer(self.base).get("lang", "es")
            self._send(200, json.dumps({
                "actual": actual,
                "idiomas": [{"codigo": c, "nombre": n, "cadenas": 0,
                             "apps": 0, "origen": "firmware"}
                            for c, n in (("es", "Espanol"), ("en", "English"),
                                         ("de", "Deutsch"))],
            }))

        elif url.path == "/api/ap":
            datos = prefs_leer(self.base)
            self._send(200, json.dumps(ap_estado(datos)))

        elif url.path == "/api/clima":
            d = prefs_leer(self.base)
            self._send(200, json.dumps({
                "ciudad": d.get("clima_city", ""),
                "lat10k": int(d.get("clima_lat", 0) or 0),
                "lon10k": int(d.get("clima_lon", 0) or 0),
            }))

        elif url.path == "/api/cotiz":
            self._send(200, json.dumps(
                {"lista": prefs_leer(self.base).get("cz_list", "")}))

        elif url.path == "/api/sensoresconf":
            self._send(200, json.dumps(
                {"lista": prefs_leer(self.base).get("sn_list", "")}))

        elif url.path == "/api/sensores":
            # Fake sensors, in the board's format:
            # entity|name|unit|value per line.
            self._send(200,
                "sensor.taller|Consumo taller|W|412.5\n"
                "sensor.temp_living|Temperatura living|\u00b0C|22.9\n"
                "sensor.pres|Presion|hPa|1013\n"
                "sensor.humedad|Humedad|%|54\n",
                "text/plain; charset=utf-8")

        elif url.path == "/api/scan":
            # Invented networks, including one with tags: it is the case that
            # broke the page before the name was escaped.
            self._send(200, json.dumps({"redes": [
                {"ssid": "casa", "rssi": -42, "segura": True},
                {"ssid": "vecino", "rssi": -71, "segura": True},
                {"ssid": "<img src=x onerror=alert(1)>", "rssi": -80,
                 "segura": False},
            ]}))

        elif url.path in ("/", "/index.html"):
            with open(INICIO_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/archivos":
            with open(PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/ajustes":
            with open(AJUSTES_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/pantalla":
            with open(PANTALLA_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/registro":
            with open(REGISTRO_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/alarmas":
            with open(ALARMAS_PAGE, "rb") as page:
                self._send(200, page.read(), "text/html; charset=utf-8")

        elif url.path == "/api/alarmas":
            # The board's packing: minute | enabled << 16, 0xFFFF = empty.
            d = prefs_leer(self.base)
            lista = []
            for i in range(6):
                v = int(d.get(f"alarm{i}", 0xFFFF) or 0xFFFF)
                vacia = (v & 0xFFFF) == 0xFFFF
                dias = (v >> 17) & 0x7F
                lista.append({"i": i, "minuto": -1 if vacia else v & 0xFFFF,
                              "on": (not vacia) and ((v >> 16) & 1) != 0,
                              "dias": dias or 0x7F})
            self._send(200, json.dumps({"alarmas": lista}))

        elif url.path == "/api/status":
            # The same fields the board sends, with invented values. The heap
            # is deliberately the real order of magnitude (40 KB), so the
            # colour thresholds of the status strip can be seen here.
            import time
            d = prefs_leer(self.base)
            self._send(200, json.dumps({
                "version": "0.2.0-dev (servidor de prueba)",
                "battery": 76, "heap": 41 * 1024, "psram": 7860 * 1024,
                "exec": 121 * 1024,
                "sd": True, "sd_total": 31914983424, "sd_free": 29817110528,
                "board": "v2 (CO5300 + CST816)", "slot": "ota_1", "trial": False,
                "vbat": 3.987, "vbus": 5.16, "charging": True, "usb": True,
                "charge_state": "cc", "charge_ma": 150, "charge_target_mv": 4100,
                "board_temp": 27.4, "drain_pct_h": 3.2, "hours_left": 21.5,
                "on_battery_s": 0, "battery_minutes": 812, "cycles": 3,
                "cpu_mhz": 240, "saving": True, "panel_asleep": False,
                "light_sleep": False, "power_on": "power key",
                "last_power_off": "power key held", "boot_reason": "power-on",
                "uptime_s": int(time.time()) % 100000, "display": 1,
                "ssid": "casa", "rssi": -52, "ip": "127.0.0.1",
                "wifi_on": str(d.get("wifi_on", "1")) == "1",
                "ap": str(d.get("ap_activo", "0")) == "1", "ap_ip": "192.168.4.1",
                "bt": "connected" if str(d.get("bt_on", "1")) == "1" else "off",
                "bt_peer": "iPhone de prueba", "phone_batt": 63,
                "app": Handler.app_abierta, "time_ok": True,
                "tz": d.get("tz", "ART3"), "now": int(time.time()),
            }))

        elif url.path == "/api/ajustes":
            d = prefs_leer(self.base)
            def i(k, por):
                try:
                    return int(d.get(k, por))
                except ValueError:
                    return por
            self._send(200, json.dumps({
                "brillo": i("bright", 80), "volumen": i("volume", 50),
                "aod": i("aod", 0), "aod_brillo": i("aod_bright", 10),
                "esfera": d.get("face", "digital"),
                "esferas": [{"id": "digital", "nombre": "Digital"},
                            {"id": "analog", "nombre": "Analogica"},
                            {"id": "nixie", "nombre": "Nixie"},
                            {"id": "rings", "nombre": "Anillos"},
                            {"id": "flip", "nombre": "Flip"},
                            {"id": "binary", "nombre": "Binaria"},
                            {"id": "minimal", "nombre": "Minima"}],
                "menu": i("launcher", 0),
                "ahorro": i("pwr_save", 1), "cuidar": i("batt_care", 1),
                "panel_slp": i("panel_slp", 0), "chip_slp": i("light_slp", 0),
                "tz": d.get("tz", "ART3"), "hora_ok": True,
                "wifi": i("wifi_on", 1), "bt": i("bt_on", 1),
                "notif": i("notif_on", 1), "notif_sonido": i("notif_snd", 1),
                "llamadas": i("notif_calls", 1),
            }))

        elif url.path == "/api/apps":
            self._send(200, json.dumps({"abierta": Handler.app_abierta, "apps": [
                {"id": "aos.settings", "nombre": "Ajustes", "dinamica": False},
                {"id": "aos.timer", "nombre": "Temporizador", "dinamica": False},
                {"id": "aos.stopwatch", "nombre": "Cronometro", "dinamica": False},
                {"id": "aos.alarm", "nombre": "Alarmas", "dinamica": False},
                {"id": "aos.calendar", "nombre": "Calendario", "dinamica": False},
                {"id": "app.claudito", "nombre": "Claudito", "dinamica": True},
                {"id": "app.gemas", "nombre": "Gemas", "dinamica": True},
                {"id": "app.clima", "nombre": "Clima", "dinamica": True},
            ]}))

        elif url.path == "/api/log":
            # A log that grows by itself, in the board's format and with its
            # colours, so the page has to strip them here too.
            import time
            desde = int((parse_qs(url.query).get("desde") or ["0"])[0])
            Handler.log_alimentar()
            total = Handler.log_total
            buf = Handler.log_buf
            inicio = max(desde, total - len(buf))
            trozo = buf[len(buf) - (total - inicio):] if total > inicio else ""
            data = trozo.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("X-Desde", str(inicio))
            self.send_header("X-Hasta", str(total))
            self.end_headers()
            self.wfile.write(data)

        elif url.path == "/api/captura":
            import time
            time.sleep(0.3)
            with open(CAPTURA_FAKE, "rb") as f:
                self._send(200, f.read(), "image/png")

        elif url.path == "/api/list":
            folder = self._safe_dir(url.query)
            if not folder:
                return self._send(400, '{"error":"dir invalido"}')
            files = []
            for name in sorted(os.listdir(folder)):
                full = os.path.join(folder, name)
                if name.startswith("."):
                    continue
                if os.path.isdir(full):
                    files.append({"name": name, "dir": True})
                elif os.path.isfile(full):
                    files.append({"name": name, "size": os.path.getsize(full)})
            self._send(200, json.dumps({"files": files}))

        elif url.path == "/api/download":
            folder = self._safe_dir(url.query)
            name = self._safe_name(url.query)
            if not folder or not name:
                return self._send(400, '{"error":"parametros invalidos"}')
            full = os.path.join(folder, name)
            if not os.path.isfile(full):
                return self._send(404, '{"error":"no existe"}')

            with open(full, "rb") as handle:
                data = handle.read()
            ctype = CONTENT_TYPES.get(os.path.splitext(name)[1].lower(),
                                      "application/octet-stream")
            # same contract as the board: an attachment only if "dl" is present
            attach = "dl" in parse_qs(url.query)
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Content-Disposition",
                             '%s; filename="%s"' % (
                                 "attachment" if attach else "inline", name))
            self.end_headers()
            self.wfile.write(data)

        else:
            self._send(404, '{"error":"no existe"}')

    def do_POST(self):
        url = urlparse(self.path)

        if url.path.startswith("/api/remoto/"):
            size = int(self.headers.get("Content-Length", 0))
            cuerpo = self.rfile.read(size) if size else b""
            if self._remoto_post(url.path, cuerpo) is not None:
                return
            return self._send(404, '{"error":"no existe"}')

        if url.path in ("/api/clima", "/api/cotiz", "/api/sensoresconf"):
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            if url.path == "/api/clima":
                nombre = (campos.get("name") or [""])[0]
                prefs_escribir(self.base, {
                    "clima_city": nombre,
                    "clima_lat": (campos.get("lat") or ["0"])[0],
                    "clima_lon": (campos.get("lon") or ["0"])[0],
                })
                return self._send(200, json.dumps({"ok": True, "ciudad": nombre}))
            clave = "cz_list" if url.path == "/api/cotiz" else "sn_list"
            prefs_escribir(self.base, {clave: (campos.get("lista") or [""])[0]})
            return self._send(200, '{"ok":true}')

        if url.path == "/api/ajustes":
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            claves = {"brillo": "bright", "volumen": "volume", "aod": "aod",
                      "aod_brillo": "aod_bright", "esfera": "face", "menu": "launcher",
                      "ahorro": "pwr_save", "cuidar": "batt_care",
                      "panel_slp": "panel_slp", "chip_slp": "light_slp", "tz": "tz",
                      "wifi": "wifi_on", "bt": "bt_on", "notif": "notif_on",
                      "notif_sonido": "notif_snd", "llamadas": "notif_calls"}
            cambios = {}
            for k, v in campos.items():
                if k in claves:
                    cambios[claves[k]] = v[0]
            prefs_escribir(self.base, cambios)
            print(f"  ajustes: {cambios}")
            return self._send(200, '{"ok":true}')

        if url.path == "/api/alarmas":
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            i = int((campos.get("i") or ["-1"])[0])
            minuto = int((campos.get("minuto") or ["-1"])[0])
            on = (campos.get("on") or ["0"])[0] == "1"
            dias = int((campos.get("dias") or ["127"])[0]) & 0x7F
            if not 0 <= i < 6 or minuto >= 1440 or (minuto >= 0 and dias == 0):
                return self._send(400, "alarma invalida", "text/plain; charset=utf-8")
            v = 0xFFFF if minuto < 0 else (minuto | (1 << 16 if on else 0) | (dias << 17))
            prefs_escribir(self.base, {f"alarm{i}": v})
            print(f"  alarma {i}: {minuto} {'on' if on else 'off'}")
            return self._send(200, '{"ok":true}')

        if url.path == "/api/accion":
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            que = (campos.get("que") or [""])[0]
            if que == "abrir":
                Handler.app_abierta = (campos.get("id") or [""])[0]
            elif que in ("volver", "inicio", "menu"):
                Handler.app_abierta = ""
            elif que not in ("despertar", "apagar", "sync_hora", "hora", "beep", "toast"):
                return self._send(400, "accion desconocida", "text/plain; charset=utf-8")
            print(f"  accion: {que} {campos}")
            Handler.log_linea("I", "aos_web", f"accion desde el portal: {que}")
            return self._send(200, '{"ok":true}')

        if url.path == "/api/ap/estado":
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            on = (campos.get("on") or ["0"])[0] in ("1", "true")
            prefs_escribir(self.base, {"ap_activo": 1 if on else 0})
            print(f"  AP {'levantado' if on else 'apagado'}")
            return self._send(200, json.dumps({"ok": True, "activo": on}))

        if url.path == "/api/ap":
            largo = int(self.headers.get("Content-Length") or 0)
            campos = parse_qs(self.rfile.read(largo).decode())
            ssid = (campos.get("ssid") or [""])[0]
            clave = (campos.get("pass") or [""])[0]
            modo = (campos.get("modo") or ["fija"])[0]

            if len(ssid) > 32:
                return self._send(200, '{"ok":false,"error":"ssid"}')
            if modo == "fija" and clave and not (8 <= len(clave) <= 63):
                return self._send(200, '{"ok":false,"error":"clave"}')

            prefs_escribir(self.base, {
                "ap_ssid": ssid,
                "ap_pmode": 1 if modo == "rotativa" else 0,
                "ap_pass": clave if modo == "fija" else "",
            })
            # Rotating: the password is generated by the device, not by the
            # browser. It is generated here and now so the page shows it
            # straight away.
            if modo == "rotativa":
                prefs_escribir(self.base, {"ap_pass": ap_clave_nueva()})
            print(f"  AP: {ssid or '(automatico)'}, clave {modo}")
            return self._send(200, '{"ok":true}')

        folder = self._safe_dir(url.query)
        name = self._safe_name(url.query)
        if not folder or not name:
            return self._send(400, '{"error":"parametros invalidos"}')

        target = os.path.join(folder, name)

        if url.path == "/api/upload":
            size = int(self.headers.get("Content-Length", 0))
            written = 0
            with open(target, "wb") as out:
                while written < size:
                    chunk = self.rfile.read(min(8192, size - written))
                    if not chunk:
                        break
                    out.write(chunk)
                    written += len(chunk)
            print(f"  subido {name} ({written} bytes)")
            self._send(200, json.dumps({"ok": True, "size": written}))

        elif url.path == "/api/delete":
            try:
                if os.path.isdir(target):
                    os.rmdir(target)
                elif os.path.isfile(target):
                    os.remove(target)
                print(f"  borrado {name}")
                self._send(200, '{"ok":true}')
            except OSError:
                self._send(409, '{"ok":false,"error":"no se pudo borrar"}')

        elif url.path == "/api/mkdir":
            try:
                os.mkdir(target)
                self._send(200, '{"ok":true}')
            except OSError:
                self._send(200, '{"ok":false}')

        else:
            self._send(404, '{"error":"no existe"}')

    def log_message(self, fmt, *args):
        pass        # el ruido de acceso no aporta


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.join(ROOT, "sim", "sim_fs"))
    parser.add_argument("--port", type=int, default=8088)
    args = parser.parse_args()

    Handler.base = args.root
    for name in DIRS:
        os.makedirs(os.path.join(args.root, name), exist_ok=True)

    print(f"test portal at http://localhost:{args.port}  (root {args.root})")
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
