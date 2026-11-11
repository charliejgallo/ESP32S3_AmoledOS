/*
 * AmoledOS - lo que comparten todas las paginas del portal.
 *
 * Cuatro cosas: los idiomas, el menu lateral, el estado en vivo (y el aviso
 * de cuando el reloj no contesta), y un par de ayudas que estaban copiadas y pegadas en cada archivo (y
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
      { url: "/",         t: "nav_inicio",   ic: "\u2302" },
      { url: "/ajustes",  t: "nav_ajustes",  ic: "\u2699" },
      { url: "/alarmas",  t: "nav_alarmas",  ic: "\u25f7" },
      { url: "/pantalla", t: "nav_pantalla", ic: "\u25a3" },
      { url: "/menu",     t: "nav_menu",     ic: "\u25a6" },
      { url: "/archivos", t: "nav_archivos", ic: "\u25a4" },
      { url: "/registro", t: "nav_registro", ic: "\u2261" },
      { url: "/usb",      t: "nav_usb",      ic: "\u21c4" }
    ]},
    { t: "grp_red", paginas: [
      { url: "/wifi",     t: "nav_wifi",     ic: "\u25e0" },
      { url: "/ap",       t: "nav_ap",       ic: "\u25c9" },
      { url: "/red",      t: "nav_red",      ic: "\u2316" }
    ]},
    /* Las de las apps de la tarjeta no estan aca: cada una trae su pagina
       (/sdcard/apps/<app>.html) y el menu las pide a /api/paginas. Las de
       las esferas (face_x.html) llegan por el mismo camino y van a su
       propio grupo. */
    { t: "grp_apps", apps: true, paginas: [
      { url: "/pato",     t: "nav_pato"     },
      { url: "/iconos",   t: "nav_iconos"   }
    ]},
    { t: "grp_esferas", esferas: true, paginas: [] }
  ];

  /* Los nombres de las paginas y de la franja de estado viven aca y no en cada
     archivo: son los mismos textos en todas. */
  var NAV = {
    es: { grp_reloj: "Reloj", grp_red: "Red", grp_apps: "Apps",
          grp_esferas: "Esferas", grp_fijas: "Fijadas", nav_buscar: "Buscar\u2026", nav_nada: "nada con ese nombre",
          nav_menu_abrir: "Men\u00fa", nav_fijar: "Fijar arriba", nav_soltar: "Dejar de fijar", nav_despertar: "Despertar",
          nav_inicio: "Inicio", nav_ajustes: "Ajustes", nav_alarmas: "Alarmas", nav_pantalla: "Pantalla", nav_menu: "Menú",
          nav_archivos: "Archivos", nav_registro: "Registro", nav_usb: "USB",
          nav_wifi: "Conectar", nav_ap: "Punto de acceso", nav_red: "Escaneos",
          nav_pato: "Pato goma", nav_iconos: "Iconos",
          lg_titulo: "Pedile permiso al reloj",
          lg_texto: "Este reloj no está en una red de confianza, así que el portal necesita que lo apruebes en el reloj.",
          lg_pedir: "Pedir permiso", lg_codigo: "Aprobá en el reloj el código", lg_esperando: "esperando la respuesta del reloj...",
          lg_rechazado: "El reloj dijo que no.", lg_vencido: "Nadie respondió en el reloj. Probá de nuevo.",
          lg_otro: "Otro navegador está esperando una respuesta del reloj.", lg_espera: "Esperá %s s antes de pedir de nuevo.",
          lg_salir: "Cerrar sesión",
          viv_cargando: "cargando", viv_usb: "USB", viv_sin: "sin conexión con el reloj",
          viv_ram: "RAM", viv_activa: "pantalla activa", viv_aod: "atenuada",
          viv_off: "pantalla apagada", viv_ap: "modo AP", viv_prueba: "a prueba",
          viv_reintenta: "Sin conexi\u00f3n con el reloj \u00b7 reintentando\u2026", viv_volvio: "el reloj volvi\u00f3",
          ac_abrir: "Abrir en el reloj", ac_abierta: "abierta en el reloj", ac_esfera: "Usar esta esfera", ac_puesta: "esfera puesta" },
    en: { grp_reloj: "Watch", grp_red: "Network", grp_apps: "Apps",
          grp_esferas: "Watchfaces", grp_fijas: "Pinned", nav_buscar: "Search\u2026", nav_nada: "nothing by that name",
          nav_menu_abrir: "Menu", nav_fijar: "Pin to the top", nav_soltar: "Unpin", nav_despertar: "Wake",
          nav_inicio: "Home", nav_ajustes: "Settings", nav_alarmas: "Alarms", nav_pantalla: "Screen", nav_menu: "Menu",
          nav_archivos: "Files", nav_registro: "Log", nav_usb: "USB",
          nav_wifi: "Connect", nav_ap: "Access point", nav_red: "Scans",
          nav_pato: "Pato goma", nav_iconos: "Icons",
          lg_titulo: "Ask the watch for permission",
          lg_texto: "This watch is not on a trusted network, so the portal needs you to approve it on the watch.",
          lg_pedir: "Ask for permission", lg_codigo: "Approve on the watch the code", lg_esperando: "waiting for the watch to answer...",
          lg_rechazado: "The watch said no.", lg_vencido: "Nobody answered on the watch. Try again.",
          lg_otro: "Another browser is waiting for an answer from the watch.", lg_espera: "Wait %s s before asking again.",
          lg_salir: "Log out",
          viv_cargando: "loading", viv_usb: "USB", viv_sin: "no connection to the watch",
          viv_ram: "RAM", viv_activa: "screen on", viv_aod: "dimmed",
          viv_off: "screen off", viv_ap: "AP mode", viv_prueba: "on trial",
          viv_reintenta: "No connection to the watch \u00b7 trying again\u2026", viv_volvio: "the watch is back",
          ac_abrir: "Open on the watch", ac_abierta: "open on the watch", ac_esfera: "Use this watchface", ac_puesta: "watchface set" },
    de: { grp_reloj: "Uhr", grp_red: "Netz", grp_apps: "Apps",
          grp_esferas: "Zifferbl\u00e4tter", grp_fijas: "Angeheftet", nav_buscar: "Suchen\u2026", nav_nada: "nichts mit diesem Namen",
          nav_menu_abrir: "Men\u00fc", nav_fijar: "Oben anheften", nav_soltar: "L\u00f6sen", nav_despertar: "Wecken",
          nav_inicio: "Start", nav_ajustes: "Einstellungen", nav_alarmas: "Wecker", nav_pantalla: "Bildschirm", nav_menu: "Menü",
          nav_archivos: "Dateien", nav_registro: "Protokoll", nav_usb: "USB",
          nav_wifi: "Verbinden", nav_ap: "Zugangspunkt", nav_red: "Scans",
          nav_pato: "Pato goma", nav_iconos: "Icons",
          lg_titulo: "Die Uhr um Erlaubnis bitten",
          lg_texto: "Diese Uhr ist in keinem vertrauenswuerdigen Netz, deshalb muss das Portal auf der Uhr erlaubt werden.",
          lg_pedir: "Um Erlaubnis bitten", lg_codigo: "Auf der Uhr den Code erlauben", lg_esperando: "warte auf die Antwort der Uhr...",
          lg_rechazado: "Die Uhr hat abgelehnt.", lg_vencido: "Niemand hat auf der Uhr geantwortet. Nochmal versuchen.",
          lg_otro: "Ein anderer Browser wartet auf eine Antwort der Uhr.", lg_espera: "%s s warten, bevor du nochmal fragst.",
          lg_salir: "Abmelden",
          viv_cargando: "laedt", viv_usb: "USB", viv_sin: "keine Verbindung zur Uhr",
          viv_ram: "RAM", viv_activa: "Bildschirm an", viv_aod: "gedimmt",
          viv_off: "Bildschirm aus", viv_ap: "AP-Modus", viv_prueba: "auf Probe",
          viv_reintenta: "Keine Verbindung zur Uhr \u00b7 neuer Versuch\u2026", viv_volvio: "die Uhr ist wieder da",
          ac_abrir: "Auf der Uhr \u00f6ffnen", ac_abierta: "auf der Uhr offen", ac_esfera: "Dieses Zifferblatt nehmen", ac_puesta: "Zifferblatt gesetzt" }
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
    /* data-t-html, para los textos que llevan marcado.
     *
     * data-t escapa, y tiene que escapar: es lo correcto para todo lo que sea
     * una frase. Pero varias ayudas nombran comandos con <code>, y en espanol
     * se veian bien -el marcado esta en el HTML- mientras que en ingles y en
     * aleman salia "<code>enter</code>" escrito tal cual, porque el texto
     * traducido entraba por textContent. Le pasaba a /pato desde que existe y
     * lo heredó /lua.
     *
     * Estos textos no vienen de la red: estan en el mismo archivo que la
     * pagina, embebido en el firmware. Aun asi va por un atributo aparte y no
     * cambiando data-t, para que quede escrito cuales son los que confian. */
    var htmls = (raiz || document).querySelectorAll("[data-t-html]");
    for (var h = 0; h < htmls.length; h++) {
      var w = dic[htmls[h].dataset.tHtml];
      if (w === undefined && NAV[codigo]) w = NAV[codigo][htmls[h].dataset.tHtml];
      if (w !== undefined) htmls[h].innerHTML = w;
    }

    /* El placeholder no es contenido, asi que lleva su propio atributo. */
    var phs = (raiz || document).querySelectorAll("[data-t-ph]");
    for (var j = 0; j < phs.length; j++) {
      var p = dic[phs[j].dataset.tPh];
      if (p === undefined && NAV[codigo]) p = NAV[codigo][phs[j].dataset.tPh];
      if (p !== undefined) phs[j].placeholder = p;
    }
  }

  /* Las paginas de las apps y las esferas de la tarjeta: [{ pagina, app,
     nombre }], de /api/paginas. La ultima lista se guarda en la sesion para
     que el menu salga entero de una al pasar de pagina, y se pide de nuevo cada vez. */
  var deApps = null;
  try { deApps = JSON.parse(sessionStorage.getItem("aos_paginas")); } catch (e) {}

  function pedirPaginas() {
    fetch("/api/paginas").then(function (r) { return r.json(); })
      .then(function (d) {
        var lista = (d && d.paginas) || [];
        lista.sort(function (a, b) { return a.nombre.localeCompare(b.nombre); });
        var antes = JSON.stringify(deApps);
        deApps = lista;
        try { sessionStorage.setItem("aos_paginas", JSON.stringify(lista)); } catch (e) {}
        if (JSON.stringify(lista) !== antes) pintarNav();
        pintarAccion(ultimo);
      })
      .catch(function () { /* sin la lista quedan las del firmware */ });
  }

  /* El menu lateral (v0.17.0). Era una cinta de pastillas arriba de cada
     pagina, y con cuarenta apps y nueve esferas en la tarjeta ya eran tres
     renglones por los que no habia como moverse. Ahora es una columna fija a
     la izquierda, como la de P4OS, y en una pantalla angosta un cajon que
     abre el boton de la barra de arriba. La arma este archivo solo, sobre el
     <body>: las paginas (las del firmware y las que cada app y cada esfera
     trae en la tarjeta) no cambian, y su <nav class="paginas"> queda vacio.

     Lo que recuerda vive en el navegador (localStorage): que paginas estan
     fijadas arriba y que grupos estan plegados. */
  function leerLS(clave, def) {
    try { var v = JSON.parse(localStorage.getItem(clave)); return v || def; } catch (e) { return def; }
  }
  function guardarLS(clave, v) {
    try { localStorage.setItem(clave, JSON.stringify(v)); } catch (e) {}
  }
  function llano(x) {       /* "Menú" -> "menu": para buscar sin acentos */
    x = String(x).toLowerCase();
    return x.normalize ? x.normalize("NFD").replace(/[̀-ͯ]/g, "") : x;
  }

  /* Los grupos con sus paginas, las del firmware y las de la tarjeta. */
  function armarGrupos() {
    var salida = [];
    for (var g = 0; g < GRUPOS.length; g++) {
      var grupo = GRUPOS[g], items = [];
      for (var i = 0; i < grupo.paginas.length; i++) {
        items.push({ url: grupo.paginas[i].url, texto: t(grupo.paginas[i].t), ic: grupo.paginas[i].ic });
      }
      if ((grupo.apps || grupo.esferas) && deApps) {
        var propias = items.length, extra = [];
        for (var k = 0; k < deApps.length; k++) {
          var esfera = deApps[k].pagina.indexOf("face_") === 0;
          if (esfera !== !!grupo.esferas) continue;
          var url = "/" + deApps[k].pagina, repetida = false;
          for (var j = 0; j < propias; j++) if (items[j].url === url) repetida = true;
          if (!repetida) extra.push({ url: url, texto: deApps[k].nombre });
        }
        items = extra.concat(items);
      }
      if (items.length) salida.push({ t: grupo.t, items: items });
    }
    return salida;
  }

  function enlace(it, aca, fijas) {
    /* sin icono propio, la inicial: las paginas de la tarjeta no traen uno */
    var ic = it.ic ? '<span class="ic">' + it.ic + "</span>"
                   : '<span class="ic letra">' + esc(it.texto.charAt(0).toUpperCase()) + "</span>";
    var fija = fijas.indexOf(it.url) >= 0;
    return '<a href="' + esc(it.url) + '"' + (it.url === aca ? ' class="aca"' : "") +
           ' data-b="' + esc(llano(it.texto)) + '">' + ic + '<span class="nom">' + esc(it.texto) + "</span>" +
           '<button class="fija' + (fija ? " on" : "") + '" data-url="' + esc(it.url) + '" title="' +
           esc(t(fija ? "nav_soltar" : "nav_fijar")) + '">' + (fija ? "★" : "☆") + "</button></a>";
  }

  function pintarNav() {
    var lado = document.getElementById("lado");
    if (!lado) {
      /* la barra de arriba (solo en angosto), el cajon y su velo */
      var barra = document.createElement("div");
      barra.id = "barra";
      barra.className = "barra";
      barra.innerHTML = '<button id="lado-abrir" class="hamb" aria-label="menu">☰</button>' +
                        '<a href="/" class="logo">AmoledOS</a><span class="quien"></span>' +
                        '<span class="viva mini" id="viva-mini"></span>';
      document.body.insertBefore(barra, document.body.firstChild);
      lado = document.createElement("aside");
      lado.id = "lado";
      lado.className = "lado";
      document.body.appendChild(lado);
      var velo = document.createElement("div");
      velo.id = "lado-velo";
      velo.className = "lado-velo";
      velo.onclick = function () { document.body.classList.remove("lado-abierto"); };
      document.body.appendChild(velo);
      document.body.classList.add("con-lado");
      barra.querySelector("#lado-abrir").onclick = function () {
        document.body.classList.toggle("lado-abierto");
      };
      document.addEventListener("keydown", function (e) {
        var en = e.target && e.target.tagName;
        var buscar = document.getElementById("lado-buscar");
        if (e.key === "Escape") {
          document.body.classList.remove("lado-abierto");
        } else if (e.key === "/" && buscar && en !== "INPUT" && en !== "TEXTAREA" && en !== "SELECT" &&
                   !(e.target && e.target.isContentEditable)) {
          e.preventDefault();
          document.body.classList.add("lado-abierto");
          buscar.focus();
          buscar.select();
        }
      });
    }
    var aca = location.pathname.replace(/\/$/, "") || "/";
    var fijas = leerLS("aos_fijas", []), plegados = leerLS("aos_plegados", {});
    var grupos = armarGrupos();
    var buscado = (document.getElementById("lado-buscar") || {}).value || "";

    var html = '<div class="lado-cab"><a href="/" class="logo">AmoledOS</a>' +
               '<span class="quien"></span></div>' +
               '<div class="viva" id="viva"><span class="chip dim">' + esc(t("viv_cargando")) + "</span></div>" +
               '<input type="search" id="lado-buscar" class="lado-buscar" autocomplete="off" placeholder="' +
               esc(t("nav_buscar")) + '"><div class="lado-lista" id="lado-lista">';
    /* las fijadas, arriba y en el orden en que se fijaron */
    var arriba = "";
    for (var f = 0; f < fijas.length; f++) {
      for (var a = 0; a < grupos.length; a++) {
        for (var b = 0; b < grupos[a].items.length; b++) {
          if (grupos[a].items[b].url === fijas[f]) arriba += enlace(grupos[a].items[b], aca, fijas);
        }
      }
    }
    if (arriba) {
      html += '<div class="lado-grupo fijas"><div class="rotulo">' + esc(t("grp_fijas")) + "</div>" + arriba + "</div>";
    }
    for (var g = 0; g < grupos.length; g++) {
      var plegado = !!plegados[grupos[g].t], tieneAca = false, dentro = "";
      for (var m = 0; m < grupos[g].items.length; m++) {
        if (grupos[g].items[m].url === aca) tieneAca = true;
        dentro += enlace(grupos[g].items[m], aca, fijas);
      }
      /* un grupo plegado no esconde la pagina en la que estas */
      html += '<div class="lado-grupo' + (plegado && !tieneAca ? " plegado" : "") + '" data-g="' + grupos[g].t + '">' +
              '<button class="rotulo">' + esc(t(grupos[g].t)) + '<span class="n">' + grupos[g].items.length +
              '</span><span class="flecha">▾</span></button>' + dentro + "</div>";
    }
    html += '<div class="lado-nada" id="lado-nada" hidden>' + esc(t("nav_nada")) + "</div></div>" +
            '<div class="lado-pie"><button class="sec" id="lado-despertar">☼ ' + esc(t("nav_despertar")) + "</button>" +
            (acceso && acceso.zona === "otra" && acceso.tiene
              ? '<a href="#" class="salir" id="salir">' + esc(t("lg_salir")) + "</a>" : "") + "</div>";
    lado.innerHTML = html;
    if (ultimo) { pintarViva(ultimo); pintarQuien(ultimo); }

    var lista = document.getElementById("lado-lista");
    var buscar = document.getElementById("lado-buscar");
    function filtrar() {
      var q = llano(buscar.value.trim()), hay = 0;
      var enlaces = lista.querySelectorAll("a");
      for (var i = 0; i < enlaces.length; i++) {
        var ok = !q || enlaces[i].getAttribute("data-b").indexOf(q) >= 0;
        enlaces[i].hidden = !ok;
        if (ok) hay++;
      }
      /* buscando, los rotulos y los pliegues estorban */
      lado.classList.toggle("buscando", !!q);
      document.getElementById("lado-nada").hidden = hay > 0;
    }
    buscar.value = buscado;
    buscar.oninput = filtrar;
    buscar.onkeydown = function (e) {
      if (e.key === "Enter") {
        var primero = lista.querySelector("a:not([hidden])");
        if (primero) location.href = primero.getAttribute("href");
      } else if (e.key === "Escape") {
        buscar.value = "";
        filtrar();
        buscar.blur();
      }
    };
    if (buscado) filtrar();

    lista.onclick = function (e) {
      var el = e.target;
      if (el.classList.contains("fija")) {            /* la estrella */
        e.preventDefault();
        var url = el.getAttribute("data-url"), fs = leerLS("aos_fijas", []);
        var i = fs.indexOf(url);
        if (i >= 0) fs.splice(i, 1); else fs.push(url);
        guardarLS("aos_fijas", fs);
        pintarNav();
        return;
      }
      var rot = el.closest ? el.closest("button.rotulo") : null;
      if (rot) {                                      /* plegar un grupo */
        var g = rot.parentNode.getAttribute("data-g"), pl = leerLS("aos_plegados", {});
        if (pl[g]) delete pl[g]; else pl[g] = 1;
        guardarLS("aos_plegados", pl);
        pintarNav();
        return;
      }
      if (el.closest && el.closest("a")) {
        /* al cambiar de pagina el menu se dibuja de nuevo: que quede donde estaba */
        try { sessionStorage.setItem("aos_lado_y", String(lista.scrollTop)); } catch (e2) {}
      }
    };
    var y = 0;
    try { y = parseInt(sessionStorage.getItem("aos_lado_y"), 10) || 0; } catch (e3) {}
    lista.scrollTop = y;
    aLaVista();
    /* y otra vez cuando lleguen los chips del estado: ocupan dos o tres
       renglones donde habia uno, y la lista se corre y se acorta */
    vistaPendiente = !ultimo;

    document.getElementById("lado-despertar").onclick = function () {
      post("/api/accion", { que: "despertar" }).then(function () { estado(); })
        .catch(function (e) { brindis(String(e.message || e)); });
    };
    var salir = document.getElementById("salir");
    if (salir) salir.onclick = function (e) {
      e.preventDefault();
      fetch("/api/salir", { method: "POST" }).then(function () { location.reload(); });
    };
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

  /* La pagina en la que estas, a la vista en el menu. */
  var vistaPendiente = false;
  function aLaVista() {
    var activa = document.querySelector("#lado-lista a.aca");
    if (activa && activa.scrollIntoView) {
      try { activa.scrollIntoView({ block: "nearest" }); } catch (e) {}
    }
  }

  function pintarViva(s) {
    var html = s ? chipsDe(s) : '<span class="chip mal">' + esc(t("viv_sin")) + "</span>";
    var el = document.getElementById("viva");
    if (el) el.innerHTML = html;
    if (vistaPendiente && s) { vistaPendiente = false; aLaVista(); }
    /* en la barra de arriba no entra todo: la bateria y la red, o el aviso */
    var mini = document.getElementById("viva-mini");
    if (mini) {
      var tmp = document.createElement("div");
      tmp.innerHTML = html;
      var chips = tmp.querySelectorAll(".chip"), corto = "";
      for (var i = 0; i < chips.length && i < 2; i++) corto += chips[i].outerHTML;
      mini.innerHTML = corto;
    }
  }

  /* Which watch this is: its name next to the logo (in the menu and on the
     top bar) and in the tab, so two of them on one network are told apart
     at a glance. */
  function pintarQuien(s) {
    var nombre = s && s.name && s.name !== "amoledos" ? s.name : "";
    var qs = document.querySelectorAll(".quien");
    for (var i = 0; i < qs.length; i++) qs[i].textContent = nombre;
    if (!document.__tituloBase) document.__tituloBase = document.title;
    document.title = nombre ? nombre + " \u00b7 " + document.__tituloBase : document.__tituloBase;
  }

  /* El reloj dejo de contestar (se reinicio, se instalo un firmware, salio
     de la wifi): un aviso arriba y se pregunta cada 3 s en vez de cada 15,
     hasta que vuelve. Una sola falla no alcanza para el aviso: el servidor
     del reloj atiende de a un pedido y una captura lo tiene ocupado un rato.
     Si al volver trae otra version, la pagina se carga de nuevo: sus
     archivos son los del firmware. */
  var fallas = 0, versionVista = null;

  function avisoRed(on) {
    var el = document.getElementById("aviso-red");
    if (on && !el) {
      el = document.createElement("div");
      el.id = "aviso-red";
      el.className = "aviso-red";
      document.body.appendChild(el);
    }
    if (el) {
      el.textContent = t("viv_reintenta");
      el.classList.toggle("on", !!on);
    }
  }

  function estado() {
    /* sin limite, un reloj apagado deja el pedido colgado un minuto */
    var corte = window.AbortController ? new AbortController() : null;
    var reloj = corte ? setTimeout(function () { corte.abort(); }, 7000) : 0;
    return fetch("/api/status", corte ? { signal: corte.signal } : undefined).then(function (r) {
        /* the session was closed from the watch: the login again */
        if (r.status === 401) { location.reload(); throw new Error("401"); }
        return r.json();
      })
      .then(function (s) {
        clearTimeout(reloj);
        var volvio = fallas >= 2;
        fallas = 0;
        if (volvio) {
          avisoRed(false);
          if (versionVista && s.version && s.version !== versionVista) { location.reload(); return s; }
          brindis(t("viv_volvio"));
        }
        if (s.version) versionVista = s.version;
        ultimo = s;
        pintarViva(s);
        pintarQuien(s);
        pintarAccion(s);
        for (var i = 0; i < oyentes.length; i++) { try { oyentes[i](s); } catch (e) {} }
        return s;
      })
      .catch(function () {
        clearTimeout(reloj);
        fallas++;
        pintarViva(null);
        if (fallas >= 2) avisoRed(true);
        return null;
      });
  }

  function ciclo() {
    estado().then(function () { setTimeout(ciclo, fallas ? 3000 : 15000); });
  }

  /* En la pagina de una app o de una esfera de la tarjeta, un boton junto al
     titulo que la abre (o la pone) en el reloj: quien esta configurando la
     Radio quiere verla. /api/paginas dice de quien es cada pagina. */
  function pintarAccion(s) {
    var aca = location.pathname.replace(/\/$/, "") || "/", mia = null;
    for (var i = 0; deApps && i < deApps.length; i++) {
      if ("/" + deApps[i].pagina === aca && deApps[i].app) mia = deApps[i];
    }
    var top = document.querySelector(".wrap .top"), b = document.getElementById("ac-reloj");
    /* las paginas de esfera de v0.16 traian su propio boton (#poner) */
    if (!mia || !top || document.getElementById("poner")) { if (b) b.remove(); return; }
    var esfera = mia.pagina.indexOf("face_") === 0;
    if (!b) {
      b = document.createElement("button");
      b.id = "ac-reloj";
      b.className = "sec chica ac-reloj";
      top.appendChild(b);
    }
    var ya = s && (esfera ? s.face === mia.app : s.app === mia.app);
    b.textContent = t(esfera ? (ya ? "ac_puesta" : "ac_esfera") : (ya ? "ac_abierta" : "ac_abrir"));
    b.disabled = !!ya;
    b.onclick = function () {
      b.disabled = true;
      (esfera ? post("/api/ajustes", { esfera: mia.app })
              : post("/api/accion", { que: "abrir", id: mia.app }))
        .then(function () { setTimeout(estado, 700); })
        .catch(function (e) { b.disabled = false; brindis(String(e.message || e)); });
    };
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

  /* Donde entro el pedido y si hace falta permiso (docs/SECURITY.md): en una
     red que no es de confianza, el reloj aprueba cada navegador. */
  var acceso = null;

  /* La pantalla de permiso, en lugar de la pagina: un boton, el codigo que
     tambien muestra el reloj, y la espera. Aprobado, se recarga la pagina y
     ya entra con la cookie. Devuelve una promesa que no se cumple: la pagina
     no carga sus datos sin permiso. */
  function login() {
    var wrap = document.querySelector(".wrap") || document.body;
    var nav = wrap.querySelector(".paginas");
    var kids = wrap.children;
    for (var i = 0; i < kids.length; i++) {
      if (kids[i] !== nav && !kids[i].classList.contains("top")) kids[i].style.display = "none";
    }
    var caja = document.createElement("div");
    caja.className = "tarjeta";
    caja.innerHTML = "<h3>" + esc(t("lg_titulo")) + "</h3><p class=\"sub\">" + esc(t("lg_texto")) +
      "</p><div class=\"fila\"><button id=\"lg-pedir\">" + esc(t("lg_pedir")) +
      "</button></div><p id=\"lg-estado\" class=\"sub\"></p>";
    wrap.appendChild(caja);
    var estadoEl = caja.querySelector("#lg-estado"), boton = caja.querySelector("#lg-pedir");
    function decir(html) { estadoEl.innerHTML = html; }
    function seguir(id) {
      fetch("/api/pedir?id=" + id).then(function (r) { return r.json(); }).then(function (d) {
        if (d.estado === "aprobado") { location.reload(); return; }
        if (d.estado === "esperando") { setTimeout(function () { seguir(id); }, 1500); return; }
        boton.disabled = false;
        decir(esc(t(d.estado === "rechazado" ? "lg_rechazado" : "lg_vencido")));
      }).catch(function () { setTimeout(function () { seguir(id); }, 3000); });
    }
    boton.onclick = function () {
      boton.disabled = true;
      fetch("/api/pedir", { method: "POST" }).then(function (r) {
        return r.json().then(function (d) { return { st: r.status, d: d }; });
      }).then(function (x) {
        if (x.st === 429) { boton.disabled = false; decir(esc(t("lg_espera").replace("%s", x.d.espera))); return; }
        if (x.st !== 200) { boton.disabled = false; decir(esc(t("lg_otro"))); return; }
        decir(esc(t("lg_codigo")) + ' <b style="font-size:1.6em;letter-spacing:.12em">' + esc(x.d.codigo) +
              "</b><br><span class=\"dim\">" + esc(t("lg_esperando")) + "</span>");
        seguir(x.d.id);
      }).catch(function () { boton.disabled = false; decir(esc(t("viv_sin"))); });
    };
    return new Promise(function () {});
  }

  /* Arranque. Se le pasa el diccionario de la pagina: { en: {...}, de: {...} }.
     Devuelve una promesa para poder encadenar la carga de datos despues, que
     es lo que quieren todas: aos.init(TR).then(cargar). */
  function init(paginaTR) {
    return fetch("/api/lang")
      .then(function (r) { return r.json(); })
      .then(function (d) {
        codigo = d.actual || "es";
        try { sessionStorage.setItem("aos_idioma", codigo); } catch (e) {}
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
        return fetch("/api/auth").then(function (r) { return r.json(); })
          .catch(function () { return null; });   /* un firmware sin login */
      })
      .then(function (a) {
        acceso = a;
        pintarNav();
        if (a && a.necesita && !a.tiene) return login();
        pedirPaginas();
        ciclo();
      });
  }

  /* El menu, ya, con lo que se sabia de la pagina anterior (el idioma y las
     paginas de la tarjeta): sin esto cada cambio de pagina lo mostraba
     vacio un instante y corria el contenido de lugar. init() lo pinta de
     nuevo con lo que el reloj conteste. */
  try { codigo = sessionStorage.getItem("aos_idioma") || "es"; } catch (e) {}
  if (!NAV[codigo]) codigo = "es";
  if (document.body) pintarNav();

  window.aos = {
    init: init, t: t, esc: esc, aplicar: aplicar, brindis: brindis,
    humano: humano, duracion: duracion, post: post,
    estado: estado,
    acceso: function () { return acceso; },
    alEstado: function (fn) { oyentes.push(fn); if (ultimo) fn(ultimo); },
    $: function (id) { return document.getElementById(id); },
    idioma: function () { return codigo; }
  };
})();
