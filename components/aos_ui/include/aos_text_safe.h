/*
 * AmoledOS - sanitising foreign text before drawing it.
 *
 * The watch's fonts cover 0x20-0x7F, 0xA0-0xFF, the middle dot and the euro,
 * plus 61 FontAwesome symbols: 254 glyphs and nothing more (see docs/I18N.md,
 * F1). And when a glyph is missing **LVGL draws nothing**: no little box, no
 * error, no log line. The text comes out with holes and the symptom reads as
 * "the message got cut off".
 *
 * Everything we write ourselves is under control. A notification's text is
 * not: it comes from the phone, and an ordinary WhatsApp carries emoji,
 * typographic quotes and em dashes. That is why it goes through here before it
 * touches a label.
 *
 * No LVGL and no HAL, on purpose: it is tested on the Mac with
 * tools/tx_harness.c.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copies 'in' to 'out' leaving only what the font knows how to draw.
 *
 *   - typographic punctuation is translated to its typewriter equivalent:
 *     curly quotes to " and ', em dashes to -, the ellipsis to three dots;
 *   - any other character the font lacks -emoji, non-Latin alphabets- is
 *     replaced, and a run of them is replaced by a SINGLE one: a message of
 *     nothing but emoji has to end up as one dot, not twenty;
 *   - control characters go, except the line feed;
 *   - malformed UTF-8 breaks nothing. That matters more than it seems: ANCS
 *     clips attributes by BYTE COUNT, so asking for 256 bytes of message can
 *     split a character in half, and the last one to arrive is half a
 *     character.
 *
 * Returns the bytes written, not counting the trailing zero. It never writes
 * more than out_len-1, and never cuts a character in half when truncating. */
size_t aos_text_safe(char *out, size_t out_len, const char *in);

/* true if the font can draw this code point. Public because it is also useful
 * for deciding whether it is worth trying. */
bool aos_text_font_has(uint32_t cp);

#ifdef __cplusplus
}
#endif
