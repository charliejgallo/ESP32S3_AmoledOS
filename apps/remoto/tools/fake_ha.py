#!/usr/bin/env python3
"""
Un Home Assistant de mentira, para probar Remoto sin tocar el de casa.

    python3 apps/remoto/tools/fake_ha.py &
    cd sim && AOS_SIM_VIEW=aos.remoto ./build/amoledos_sim

Habla los dos unicos endpoints que usa el mando y se comporta como el de
verdad en lo que importa: exige el token, contesta 200 al servicio, cambia el
estado que corresponda y resuelve la plantilla. Imprime todo lo que recibe, que
es para lo que sirve: se aprieta un boton en el simulador y se ve aca si salio
la llamada bien formada.

Tambien sirve para verificar dos cosas que en el Home Assistant de verdad son
incomodas de forzar:

    --sin-token     contesta 401, para ver el mensaje de token rechazado
    --lento N       tarda N segundos, para ver que la pantalla no se congela
"""
import argparse
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ESTADO = {
    "light.salon":        {"state": "off", "attrs": {"brightness": 0}},
    "light.velador":      {"state": "on",  "attrs": {"brightness": 180}},
    "light.pie":          {"state": "off", "attrs": {"brightness": 0}},
    "cover.living":       {"state": "closed", "attrs": {}},
    "media_player.tv":    {"state": "playing", "attrs": {"volume_level": 0.4}},
    "sensor.living_temp": {"state": "21.4", "attrs": {}},
    "scene.cine":         {"state": "unknown", "attrs": {}},
}

ENCENDIDO = {"on", "open", "playing", "home"}
APAGADO   = {"off", "closed", "paused", "not_home"}

opciones = None


def aplicar(dominio, servicio, datos):
    """Lo minimo para que la pantalla del mando reaccione de verdad."""
    ids = datos.get("entity_id", "")
    if isinstance(ids, str):
        ids = [ids] if ids else []
    if ids == ["all"]:
        ids = [k for k in ESTADO if k.startswith(dominio + ".")]

    for eid in ids:
        e = ESTADO.setdefault(eid, {"state": "off", "attrs": {}})
        if servicio == "toggle":
            e["state"] = "off" if e["state"] in ENCENDIDO else "on"
            if eid.startswith("cover."):
                e["state"] = "closed" if e["state"] == "off" else "open"
        elif servicio in ("turn_on", "media_play"):
            e["state"] = "open" if eid.startswith("cover.") else "on"
        elif servicio in ("turn_off", "media_pause"):
            e["state"] = "closed" if eid.startswith("cover.") else "off"
        elif servicio == "media_play_pause":
            e["state"] = "paused" if e["state"] == "playing" else "playing"

        if "brightness_pct" in datos:
            e["attrs"]["brightness"] = int(datos["brightness_pct"] * 255 / 100)
            e["state"] = "on"
        if "brightness" in datos:
            e["attrs"]["brightness"] = int(datos["brightness"])
            e["state"] = "on"
        if "volume_level" in datos:
            e["attrs"]["volume_level"] = datos["volume_level"]


# La pagina del portal pide la lista de entidades con esta expresion; se
# reconoce entera porque no vale la pena traer Jinja para una linea.
PAT_LISTA = re.compile(
    r"\{\{\s*states\s*\|\s*map\(attribute='entity_id'\)\s*\|\s*"
    r"join\(','\)\s*\}\}")
PAT_STATE = re.compile(r"\{\{\s*states\('([^']+)'\)\s*\}\}")
PAT_ATTR  = re.compile(r"\{\{\s*state_attr\('([^']+)'\s*,\s*'([^']+)'\)\s*\}\}")


def render(plantilla):
    def de_estado(m):
        e = ESTADO.get(m.group(1))
        return e["state"] if e else "unknown"

    def de_atributo(m):
        e = ESTADO.get(m.group(1))
        if not e:
            return "None"
        v = e["attrs"].get(m.group(2))
        return "None" if v is None else str(v)

    salida = PAT_LISTA.sub(",".join(sorted(ESTADO)), plantilla)
    return PAT_ATTR.sub(de_atributo, PAT_STATE.sub(de_estado, salida))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass                                    # el log lo hacemos nosotros

    @staticmethod
    def di(texto):
        print(texto, flush=True)         # sin esto el log no se ve hasta salir

    def responder(self, code, body, tipo="application/json"):
        raw = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_POST(self):
        largo = int(self.headers.get("Content-Length", 0))
        cuerpo = self.rfile.read(largo).decode("utf-8", "replace") if largo else ""
        auth = self.headers.get("Authorization", "")

        if opciones.lento:
            time.sleep(opciones.lento)

        if opciones.sin_token or not auth.startswith("Bearer "):
            self.di(f"  401  {self.path}   (Authorization: {auth[:24] or 'no vino'})")
            return self.responder(401, '{"message":"Unauthorized"}')

        try:
            datos = json.loads(cuerpo) if cuerpo else {}
        except json.JSONDecodeError as exc:
            self.di(f"  400  {self.path}   cuerpo ilegible: {exc}\n       {cuerpo!r}")
            return self.responder(400, '{"message":"Bad Request"}')

        if self.path == "/api/template":
            salida = render(datos.get("template", ""))
            self.di(f"  200  plantilla -> {salida}")
            return self.responder(200, salida, "text/plain; charset=utf-8")

        m = re.fullmatch(r"/api/services/([a-z_]+)/([a-z_]+)", self.path)
        if not m:
            self.di(f"  404  {self.path}")
            return self.responder(404, '{"message":"Not Found"}')

        dominio, servicio = m.group(1), m.group(2)
        self.di(f"  200  {dominio}.{servicio}  {cuerpo}")
        aplicar(dominio, servicio, datos)
        return self.responder(200, "[]")

    def do_GET(self):
        if self.path == "/api/":
            # Lo que contesta Home Assistant de verdad; es lo que mira "probar".
            if opciones.sin_token or not self.headers.get(
                    "Authorization", "").startswith("Bearer "):
                return self.responder(401, '{"message":"Unauthorized"}')
            return self.responder(200, '{"message":"API running."}')

        if self.path == "/":
            return self.responder(200, json.dumps(
                {k: v["state"] for k, v in ESTADO.items()}, indent=2))
        return self.responder(404, '{"message":"Not Found"}')


def main():
    global opciones
    ap = argparse.ArgumentParser()
    ap.add_argument("--puerto", type=int, default=8123)
    ap.add_argument("--sin-token", action="store_true",
                    help="contesta 401 siempre, para ver el mensaje de error")
    ap.add_argument("--lento", type=float, default=0,
                    help="segundos de demora, para ver que la pantalla no se traba")
    opciones = ap.parse_args()

    srv = ThreadingHTTPServer(("127.0.0.1", opciones.puerto), Handler)
    print(f"Home Assistant de mentira en http://127.0.0.1:{opciones.puerto}")
    print("  GET / muestra los estados actuales")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
