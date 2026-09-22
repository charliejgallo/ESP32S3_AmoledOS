/*
 * GOLF - the second and third courses. Included by gf_holes.c, after its
 * macros (S, N, T3, HOLE): not a header of its own.
 *
 *   DUNAS DEL FARO       links by the sea: the sea along one side of most
 *                        holes, beaches, dunes of marram grass, pot bunkers,
 *                        palms and scrub, and 1.6 times the wind
 *   PARQUE DE LOS LAGOS  a park where water is the hazard of every hole:
 *                        ponds, a creek, an island green; calm air
 *
 * BLOB7(cx, cy, rx, ry) is seven points round an ellipse: greens, bunkers,
 * ponds. BLOBW adds a lopsided wobble so two ponds do not look stamped.
 */

#define BLOB7(cx, cy, rx, ry) \
    { (cx) + (rx), (cy) }, { (cx) + (rx) * 63 / 100, (cy) + (ry) * 78 / 100 }, \
    { (cx) - (rx) * 22 / 100, (cy) + (ry) * 97 / 100 }, { (cx) - (rx) * 90 / 100, (cy) + (ry) * 44 / 100 }, \
    { (cx) - (rx) * 90 / 100, (cy) - (ry) * 44 / 100 }, { (cx) - (rx) * 22 / 100, (cy) - (ry) * 97 / 100 }, \
    { (cx) + (rx) * 63 / 100, (cy) - (ry) * 78 / 100 }
#define BLOBW(cx, cy, rx, ry) \
    { (cx) + (rx) * 110 / 100, (cy) + (ry) * 10 / 100 }, { (cx) + (rx) * 55 / 100, (cy) + (ry) * 85 / 100 }, \
    { (cx) - (rx) * 30 / 100, (cy) + (ry) * 90 / 100 }, { (cx) - (rx) * 100 / 100, (cy) + (ry) * 30 / 100 }, \
    { (cx) - (rx) * 80 / 100, (cy) - (ry) * 55 / 100 }, { (cx) - (rx) * 10 / 100, (cy) - (ry) * 105 / 100 }, \
    { (cx) + (rx) * 70 / 100, (cy) - (ry) * 70 / 100 }
#define TEEBOX(cx) { {(cx) - 5, -7}, {(cx) + 5, -7}, {(cx) + 6, 34}, {(cx) - 4, 34} }

#define HOLE_T(th, nm, par, t, p, bx0, by0, bx1, by1, rise, rough, seed, tx, ty, bump, sh, tr, md) \
    { nm, par, t, p, bx0, by0, bx1, by1, rise, rough, seed, tx, ty, bump, th, \
      sh, N(sh), tr, N(tr), md, N(md) }

/* ==========================================================================
 * DUNAS DEL FARO
 * ========================================================================== */

/* C1 EL FARO - par 4. The sea all down the right, a beach before it, dunes
 * on the left; a slice finds the water. */
static const gf_pt_t c1_tee[] = TEEBOX(0);
static const gf_pt_t c1_sea[] = { {60,-50}, {150,-50}, {150,400}, {56,400}, {50,300}, {58,200}, {52,100} };
static const gf_pt_t c1_bch[] = { {40,-30}, {60,-30}, {52,100}, {58,200}, {50,300}, {56,380}, {36,380}, {38,300}, {44,200}, {38,100} };
static const gf_pt_t c1_dun[] = { {-90,-30}, {-30,-30}, {-26,120}, {-34,260}, {-24,380}, {-90,380} };
static const gf_pt_t c1_fw[]  = { {-16,50}, {-19,150}, {-13,250}, {0,296}, {26,296}, {30,250}, {24,150}, {16,50} };
static const gf_pt_t c1_gr[]  = { BLOB7(18, 318, 15, 12) };
static const gf_pt_t c1_b1[]  = { BLOB7(-6, 205, 4, 3) };
static const gf_pt_t c1_b2[]  = { BLOB7(36, 300, 5, 4) };
static const gf_pt_t c1_b3[]  = { BLOB7(0, 322, 4, 4) };
static const gf_shape_t c1_sh[] = {
    S(SH_DEEP, 0, 0, c1_dun), S(SH_WASTE, 0, 0, c1_bch), S(SH_TEE, 0, 0, c1_tee),
    S(SH_FAIRWAY, 0, 0, c1_fw), S(SH_WATER, 0, 0, c1_sea), S(SH_GREEN, 0, 0, c1_gr),
    S(SH_BUNKER, 11, 0, c1_b1), S(SH_BUNKER, 0, 0, c1_b2), S(SH_BUNKER, 11, 0, c1_b3),
};
static const gf_tree_def_t c1_tr[] = {
    { -30, 40, GF_TREE_BUSH, 130 }, { -34, 160, GF_TREE_BUSH, 110 }, { -40, 290, GF_TREE_PALM, 110 },
    { -28, 330, GF_TREE_BUSH, 120 }, { 34, 20, GF_TREE_PALM, 100 },
};
static const gf_mound_t c1_md[] = { { -45, 60, 11, 320 }, { -50, 140, 13, 380 }, { -42, 230, 11, 300 }, { -48, 320, 12, 340 }, { -30, 100, 6, 150 } };

/* C2 LA ROMPIENTE - par 3. A green out on the rocks: sea behind and right,
 * the beach short of it. */
static const gf_pt_t c2_tee[] = TEEBOX(0);
static const gf_pt_t c2_sea[] = { {-90,175}, {-40,168}, {0,176}, {40,150}, {50,100}, {60,40}, {72,-20}, {80,-50}, {150,-50}, {150,260}, {-90,260} };
static const gf_pt_t c2_bch[] = { {-60,70}, {-10,80}, {30,70}, {46,100}, {40,138}, {12,124}, {-20,130}, {-50,120} };
static const gf_pt_t c2_dun[] = { {-90,-30}, {-30,-30}, {-40,60}, {-70,160}, {-90,160} };
static const gf_pt_t c2_gr[]  = { BLOB7(4, 150, 14, 11) };
static const gf_pt_t c2_b1[]  = { BLOB7(-16, 148, 5, 4) };
static const gf_pt_t c2_b2[]  = { BLOB7(20, 136, 4, 4) };
static const gf_pt_t c2_fw[]  = { BLOB7(-4, 60, 12, 16) };
static const gf_shape_t c2_sh[] = {
    S(SH_DEEP, 0, 0, c2_dun), S(SH_TEE, 0, 0, c2_tee), S(SH_FAIRWAY, 0, 0, c2_fw),
    S(SH_WASTE, 0, 0, c2_bch), S(SH_WATER, 0, 0, c2_sea), S(SH_GREEN, 0, 0, c2_gr),
    S(SH_BUNKER, 11, 0, c2_b1), S(SH_BUNKER, 11, 0, c2_b2),
};
static const gf_tree_def_t c2_tr[] = { { -28, 20, GF_TREE_PALM, 110 }, { 24, 30, GF_TREE_BUSH, 120 }, { -40, 140, GF_TREE_BUSH, 110 } };
static const gf_mound_t c2_md[] = { { -50, 30, 12, 300 }, { -55, 110, 12, 280 }, { 30, 20, 8, 150 } };

/* C3 MEDANOS - par 4. A fairway that winds between dunes of marram grass,
 * pot bunkers where the drive comes down. */
static const gf_pt_t c3_tee[] = TEEBOX(0);
static const gf_pt_t c3_dl[]  = { {-90,-30}, {-22,-30}, {-30,90}, {-10,180}, {-30,280}, {-12,400}, {-90,400} };
static const gf_pt_t c3_dr[]  = { {28,-30}, {90,-30}, {90,400}, {40,400}, {60,290}, {36,180}, {48,90} };
static const gf_pt_t c3_fw[]  = { {-14,50}, {-20,120}, {-6,190}, {-14,260}, {0,330}, {26,330}, {14,260}, {26,190}, {18,120}, {14,50} };
static const gf_pt_t c3_gr[]  = { BLOB7(14, 352, 13, 12) };
static const gf_pt_t c3_b1[]  = { BLOB7(6, 214, 4, 3) };
static const gf_pt_t c3_b2[]  = { BLOB7(-14, 232, 4, 4) };
static const gf_pt_t c3_b3[]  = { BLOB7(30, 344, 4, 5) };
static const gf_pt_t c3_b4[]  = { BLOB7(-2, 360, 4, 4) };
static const gf_shape_t c3_sh[] = {
    S(SH_DEEP, 0, 0, c3_dl), S(SH_DEEP, 0, 0, c3_dr), S(SH_TEE, 0, 0, c3_tee),
    S(SH_FAIRWAY, 0, 0, c3_fw), S(SH_GREEN, 0, 0, c3_gr),
    S(SH_BUNKER, 12, 0, c3_b1), S(SH_BUNKER, 12, 0, c3_b2), S(SH_BUNKER, 11, 0, c3_b3), S(SH_BUNKER, 11, 0, c3_b4),
};
static const gf_tree_def_t c3_tr[] = {
    { -40, 70, GF_TREE_BUSH, 130 }, { 50, 140, GF_TREE_BUSH, 120 }, { -44, 250, GF_TREE_BUSH, 140 },
    { 56, 320, GF_TREE_PALM, 100 },
};
static const gf_mound_t c3_md[] = {
    { -40, 60, 12, 350 }, { 50, 90, 14, 420 }, { -34, 170, 10, 300 }, { 56, 200, 12, 380 },
    { -40, 290, 13, 360 }, { 58, 300, 12, 340 }, { 12, 390, 10, 250 },
};

/* C4 LA COSTANERA - par 5. The sea along the whole left, the hole bending
 * with the shore; the brave line cuts the beach. */
static const gf_pt_t c4_tee[] = TEEBOX(0);
static const gf_pt_t c4_sea[] = { {-160,-50}, {-40,-50}, {-34,120}, {-50,260}, {-80,380}, {-100,500}, {-160,500} };
static const gf_pt_t c4_bch[] = { {-44,-30}, {-26,-30}, {-20,120}, {-34,260}, {-62,380}, {-80,490}, {-98,490}, {-84,380}, {-52,260}, {-38,120} };
static const gf_pt_t c4_dun[] = { {32,-30}, {110,-30}, {110,500}, {10,500}, {26,380}, {44,250}, {40,120} };
static const gf_pt_t c4_fw[]  = { {-12,50}, {-10,160}, {-20,260}, {-44,370}, {-60,440}, {-34,450}, {-14,370}, {6,260}, {18,160}, {16,50} };
static const gf_pt_t c4_gr[]  = { BLOB7(-54, 468, 14, 12) };
static const gf_pt_t c4_b1[]  = { BLOB7(22, 232, 5, 4) };
static const gf_pt_t c4_b2[]  = { BLOB7(-4, 330, 4, 4) };
static const gf_pt_t c4_b3[]  = { BLOB7(-34, 470, 5, 5) };
static const gf_shape_t c4_sh[] = {
    S(SH_DEEP, 0, 0, c4_dun), S(SH_WASTE, 0, 0, c4_bch), S(SH_TEE, 0, 0, c4_tee),
    S(SH_FAIRWAY, 0, 0, c4_fw), S(SH_WATER, 0, 0, c4_sea), S(SH_GREEN, 0, 0, c4_gr),
    S(SH_BUNKER, 11, 0, c4_b1), S(SH_BUNKER, 12, 0, c4_b2), S(SH_BUNKER, 0, 0, c4_b3),
};
static const gf_tree_def_t c4_tr[] = {
    { 36, 60, GF_TREE_PALM, 110 }, { 44, 200, GF_TREE_BUSH, 130 }, { 30, 340, GF_TREE_PALM, 100 },
    { 12, 460, GF_TREE_BUSH, 120 },
};
static const gf_mound_t c4_md[] = { { 50, 80, 13, 380 }, { 60, 190, 14, 420 }, { 44, 320, 12, 340 }, { 30, 440, 11, 300 } };

/* C5 EL ACANTILADO - par 4, short. The sea cuts into the fairway at 180 m:
 * lay up short of the cove, or carry it for a wedge in. */
static const gf_pt_t c5_tee[] = TEEBOX(0);
static const gf_pt_t c5_sea[] = { {48,120}, {66,70}, {82,-10}, {90,-50}, {150,-50}, {150,300}, {70,280}, {30,222}, {-20,210}, {-40,196}, {-20,180}, {20,176} };
static const gf_pt_t c5_bch[] = { {-50,176}, {10,164}, {40,110}, {54,112}, {32,178}, {-8,188}, {-46,190} };
static const gf_pt_t c5_dun[] = { {-90,-30}, {-34,-30}, {-40,150}, {-70,300}, {-90,300} };
static const gf_pt_t c5_fwa[] = { {-14,50}, {-16,120}, {-8,158}, {20,158}, {22,120}, {16,50} };
static const gf_pt_t c5_fwb[] = { {-24,232}, {-26,262}, {-10,272}, {20,264}, {26,240}, {0,232} };
static const gf_pt_t c5_gr[]  = { BLOB7(-2, 290, 14, 11) };
static const gf_pt_t c5_b1[]  = { BLOB7(-20, 280, 4, 4) };
static const gf_pt_t c5_b2[]  = { BLOB7(16, 298, 4, 4) };
static const gf_shape_t c5_sh[] = {
    S(SH_DEEP, 0, 0, c5_dun), S(SH_TEE, 0, 0, c5_tee), S(SH_FAIRWAY, 0, 0, c5_fwa),
    S(SH_FAIRWAY, 0, 0, c5_fwb), S(SH_WASTE, 0, 0, c5_bch), S(SH_WATER, 0, 0, c5_sea),
    S(SH_GREEN, 0, 0, c5_gr), S(SH_BUNKER, 11, 0, c5_b1), S(SH_BUNKER, 11, 0, c5_b2),
};
static const gf_tree_def_t c5_tr[] = { { -30, 90, GF_TREE_BUSH, 120 }, { 30, 40, GF_TREE_PALM, 110 }, { -36, 260, GF_TREE_PALM, 100 } };
static const gf_mound_t c5_md[] = { { -50, 60, 12, 330 }, { -55, 200, 12, 300 }, { 30, 70, 9, 200 } };

/* C6 VIENTO EN CONTRA - par 3, long. Straight into the wind, a green in a
 * ring of pot bunkers among the dunes. */
static const gf_pt_t c6_tee[] = TEEBOX(0);
static const gf_pt_t c6_dun[] = { {-90,-30}, {-20,-30}, {-30,80}, {-24,150}, {-40,230}, {-90,230} };
static const gf_pt_t c6_dun2[] = { {26,-30}, {90,-30}, {90,230}, {34,230}, {32,150}, {40,80} };
static const gf_pt_t c6_fw[]  = { {-12,110}, {-16,150}, {-6,158}, {14,156}, {18,118} };
static const gf_pt_t c6_gr[]  = { BLOB7(2, 178, 13, 11) };
static const gf_pt_t c6_b1[]  = { BLOB7(-14, 166, 4, 4) };
static const gf_pt_t c6_b2[]  = { BLOB7(18, 168, 4, 4) };
static const gf_pt_t c6_b3[]  = { BLOB7(2, 160, 4, 3) };
static const gf_pt_t c6_b4[]  = { BLOB7(-6, 194, 4, 4) };
static const gf_shape_t c6_sh[] = {
    S(SH_DEEP, 0, 0, c6_dun), S(SH_DEEP, 0, 0, c6_dun2), S(SH_TEE, 0, 0, c6_tee),
    S(SH_FAIRWAY, 0, 0, c6_fw), S(SH_GREEN, 0, 0, c6_gr),
    S(SH_BUNKER, 12, 0, c6_b1), S(SH_BUNKER, 12, 0, c6_b2), S(SH_BUNKER, 12, 0, c6_b3), S(SH_BUNKER, 11, 0, c6_b4),
};
static const gf_tree_def_t c6_tr[] = { { -36, 60, GF_TREE_BUSH, 130 }, { 44, 120, GF_TREE_BUSH, 120 }, { -44, 190, GF_TREE_PALM, 110 } };
static const gf_mound_t c6_md[] = { { -44, 40, 12, 380 }, { 50, 60, 13, 400 }, { -46, 140, 12, 360 }, { 52, 170, 12, 340 } };

/* C7 LA BAHIA - par 5. The bay bites into the right: the second shot
 * decides between going round it and going for the green over the water. */
static const gf_pt_t c7_tee[] = TEEBOX(0);
static const gf_pt_t c7_sea[] = { {40,200}, {66,150}, {86,60}, {96,-50}, {150,-50}, {150,540}, {50,540}, {40,470}, {30,420}, {10,380}, {6,330}, {20,260} };
static const gf_pt_t c7_bch[] = { {26,196}, {44,180}, {52,196}, {28,262}, {16,332}, {22,380}, {44,420}, {30,432}, {6,392}, {-4,332}, {10,258} };
static const gf_pt_t c7_dun[] = { {-90,-30}, {-34,-30}, {-40,200}, {-52,400}, {-40,540}, {-90,540} };
static const gf_pt_t c7_fw[]  = { {-14,50}, {-18,160}, {-20,260}, {-24,360}, {-14,430}, {6,440}, {-2,360}, {0,260}, {12,160}, {16,50} };
static const gf_pt_t c7_gr[]  = { BLOB7(18, 470, 14, 12) };
static const gf_pt_t c7_b1[]  = { BLOB7(-28, 300, 5, 4) };
static const gf_pt_t c7_b2[]  = { BLOB7(0, 462, 4, 4) };
static const gf_pt_t c7_b3[]  = { BLOB7(18, 490, 5, 3) };
static const gf_shape_t c7_sh[] = {
    S(SH_DEEP, 0, 0, c7_dun), S(SH_WASTE, 0, 0, c7_bch), S(SH_TEE, 0, 0, c7_tee),
    S(SH_FAIRWAY, 0, 0, c7_fw), S(SH_WATER, 0, 0, c7_sea), S(SH_GREEN, 0, 0, c7_gr),
    S(SH_BUNKER, 11, 0, c7_b1), S(SH_BUNKER, 11, 0, c7_b2), S(SH_BUNKER, 0, 0, c7_b3),
};
static const gf_tree_def_t c7_tr[] = {
    { -40, 80, GF_TREE_PALM, 110 }, { -46, 240, GF_TREE_BUSH, 130 }, { -50, 380, GF_TREE_PALM, 100 },
    { 34, 60, GF_TREE_BUSH, 120 },
};
static const gf_mound_t c7_md[] = { { -52, 100, 13, 360 }, { -56, 260, 14, 420 }, { -50, 420, 12, 340 } };

/* C8 EL MUELLE - par 4. Home along the shore, the green by the water. */
static const gf_pt_t c8_tee[] = TEEBOX(0);
static const gf_pt_t c8_sea[] = { {54,-50}, {150,-50}, {150,470}, {60,470}, {44,410}, {46,300}, {54,160}, {50,60} };
static const gf_pt_t c8_bch[] = { {36,-30}, {54,-30}, {50,60}, {54,160}, {46,300}, {44,410}, {50,450}, {32,450}, {28,410}, {32,300}, {40,160}, {34,60} };
static const gf_pt_t c8_dun[] = { {-90,-30}, {-30,-30}, {-34,150}, {-26,300}, {-36,450}, {-90,450} };
static const gf_pt_t c8_fw[]  = { {-16,50}, {-20,160}, {-16,280}, {-6,360}, {18,364}, {22,280}, {24,160}, {18,50} };
static const gf_pt_t c8_gr[]  = { BLOB7(12, 392, 15, 13) };
static const gf_pt_t c8_b1[]  = { BLOB7(-22, 250, 5, 4) };
static const gf_pt_t c8_b2[]  = { BLOB7(-6, 384, 4, 5) };
static const gf_pt_t c8_b3[]  = { BLOB7(8, 262, 4, 3) };
static const gf_shape_t c8_sh[] = {
    S(SH_DEEP, 0, 0, c8_dun), S(SH_WASTE, 0, 0, c8_bch), S(SH_TEE, 0, 0, c8_tee),
    S(SH_FAIRWAY, 0, 0, c8_fw), S(SH_WATER, 0, 0, c8_sea), S(SH_GREEN, 0, 0, c8_gr),
    S(SH_BUNKER, 11, 0, c8_b1), S(SH_BUNKER, 11, 0, c8_b2), S(SH_BUNKER, 12, 0, c8_b3),
};
static const gf_tree_def_t c8_tr[] = {
    { -36, 100, GF_TREE_PALM, 110 }, { -40, 220, GF_TREE_BUSH, 130 }, { -34, 350, GF_TREE_PALM, 110 },
    { -20, 420, GF_TREE_BUSH, 120 },
};
static const gf_mound_t c8_md[] = { { -48, 80, 12, 340 }, { -50, 200, 13, 380 }, { -46, 330, 12, 320 }, { -40, 420, 10, 250 } };

#define CT THEME_COAST
static const gf_hole_t c_holes[] = {
    HOLE_T(CT, "El Faro", 4, T3(0, 0, 0, 14, 1, 28), T3(18, 318, 24, 322, 12, 314),
           -70, -20, 70, 350, 150, 3, 101, 12, -8, 10, c1_sh, c1_tr, c1_md),
    HOLE_T(CT, "La Rompiente", 3, T3(0, 0, 0, 12, 1, 24), T3(4, 150, 10, 153, -2, 147),
           -60, -20, 60, 190, 100, 2, 103, -15, -12, 0, c2_sh, c2_tr, c2_md),
    HOLE_T(CT, "Médanos", 4, T3(0, 0, 0, 14, 1, 28), T3(14, 352, 8, 356, 20, 348),
           -70, -20, 70, 385, 200, 4, 107, 8, 12, 15, c3_sh, c3_tr, c3_md),
    HOLE_T(CT, "La Costanera", 5, T3(0, 0, 0, 14, 1, 28), T3(-54, 468, -60, 472, -48, 464),
           -120, -20, 70, 495, 100, 3, 109, -12, 6, 12, c4_sh, c4_tr, c4_md),
    HOLE_T(CT, "El Acantilado", 4, T3(0, 0, 0, 14, 1, 28), T3(-2, 290, 4, 292, -8, 288),
           -70, -20, 70, 315, 250, 3, 113, 10, -10, 8, c5_sh, c5_tr, c5_md),
    HOLE_T(CT, "Viento en Contra", 3, T3(0, 0, 0, 12, 1, 24), T3(2, 178, -4, 181, 8, 175),
           -60, -20, 60, 215, 150, 3, 127, 6, 14, 0, c6_sh, c6_tr, c6_md),
    HOLE_T(CT, "La Bahía", 5, T3(0, 0, 0, 14, 1, 28), T3(18, 470, 24, 474, 12, 466),
           -70, -20, 70, 515, 150, 3, 131, -10, -8, 12, c7_sh, c7_tr, c7_md),
    HOLE_T(CT, "El Muelle", 4, T3(0, 0, 0, 14, 1, 28), T3(12, 392, 18, 396, 6, 388),
           -70, -20, 70, 430, 200, 3, 137, 14, 8, 20, c8_sh, c8_tr, c8_md),
};

/* ==========================================================================
 * PARQUE DE LOS LAGOS
 * ========================================================================== */

/* P1 LA FUENTE - par 4. A pond right of where the drive lands, another
 * guarding the green's front left. */
static const gf_pt_t p1_tee[] = TEEBOX(0);
static const gf_pt_t p1_fw[]  = { {-16,50}, {-18,160}, {-12,270}, {-2,300}, {20,298}, {22,200}, {18,100}, {16,50} };
static const gf_pt_t p1_w1[]  = { BLOBW(40, 200, 18, 36) };
static const gf_pt_t p1_w2[]  = { BLOBW(-18, 306, 12, 9) };
static const gf_pt_t p1_gr[]  = { BLOB7(10, 322, 15, 12) };
static const gf_pt_t p1_b1[]  = { BLOB7(28, 330, 5, 4) };
static const gf_pt_t p1_fl[]  = { {-90,-30}, {-34,-30}, {-38,120}, {-34,260}, {-40,380}, {-90,380} };
static const gf_pt_t p1_fr[]  = { {66,-30}, {100,-30}, {100,380}, {44,380}, {48,300}, {70,150} };
static const gf_pt_t p1_path[] = { {9,-12}, {26,80}, {22,160}, {28,260}, {40,330} };
static const gf_shape_t p1_sh[] = {
    S(SH_FOREST, 4, GF_TREE_OAK, p1_fl), S(SH_FOREST, 4, GF_TREE_POPLAR, p1_fr), S(SH_TEE, 0, 0, p1_tee),
    S(SH_FAIRWAY, 0, 0, p1_fw), S(SH_WATER, 0, 0, p1_w1), S(SH_WATER, 0, 0, p1_w2),
    S(SH_GREEN, 0, 0, p1_gr), S(SH_BUNKER, 0, 0, p1_b1), S(SH_PATH, 0, 0, p1_path),
};
static const gf_tree_def_t p1_tr[] = {
    { -26, 60, GF_TREE_OAK, 110 }, { 30, 90, GF_TREE_POPLAR, 110 }, { 62, 250, GF_TREE_OAK, 100 },
    { -30, 200, GF_TREE_OAK, 120 }, { 36, 350, GF_TREE_POPLAR, 100 },
};
static const gf_mound_t p1_md[] = { { -26, 130, 12, 120 }, { 20, 350, 10, 100 } };

/* P2 LOS PATOS - par 3. Water on three sides of the green. */
static const gf_pt_t p2_tee[] = TEEBOX(0);
static const gf_pt_t p2_w[]   = { {-40,100}, {0,92}, {40,100}, {48,140}, {40,175}, {20,178}, {24,150}, {14,128}, {-6,124}, {-18,140}, {-14,168}, {-34,172}, {-46,136} };
static const gf_pt_t p2_gr[]  = { BLOB7(4, 148, 13, 11) };
static const gf_pt_t p2_b1[]  = { BLOB7(4, 170, 6, 3) };
static const gf_pt_t p2_fw[]  = { BLOB7(0, 58, 12, 14) };
static const gf_pt_t p2_fl[]  = { {-90,-30}, {-28,-30}, {-34,80}, {-60,120}, {-50,210}, {-90,210} };
static const gf_pt_t p2_fr[]  = { {30,-30}, {90,-30}, {90,210}, {60,210}, {62,120}, {40,70} };
static const gf_shape_t p2_sh[] = {
    S(SH_FOREST, 5, GF_TREE_OAK, p2_fl), S(SH_FOREST, 5, GF_TREE_MIXED, p2_fr), S(SH_TEE, 0, 0, p2_tee),
    S(SH_FAIRWAY, 0, 0, p2_fw), S(SH_WATER, 0, 0, p2_w), S(SH_GREEN, 0, 0, p2_gr), S(SH_BUNKER, 0, 0, p2_b1),
};
static const gf_tree_def_t p2_tr[] = { { -20, 180, GF_TREE_POPLAR, 110 }, { 30, 190, GF_TREE_OAK, 100 }, { -30, 40, GF_TREE_OAK, 100 } };
static const gf_mound_t p2_md[] = { { 0, 196, 10, 150 } };

/* P3 EL ARROYO - par 5. A creek that wanders down the left and crosses the
 * fairway twice. */
static const gf_pt_t p3_tee[] = TEEBOX(0);
static const gf_pt_t p3_crk[] = { {-60,60}, {-40,120}, {-44,170}, {-10,200}, {40,212}, {70,230}, {70,238}, {38,222}, {-12,210}, {-50,176}, {-48,122}, {-68,62} };
static const gf_pt_t p3_crk2[] = { {-80,380}, {-20,372}, {30,380}, {80,372}, {80,380}, {30,388}, {-20,380}, {-80,388} };
static const gf_pt_t p3_fw[]  = { {-16,50}, {-18,150}, {-14,260}, {-6,360}, {-2,440}, {22,440}, {18,360}, {16,260}, {22,150}, {18,50} };
static const gf_pt_t p3_gr[]  = { BLOB7(10, 466, 14, 12) };
static const gf_pt_t p3_b1[]  = { BLOB7(-8, 470, 4, 5) };
static const gf_pt_t p3_b2[]  = { BLOB7(28, 460, 4, 4) };
static const gf_pt_t p3_fl[]  = { {-100,-30}, {-74,-30}, {-80,300}, {-100,300} };
static const gf_pt_t p3_fr[]  = { {40,-30}, {100,-30}, {100,500}, {50,500}, {46,300}, {52,120} };
static const gf_shape_t p3_sh[] = {
    S(SH_FOREST, 5, GF_TREE_OAK, p3_fl), S(SH_FOREST, 4, GF_TREE_MIXED, p3_fr), S(SH_TEE, 0, 0, p3_tee),
    S(SH_FAIRWAY, 0, 0, p3_fw), S(SH_WATER, 0, 0, p3_crk), S(SH_WATER, 0, 0, p3_crk2),
    S(SH_GREEN, 0, 0, p3_gr), S(SH_BUNKER, 0, 0, p3_b1), S(SH_BUNKER, 0, 0, p3_b2),
};
static const gf_tree_def_t p3_tr[] = {
    { -30, 100, GF_TREE_POPLAR, 110 }, { -56, 200, GF_TREE_OAK, 110 }, { 34, 320, GF_TREE_OAK, 100 },
    { -30, 330, GF_TREE_POPLAR, 110 }, { -26, 430, GF_TREE_OAK, 100 },
};
static const gf_mound_t p3_md[] = { { 30, 140, 10, 120 }, { -30, 420, 10, 130 } };

/* P4 LOS SAUCES - par 4. A long lake all down the left. */
static const gf_pt_t p4_tee[] = TEEBOX(0);
static const gf_pt_t p4_lake[] = { {-24,60}, {-20,140}, {-24,240}, {-18,320}, {-30,370}, {-70,380}, {-90,300}, {-86,160}, {-70,60} };
static const gf_pt_t p4_fw[]  = { {-10,50}, {-8,150}, {-6,260}, {-2,340}, {22,340}, {26,250}, {24,150}, {18,50} };
static const gf_pt_t p4_gr[]  = { BLOB7(8, 364, 14, 12) };
static const gf_pt_t p4_b1[]  = { BLOB7(30, 250, 5, 4) };
static const gf_pt_t p4_b2[]  = { BLOB7(26, 372, 5, 4) };
static const gf_pt_t p4_fr[]  = { {44,-30}, {100,-30}, {100,410}, {40,410}, {50,300}, {52,120} };
static const gf_shape_t p4_sh[] = {
    S(SH_FOREST, 4, GF_TREE_POPLAR, p4_fr), S(SH_TEE, 0, 0, p4_tee), S(SH_FAIRWAY, 0, 0, p4_fw),
    S(SH_WATER, 0, 0, p4_lake), S(SH_GREEN, 0, 0, p4_gr), S(SH_BUNKER, 0, 0, p4_b1), S(SH_BUNKER, 0, 0, p4_b2),
};
static const gf_tree_def_t p4_tr[] = {
    { -96, 100, GF_TREE_POPLAR, 110 }, { -98, 220, GF_TREE_OAK, 120 }, { -94, 340, GF_TREE_POPLAR, 100 },
    { -12, 390, GF_TREE_OAK, 110 }, { 34, 60, GF_TREE_OAK, 100 },
};
static const gf_mound_t p4_md[] = { { 34, 180, 10, 120 }, { 20, 395, 10, 140 } };

/* P5 LA ISLA - par 3, short. The green is an island: all carry. */
static const gf_pt_t p5_tee[] = TEEBOX(0);
static const gf_pt_t p5_w[]   = { BLOBW(2, 124, 40, 34) };
static const gf_pt_t p5_isl[] = { BLOB7(2, 126, 16, 13) };
static const gf_pt_t p5_gr[]  = { BLOB7(2, 126, 13, 10) };
static const gf_pt_t p5_b1[]  = { BLOB7(-12, 118, 3, 3) };
static const gf_pt_t p5_fl[]  = { {-90,-30}, {-30,-30}, {-54,70}, {-56,190}, {-90,190} };
static const gf_pt_t p5_fr[]  = { {30,-30}, {90,-30}, {90,190}, {56,190}, {58,70} };
static const gf_pt_t p5_path[] = { {9,-12}, {40,60}, {48,120}, {18,128} };
static const gf_shape_t p5_sh[] = {
    S(SH_FOREST, 4, GF_TREE_OAK, p5_fl), S(SH_FOREST, 4, GF_TREE_POPLAR, p5_fr), S(SH_TEE, 0, 0, p5_tee),
    S(SH_WATER, 1, 0, p5_w), S(SH_FAIRWAY, 0, 0, p5_isl), S(SH_GREEN, 0, 0, p5_gr), S(SH_BUNKER, 0, 0, p5_b1),
    S(SH_PATH, 0, 0, p5_path),
};
static const gf_tree_def_t p5_tr[] = { { -20, 170, GF_TREE_POPLAR, 110 }, { 24, 176, GF_TREE_OAK, 100 } };
static const gf_mound_t p5_md[] = { { 2, 180, 12, 140 } };

/* P6 EL PUENTE - par 4. The drive carries a lake to a fairway beyond. */
static const gf_pt_t p6_tee[] = TEEBOX(0);
static const gf_pt_t p6_lake[] = { {-60,50}, {-10,44}, {40,50}, {60,90}, {50,140}, {0,150}, {-50,140}, {-66,96} };
static const gf_pt_t p6_fw[]  = { {-18,160}, {-20,220}, {-12,270}, {0,280}, {22,276}, {26,220}, {20,160} };
static const gf_pt_t p6_gr[]  = { BLOB7(4, 300, 14, 12) };
static const gf_pt_t p6_b1[]  = { BLOB7(-14, 290, 4, 4) };
static const gf_pt_t p6_b2[]  = { BLOB7(22, 306, 4, 4) };
static const gf_pt_t p6_fl[]  = { {-100,-30}, {-36,-30}, {-76,40}, {-40,160}, {-44,340}, {-100,340} };
static const gf_pt_t p6_fr[]  = { {34,-30}, {100,-30}, {100,340}, {44,340}, {46,160}, {74,60} };
static const gf_pt_t p6_path[] = { {9,-12}, {20,40}, {12,100}, {14,160} };
static const gf_shape_t p6_sh[] = {
    S(SH_FOREST, 5, GF_TREE_MIXED, p6_fl), S(SH_FOREST, 5, GF_TREE_OAK, p6_fr), S(SH_TEE, 0, 0, p6_tee),
    S(SH_FAIRWAY, 0, 0, p6_fw), S(SH_WATER, 0, 0, p6_lake), S(SH_GREEN, 0, 0, p6_gr),
    S(SH_BUNKER, 0, 0, p6_b1), S(SH_BUNKER, 0, 0, p6_b2), S(SH_PATH, 0, 0, p6_path),
};
static const gf_tree_def_t p6_tr[] = { { -30, 230, GF_TREE_POPLAR, 110 }, { 36, 250, GF_TREE_OAK, 100 } };
static const gf_mound_t p6_md[] = { { 0, 330, 12, 150 } };

/* P7 LAS CASCADAS - par 4. Two ponds staggered along the way. */
static const gf_pt_t p7_tee[] = TEEBOX(0);
static const gf_pt_t p7_w1[]  = { BLOBW(-30, 180, 16, 26) };
static const gf_pt_t p7_w2[]  = { BLOBW(34, 290, 16, 24) };
static const gf_pt_t p7_fw[]  = { {-12,50}, {-10,150}, {-6,240}, {-12,330}, {-2,360}, {20,356}, {16,280}, {20,180}, {18,50} };
static const gf_pt_t p7_gr[]  = { BLOB7(6, 384, 14, 12) };
static const gf_pt_t p7_b1[]  = { BLOB7(-12, 378, 4, 5) };
static const gf_pt_t p7_fl[]  = { {-100,-30}, {-34,-30}, {-54,140}, {-52,260}, {-40,420}, {-100,420} };
static const gf_pt_t p7_fr[]  = { {36,-30}, {100,-30}, {100,420}, {40,420}, {56,340}, {44,160} };
static const gf_shape_t p7_sh[] = {
    S(SH_FOREST, 5, GF_TREE_MIXED, p7_fl), S(SH_FOREST, 5, GF_TREE_POPLAR, p7_fr), S(SH_TEE, 0, 0, p7_tee),
    S(SH_FAIRWAY, 0, 0, p7_fw), S(SH_WATER, 0, 0, p7_w1), S(SH_WATER, 0, 0, p7_w2),
    S(SH_GREEN, 0, 0, p7_gr), S(SH_BUNKER, 0, 0, p7_b1),
};
static const gf_tree_def_t p7_tr[] = { { 30, 120, GF_TREE_OAK, 100 }, { -28, 300, GF_TREE_POPLAR, 110 }, { 26, 400, GF_TREE_OAK, 110 } };
static const gf_mound_t p7_md[] = { { 24, 90, 10, 120 }, { -24, 400, 10, 140 } };

/* P8 EL LAGO GRANDE - par 5. A lake between the tee and the fairway, and
 * the green on its far shore. */
static const gf_pt_t p8_tee[] = TEEBOX(0);
static const gf_pt_t p8_lake[] = { {-90,50}, {-20,44}, {50,56}, {70,120}, {40,170}, {10,180}, {-20,230}, {-30,330}, {-10,420}, {-40,480}, {-80,470}, {-100,300}, {-110,120} };
static const gf_pt_t p8_fw[]  = { {6,190}, {-6,260}, {-8,350}, {-2,440}, {20,450}, {30,350}, {34,260}, {40,190} };
static const gf_pt_t p8_gr[]  = { BLOB7(8, 474, 14, 12) };
static const gf_pt_t p8_b1[]  = { BLOB7(28, 466, 5, 4) };
static const gf_pt_t p8_b2[]  = { BLOB7(40, 300, 5, 4) };
static const gf_pt_t p8_fr[]  = { {60,-30}, {110,-30}, {110,520}, {50,520}, {60,400}, {70,160} };
static const gf_pt_t p8_path[] = { {9,-12}, {60,40}, {80,140}, {56,200}, {50,320}, {40,460} };
static const gf_shape_t p8_sh[] = {
    S(SH_FOREST, 5, GF_TREE_MIXED, p8_fr), S(SH_TEE, 0, 0, p8_tee), S(SH_FAIRWAY, 0, 0, p8_fw),
    S(SH_WATER, 0, 0, p8_lake), S(SH_GREEN, 0, 0, p8_gr), S(SH_BUNKER, 0, 0, p8_b1), S(SH_BUNKER, 0, 0, p8_b2),
    S(SH_PATH, 0, 0, p8_path),
};
static const gf_tree_def_t p8_tr[] = {
    { -30, 20, GF_TREE_POPLAR, 110 }, { 30, 20, GF_TREE_OAK, 100 }, { -120, 200, GF_TREE_OAK, 110 },
    { -118, 380, GF_TREE_POPLAR, 110 }, { -40, 500, GF_TREE_OAK, 100 },
};
static const gf_mound_t p8_md[] = { { 40, 420, 10, 130 } };

#define LK THEME_LAKES
static const gf_hole_t p_holes[] = {
    HOLE_T(LK, "La Fuente", 4, T3(0, 0, 0, 14, 1, 28), T3(10, 322, 16, 326, 4, 318),
           -70, -20, 80, 360, 150, 3, 151, 10, -12, 8, p1_sh, p1_tr, p1_md),
    HOLE_T(LK, "Los Patos", 3, T3(0, 0, 0, 12, 1, 24), T3(4, 148, 10, 150, -2, 146),
           -60, -20, 60, 200, -100, 2, 157, -10, -15, 0, p2_sh, p2_tr, p2_md),
    HOLE_T(LK, "El Arroyo", 5, T3(0, 0, 0, 14, 1, 28), T3(10, 466, 16, 470, 4, 462),
           -85, -20, 80, 490, 300, 3, 163, 8, 10, 15, p3_sh, p3_tr, p3_md),
    HOLE_T(LK, "Los Sauces", 4, T3(0, 0, 0, 14, 1, 28), T3(8, 364, 14, 368, 2, 360),
           -100, -20, 70, 395, 200, 3, 167, -12, 8, 10, p4_sh, p4_tr, p4_md),
    HOLE_T(LK, "La Isla", 3, T3(0, 0, 0, 12, 1, 24), T3(2, 126, 7, 128, -4, 124),
           -60, -20, 60, 185, 0, 2, 173, 8, -8, 0, p5_sh, p5_tr, p5_md),
    HOLE_T(LK, "El Puente", 4, T3(0, 0, 0, 14, 1, 28), T3(4, 300, 10, 303, -2, 297),
           -80, -20, 80, 335, 150, 3, 179, -6, 12, 10, p6_sh, p6_tr, p6_md),
    HOLE_T(LK, "Las Cascadas", 4, T3(0, 0, 0, 14, 1, 28), T3(6, 384, 12, 388, 0, 380),
           -80, -20, 80, 415, 400, 3, 181, 12, -6, 14, p7_sh, p7_tr, p7_md),
    HOLE_T(LK, "El Lago Grande", 5, T3(0, 0, 0, 14, 1, 28), T3(8, 474, 14, 478, 2, 470),
           -125, -20, 90, 510, 150, 3, 191, -8, 10, 12, p8_sh, p8_tr, p8_md),
};
