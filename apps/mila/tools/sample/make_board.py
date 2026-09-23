#!/usr/bin/env python3
"""make_board.py - the sample board (one HTML page, images inlined).

    python3 make_board.py <board dir>      (after compose_sample.py)
"""
import base64
import os
import re
import sys

PAGE = '''<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mila: muestra</title><style>
:root{--bg:#141217;--card:#1f1b24;--ink:#f1ece6;--dim:#b7aea6;--acc:#ffc53d;--line:#342f3b}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 -apple-system,system-ui,sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px 60px}
h1{font-size:28px;margin:0 0 4px}h2{font-size:20px;margin:36px 0 10px;border-top:1px solid var(--line);padding-top:22px}
p.sub{color:var(--dim);margin:0 0 10px}
.row{display:flex;flex-wrap:wrap;gap:18px;align-items:flex-start}
figure{margin:0;background:var(--card);border-radius:16px;padding:12px}
figure img{display:block;border-radius:22px;max-width:100%}
figcaption{color:var(--dim);font-size:13px;margin-top:8px;max-width:368px}
.wide figcaption{max-width:none}
.watch img{width:368px;height:448px;border-radius:34px;border:6px solid #2a2530;box-sizing:content-box}
ul{margin:6px 0;padding-left:20px}li{margin:3px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}
.box{background:var(--card);border-radius:14px;padding:12px 16px}
.box h3{margin:2px 0 6px;font-size:16px;color:var(--acc)}
@media (max-width:420px){.watch img{width:100%;height:auto}}
</style></head><body><main>
<h1>Mila, un Sokoban gatuno</h1>
<p class="sub">Primera muestra para AmoledOS: Mila modelada en Blender, un nivel real resuelto por el solucionador (4 ovillos, 8 empujes como mínimo), dos cámaras para elegir, la tienda y la casita. Todo es un render directo de la escena 3D. En el reloj se arma con sprites pre-renderizados desde la misma cámara, así que se ve igual.</p>

<h2>1. Mila</h2>
<div class="row">
<figure class="wide"><img src="{{mila_sheet.png}}"><figcaption>Arriba: quieta, caminando, empujando (con la cabeza, como hacen los gatos), de espaldas y caminando al otro lado. Abajo: moño + collar, gorrito de fiesta + placa de pescado, corona, collar con cascabel. Es negra con un brillo azulado en el borde para que no se pierda sobre el negro del AMOLED, y tiene los ojos ámbar apenas luminosos.</figcaption></figure>
</div>

<h2>2. La cámara: vista general, después sigue a Mila</h2>
<p class="sub">Al entrar al nivel se ve todo junto con el objetivo; al tocar, la cámara hace zoom sobre Mila y la sigue. Como los niveles de Sokoban son chicos, el reloj arma el nivel entero una sola vez en PSRAM (~1 MB): el zoom y el seguimiento son un recorte y no cuestan dibujo.</p>
<div class="row">
<figure class="watch"><img src="{{zoom_A.gif}}"><figcaption><b>Cámara A, recta.</b> Arriba en la pantalla es arriba en la grilla: deslizar a la derecha es una casilla a la derecha, sin ambigüedad. La casilla mide 56×42 px.</figcaption></figure>
<figure class="watch"><img src="{{zoom_B.gif}}"><figcaption><b>Cámara B, girada ~16°</b> (de la familia de Monster Hop). Tiene más volumen y se ven los costados de los muebles, pero la grilla queda en diagonal respecto del deslizar.</figcaption></figure>
</div>
<div class="row" style="margin-top:18px">
<figure class="watch"><img src="{{screen_A_overview.png}}"><figcaption>A, vista general con el objetivo.</figcaption></figure>
<figure class="watch"><img src="{{screen_A_follow.png}}"><figcaption>A, jugando: movimientos contra el par, ovillos ubicados (1 de 4), deshacer y reiniciar.</figcaption></figure>
<figure class="watch"><img src="{{screen_B_overview.png}}"><figcaption>B, vista general.</figcaption></figure>
<figure class="watch"><img src="{{screen_B_follow.png}}"><figcaption>B, jugando.</figcaption></figure>
</div>

<h2>3. Tienda y casita</h2>
<div class="row">
<figure class="watch"><img src="{{screen_shop.png}}"><figcaption>Tienda: pestañas Gorros / Collares / Juguetes; Mila en grande con lo que estás mirando, los colores elegibles y el precio en monedas.</figcaption></figure>
<figure class="watch"><img src="{{screen_casita.png}}"><figcaption>Casita (el hub, como la casa de Tommy): Mila vive ahí con sus juguetes. Desde acá se va a Jugar (mapa de mundos), a la Tienda y a Ajustes.</figcaption></figure>
</div>

<h2>4. Propuesta de juego</h2>
<div class="grid">
<div class="box"><h3>Controles</h3><ul>
<li>Deslizar: una casilla (empuja si hay algo delante).</li>
<li>Tocar una casilla libre: Mila camina sola hasta ahí, sin empujar nada.</li>
<li>BOOT corto: deshacer (ilimitado). BOOT largo: pausa (reiniciar, salir).</li>
<li>Mantener el dedo sobre Mila: vuelve la vista general mientras no lo soltás.</li>
<li>Si un mueble tapa a Mila, su silueta se dibuja encima (los rayos X de Monster Hop).</li></ul></div>
<div class="box"><h3>Mundos (uno por ambiente de la casa)</h3><ul>
<li><b>Living</b>: ovillos a las cestas. El Sokoban clásico, con tutorial.</li>
<li><b>Cocina</b>: latas a la alacena. Piso mojado: lo que empujás se desliza hasta chocar.</li>
<li><b>Jardín</b>: macetas a sus marcas. Placas en el piso que abren rejas mientras tengan algo encima.</li>
<li><b>Altillo</b>: cajas de cartón. Gateras: Mila pasa, los objetos no.</li>
<li><b>Tejados de noche</b>: pelotas que ruedan, y huecos que se tapan con una caja para cruzar.</li></ul>
Cada mundo enseña su mecánica y después la mezcla con las anteriores.</div>
<div class="box"><h3>Niveles y dificultad</h3><ul>
<li>Niveles propios, verificados con el solucionador, que además calcula el mínimo de movimientos (el par).</li>
<li>La curva va de 1 empuje (tutorial) a 40-60 empujes en los últimos.</li>
<li>Estrellas: ★ resolver, ★★ hasta par +25%, ★★★ en el par. Los mundos se abren por estrellas.</li>
<li>Monedas: al resolver por primera vez y por cada estrella nueva.</li></ul></div>
<div class="box"><h3>Tienda</h3><ul>
<li><b>Gorros</b>: moño, gorrito de fiesta, corona, boina, gorro de lana con pompón, flor, orejas de conejo, sombrero de bruja…</li>
<li><b>Collares</b>: cascabel, placa de pescado, pañuelo, moño al cuello, collar de perlas.</li>
<li><b>Juguetes para la casita</b>: ratón, caña con pluma, ovillo, rascador, caja de cartón, túnel, pelota con cascabel, pecera, hamaca de ventana, catnip.</li>
<li>Colores: una vez comprado, el color se cambia gratis (el reloj recolorea, así que no cuesta sprites).</li></ul></div>
<div class="box"><h3>Casita (sin barras, solo mimos)</h3><ul>
<li>Mila pasea sola, se sienta en la ventana, duerme en su cucha, se mete en la caja.</li>
<li>Si la tocás: maúlla, ronronea (corazones) o se tira panza arriba.</li>
<li>Si arrastrás el ratón o la pluma, lo persigue y salta. Si tocás un juguete, lo usa (rascador, pelota, túnel).</li>
<li>Los juguetes comprados aparecen en la casita. Las estadísticas y los ajustes también se abren desde acá.</li></ul></div>
<div class="box"><h3>Técnica (resumen)</h3><ul>
<li>La misma receta que Golf, Turbo y Monster Hop: Blender por código, pak en la SD y .so.</li>
<li>Mila: 4 direcciones × (quieta, caminar, empujar, festejo), más las de la casita (sentada, dormir, jugar, panza arriba). Gorros y collares van como capas encima, igual que la gorra de Tommy.</li>
<li>El nivel entero se arma una vez en PSRAM; la vista general es ese mismo nivel achicado.</li></ul></div>
</div>

<h2>5. Lo que noté armando la muestra</h2>
<ul>
<li>De frente, <b>la cabeza casi tapa el collar</b>: se ven el cascabel o la placa, pero no la tira. Propongo sumar <b>pañuelos</b>, que se ven desde todos lados.</li>
<li>En la vista que la sigue, Mila mide ~45 px (Tommy mide ~50). Se lee bien, pero si la querés más grande entran menos casillas.</li>
<li>Los muebles que ocupan una casilla le dan mucha más vida al nivel que las paredes lisas. Dejé oscuras las paredes del borde para que contrasten con el piso.</li>
</ul>
</main></body></html>'''


def uri(p):
    kind = 'gif' if p.endswith('.gif') else 'png'
    with open(p, 'rb') as fh:
        return 'data:image/%s;base64,%s' % (kind, base64.b64encode(fh.read()).decode())


def main():
    d = os.path.abspath(sys.argv[1])
    html = re.sub(r'\{\{([^}]+)\}\}', lambda m: uri(os.path.join(d, m.group(1))), PAGE)
    out = os.path.join(d, 'index.html')
    with open(out, 'w') as fh:
        fh.write(html)
    print(out, os.path.getsize(out))


if __name__ == '__main__':
    main()
