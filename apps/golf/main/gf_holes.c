/*
 * GOLF - the first course: eight holes, par 32
 *
 * Every shape is a handful of control points in metres; the loader passes a
 * closed Catmull-Rom through them, so corners come out round and a green
 * with seven points is a smooth blob. x across, y along the hole, the back
 * tee at the origin. To add a hole: copy one, move the points, and look at
 * it with the harness (tools/gf_harness.c "map <hole>").
 *
 * A course is a table of holes; more courses are more tables (or, later,
 * files on the card with the same layout).
 */
#include "gf_world.h"

#define S(kind, param, tree, arr) { kind, (uint8_t)(sizeof(arr) / sizeof(arr[0])), param, tree, arr }
#define N(arr) (uint8_t)(sizeof(arr) / sizeof(arr[0]))

/* ==========================================================================
 * 1  LA BIENVENIDA - par 4, 360 yd. A gentle dogleg right: the bunker at
 *    the corner eats the drive that cuts it, the green sits between two.
 * ========================================================================== */
static const gf_pt_t h1_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h1_fw[]   = { {-14,50}, {-16,130}, {-9,210}, {7,264}, {30,298}, {48,284}, {30,244}, {21,200}, {17,130}, {14,50} };
static const gf_pt_t h1_gr[]   = { {40,318}, {43,331}, {56,337}, {68,331}, {70,318}, {62,307}, {48,307} };
static const gf_pt_t h1_b1[]   = { {33,231}, {35,244}, {44,247}, {47,237}, {41,229} };
static const gf_pt_t h1_b2[]   = { {29,302}, {30,314}, {37,316}, {39,304} };
static const gf_pt_t h1_b3[]   = { {71,322}, {74,337}, {80,335}, {79,322} };
static const gf_pt_t h1_fl[]   = { {-80,-20}, {-34,-12}, {-33,100}, {-31,200}, {-22,280}, {-10,340}, {-80,350} };
static const gf_pt_t h1_fr[]   = { {36,10}, {110,10}, {110,230}, {70,236}, {44,196}, {36,110} };
static const gf_pt_t h1_fb[]   = { {-10,352}, {120,350}, {120,390}, {-10,390} };
static const gf_pt_t h1_path[] = { {9,-12}, {22,60}, {30,150}, {40,205}, {58,262}, {86,300}, {92,340} };
static const gf_shape_t h1_sh[] = {
    S(SH_FOREST, 6, GF_TREE_MIXED, h1_fl),
    S(SH_FOREST, 5, GF_TREE_MIXED, h1_fr),
    S(SH_FOREST, 7, GF_TREE_PINE, h1_fb),
    S(SH_TEE, 0, 0, h1_tee),
    S(SH_FAIRWAY, 0, 0, h1_fw),
    S(SH_GREEN, 0, 0, h1_gr),
    S(SH_BUNKER, 0, 0, h1_b1),
    S(SH_BUNKER, 9, 0, h1_b2),
    S(SH_BUNKER, 0, 0, h1_b3),
    S(SH_PATH, 0, 0, h1_path),
};
static const gf_tree_def_t h1_tr[] = {
    { -24, 60, GF_TREE_OAK, 110 }, { 26, 110, GF_TREE_OAK, 90 }, { 30, 180, GF_TREE_POPLAR, 100 },
    { -22, 240, GF_TREE_OAK, 120 }, { 84, 318, GF_TREE_OAK, 100 }, { 10, 320, GF_TREE_PINE, 100 },
};
static const gf_mound_t h1_md[] = { { -24, 150, 14, 160 }, { 28, 270, 9, 110 }, { 60, 344, 10, 140 } };

/* ==========================================================================
 * 2  EL ESTANQUE - par 3, 165 yd. All carry over the pond, and the green
 *    leans towards it.
 * ========================================================================== */
static const gf_pt_t h2_tee[]  = { {-6,-6}, {6,-6}, {7,28}, {-5,28} };
static const gf_pt_t h2_gr[]   = { {-10,140}, {-12,152}, {-4,163}, {10,164}, {18,154}, {15,141}, {4,136} };
static const gf_pt_t h2_wa[]   = { {-34,96}, {-20,88}, {10,92}, {34,97}, {42,112}, {30,127}, {12,130}, {-8,129}, {-28,124}, {-40,110} };
static const gf_pt_t h2_b1[]   = { {0,169}, {4,176}, {15,174}, {17,167}, {9,167} };
static const gf_pt_t h2_b2[]   = { {-21,145}, {-20,157}, {-15,158}, {-15,146} };
static const gf_pt_t h2_fw[]   = { {-6,40}, {-10,70}, {8,78}, {12,46} };
static const gf_pt_t h2_fl[]   = { {-80,-20}, {-26,-16}, {-30,60}, {-48,90}, {-44,140}, {-30,200}, {-80,210} };
static const gf_pt_t h2_fr[]   = { {26,-20}, {80,-20}, {80,210}, {40,200}, {32,150}, {52,100}, {30,60} };
static const gf_pt_t h2_fb[]   = { {-30,184}, {40,184}, {40,210}, {-30,210} };
static const gf_shape_t h2_sh[] = {
    S(SH_FOREST, 6, GF_TREE_MIXED, h2_fl),
    S(SH_FOREST, 6, GF_TREE_MIXED, h2_fr),
    S(SH_FOREST, 7, GF_TREE_OAK, h2_fb),
    S(SH_TEE, 0, 0, h2_tee),
    S(SH_FAIRWAY, 0, 0, h2_fw),
    S(SH_WATER, 0, 0, h2_wa),
    S(SH_GREEN, 0, 0, h2_gr),
    S(SH_BUNKER, 0, 0, h2_b1),
    S(SH_BUNKER, 0, 0, h2_b2),
};
static const gf_tree_def_t h2_tr[] = {
    { -22, 30, GF_TREE_POPLAR, 100 }, { 20, 36, GF_TREE_OAK, 90 }, { -36, 150, GF_TREE_OAK, 110 },
    { 28, 160, GF_TREE_POPLAR, 110 }, { 40, 120, GF_TREE_BUSH, 120 }, { -44, 104, GF_TREE_BUSH, 130 },
};
static const gf_mound_t h2_md[] = { { -24, 176, 8, 120 }, { 26, 140, 9, 90 } };

/* ==========================================================================
 * 3  LA SERPIENTE - par 5, 520 yd. S-shaped, with a creek across at 300 m:
 *    lay up short of it or carry it.
 * ========================================================================== */
static const gf_pt_t h3_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h3_fwa[]  = { {-16,45}, {-22,120}, {-32,190}, {-46,242}, {-34,268}, {-14,252}, {-2,190}, {10,120}, {14,45} };
static const gf_pt_t h3_cr[]   = { {-130,282}, {-60,278}, {0,285}, {80,281}, {110,285}, {110,295}, {60,294}, {0,297}, {-60,291}, {-130,294} };
static const gf_pt_t h3_fwb[]  = { {-40,306}, {-45,352}, {-22,402}, {2,440}, {26,432}, {18,392}, {0,346}, {-8,306} };
static const gf_pt_t h3_gr[]   = { {10,455}, {12,468}, {22,476}, {36,472}, {39,460}, {30,449}, {18,449} };
static const gf_pt_t h3_b1[]   = { {-45,375}, {-45,387}, {-36,389}, {-34,378} };
static const gf_pt_t h3_b2[]   = { {41,458}, {42,474}, {49,472}, {48,460} };
static const gf_pt_t h3_b3[]   = { {1,443}, {3,452}, {10,453}, {10,444} };
static const gf_pt_t h3_b4[]   = { {3,203}, {6,215}, {13,216}, {14,205} };
static const gf_pt_t h3_dp[]   = { {-130,268}, {110,268}, {110,280}, {-130,277} };
static const gf_pt_t h3_fl[]   = { {-130,-20}, {-40,-16}, {-50,110}, {-72,200}, {-80,262}, {-130,266} };
static const gf_pt_t h3_fl2[]  = { {-130,300}, {-72,304}, {-74,380}, {-48,430}, {-20,480}, {-130,500} };
static const gf_pt_t h3_fr[]   = { {32,-20}, {110,-20}, {110,262}, {40,262}, {22,200}, {34,110} };
static const gf_pt_t h3_fr2[]  = { {30,300}, {110,300}, {110,500}, {64,500}, {60,440}, {40,380} };
static const gf_pt_t h3_path[] = { {9,-12}, {20,80}, {24,180}, {18,262}, {20,300}, {34,360}, {50,430}, {56,480} };
static const gf_shape_t h3_sh[] = {
    S(SH_FOREST, 7, GF_TREE_MIXED, h3_fl),
    S(SH_FOREST, 6, GF_TREE_MIXED, h3_fl2),
    S(SH_FOREST, 6, GF_TREE_MIXED, h3_fr),
    S(SH_FOREST, 6, GF_TREE_PINE, h3_fr2),
    S(SH_DEEP, 0, 0, h3_dp),
    S(SH_TEE, 0, 0, h3_tee),
    S(SH_FAIRWAY, 0, 0, h3_fwa),
    S(SH_FAIRWAY, 0, 0, h3_fwb),
    S(SH_WATER, 0, 0, h3_cr),
    S(SH_GREEN, 0, 0, h3_gr),
    S(SH_BUNKER, 0, 0, h3_b1),
    S(SH_BUNKER, 0, 0, h3_b2),
    S(SH_BUNKER, 10, 0, h3_b3),
    S(SH_BUNKER, 0, 0, h3_b4),
    S(SH_PATH, 0, 0, h3_path),
};
static const gf_tree_def_t h3_tr[] = {
    { -30, 60, GF_TREE_OAK, 100 }, { 22, 150, GF_TREE_POPLAR, 110 }, { -58, 230, GF_TREE_OAK, 120 },
    { -60, 330, GF_TREE_POPLAR, 100 }, { 20, 350, GF_TREE_OAK, 90 }, { -8, 470, GF_TREE_PINE, 110 },
};
static const gf_mound_t h3_md[] = { { -40, 150, 14, 200 }, { 30, 240, 10, 150 }, { -30, 420, 12, 180 } };

/* ==========================================================================
 * 4  LA CUESTA - par 4, 410 yd. Straight and uphill, fairway bunkers on
 *    both sides where the drive lands, a raised green guarded in front.
 * ========================================================================== */
static const gf_pt_t h4_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h4_fw[]   = { {-15,50}, {-17,150}, {-15,250}, {-12,330}, {-2,343}, {10,339}, {15,250}, {17,150}, {15,50} };
static const gf_pt_t h4_gr[]   = { {-12,362}, {-10,374}, {0,380}, {12,375}, {13,363}, {4,356}, {-6,356} };
static const gf_pt_t h4_b1[]   = { {-30,222}, {-30,240}, {-21,246}, {-18,230} };
static const gf_pt_t h4_b2[]   = { {18,198}, {18,214}, {26,220}, {28,204} };
static const gf_pt_t h4_b3[]   = { {-19,345}, {-17,356}, {-9,354}, {-11,345} };
static const gf_pt_t h4_b4[]   = { {10,346}, {10,354}, {18,357}, {21,348} };
static const gf_pt_t h4_ws[]   = { {-44,16}, {-26,58}, {-28,112}, {-46,86} };
static const gf_pt_t h4_fl[]   = { {-90,-20}, {-40,-20}, {-50,30}, {-42,140}, {-40,260}, {-34,400}, {-90,400} };
static const gf_pt_t h4_fr[]   = { {34,-20}, {90,-20}, {90,400}, {34,400}, {40,280}, {42,150} };
static const gf_shape_t h4_sh[] = {
    S(SH_FOREST, 6, GF_TREE_MIXED, h4_fl),
    S(SH_FOREST, 6, GF_TREE_MIXED, h4_fr),
    S(SH_WASTE, 0, 0, h4_ws),
    S(SH_TEE, 0, 0, h4_tee),
    S(SH_FAIRWAY, 0, 0, h4_fw),
    S(SH_GREEN, 0, 0, h4_gr),
    S(SH_BUNKER, 0, 0, h4_b1),
    S(SH_BUNKER, 0, 0, h4_b2),
    S(SH_BUNKER, 10, 0, h4_b3),
    S(SH_BUNKER, 10, 0, h4_b4),
};
static const gf_tree_def_t h4_tr[] = {
    { -30, 124, GF_TREE_OAK, 100 }, { 30, 160, GF_TREE_OAK, 90 }, { -28, 300, GF_TREE_POPLAR, 100 },
    { 26, 320, GF_TREE_OAK, 100 },
};
static const gf_mound_t h4_md[] = { { -26, 180, 10, 150 }, { 26, 260, 10, 140 }, { 0, 392, 14, 220 } };

/* ==========================================================================
 * 5  ARENA - par 3, 190 yd. Downhill over a sandy waste, a green ringed by
 *    bunkers, palms.
 * ========================================================================== */
static const gf_pt_t h5_tee[]  = { {-6,-6}, {6,-6}, {7,30}, {-5,30} };
static const gf_pt_t h5_gr[]   = { {-20,166}, {-18,178}, {-6,186}, {8,182}, {10,170}, {0,160}, {-14,160} };
static const gf_pt_t h5_b1[]   = { {-27,157}, {-31,170}, {-25,176}, {-22,165} };
static const gf_pt_t h5_b2[]   = { {-16,149}, {-10,155}, {0,153}, {4,147}, {-8,143} };
static const gf_pt_t h5_b3[]   = { {13,163}, {18,175}, {23,168}, {17,157} };
static const gf_pt_t h5_b4[]   = { {-4,191}, {6,195}, {14,189}, {8,185} };
static const gf_pt_t h5_b5[]   = { {-27,182}, {-19,192}, {-14,189}, {-21,180} };
static const gf_pt_t h5_ws[]   = { {-50,58}, {-10,68}, {40,60}, {56,90}, {40,128}, {0,120}, {-40,130}, {-60,98} };
static const gf_pt_t h5_fw[]   = { {-18,124}, {10,126}, {9,140}, {-16,141} };
static const gf_pt_t h5_fl[]   = { {-80,-20}, {-30,-20}, {-40,40}, {-66,120}, {-50,200}, {-80,220} };
static const gf_pt_t h5_fr[]   = { {30,-20}, {80,-20}, {80,220}, {40,210}, {60,140}, {46,40} };
static const gf_shape_t h5_sh[] = {
    S(SH_FOREST, 4, GF_TREE_PALM, h5_fl),
    S(SH_FOREST, 4, GF_TREE_PALM, h5_fr),
    S(SH_WASTE, 0, 0, h5_ws),
    S(SH_TEE, 0, 0, h5_tee),
    S(SH_FAIRWAY, 0, 0, h5_fw),
    S(SH_GREEN, 0, 0, h5_gr),
    S(SH_BUNKER, 0, 0, h5_b1),
    S(SH_BUNKER, 0, 0, h5_b2),
    S(SH_BUNKER, 0, 0, h5_b3),
    S(SH_BUNKER, 0, 0, h5_b4),
    S(SH_BUNKER, 0, 0, h5_b5),
};
static const gf_tree_def_t h5_tr[] = {
    { -20, 40, GF_TREE_PALM, 110 }, { 18, 50, GF_TREE_PALM, 100 }, { -36, 150, GF_TREE_PALM, 120 },
    { 30, 178, GF_TREE_PALM, 100 }, { 32, 110, GF_TREE_BUSH, 100 }, { -30, 96, GF_TREE_BUSH, 110 },
    { 14, 82, GF_TREE_BUSH, 90 },
};
static const gf_mound_t h5_md[] = { { 20, 100, 8, 100 }, { -30, 180, 8, 160 } };

/* ==========================================================================
 * 6  EL ATAJO - par 4, 330 yd. Dogleg left around a lake: round it on the
 *    fairway, or carry the water straight at the green.
 * ========================================================================== */
static const gf_pt_t h6_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h6_fw[]   = { {-12,50}, {-14,130}, {-22,200}, {-40,242}, {-62,262}, {-60,284}, {-36,282}, {-4,252}, {14,190}, {16,120}, {12,50} };
static const gf_pt_t h6_wa[]   = { {-100,90}, {-60,80}, {-26,100}, {-21,160}, {-30,212}, {-50,236}, {-80,248}, {-104,220}, {-112,150} };
static const gf_pt_t h6_gr[]   = { {-94,284}, {-92,296}, {-80,302}, {-68,298}, {-66,286}, {-76,278}, {-88,278} };
static const gf_pt_t h6_b1[]   = { {-71,271}, {-64,278}, {-58,274}, {-62,266} };
static const gf_pt_t h6_b2[]   = { {-97,302}, {-89,309}, {-84,305}, {-92,299} };
static const gf_pt_t h6_b3[]   = { {18,210}, {20,226}, {28,228}, {30,214} };
static const gf_pt_t h6_fr[]   = { {40,-20}, {80,-20}, {80,330}, {-10,330}, {10,290}, {44,230}, {44,120} };
static const gf_pt_t h6_fl[]   = { {-140,-20}, {-30,-20}, {-40,50}, {-120,70}, {-130,200}, {-140,260} };
static const gf_pt_t h6_fb[]   = { {-140,300}, {-110,300}, {-100,318}, {-40,316}, {-40,340}, {-140,340} };
static const gf_pt_t h6_path[] = { {9,-12}, {26,70}, {30,170}, {10,262}, {-30,300}, {-56,312} };
static const gf_shape_t h6_sh[] = {
    S(SH_FOREST, 6, GF_TREE_MIXED, h6_fr),
    S(SH_FOREST, 6, GF_TREE_MIXED, h6_fl),
    S(SH_FOREST, 7, GF_TREE_PINE, h6_fb),
    S(SH_TEE, 0, 0, h6_tee),
    S(SH_FAIRWAY, 0, 0, h6_fw),
    S(SH_WATER, 0, 0, h6_wa),
    S(SH_GREEN, 0, 0, h6_gr),
    S(SH_BUNKER, 0, 0, h6_b1),
    S(SH_BUNKER, 0, 0, h6_b2),
    S(SH_BUNKER, 0, 0, h6_b3),
    S(SH_PATH, 0, 0, h6_path),
};
static const gf_tree_def_t h6_tr[] = {
    { -30, 40, GF_TREE_POPLAR, 110 }, { 26, 120, GF_TREE_OAK, 100 }, { -120, 260, GF_TREE_OAK, 110 },
    { -46, 300, GF_TREE_OAK, 100 }, { -18, 100, GF_TREE_BUSH, 110 },
};
static const gf_mound_t h6_md[] = { { 30, 160, 12, 160 }, { -110, 290, 10, 150 } };

/* ==========================================================================
 * 7  EL LARGO - par 5, 540 yd. Water all down the right from 250 m to the
 *    green, which it wraps.
 * ========================================================================== */
static const gf_pt_t h7_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h7_fw[]   = { {-18,50}, {-22,200}, {-16,300}, {-8,400}, {2,458}, {12,468}, {22,452}, {16,400}, {14,300}, {18,200}, {18,50} };
static const gf_pt_t h7_wa[]   = { {30,236}, {60,232}, {72,300}, {68,400}, {62,470}, {52,502}, {38,496}, {40,462}, {30,420}, {27,330} };
static const gf_pt_t h7_gr[]   = { {8,474}, {10,488}, {22,494}, {33,488}, {34,474}, {24,466}, {12,466} };
static const gf_pt_t h7_b1[]   = { {-6,474}, {-5,488}, {2,490}, {4,476} };
static const gf_pt_t h7_b2[]   = { {-32,290}, {-30,306}, {-22,310}, {-20,294} };
static const gf_pt_t h7_b3[]   = { {-14,372}, {-12,384}, {0,386}, {2,376} };
static const gf_pt_t h7_fl[]   = { {-100,-20}, {-40,-20}, {-46,200}, {-50,380}, {-30,520}, {-100,520} };
static const gf_pt_t h7_fr[]   = { {34,-20}, {110,-20}, {110,520}, {84,520}, {86,300}, {40,210} };
static const gf_shape_t h7_sh[] = {
    S(SH_FOREST, 6, GF_TREE_MIXED, h7_fl),
    S(SH_FOREST, 5, GF_TREE_MIXED, h7_fr),
    S(SH_TEE, 0, 0, h7_tee),
    S(SH_FAIRWAY, 0, 0, h7_fw),
    S(SH_WATER, 0, 0, h7_wa),
    S(SH_GREEN, 0, 0, h7_gr),
    S(SH_BUNKER, 0, 0, h7_b1),
    S(SH_BUNKER, 0, 0, h7_b2),
    S(SH_BUNKER, 0, 0, h7_b3),
};
static const gf_tree_def_t h7_tr[] = {
    { -30, 120, GF_TREE_OAK, 110 }, { 26, 140, GF_TREE_POPLAR, 100 }, { -34, 250, GF_TREE_OAK, 100 },
    { -30, 440, GF_TREE_POPLAR, 110 }, { 78, 230, GF_TREE_BUSH, 120 },
};
static const gf_mound_t h7_md[] = { { -30, 360, 12, 150 }, { 0, 505, 12, 160 } };

/* ==========================================================================
 * 8  LA DESPEDIDA - par 4, 440 yd. The long one home: dogleg left behind
 *    a wood, a big bunker short right, and a green in two tiers.
 * ========================================================================== */
static const gf_pt_t h8_tee[]  = { {-5,-7}, {5,-7}, {6,34}, {-4,34} };
static const gf_pt_t h8_fw[]   = { {-14,50}, {-16,160}, {-22,240}, {-40,300}, {-64,346}, {-50,358}, {-26,322}, {-2,270}, {14,180}, {16,50} };
static const gf_pt_t h8_gr[]   = { {-90,380}, {-88,394}, {-74,400}, {-60,396}, {-58,382}, {-68,372}, {-82,372} };
static const gf_pt_t h8_b1[]   = { {-60,358}, {-50,370}, {-44,366}, {-46,352}, {-54,348} };
static const gf_pt_t h8_b2[]   = { {-97,372}, {-99,384}, {-93,386}, {-91,374} };
static const gf_pt_t h8_b3[]   = { {-44,232}, {-42,248}, {-32,252}, {-30,238} };
static const gf_pt_t h8_fc[]   = { {-130,150}, {-42,150}, {-52,226}, {-80,300}, {-130,322} };
static const gf_pt_t h8_fl[]   = { {-130,-20}, {-34,-20}, {-40,110}, {-130,120} };
static const gf_pt_t h8_fr[]   = { {36,-20}, {80,-20}, {80,420}, {-30,420}, {-20,370}, {20,300}, {40,200} };
static const gf_pt_t h8_fb[]   = { {-130,408}, {-30,408}, {-30,420}, {-130,420} };
static const gf_pt_t h8_path[] = { {9,-12}, {26,80}, {28,200}, {6,300}, {-30,350}, {-50,398}, {-60,412} };
static const gf_shape_t h8_sh[] = {
    S(SH_FOREST, 8, GF_TREE_MIXED, h8_fc),
    S(SH_FOREST, 6, GF_TREE_MIXED, h8_fl),
    S(SH_FOREST, 6, GF_TREE_MIXED, h8_fr),
    S(SH_FOREST, 7, GF_TREE_PINE, h8_fb),
    S(SH_TEE, 0, 0, h8_tee),
    S(SH_FAIRWAY, 0, 0, h8_fw),
    S(SH_GREEN, 0, 0, h8_gr),
    S(SH_BUNKER, 0, 0, h8_b1),
    S(SH_BUNKER, 0, 0, h8_b2),
    S(SH_BUNKER, 0, 0, h8_b3),
    S(SH_PATH, 0, 0, h8_path),
};
static const gf_tree_def_t h8_tr[] = {
    { 26, 110, GF_TREE_OAK, 100 }, { -28, 60, GF_TREE_POPLAR, 110 }, { 24, 250, GF_TREE_OAK, 110 },
    { -104, 360, GF_TREE_OAK, 100 },
};
static const gf_mound_t h8_md[] = { { 30, 150, 12, 170 }, { -40, 390, 10, 160 } };

/* ========================================================================== */

#define T3(a, b, c, d, e, f) { {a, b}, {c, d}, {e, f} }
#define HOLE(nm, par, t, p, bx0, by0, bx1, by1, rise, rough, seed, tx, ty, bump, sh, tr, md) \
    { nm, par, t, p, bx0, by0, bx1, by1, rise, rough, seed, tx, ty, bump, 0, \
      sh, N(sh), tr, N(tr), md, N(md) }

static const gf_hole_t s_holes[] = {
    HOLE("La Bienvenida", 4, T3(0, 0, 0, 14, 1, 28), T3(54, 322, 61, 318, 48, 326),
         -60, -20, 100, 370, 300, 4, 11, -15, 10, 12, h1_sh, h1_tr, h1_md),
    HOLE("El Estanque", 3, T3(0, 0, 0, 12, 1, 24), T3(4, 150, -4, 155, 10, 146),
         -60, -20, 60, 200, -200, 3, 23, 10, -20, 0, h2_sh, h2_tr, h2_md),
    HOLE("La Serpiente", 5, T3(0, 0, 0, 14, 1, 28), T3(24, 462, 30, 466, 18, 456),
         -110, -20, 90, 495, 500, 5, 37, -10, 15, 15, h3_sh, h3_tr, h3_md),
    HOLE("La Cuesta", 4, T3(0, 0, 0, 14, 1, 28), T3(0, 368, -5, 371, 6, 364),
         -70, -20, 70, 400, 900, 4, 41, 5, -18, 10, h4_sh, h4_tr, h4_md),
    HOLE("Arena", 3, T3(0, 0, 0, 12, 1, 26), T3(-5, 172, -12, 168, 2, 176),
         -60, -20, 60, 205, -600, 3, 53, 20, -10, 0, h5_sh, h5_tr, h5_md),
    HOLE("El Atajo", 4, T3(0, 0, 0, 14, 1, 28), T3(-80, 290, -86, 292, -73, 286),
         -130, -20, 60, 330, 100, 4, 67, 15, 5, 8, h6_sh, h6_tr, h6_md),
    HOLE("El Largo", 5, T3(0, 0, 0, 14, 1, 28), T3(21, 480, 26, 478, 14, 484),
         -80, -20, 100, 515, 200, 4, 71, -8, 12, 12, h7_sh, h7_tr, h7_md),
    HOLE("La Despedida", 4, T3(0, 0, 0, 14, 1, 28), T3(-74, 386, -80, 390, -66, 383),
         -130, -20, 70, 415, 300, 5, 83, 0, 12, 45, h8_sh, h8_tr, h8_md),
};

#include "gf_courses_extra.h"

static const gf_course_t s_courses[] = {
    { "Sierra Verde", (uint8_t)(sizeof(s_holes) / sizeof(s_holes[0])), s_holes, THEME_WOODS, 10 },
    { "Dunas del Faro", (uint8_t)(sizeof(c_holes) / sizeof(c_holes[0])), c_holes, THEME_COAST, 16 },
    { "Parque de los Lagos", (uint8_t)(sizeof(p_holes) / sizeof(p_holes[0])), p_holes, THEME_LAKES, 7 },
};
static int s_sel;

int gf_course_n(void)
{
    return (int)(sizeof(s_courses) / sizeof(s_courses[0]));
}

const gf_course_t *gf_course_get(int i)
{
    return &s_courses[i < 0 || i >= gf_course_n() ? 0 : i];
}

const gf_course_t *gf_course(void)
{
    return &s_courses[s_sel];
}

void gf_course_select(int i)
{
    s_sel = i < 0 || i >= gf_course_n() ? 0 : i;
}

int gf_course_index(void)
{
    return s_sel;
}
