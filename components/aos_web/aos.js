/*
 * AmoledOS - lo que comparten todas las paginas del portal.
 *
 * Cuatro cosas: los idiomas, la cinta de navegacion, la franja de estado en
 * vivo, y un par de ayudas que estaban copiadas y pegadas en cada archivo (y
 * no siempre iguales).
 *
 * Los idiomas siguen al IDIOMA DEL RELOJ, no al del navegador: es el mismo
 * aparato y seria raro que la pantalla dijera una cosa y la web otra. Sale de
 * /api/lang, que ya existia para el desplegable de Ajustes.
 *
 * Las claves son cortas e inventadas -data-t="btn_guardar"- y NO la frase en
 * espanol, al reves que en el firmware. Aca no hay gen_lang.py que extraiga
 * nada: el espanol es el que ya esta escrito en el HTML y cada pagina lleva
 * adentro su diccionario de en y de. Un idioma que no este en el diccionario
 * cae en espanol sin romper nada, que es lo que pasa con cualquier pack que
 * alguien copie a la tarjeta.
 *
 * Nada de esto le cuesta RAM al reloj: el archivo vive en la flash y lo corre
 * el navegador. La unica carga que agrega es un GET /api/status cada 15 s por
 * pagina abierta, que son 700 bytes de JSON armados en la pila del servidor.
 */
(function () {
  "use strict";

  /* Las paginas del portal, en un solo lugar y en tres grupos: lo que es del
     reloj, lo que es de la red, y lo que configura una app. Antes cada archivo
     tenia su propia lista y ninguna coincidia: desde /clima no se podia
     volver, y /ap no figuraba en ninguna salvo en las dos que la nombraban a
     mano. */
  var GRUPOS = [
    { t: "grp_reloj", paginas: [
      { url: "/",         t: "nav_inicio"   },
      { url: "/ajustes",  t: "nav_ajustes"  },
      { url: "/alarmas",  t: "nav_alarmas"  },
      { url: "/pantalla", t: "nav_pantalla" },
      { url: "/archivos", t: "nav_archivos" },
      { url: "/registro", t: "nav_registro" }
    ]},
    { t: "grp_red", paginas: [
      { url: "/wifi",     t: "nav_wifi"     },
      { url: "/ap",       t: "nav_ap"       },
      { url: "/red",      t: "nav_red"      }
    ]},
    { t: "grp_apps", paginas: [
      { url: "/clima",    t: "nav_clima"    },
      { url: "/cotiz",    t: "nav_cotiz"    },
      { url: "/sensores", t: "nav_sensores" },
      { url: "/remoto",   t: "nav_remoto"   },
      { url: "/pixel",    t: "nav_pixel"    }
    ]}
  ];

  /* Los nombres de las paginas y de la franja de estado viven aca y no en cada
     archivo: son los mismos textos en todas. */
  var NAV = {
    es: { grp_reloj: "Reloj", grp_red: "Red", grp_apps: "Apps",
          nav_inicio: "Inicio", nav_ajustes: "Ajustes", nav_alarmas: "Alarmas", nav_pantalla: "Pantalla",
          nav_archivos: "Archivos", nav_registro: "Registro",
          nav_wifi: "Conectar", nav_ap: "Punto de acceso", nav_red: "Escaneos",
          nav_clima: "Clima", nav_cotiz: "Cotizaciones",
          nav_sensores: "Sensores", nav_remoto: "Remoto", nav_pixel: "Pixel Art",
          viv_cargando: "cargando", viv_usb: "USB", viv_sin: "sin conexión con el reloj",
          viv_ram: "RAM", viv_activa: "pantalla activa", viv_aod: "atenuada",
          viv_off: "pantalla apagada", viv_ap: "modo AP", viv_prueba: "a prueba" },
    en: { grp_reloj: "Watch", grp_red: "Network", grp_apps: "Apps",
          nav_inicio: "Home", nav_ajustes: "Settings", nav_alarmas: "Alarms", nav_pantalla: "Screen",
          nav_archivos: "Files", nav_registro: "Log",
          nav_wifi: "Connect", nav_ap: "Access point", nav_red: "Scans",
          nav_clima: "Weather", nav_cotiz: "Exchange rates",
          nav_sensores: "Sensors", nav_remoto: "Remote", nav_pixel: "Pixel Art",
          viv_cargando: "loading", viv_usb: "USB", viv_sin: "no connection to the watch",
          viv_ram: "RAM", viv_activa: "screen on", viv_aod: "dimmed",
          viv_off: "screen off", viv_ap: "AP mode", viv_prueba: "on trial" },
    de: { grp_reloj: "Uhr", grp_red: "Netz", grp_apps: "Apps",
          nav_inicio: "Start", nav_ajustes: "Einstellungen", nav_alarmas: "Wecker", nav_pantalla: "Bildschirm",
          nav_archivos: "Dateien", nav_registro: "Protokoll",
          nav_wifi: "Verbinden", nav_ap: "Zugangspunkt", nav_red: "Scans",
          nav_clima: "Wetter", nav_cotiz: "Wechselkurse",
          nav_sensores: "Sensoren", nav_remoto: "Fernbedienung", nav_pixel: "Pixel Art",
          viv_cargando: "laedt", viv_usb: "USB", viv_sin: "keine Verbindung zur Uhr",
          viv_ram: "RAM", viv_activa: "Bildschirm an", viv_aod: "gedimmt",
          viv_off: "Bildschirm aus", viv_ap: "AP-Modus", viv_prueba: "auf Probe" }
  };

  var dic = {};          /* el diccionario del idioma puesto, o {} en espanol */
  var codigo = "es";

  function t(clave) {
    if (dic[clave] !== undefined) return dic[clave];
    if (NAV[codigo] && NAV[codigo][clave] !== undefined) return NAV[codigo][clave];
    if (NAV.es[clave] !== undefined) return NAV.es[clave];
    return clave;       /* sin traduccion: se ve la clave, que avisa del olvido */
  }

  /* Texto de afuera -un SSID del vecino, el nombre de una entidad de Home
     Assistant- que termina adentro de innerHTML. Sin esto, un nombre con
     etiquetas se ejecuta en la pagina; paso de verdad en wifi.html. */
  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s === undefined || s === null ? "" : String(s);
    return d.innerHTML;
  }

  function aplicar(raiz) {
    var nodos = (raiz || document).querySelectorAll("[data-t]");
    for (var i = 0; i < nodos.length; i++) {
      var v = dic[nodos[i].dataset.t];
      if (v === undefined && NAV[codigo]) v = NAV[codigo][nodos[i].dataset.t];
      if (v !== undefined) nodos[i].textContent = v;
    }
    /* El placeholder no es contenido, asi que lleva su propio atributo. */
    var phs = (raiz || document).querySelectorAll("[data-t-ph]");
    for (var j = 0; j < phs.length; j++) {
      var p = dic[phs[j].dataset.tPh];
      if (p === undefined && NAV[codigo]) p = NAV[codigo][phs[j].dataset.tPh];
      if (p !== undefined) phs[j].placeholder = p;
    }
  }

  /* La cinta: la marca a la izquierda con la franja de estado, y debajo las
     paginas en sus tres grupos. En pantallas angostas la fila de paginas se
     desplaza de costado en vez de apilarse en tres renglones, y la pagina
     actual se trae a la vista sola. */
  function pintarNav() {
    var cinta = document.querySelector(".paginas");
    if (!cinta) return;
    var aca = location.pathname.replace(/\/$/, "") || "/";
    var html = '<div class="marca"><a href="/" class="logo">AmoledOS</a>' +
               '<span class="viva" id="viva"><span class="chip dim">' +
               esc(t("viv_cargando")) + '</span></span></div><div class="pills">';
    for (var g = 0; g < GRUPOS.length; g++) {
      var grupo = GRUPOS[g];
      html += '<span class="grupo"><span class="rotulo">' + esc(t(grupo.t)) + "</span>";
      for (var i = 0; i < grupo.paginas.length; i++) {
        var p = grupo.paginas[i];
        html += '<a href="' + p.url + '"' +
                (p.url === aca ? ' class="aca"' : "") + ">" + esc(t(p.t)) + "</a>";
      }
      html += "</span>";
    }
    html += "</div>";
    cinta.innerHTML = html;
    var activa = cinta.querySelector("a.aca");
    if (activa && activa.scrollIntoView) {
      try { activa.scrollIntoView({ block: "nearest", inline: "center" }); } catch (e) {}
    }
  }

  /* La franja de estado: bateria, red, memoria y pantalla, en chips. Se pinta
     con lo que devuelve /api/status y se refresca cada 15 s. Las paginas que
     quieren mas (Inicio) piden el JSON completo por su cuenta con estado(). */
  var ultimo = null;
  var oyentes = [];

  function chipsDe(s) {
    var c = [];
    var bat = s.battery >= 0 ? s.battery + "%" : "?";
    var clase = s.battery < 15 ? "mal" : s.battery < 30 ? "ojo" : "bien";
    c.push('<span class="chip ' + clase + '" title="' + esc(s.vbat ? s.vbat.toFixed(2) + " V" : "") + '">' +
           (s.charging ? "&#9889; " : "") + esc(bat) + "</span>");
    if (s.usb && !s.charging) c.push('<span class="chip dim">' + esc(t("viv_usb")) + "</span>");
    if (s.ssid) {
      c.push('<span class="chip" title="' + esc(s.ip || "") + '">' + esc(s.ssid) +
             (s.rssi ? ' <span class="dim">' + esc(s.rssi) + "</span>" : "") + "</span>");
    } else if (s.ap) {
      c.push('<span class="chip ojo">' + esc(t("viv_ap")) + "</span>");
    }
    if (s.heap !== undefined) {
      var kb = Math.round(s.heap / 1024);
      c.push('<span class="chip ' + (kb < 30 ? "mal" : kb < 45 ? "ojo" : "") + '">' +
             esc(t("viv_ram")) + " " + kb + " KB</span>");
    }
    if (s.display !== undefined) {
      c.push('<span class="chip dim">' +
             esc(t(s.display === 1 ? "viv_activa" : s.display === 2 ? "viv_aod" : "viv_off")) +
             "</span>");
    }
    if (s.trial) c.push('<span class="chip ojo">' + esc(t("viv_prueba")) + "</span>");
    if (s.version) c.push('<span class="chip dim">' + esc(s.version) + "</span>");
    return c.join("");
  }

  function pintarViva(s) {
    var el = document.getElementById("viva");
    if (!el) return;
    el.innerHTML = s ? chipsDe(s) : '<span class="chip mal">' + esc(t("viv_sin")) + "</span>";
  }

  function estado() {
    return fetch("/api/status").then(function (r) { return r.json(); })
      .then(function (s) {
        ultimo = s;
        pintarViva(s);
        for (var i = 0; i < oyentes.length; i++) { try { oyentes[i](s); } catch (e) {} }
        return s;
      })
      .catch(function () { pintarViva(null); return null; });
  }

  /* Aviso corto que se va solo. */
  function brindis(texto) {
    var el = document.getElementById("brindis");
    if (!el) {
      el = document.createElement("div");
      el.id = "brindis";
      document.body.appendChild(el);
    }
    el.textContent = texto;
    el.classList.add("on");
    clearTimeout(brindis._t);
    brindis._t = setTimeout(function () { el.classList.remove("on"); }, 2200);
  }

  /* Bytes a algo legible. Estaba copiada en tres paginas. */
  function humano(bytes) {
    if (bytes === undefined || bytes === null) return "";
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB";
    return (bytes / 1024 / 1024 / 1024).toFixed(2) + " GB";
  }

  /* Segundos a "3d 4h 12m". */
  function duracion(seg) {
    seg = Math.max(0, Math.floor(seg || 0));
    var d = Math.floor(seg / 86400), h = Math.floor(seg % 86400 / 3600),
        m = Math.floor(seg % 3600 / 60), s = seg % 60;
    if (d) return d + "d " + h + "h " + m + "m";
    if (h) return h + "h " + m + "m";
    if (m) return m + "m " + s + "s";
    return s + "s";
  }

  /* POST con cuerpo de formulario, que es lo que entienden todos los
     endpoints del firmware (httpd_query_key_value sobre el cuerpo). Devuelve
     el JSON de la respuesta, o lanza con el texto del error. */
  function post(url, campos) {
    var partes = [];
    for (var k in campos) {
      if (campos[k] === undefined || campos[k] === null) continue;
      partes.push(encodeURIComponent(k) + "=" + encodeURIComponent(campos[k]));
    }
    return fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: partes.join("&")
    }).then(function (r) {
      if (!r.ok) return r.text().then(function (tx) { throw new Error(tx || r.status); });
      return r.json().catch(function () { return {}; });
    });
  }

  /* Arranque. Se le pasa el diccionario de la pagina: { en: {...}, de: {...} }.
     Devuelve una promesa para poder encadenar la carga de datos despues, que
     es lo que quieren todas: aos.init(TR).then(cargar). */
  function init(paginaTR) {
    return fetch("/api/lang")
      .then(function (r) { return r.json(); })
      .then(function (d) {
        codigo = d.actual || "es";
        if (paginaTR && paginaTR[codigo]) {
          dic = paginaTR[codigo];
          document.documentElement.lang = codigo;
        } else if (NAV[codigo]) {
          /* Sin diccionario de pagina igual se traduce la navegacion: es mejor
             que media pagina en espanol y la cinta en ingles. */
          dic = {};
          document.documentElement.lang = codigo;
        }
        aplicar();
      })
      .catch(function () { /* sin endpoint queda el espanol del HTML */ })
      .then(function () {
        pintarNav();
        estado();
        setInterval(estado, 15000);
      });
  }

  window.aos = {
    init: init, t: t, esc: esc, aplicar: aplicar, brindis: brindis,
    humano: humano, duracion: duracion, post: post,
    estado: estado,
    alEstado: function (fn) { oyentes.push(fn); if (ultimo) fn(ultimo); },
    $: function (id) { return document.getElementById(id); },
    idioma: function () { return codigo; }
  };
})();
