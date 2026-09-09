/* AmoledOS - sanitising of foreign text. See aos_text_safe.h. */
#include "aos_text_safe.h"

#include <string.h>

/* The middle dot, which is the replacement. It is in the font (docs/I18N.md,
 * F1: the generated range includes 0x2022) and it is discreet enough that a
 * run of emoji does not end up shouting. */
#define REEMPLAZO       0x2022

bool aos_text_font_has(uint32_t cp)
{
    if (cp == '\n') {
        return true;
    }
    if (cp >= 0x20 && cp <= 0x7E) {
        return true;                    /* printable ASCII */
    }
    if (cp >= 0xA0 && cp <= 0xFF) {
        return true;                    /* Latin-1: accents, n-tildes, inverted ? and ! , degree */
    }
    if (cp == 0x2022 || cp == 0x20AC) {
        return true;                    /* middle dot and euro */
    }
    /* The 61 FontAwesome glyphs behind LV_SYMBOL_* are at 0xF000+, but we do
     * not put them here: they are OUR icons, not something that can arrive in
     * a phone's text. If a real one ever turns up, let it fall through to the
     * replacement. */
    return false;
}

/* --------------------------------------------------------------------------
 * Emoji -> FontAwesome pictogram
 *
 * The watch cannot draw emoji: there are thousands of them and they are in
 * colour. But the phone sends them all the time, and until this table existed
 * every one of them came out as a dot.
 *
 * FontAwesome -which this project already ships, and of which 61 glyphs out of
 * its 1418 were being used until now- turns out to carry a set of faces that
 * map ONE TO ONE onto the most used ones: grin-tears IS the face crying with
 * laughter, sad-cry IS the one crying its eyes out, kiss IS the kissing one.
 * Plus the heart, the fire, the thumb, the star. Which means the emoji are
 * drawn with glyphs that were already paid for, in the same filled stroke as
 * the rest of the watch's symbols, and without bringing a new font into the
 * tree.
 *
 * Those with no counterpart -the hundred, the party sparkles- still fall
 * through to the replacement dot, which is what it is there for.
 *
 * Ordered by code point: looked up by bisection. The FontAwesome code points
 * appearing here have to be in tools/gen_fonts.py's SYMBOL_RANGE, or the emoji
 * is swapped for a glyph that does not exist and we are back to the hole,
 * which is worse than the dot.
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t    cp;
    const char *fa;     /* the pictogram, already in UTF-8 */
} emoji_t;

static const emoji_t EMOJI[] = {
    { 0x023F0, "\xEF\x80\x97" },  /* clock */
    { 0x02600, "\xEF\x86\x85" },  /* sun */
    { 0x02601, "\xEF\x83\x82" },  /* cloud */
    { 0x02614, "\xEF\x83\xA9" },  /* rain */
    { 0x02615, "\xEF\x83\xB4" },  /* coffee */
    { 0x0263A, "\xEF\x84\x98" },  /* smiley */
    { 0x02665, "\xEF\x80\x84" },  /* heart */
    { 0x026A1, "\xEF\x83\xA7" },  /* lightning */
    { 0x026BD, "\xEF\x87\xA3" },  /* ball */
    { 0x02705, "\xEF\x80\x8C" },  /* check */
    { 0x02708, "\xEF\x81\xB2" },  /* plane */
    { 0x0270C, "\xEF\x89\x9B" },  /* victory */
    { 0x02714, "\xEF\x80\x8C" },  /* check */
    { 0x02728, "\xEF\x80\x85" },  /* sparkles */
    { 0x02744, "\xEF\x8B\x9C" },  /* snow */
    { 0x0274C, "\xEF\x80\x8D" },  /* cross */
    { 0x0274E, "\xEF\x80\x8D" },  /* cross */
    { 0x02764, "\xEF\x80\x84" },  /* heart */
    { 0x02B50, "\xEF\x80\x85" },  /* star */
    { 0x1F319, "\xEF\x86\x86" },  /* moon */
    { 0x1F31F, "\xEF\x80\x85" },  /* glowing star */
    { 0x1F331, "\xEF\x93\x98" },  /* seedling */
    { 0x1F333, "\xEF\x86\xBB" },  /* tree */
    { 0x1F354, "\xEF\x8B\xA7" },  /* food */
    { 0x1F355, "\xEF\x8B\xA7" },  /* food */
    { 0x1F37A, "\xEF\x83\xBC" },  /* beer */
    { 0x1F37B, "\xEF\x83\xBC" },  /* beers */
    { 0x1F381, "\xEF\x81\xAB" },  /* gift */
    { 0x1F382, "\xEF\x87\xBD" },  /* cake */
    { 0x1F389, "\xEF\x9E\x9F" },  /* celebration */
    { 0x1F38A, "\xEF\x9E\x9F" },  /* celebration */
    { 0x1F3B5, "\xEF\x80\x81" },  /* note */
    { 0x1F3B6, "\xEF\x80\x81" },  /* notes */
    { 0x1F3C3, "\xEF\x9C\x8C" },  /* running */
    { 0x1F3C6, "\xEF\x82\x91" },  /* trophy */
    { 0x1F3E0, "\xEF\x80\x95" },  /* house */
    { 0x1F431, "\xEF\x9A\xBE" },  /* cat */
    { 0x1F436, "\xEF\x9B\x93" },  /* dog */
    { 0x1F440, "\xEF\x81\xAE" },  /* eyes */
    { 0x1F446, "\xEF\x82\xA6" },  /* finger up */
    { 0x1F449, "\xEF\x82\xA4" },  /* finger right */
    { 0x1F44C, "\xEF\x85\xA4" },  /* ok */
    { 0x1F44D, "\xEF\x85\xA4" },  /* thumbs up */
    { 0x1F44E, "\xEF\x85\xA5" },  /* thumbs down */
    { 0x1F44F, "\xEF\x93\x82" },  /* applause */
    { 0x1F47B, "\xEF\x9B\xA2" },  /* ghost */
    { 0x1F480, "\xEF\x95\x8C" },  /* skull */
    { 0x1F48E, "\xEF\x8E\xA5" },  /* gem */
    { 0x1F494, "\xEF\x9E\xA9" },  /* broken heart */
    { 0x1F495, "\xEF\x80\x84" },  /* two hearts */
    { 0x1F496, "\xEF\x80\x84" },  /* sparkling heart */
    { 0x1F497, "\xEF\x80\x84" },  /* growing heart */
    { 0x1F499, "\xEF\x80\x84" },  /* blue heart */
    { 0x1F49A, "\xEF\x80\x84" },  /* green heart */
    { 0x1F49B, "\xEF\x80\x84" },  /* yellow heart */
    { 0x1F49C, "\xEF\x80\x84" },  /* purple heart */
    { 0x1F49E, "\xEF\x80\x84" },  /* hearts */
    { 0x1F4A3, "\xEF\x87\xA2" },  /* bomb */
    { 0x1F4A9, "\xEF\x8B\xBE" },  /* poo */
    { 0x1F4AA, "\xEF\x91\x8B" },  /* biceps */
    { 0x1F4CD, "\xEF\x8F\x85" },  /* location */
    { 0x1F4DE, "\xEF\x82\x95" },  /* phone */
    { 0x1F4E7, "\xEF\x83\xA0" },  /* mail */
    { 0x1F4F7, "\xEF\x80\xB0" },  /* camera */
    { 0x1F525, "\xEF\x81\xAD" },  /* fire */
    { 0x1F600, "\xEF\x96\x80" },  /* smiling */
    { 0x1F601, "\xEF\x96\x82" },  /* with teeth */
    { 0x1F602, "\xEF\x96\x88" },  /* tears of laughter */
    { 0x1F603, "\xEF\x96\x80" },  /* smiling */
    { 0x1F604, "\xEF\x96\x82" },  /* big grin */
    { 0x1F605, "\xEF\x96\x83" },  /* with sweat */
    { 0x1F606, "\xEF\x96\x9B" },  /* laughing hard */
    { 0x1F609, "\xEF\x96\x8C" },  /* wink */
    { 0x1F60A, "\xEF\x96\xB8" },  /* happy eyes */
    { 0x1F60B, "\xEF\x96\x89" },  /* tongue out */
    { 0x1F60D, "\xEF\x96\x84" },  /* heart eyes */
    { 0x1F60E, "\xEF\x96\x80" },  /* with sunglasses */
    { 0x1F60F, "\xEF\x93\x9A" },  /* smirk */
    { 0x1F610, "\xEF\x84\x9A" },  /* neutral */
    { 0x1F611, "\xEF\x96\xA4" },  /* expressionless */
    { 0x1F614, "\xEF\x84\x99" },  /* unamused */
    { 0x1F615, "\xEF\x95\xBA" },  /* confused */
    { 0x1F617, "\xEF\x96\x96" },  /* kiss */
    { 0x1F618, "\xEF\x96\x96" },  /* kiss */
    { 0x1F61B, "\xEF\x96\x89" },  /* tongue out */
    { 0x1F61C, "\xEF\x96\x8B" },  /* tongue and wink */
    { 0x1F61E, "\xEF\x95\xBA" },  /* disappointed */
    { 0x1F620, "\xEF\x95\x96" },  /* angry */
    { 0x1F621, "\xEF\x95\x96" },  /* angry */
    { 0x1F622, "\xEF\x96\xB4" },  /* crying */
    { 0x1F629, "\xEF\x97\x88" },  /* weary */
    { 0x1F62B, "\xEF\x97\x88" },  /* weary */
    { 0x1F62C, "\xEF\x95\xBF" },  /* grimace */
    { 0x1F62D, "\xEF\x96\xB3" },  /* sobbing */
    { 0x1F62E, "\xEF\x97\x82" },  /* astonished */
    { 0x1F631, "\xEF\x97\x82" },  /* screaming */
    { 0x1F633, "\xEF\x95\xB9" },  /* flushed */
    { 0x1F634, "\xEF\x88\xB6" },  /* sleeping */
    { 0x1F635, "\xEF\x95\xA7" },  /* dizzy */
    { 0x1F641, "\xEF\x84\x99" },  /* sad */
    { 0x1F642, "\xEF\x84\x98" },  /* slight smile */
    { 0x1F644, "\xEF\x96\xA5" },  /* rolling eyes */
    { 0x1F64F, "\xEF\x9A\x84" },  /* hands together */
    { 0x1F680, "\xEF\x84\xB5" },  /* rocket */
    { 0x1F697, "\xEF\x86\xB9" },  /* car */
    { 0x1F6D2, "\xEF\x81\xBA" },  /* trolley */
    { 0x1F917, "\xEF\x96\xB8" },  /* hug */
    { 0x1F923, "\xEF\x96\x86" },  /* rolling on the floor */
    { 0x1F926, "\xEF\x97\x88" },  /* facepalm */
    { 0x1F929, "\xEF\x96\x87" },  /* star eyes */
    { 0x1F937, "\xEF\x84\x9A" },  /* shrug */
    { 0x1F942, "\xEF\x9E\x9F" },  /* toast */
    { 0x1F970, "\xEF\x96\x84" },  /* in love */
    { 0x1F973, "\xEF\x96\x87" },  /* partying */
    { 0x1F97A, "\xEF\x96\xB4" },  /* pleading eyes */
    { 0x1F9E1, "\xEF\x80\x84" },  /* orange heart */
};

#define EMOJI_N  ((int)(sizeof(EMOJI) / sizeof(EMOJI[0])))

static const char *emoji(uint32_t cp)
{
    int lo = 0, hi = EMOJI_N - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (EMOJI[m].cp == cp) {
            return EMOJI[m].fa;
        }
        if (EMOJI[m].cp < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return NULL;
}

/* What can be translated instead of thrown away. Nearly all of this comes from
 * iOS putting typographic quotes and dashes in BY ITSELF, without anybody
 * asking: a message written on the iPhone's keyboard almost certainly carries
 * one. */
static const char *traducir(uint32_t cp)
{
    /* The emoji's invisible companions. They go BEFORE anything else because
     * the emoji now turns into a real glyph, and if the variation selector
     * that follows it fell through to the replacement dot, a heart would end
     * up as "heart dot". While everything ended in a dot this went unnoticed:
     * the two consecutive dots merged into one. */
    if (cp == 0xFE0E || cp == 0xFE0F ||         /* variation selector        */
        (cp >= 0x1F3FB && cp <= 0x1F3FF) ||     /* skin tone                 */
        cp == 0x20E3) {                         /* keycap frame              */
        return "";
    }

    const char *pic = emoji(cp);
    if (pic) {
        return pic;
    }

    switch (cp) {
    case 0x2018: case 0x2019: case 0x201B: return "'";      /* ' ' ‛ */
    case 0x201C: case 0x201D: case 0x201F: return "\"";     /* " " ‟ */
    case 0x2010: case 0x2011: case 0x2012:
    case 0x2013: case 0x2014: case 0x2015: return "-";      /* – — */
    case 0x2212:                           return "-";      /* minus */
    case 0x2026:                           return "...";    /* … */
    case 0x2007: case 0x202F: case 0x2000:
    case 0x2001: case 0x2002: case 0x2003:
    case 0x2009: case 0x200A:              return " ";      /* odd spaces */
    case 0x200B: case 0x200C: case 0x200D:
    case 0xFEFF:                           return "";       /* invisibles */
    case 0x2039:                           return "<";
    case 0x203A:                           return ">";
    case 0x2032:                           return "'";
    case 0x2033:                           return "\"";
    case 0x2044:                           return "/";
    case 0x2122:                           return "(TM)";
    case 0x2192:                           return "->";
    case 0x2190:                           return "<-";
    case 0x2713: case 0x2714:              return "OK";
    default:                               return NULL;
    }
}

/* Decodes one UTF-8 character. Returns how many bytes it consumed, always at
 * least 1: faced with anything odd it advances ONE byte and returns 0xFFFD,
 * which is not in the font and therefore ends up as the replacement dot.
 *
 * That it is 0xFFFD and not 0 is deliberate. With 0 the broken byte fell into
 * the control-character filter and disappeared without a trace, which is
 * exactly the defect this file exists to avoid: text with something missing
 * and nothing to show for it. A character cut in half -and ANCS cuts them,
 * because it clips by bytes- has to be visible. */
#define INVALIDO    0xFFFDu
static int decodificar(const unsigned char *s, size_t quedan, uint32_t *out)
{
    unsigned char c = s[0];

    if (c < 0x80) {
        *out = c;
        return 1;
    }

    int n;
    uint32_t cp;
    if      ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
    else                         { *out = INVALIDO; return 1; } /* stray continuation */

    if ((size_t)n > quedan) {
        *out = INVALIDO;
        return 1;                       /* cut off at the end: half a character */
    }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *out = INVALIDO;
            return 1;                   /* broken sequence */
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }

    /* Overlong forms and surrogates: we reject them just as a serious decoder
     * does, because they are the classic way of slipping in a disguised
     * character. */
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF) {
        *out = INVALIDO;
        return 1;
    }

    *out = cp;
    return n;
}

/* Writes a code point in UTF-8. Returns the bytes, or 0 if it does not fit. */
static size_t poner_cp(char *out, size_t espacio, uint32_t cp)
{
    if (cp < 0x80) {
        if (espacio < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (espacio < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (espacio < 3) return 0;
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

size_t aos_text_safe(char *out, size_t out_len, const char *in)
{
    if (!out || out_len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!in) {
        return 0;
    }

    const unsigned char *s = (const unsigned char *)in;
    size_t largo = strlen(in);
    size_t i = 0, w = 0;
    bool ultimo_fue_reemplazo = false;

    while (i < largo && w + 1 < out_len) {
        uint32_t cp;
        int n = decodificar(s + i, largo - i, &cp);
        i += (size_t)n;

        /* Control characters: out, except the line feed. The carriage return
         * is swallowed because the \r\n pair would leave two line breaks. */
        if (cp < 0x20 && cp != '\n') {
            continue;
        }
        if (cp == 0x7F) {
            continue;
        }

        const char *sust = NULL;
        if (!aos_text_font_has(cp)) {
            sust = traducir(cp);
            if (!sust) {
                /* No translation: the replacement goes in, and a run of them
                 * collapses into a single one. */
                if (ultimo_fue_reemplazo) {
                    continue;
                }
                size_t k = poner_cp(out + w, out_len - 1 - w, REEMPLAZO);
                if (k == 0) {
                    break;              /* it does not fit whole: better to cut here */
                }
                w += k;
                ultimo_fue_reemplazo = true;
                continue;
            }
        }

        ultimo_fue_reemplazo = false;

        if (sust) {
            size_t largo_sust = strlen(sust);
            if (largo_sust == 0) {
                continue;               /* invisible: simply dropped */
            }
            if (w + largo_sust + 1 > out_len) {
                break;
            }
            memcpy(out + w, sust, largo_sust);
            w += largo_sust;
        } else {
            size_t k = poner_cp(out + w, out_len - 1 - w, cp);
            if (k == 0) {
                break;                  /* it does not fit whole: we do not split it */
            }
            w += k;
        }
    }

    out[w] = '\0';
    return w;
}
