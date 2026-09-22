/* A global table of structs with a pointer in them, defined here and read
 * from the other file: the case that goes through R_XTENSA_GLOB_DAT with an
 * addend when the compiler folds a field's offset into the GOT entry. */
typedef struct { const char *name; int a; int b; } gt_row_t;
const gt_row_t gt_table[4] = {
    { "uno", 11, 12 }, { "dos", 21, 22 }, { "tres", 31, 32 }, { "cuatro", 41, 42 },
};
