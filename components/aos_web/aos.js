/*
 * AmoledOS - lo que comparten todas las paginas del portal.
 *
 * Tres cosas: los idiomas, la cinta de navegacion y un par de ayudas que
 * estaban copiadas y pegadas en cada archivo (y no siempre iguales).
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
 */
(function () {
  "use strict";

  /* Las paginas del portal, en un solo lugar. Antes cada archivo tenia su
     propia lista y ninguna coincidia: desde /clima no se podia volver, y /ap
     no figuraba en ninguna salvo en las dos que la nombraban a mano. */
  var PAGINAS = [
    { url: "/",         t: "nav_portal"   },
    { url: "/wifi",     t: "nav_wifi"     },
    { url: "/ap",       t: "nav_ap"       },
    { url: "/red",      t: "nav_red"      },
    { url: "/clima",    t: "nav_clima"    },
    { url: "/cotiz",    t: "nav_cotiz"    },
    { url: "/sensores", t: "nav_sensores" },
    { url: "/remoto",   t: "nav_remoto"   }
  ];

  /* Los nombres de las paginas viven aca y no en cada archivo: son los mismos
     ocho textos en las ocho paginas. */
  var NAV = {
    es: { nav_portal: "Archivos", nav_wifi: "Conectar", nav_ap: "Punto de acceso",
          nav_red: "Escaneos", nav_clima: "Clima", nav_cotiz: "Cotizaciones",
          nav_sensores: "Sensores", nav_remoto: "Remoto" },
    en: { nav_portal: "Files", nav_wifi: "Connect", nav_ap: "Access point",
          nav_red: "Scans", nav_clima: "Weather", nav_cotiz: "Exchange rates",
          nav_sensores: "Sensors", nav_remoto: "Remote" },
    de: { nav_portal: "Dateien", nav_wifi: "Verbinden", nav_ap: "Zugangspunkt",
          nav_red: "Scans", nav_clima: "Wetter", nav_cotiz: "Wechselkurse",
          nav_sensores: "Sensoren", nav_remoto: "Fernbedienung" }
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

  function pintarNav() {
    var cinta = document.querySelector(".paginas");
    if (!cinta) return;
    var aca = location.pathname.replace(/\/$/, "") || "/";
    var html = "";
    for (var i = 0; i < PAGINAS.length; i++) {
      var p = PAGINAS[i];
      html += '<a href="' + p.url + '"' +
              (p.url === aca ? ' class="aca"' : "") + ">" + esc(t(p.t)) + "</a>";
    }
    cinta.innerHTML = html;
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
      .then(function () { pintarNav(); });
  }

  window.aos = {
    init: init, t: t, esc: esc, aplicar: aplicar, brindis: brindis,
    $: function (id) { return document.getElementById(id); },
    idioma: function () { return codigo; }
  };
})();
