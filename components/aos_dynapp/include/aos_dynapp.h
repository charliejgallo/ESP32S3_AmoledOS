/*
 * AmoledOS - Dynamic loading of applications.
 *
 * An external app is a shared object (.so) compiled separately with ESP-IDF
 * and copied to /sdcard/apps. At startup the firmware opens them with
 * dlopen(), looks for the "aos_app_entry" symbol and registers the app in the
 * menu.
 *
 * The symbols the app leaves unresolved (LVGL, libc, ESP-IDF and AmoledOS's
 * API) are filled in by the firmware at load time.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Folder where the .so files live. It has to sit below
 * CONFIG_ELF_FILE_SYSTEM_BASE_PATH. */
#define AOS_DYNAPP_DIR          "/sdcard/apps"
#define AOS_DYNAPP_RELATIVE     "apps"

/* Walks the folder, loads each .so and registers the apps. Returns how many it
 * loaded successfully. */
int aos_dynapp_scan(void);

/* Loads one specific .so (file name, without a path). */
bool aos_dynapp_load(const char *filename);

/* Closes the modules of apps that have just been closed. Called from the main
 * loop: the dlclose cannot be done in destroy(), because the runtime deletes
 * the LVGL objects afterwards and those objects may have callbacks living
 * inside the .so. */
void aos_dynapp_tick(void);

/* Unloads an already registered dynamic app and takes it off the menu. */
bool aos_dynapp_unload(const char *app_id);


/* .so modules loaded right now. Returns how many; 'out' receives the names
 * separated by spaces. Those remaining are the background ones. */
int aos_dynapp_loaded_list(char *out, size_t len);


/* State of the contiguous code reservation: free bytes, largest hole and how
 * many modules occupy it (-1 if it could not be reserved). */
void aos_dynapp_pool_info(uint32_t *libre, uint32_t *mayor, int *usados);

#ifdef __cplusplus
}
#endif
