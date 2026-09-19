/*
 * AmoledOS - The watch's name (see aos_hal.h). Shared by both HALs: the
 * board's HAL adds the mDNS re-announce through aos_hal_device_name_applied().
 */
#include "aos_hal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define NAME_KEY      "dev_name"
#define NAME_DEFAULT  "amoledos"

static char s_name[AOS_DEVICE_NAME_MAX + 1];
static bool s_loaded;

/* Implemented by each HAL: the board re-announces mDNS, the simulator prints. */
void aos_hal_device_name_applied(const char *name);

bool aos_hal_device_name_valid(const char *name)
{
    if (!name) {
        return false;
    }
    size_t n = strlen(name);
    if (n < 1 || n > AOS_DEVICE_NAME_MAX || name[0] == '-' || name[n - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(islower(c) || isdigit(c) || c == '-')) {
            return false;
        }
    }
    return true;
}

const char *aos_hal_device_name(void)
{
    if (!s_loaded) {
        char stored[AOS_DEVICE_NAME_MAX + 1];
        if (aos_hal_pref_get_str(NAME_KEY, stored, sizeof(stored)) &&
            aos_hal_device_name_valid(stored)) {
            snprintf(s_name, sizeof(s_name), "%s", stored);
        } else {
            snprintf(s_name, sizeof(s_name), "%s", NAME_DEFAULT);
        }
        s_loaded = true;
    }
    return s_name;
}

bool aos_hal_device_name_set(const char *name)
{
    if (!aos_hal_device_name_valid(name)) {
        return false;
    }
    aos_hal_device_name();              /* load first, so the compare below holds */
    if (strcmp(name, s_name) == 0) {
        return true;
    }
    snprintf(s_name, sizeof(s_name), "%s", name);
    aos_hal_pref_set_str(NAME_KEY, s_name);
    aos_hal_device_name_applied(s_name);
    return true;
}
