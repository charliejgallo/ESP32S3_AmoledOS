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

## What is not covered

- **Anyone on the same network can use the portal.** There is no password
  and no notion of a trusted network yet: on a hotel's or a friend's Wi-Fi,
  every device there reaches `<name>.local` and the whole API. P4OS has
  trusted networks, a password with sessions and a token for scripts
  (its docs/SECURITY.md); not ported yet.
- **Plain HTTP.** Whatever crosses the network is readable on it. HTTPS is
  not planned for the watch: mbedTLS as a server costs ~40 KB of internal
  RAM per session.
- **The firmware is not signed, on purpose**: anyone can build and load
  their own. Whoever reaches the portal can install theirs.
- **The flash is not encrypted**: the Wi-Fi passwords and the Home Assistant
  token can be read from a watch in hand. The card is readable by whoever
  holds it.
