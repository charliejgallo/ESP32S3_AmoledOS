/*
 * PIXEL ART - .pix to GIF or PNG on the desktop, with the app's own encoders.
 *
 *     cc -O1 -Iapps/pixel/main apps/pixel/tools/px_convert.c \
 *        apps/pixel/main/px_file.c apps/pixel/main/px_export.c -o /tmp/px_convert
 *     /tmp/px_convert lienzo1.pix kitten.gif 16          # every frame, x16
 *     /tmp/px_convert lienzo1.pix frame2.png 16 2        # frame 2 only
 *
 * The same bytes the watch would write: it exists to put a canvas from the
 * card into a README, a chat or a release without the watch in between.
 */
#include "px_file.h"
#include "px_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: px_convert in.pix out.gif|out.png [scale] [frame]\n");
        return 2;
    }
    int scale = argc > 3 ? atoi(argv[3]) : 8;
    int frame = argc > 4 ? atoi(argv[4]) - 1 : 0;
    px_doc_t *d = malloc(sizeof(*d));
    if (!px_doc_load(d, argv[1])) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const char *dot = strrchr(argv[2], '.');
    bool ok = dot && strcmp(dot, ".png") == 0 ? px_export_png(d, frame, scale, argv[2])
                                             : px_export_gif(d, scale, argv[2]);
    printf("%s: %dx%d, %d frame(s), %d ms -> %s (x%d)\n", argv[1], d->size, d->size,
           d->frames, d->delay_ms, argv[2], scale);
    return ok ? 0 : 1;
}
