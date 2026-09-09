/*
 * AmoledOS - bench for the sanitising of foreign text.
 *
 * It links against components/aos_ui/aos_text_safe.c, which has not one line
 * of LVGL or of the HAL. Same division of labour as qr_harness.c.
 *
 *   cc -Wall -Wextra -I components/aos_ui/include tools/tx_harness.c \
 *      components/aos_ui/aos_text_safe.c -o /tmp/tx && /tmp/tx
 *
 * Why it exists: when a glyph is missing, LVGL draws NOTHING. There is no
 * error, no little box, no log. The defect looks like a message that got cut
 * off, and the only place it shows is by looking at the screen with the real
 * text beside it. A bench comparing strings catches it without looking at
 * anything.
 */
#include <stdio.h>
#include <string.h>

#include "aos_text_safe.h"

static int pruebas, fallos;

static void esperar(const char *que, const char *entra, const char *sale)
{
    char buf[512];
    aos_text_safe(buf, sizeof(buf), entra);
    pruebas++;
    int bien = strcmp(buf, sale) == 0;
    if (!bien) fallos++;
    printf("  %s  %-44s [%s]\n", bien ? "OK " : "BAD", que, buf);
    if (!bien) {
        printf("       esperaba [%s]\n", sale);
    }
}

int main(void)
{
    printf("\n== text sanitising ==\n");

    printf("\nwhat can already be drawn passes through intact\n");
    esperar("ASCII", "Hola, mundo!", "Hola, mundo!");
    esperar("acentos y enes", "\xC3\xA1\xC3\xA9\xC3\xB1 \xC2\xBFque tal?",
                              "\xC3\xA1\xC3\xA9\xC3\xB1 \xC2\xBFque tal?");
    esperar("grados y euro", "21\xC2\xB0 y 5\xE2\x82\xAC", "21\xC2\xB0 y 5\xE2\x82\xAC");
    esperar("line break", "one\ntwo", "one\ntwo");

    printf("\npunctuation from the iPhone keyboard\n");
    esperar("comillas curvas", "dijo \xE2\x80\x9Chola\xE2\x80\x9D", "dijo \"hola\"");
    esperar("apostrofo curvo", "n\xE2\x80\x99importe", "n'importe");
    esperar("em dash", "Factura \xE2\x80\x94 vencida", "Factura - vencida");
    esperar("puntos suspensivos", "espera\xE2\x80\xA6", "espera...");
    esperar("flecha", "A \xE2\x86\x92 B", "A -> B");

    printf("\nemoji that CAN be drawn\n");
    /* FontAwesome's pictograms are in the private use area, so in the terminal
     * they show as a gap: what is compared are the bytes. */
    esperar("the pizza comes out as the cutlery",
            "pizza \xF0\x9F\x8D\x95 now", "pizza \xEF\x8B\xA7 now");
    esperar("the thumbs up", "\xF0\x9F\x91\x8D", "\xEF\x85\xA4");
    esperar("the fire", "\xF0\x9F\x94\xA5", "\xEF\x81\xAD");
    esperar("two thumbs are two thumbs, not one",
            "\xF0\x9F\x91\x8D\xF0\x9F\x91\x8D", "\xEF\x85\xA4\xEF\x85\xA4");

    /* The heart nearly always comes with the variation selector behind it.
     * While EVERYTHING ended in a dot this went unnoticed -the two consecutive
     * dots merged into one-, but now the heart is a real glyph and the
     * selector would land beside it as "heart dot". */
    esperar("the heart with a variation selector does not drag a dot along",
            "\xE2\x9D\xA4\xEF\xB8\x8F", "\xEF\x80\x84");
    esperar("nor does the thumb with a skin tone",
            "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", "\xEF\x85\xA4");

    printf("\nwhat still cannot be drawn\n");
    esperar("an emoji with no analogue becomes a dot",
            "\xF0\x9F\x92\xAF", "\xE2\x80\xA2");
    esperar("cirilico", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
            "\xE2\x80\xA2");
    esperar("and a run of them becomes ONE",
            "\xD0\x9F\xD1\x80\xD0\xB8\xF0\x9F\x92\xAF", "\xE2\x80\xA2");

    printf("\ncontroles e invisibles\n");
    esperar("tabulador y campana fuera", "a\tb\x07""c", "abc");
    esperar("the carriage return does not duplicate the break", "one\r\ntwo", "one\ntwo");
    esperar("zero width dropped", "a\xE2\x80\x8B" "b", "ab");

    printf("\nbroken UTF-8 (ANCS clips by bytes, not by characters)\n");
    esperar("character cut at the end", "hola \xC3", "hola \xE2\x80\xA2");
    esperar("emoji cut at the end", "pizza \xF0\x9F\x8D", "pizza \xE2\x80\xA2");
    esperar("stray continuation byte", "a\xA9" "b", "a\xE2\x80\xA2" "b");
    esperar("sequence broken in the middle", "a\xC3zb", "a\xE2\x80\xA2zb");
    esperar("sobrelarga", "a\xC0\xAF" "b", "a\xE2\x80\xA2" "b");

    printf("\ntruncado seguro\n");
    {
        char chico[8];
        size_t n = aos_text_safe(chico, sizeof(chico), "abcdefghijklm");
        pruebas++;
        int bien = n == 7 && strcmp(chico, "abcdefg") == 0;
        if (!bien) fallos++;
        printf("  %s  %-44s [%s]\n", bien ? "OK " : "BAD",
               "it cuts at out_len-1", chico);
    }
    {
        /* Four n-tildes: two bytes each. In seven usable bytes three whole
         * ones fit and the fourth is NOT split in half. */
        char chico[8];
        aos_text_safe(chico, sizeof(chico), "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1");
        pruebas++;
        int bien = strcmp(chico, "\xC3\xB1\xC3\xB1\xC3\xB1") == 0;
        if (!bien) fallos++;
        printf("  %s  %-44s [%s] (%d bytes)\n", bien ? "OK " : "BAD",
               "it does not split a character when truncating", chico, (int)strlen(chico));
    }
    {
        char chico[1];
        size_t n = aos_text_safe(chico, sizeof(chico), "hola");
        pruebas++;
        int bien = n == 0 && chico[0] == '\0';
        if (!bien) fallos++;
        printf("  %s  %-44s\n", bien ? "OK " : "BAD", "an out_len of 1 leaves an empty string");
    }

    printf("\nbordes\n");
    esperar("empty string", "", "");
    {
        char buf[16];
        size_t n = aos_text_safe(buf, sizeof(buf), NULL);
        pruebas++;
        int bien = n == 0 && buf[0] == '\0';
        if (!bien) fallos++;
        printf("  %s  %-44s\n", bien ? "OK " : "BAD", "NULL does not blow up");
    }

    printf("\n%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
