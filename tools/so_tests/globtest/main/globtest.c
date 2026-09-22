/* Regression test for the .so loader: reads fields of a global table that
 * lives in another file and logs whether they came out right. Not an app
 * anybody opens: the check runs in init(), at boot. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")     /* golf's physics had it: that is when GCC folds the offset */
#endif
#include "aos_app.h"
#include "aos_hal.h"

typedef struct { const char *name; int a; int b; } gt_row_t;
extern const gt_row_t gt_table[4];

static volatile int s_i = 2;

static bool gt_init(aos_app_t *app)
{
    /* a field read with a variable index: the literal is gt_table + 4 (the
     * offset of 'a') and the index is added in code, which is the case the
     * loader got wrong */
    int n = s_i + 2, sum = 0;
    for (int i = 0; i < n; i++) sum += gt_table[i].a;     /* 11+21+31+41 */
    bool ok = sum == 104;
    aos_hal_log("globtest", "GLOB_DAT check %s: sum of 'a' = %d (want 104)", ok ? "OK" : "FAILED", sum);
    app->desc.id    = "test.globtest";
    app->desc.name  = "globtest";
    app->desc.icon  = LV_SYMBOL_WARNING;
    app->desc.order = 999;
    return false;               /* do not register: it is only a check */
}

AOS_APP_ENTRY(gt_init);
