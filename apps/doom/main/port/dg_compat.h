/*
 * AmoledOS - Doom: what the engine sees instead of the C library.
 *
 * doomtype.h includes this, so every engine file gets it before its own
 * code. Four things are rerouted, each for a reason that bites on a watch
 * and not on a PC:
 *
 *   - malloc and company: on the board the engine's heap is PSRAM (small
 *     blocks would otherwise land in internal RAM, CONFIG_SPIRAM_MALLOC_
 *     ALWAYSINTERNAL), and every block is on a list so that leaving the game
 *     gives ALL of it back. Chocolate Doom never frees what it allocates at
 *     startup: a PC process ends, a .so on the watch is unloaded and the
 *     heap stays.
 *   - fopen/fclose: the open files are kept for the same reason.
 *   - exit(): there is no process to end. It longjmps back to the worker
 *     that started the engine (port/dg_system.c), which cleans up and tells
 *     the app.
 *   - printf and friends: stdout goes to the serial console, which nobody
 *     reads; aos_hal_log reaches /api/log.
 *
 * The system headers go first, so their prototypes are declared with the
 * real names before the macros exist.
 */
#ifndef DG_COMPAT_H
#define DG_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>

void *dg_malloc(size_t n);
void *dg_calloc(size_t n, size_t size);
void *dg_realloc(void *p, size_t n);
void  dg_free(void *p);
char *dg_strdup(const char *s);

FILE *dg_fopen(const char *path, const char *mode);
int   dg_fclose(FILE *f);

void  dg_exit(int code) __attribute__((noreturn));

int   dg_printf(const char *fmt, ...);
int   dg_vprintf(const char *fmt, va_list ap);
int   dg_fprintf(FILE *f, const char *fmt, ...);
int   dg_vfprintf(FILE *f, const char *fmt, va_list ap);
int   dg_puts(const char *s);
int   dg_putchar(int c);
int   dg_fflush(FILE *f);
int   dg_fgetc(FILE *f);

int   dg_strncasecmp(const char *a, const char *b, size_t n);
int   dg_strcasecmp(const char *a, const char *b);
int   dg_toupper(int c);
int   dg_tolower(int c);
int   dg_isspace(int c);
int   dg_isdigit(int c);
int   dg_isalnum(int c);
int   dg_isprint(int c);
double dg_atof(const char *s);

#define malloc      dg_malloc
#define calloc      dg_calloc
#define realloc     dg_realloc
#define free        dg_free
#undef  strdup
#define strdup      dg_strdup
#define fopen       dg_fopen
#define fclose      dg_fclose
#define exit        dg_exit
#undef  printf
#define printf      dg_printf
#define vprintf     dg_vprintf
#define fprintf     dg_fprintf
#define vfprintf    dg_vfprintf
#undef  puts
#define puts        dg_puts
#undef  putchar
#define putchar     dg_putchar
#define fflush      dg_fflush
#undef  fgetc
#define fgetc       dg_fgetc

/* Not in the firmware's table, or macros over newlib internals that are not
 * either: cheaper to carry our own than to reflash every watch. */
#define strncasecmp dg_strncasecmp
#define strcasecmp  dg_strcasecmp
#undef  toupper
#define toupper     dg_toupper
#undef  tolower
#define tolower     dg_tolower
#undef  isspace
#define isspace     dg_isspace
#undef  isdigit
#define isdigit     dg_isdigit
#undef  isalnum
#define isalnum     dg_isalnum
#undef  isprint
#define isprint     dg_isprint
#define atof        dg_atof

/* ---- the rest of the port, called from patched engine files ---- */

/* <card>/doom/ with the trailing slash: config and saves (m_config.c). */
const char *dg_data_dir(void);

/* The zone: as big as the free PSRAM allows, bounded (i_system.c). */
unsigned char *dg_zone_alloc(int *size);

/* I_Error's way out: keeps the message for the app and leaves. */
void dg_fatal(const char *msg) __attribute__((noreturn));

/* Once per tic, before the events are read: the analogue stick (i_input.c). */
void dg_input_tic(void);

/* The worker was asked to stop: leave now, from wherever the engine is. */
void dg_poll_stop(void);

/* m_menu.c: 0 = playing, 1 = a menu is open, 2 = a y/n question is up. */
int dg_menu_state(void);

#endif
