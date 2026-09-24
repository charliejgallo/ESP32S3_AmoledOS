/*
 * VISOR 3D - meshes: loading STL (binary and ASCII) and M3D, welding and
 * reducing to a triangle budget. Nothing here touches LVGL: it runs in the
 * app's worker.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The most triangles drawn: a watch at 240 MHz redraws this many a few dozen
 * times a second at half resolution. Bigger files are reduced on loading. */
#define V3_BUDGET       24000

typedef struct {
    int       nv, nt;
    float    *v;            /* nv * 3, centred on the origin, radius 1       */
    uint32_t *idx;          /* nt * 3                                        */
    float    *fn;           /* nt * 3, unit face normals                     */
    uint16_t *fc;           /* nt face colours (RGB565), NULL = one colour   */
    int       nt_file;      /* triangles in the file, before any reduction   */
    char      err[64];      /* why it did not load                           */
} v3_mesh_t;

/* Loads .stl (binary or ASCII) or .m3d. 'progress', if given, is
 * stage * 1000 + percent: stage 1 is the first read of the triangles, 2 and
 * up the passes that reduce a model over the budget (each one reads the
 * file again). The UI polls it from the other task. */
bool v3_mesh_load(v3_mesh_t *m, const char *path, volatile int *progress);
void v3_mesh_free(v3_mesh_t *m);

/* Called every 1024 triangles while loading: the worker gives its core away
 * for a moment (or the task watchdog fires on IDLE0: a big STL is seconds
 * of work), and says whether to go on. false cancels the load at once, with
 * "cancelled" as the error: an app leaving mid-load must not wait for it,
 * because its code is unloaded as soon as the worker is given up on. */
void v3_mesh_set_yield(bool (*fn)(void));

/* M3D, the portal's format (/3d converts STL, OBJ and GLB into it):
 *
 *   "M3D1"  uint32 nv  uint32 nt  uint32 flags
 *   nv * 3 float32               vertices (any scale: centred on loading)
 *   nt * 3 uint32                triangles
 *   if flags & 1: nt * uint16    face colours, RGB565
 *
 * Little-endian, like both ends. Y is up, as in glTF. */
#define V3_M3D_COLORS   1u
