# The portal's security

The web portal can do anything the watch can: read and write every file on
the card, see and touch the screen, change the Wi-Fi, install firmware. This
is what guards it, and what does not yet.

## Other sites in your browser

Since 2026-10-14 (from P4OS, `components/aos_web/aos_web_access.c`), two
attacks that need nothing but your browser on the same network as the watch
are closed:

- **Requests from another page.** A page from anywhere can make your browser
  send a "simple" request (a form, or a `fetch` with a `text/plain` body) to
  `http://charlie.local/api/...` with no preflight, and the watch used to act
  on it: delete a file, change the Wi-Fi, install firmware. The API now
  answers only what the browser marks as coming from the portal's own pages
  (`Sec-Fetch-Site` same-origin or none, and an `Origin` that is the watch
  itself, port included). curl and the scripts in `tools/` send neither
  header and are not affected. A refused request gets 403 and a line in the
  log (`refused /api/...`).
- **DNS rebinding.** A name of the attacker's that first points to their
  server and then to the watch lets their page read the watch's answers. The
  portal answers only to an IP or to the watch's own name (bare, `.local`,
  `.lan`, `.home`, `.home.arpa`, `.localdomain`; the name it had at boot is
  accepted too until the next restart, so renaming it in `/ajustes` does not
  lock that tab out); anything else gets 421.

Every answer also carries `X-Content-Type-Options: nosniff` and
`X-Frame-Options: DENY`.

It is one function in front of every route (`gate()` in `aos_web.c`, and
the 404 handler that serves the apps' pages), so a route added later cannot
skip it.

## Where a request comes from

Since 2026-10-14 (`components/aos_ui/aos_access.c`, the check in
`aos_web_access.c`), what a request needs depends on where it came in, told
by the watch's own address the connection arrived at:

| Way in | Needs |
|---|---|
| The USB cable (`192.168.7.1`) | nothing: whoever holds the cable holds the watch |
| The watch's own access point (`192.168.4.1`) | nothing: it has its own WPA2 password |
| A trusted Wi-Fi network | nothing |
| Any other Wi-Fi network | a session the watch approved, or the token |

The pages themselves (`/radio`, `/aos.js`, the apps' pages) are served to
anyone: they hold no data, and the login is drawn by one of them. What is
guarded is `/api/`, all of it but `/api/auth`, `/api/pedir`, `/api/salir`
and reading `/api/lang`.

- **Trusted networks** are chosen on the watch, Settings > Wi-Fi, card
  *Web portal*: a switch for the network it is on and one for each trusted
  network (off takes it out). Only there: someone let into the portal
  cannot open it further, and a backup neither carries nor restores them
  (`web_*` preferences are skipped both ways). The first time this firmware
  runs, the network the watch connects to becomes trusted, so the update
  does not close the portal on its owner. Eight at most; a ninth pushes out
  the oldest.
- **Asking the watch.** On another network the portal's pages show *Ask the
  watch for permission*. `POST /api/pedir` makes a request; the watch wakes,
  beeps and shows *Allow this browser into the portal?* with a four-digit
  code, the browser's address and the network. The browser shows the same
  code: you allow the browser in front of you, not whichever asked last.
  *Allow* gives that browser a session; *Decline*, back or a swipe refuse,
  and each refusal in a row doubles the wait before the next request (10 s,
  20 s... up to five minutes). A request nobody answers is gone after a
  minute; one browser at a time.
- **Sessions** are a cookie, `aos_s`, `HttpOnly` (the page's scripts cannot
  read it) and `SameSite=Strict` (no other site sends it), 16 random bytes.
  They live in RAM: a restart closes them all, and there are eight at most
  (a ninth replaces the oldest). *Log out* on the ribbon ends this browser's;
  *Close portal sessions* on the watch ends them all.
- **The token** is for scripts: `Authorization: Bearer <token>`. The
  portal's Settings, *Security*, shows it and makes a new one (the old stops
  working). Every script in `tools/` that talks to the watch sends it when
  it is in `AOS_TOKEN`:

      AOS_TOKEN=<token> tools/install_fw.sh 192.168.1.107

The API for it:

    GET  /api/auth      the zone, whether it needs a session, whether it has one,
                        the network, the trusted ones, the open sessions
    POST /api/pedir     ask the watch: {"id","codigo"}; 409 while another browser
                        waits; 429 {"espera": s} after refusals
    GET  /api/pedir?id= {"estado": esperando | aprobado | rechazado | vencido};
                        aprobado comes with the cookie
    POST /api/salir     ends this browser's session
    GET  /api/token     the token; POST makes a new one

**Not done:** the watch keeps announcing `<name>.local` on mDNS on any
network (P4OS goes quiet on untrusted ones); there the portal is reachable
by name, closed.

## What is not covered

- **Plain HTTP.** Whatever crosses the network is readable on it. HTTPS is
  not planned for the watch: mbedTLS as a server costs ~40 KB of internal
  RAM per session.
- **The firmware is not signed, on purpose**: anyone can build and load
  their own. Whoever may use the portal (a trusted network, an approved
  browser, the token) can install theirs.
- **A trusted network trusts everyone on it**, a flatmate's laptop too. Mark
  only networks whose devices you would hand the watch to.
- **The flash is not encrypted**: the Wi-Fi passwords and the Home Assistant
  token can be read from a watch in hand. The card is readable by whoever
  holds it.
