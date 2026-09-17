/* Runs the board's step detector over /api/imu dumps on the desktop.
 *   cc -O2 -I components/aos_board/include tools/steps/bench.c components/aos_board/aos_step_detect.c -o /tmp/stepbench
 *   /tmp/stepbench tools/steps/walk_pocket_100.csv */
#include "aos_step_detect.h"
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "r");
        if (!f) { perror(argv[a]); continue; }
        char line[128];
        aos_step_detect_t d;
        aos_step_detect_init(&d);
        int count = 0, n = 0;
        unsigned long t, fw0 = 0, fw = 0; int ax, ay, az;
        fgets(line, sizeof line, f);                       /* header */
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "%lu,%d,%d,%d,%lu", &t, &ax, &ay, &az, &fw) != 5) continue;
            if (n++ == 0) fw0 = fw;
            float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az) / 1000.0f;
            count += aos_step_detect_feed(&d, (uint32_t)t, mag);
        }
        fclose(f);
        printf("%s: %d samples, firmware counted %lu, detector %d\n", argv[a], n, fw - fw0, count);
    }
    return 0;
}
