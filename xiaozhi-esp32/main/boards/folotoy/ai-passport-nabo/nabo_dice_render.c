/* Procedural dice for the 240x320 NABO panel.
 *
 * Every pixel is computed on demand for the strip LVGL asks for: a felt tray,
 * soft contact shadows, and up to three rounded ivory dice with Chinese red
 * one/four pips. Orthographic projection keeps each face an affine map, so a
 * pixel costs a few integer multiply-adds per visible face. Colours carry
 * 4 fractional bits and are ordered-dithered into RGB565 so gradients do not
 * band. No float (the C3 has no FPU), heap, or Flash images. */
#include "nabo_dice_render.h"
#include <string.h>


#define ONE 16384                       /* Q14 */
#define U1 65536                        /* Q16 face coordinate */
#define TILT 8556                       /* 47 degrees in 1/65536 turns */
#define VIS_Z 600                       /* faces nearer edge-on are skipped */
#define BEVEL_START 48496               /* 0.74: rounded edge band */
#define BEVEL_K ((256 << 16) / (U1 - BEVEL_START))
#define PIP_STEP 34734                  /* 0.53 */
#define PIP_SPLIT 17367
#define PIP_R 2900                      /* Q14 radius, 0.177 */
#define PIP_R_ONE 4590                  /* Q14 radius of the single red pip */
#define TRAY_R 22
#define RIM 7

static const int16_t QSIN[257] = {
    0,101,201,302,402,503,603,704,804,904,1005,1105,1205,1306,1406,1506,
    1606,1706,1806,1906,2006,2105,2205,2305,2404,2503,2603,2702,2801,2900,2999,3098,
    3196,3295,3393,3492,3590,3688,3786,3883,3981,4078,4176,4273,4370,4467,4563,4660,
    4756,4852,4948,5044,5139,5235,5330,5425,5520,5614,5708,5803,5897,5990,6084,6177,
    6270,6363,6455,6547,6639,6731,6823,6914,7005,7096,7186,7276,7366,7456,7545,7635,
    7723,7812,7900,7988,8076,8163,8250,8337,8423,8509,8595,8680,8765,8850,8935,9019,
    9102,9186,9269,9352,9434,9516,9598,9679,9760,9841,9921,10001,10080,10159,10238,10316,
    10394,10471,10549,10625,10702,10778,10853,10928,11003,11077,11151,11224,11297,11370,11442,11514,
    11585,11656,11727,11797,11866,11935,12004,12072,12140,12207,12274,12340,12406,12472,12537,12601,
    12665,12729,12792,12854,12916,12978,13039,13100,13160,13219,13279,13337,13395,13453,13510,13567,
    13623,13678,13733,13788,13842,13896,13949,14001,14053,14104,14155,14206,14256,14305,14354,14402,
    14449,14497,14543,14589,14635,14680,14724,14768,14811,14854,14896,14937,14978,15019,15059,15098,
    15137,15175,15213,15250,15286,15322,15357,15392,15426,15460,15493,15525,15557,15588,15619,15649,
    15679,15707,15736,15763,15791,15817,15843,15868,15893,15917,15941,15964,15986,16008,16029,16049,
    16069,16088,16107,16125,16143,16160,16176,16192,16207,16221,16235,16248,16261,16273,16284,16295,
    16305,16315,16324,16332,16340,16347,16353,16359,16364,16369,16373,16376,16379,16381,16383,16384,
    16384,
};

static const uint8_t BAYER[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
/* Felt colour by vignette (Q12 >> 5), Q4: centre (36,112,84) to edge (12,54,42). */
static const int16_t FELT[129][3] = {
    {576,1792,1344}, {573,1785,1339}, {570,1778,1334}, {567,1770,1328},
    {564,1763,1323}, {561,1756,1318}, {558,1748,1312}, {555,1741,1307},
    {552,1734,1302}, {549,1727,1297}, {546,1720,1292}, {543,1712,1286},
    {540,1705,1281}, {537,1698,1276}, {534,1690,1270}, {531,1683,1265},
    {528,1676,1260}, {525,1669,1255}, {522,1662,1250}, {519,1654,1244},
    {516,1647,1239}, {513,1640,1234}, {510,1632,1228}, {507,1625,1223},
    {504,1618,1218}, {501,1611,1213}, {498,1604,1208}, {495,1596,1202},
    {492,1589,1197}, {489,1582,1192}, {486,1574,1186}, {483,1567,1181},
    {480,1560,1176}, {477,1553,1171}, {474,1546,1166}, {471,1538,1160},
    {468,1531,1155}, {465,1524,1150}, {462,1516,1144}, {459,1509,1139},
    {456,1502,1134}, {453,1495,1129}, {450,1488,1124}, {447,1480,1118},
    {444,1473,1113}, {441,1466,1108}, {438,1458,1102}, {435,1451,1097},
    {432,1444,1092}, {429,1437,1087}, {426,1430,1082}, {423,1422,1076},
    {420,1415,1071}, {417,1408,1066}, {414,1400,1060}, {411,1393,1055},
    {408,1386,1050}, {405,1379,1045}, {402,1372,1040}, {399,1364,1034},
    {396,1357,1029}, {393,1350,1024}, {390,1342,1018}, {387,1335,1013},
    {384,1328,1008}, {381,1321,1003}, {378,1314,998}, {375,1306,992},
    {372,1299,987}, {369,1292,982}, {366,1284,976}, {363,1277,971},
    {360,1270,966}, {357,1263,961}, {354,1256,956}, {351,1248,950},
    {348,1241,945}, {345,1234,940}, {342,1226,934}, {339,1219,929},
    {336,1212,924}, {333,1205,919}, {330,1198,914}, {327,1190,908},
    {324,1183,903}, {321,1176,898}, {318,1168,892}, {315,1161,887},
    {312,1154,882}, {309,1147,877}, {306,1140,872}, {303,1132,866},
    {300,1125,861}, {297,1118,856}, {294,1110,850}, {291,1103,845},
    {288,1096,840}, {285,1089,835}, {282,1082,830}, {279,1074,824},
    {276,1067,819}, {273,1060,814}, {270,1052,808}, {267,1045,803},
    {264,1038,798}, {261,1031,793}, {258,1024,788}, {255,1016,782},
    {252,1009,777}, {249,1002,772}, {246,994,766}, {243,987,761},
    {240,980,756}, {237,973,751}, {234,966,746}, {231,958,740},
    {228,951,735}, {225,944,730}, {222,936,724}, {219,929,719},
    {216,922,714}, {213,915,709}, {210,908,704}, {207,900,698},
    {204,893,693}, {201,886,688}, {198,878,682}, {195,871,677},
    {192,864,672},
};
/* Felt nap: a fixed 16x16 grain, tiled. */
static const int8_t GRAIN[256] = {
    -7,-14,0,14,-21,-21,21,7,-21,-7,7,-21,7,-14,-21,-21,
    0,0,-21,-14,-21,7,0,-21,21,7,-21,-14,14,14,7,-21,
    7,7,0,-21,-14,-21,7,21,-14,-7,0,-14,7,-21,7,-7,
    7,21,14,-14,-21,7,7,14,-14,-7,-21,7,14,-21,7,-21,
    7,-14,0,14,7,0,21,-7,0,7,0,-7,-7,-14,21,-14,
    14,21,-14,-21,7,-7,7,0,-7,14,0,-7,7,-21,-21,7,
    0,-14,21,-7,-14,0,0,-21,14,-21,21,7,7,21,21,-7,
    -7,14,-7,7,0,7,21,0,-21,21,-21,-7,0,14,14,-21,
    -21,14,14,-7,14,7,14,21,0,-7,14,0,14,-7,-21,0,
    -7,-14,7,-21,0,-21,-14,21,-7,-14,14,-14,0,0,21,0,
    -21,-14,0,0,7,-7,-14,21,0,21,7,-7,14,0,-7,14,
    0,-14,-14,-21,-14,-14,-14,14,-14,-21,0,21,7,-14,-7,-7,
    -21,-14,0,7,-7,7,7,-7,-14,14,21,7,7,14,14,14,
    -21,0,21,21,21,14,21,7,0,0,0,0,-21,0,14,0,
    -21,-14,-21,-14,0,-14,-21,-7,7,-21,-21,-21,7,-14,7,-21,
    -7,7,-21,-21,21,-14,7,0,-14,14,-7,-7,7,-7,0,-21,
};
/* Pip cells, row-major 3x3 from (-u,-v). Opposite faces sum to seven. */
static const uint16_t PIPS[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d};

/* Light from upper left, in view space (z toward the viewer), and the
 * Blinn half vector for a viewer on +z. */
static const int32_t LX = -6891, LY = 11813, LZ = 9024;
static const int32_t HX = -3913, HY = 6707, HZ = 14426;

static int32_t isin(uint32_t a)
{
    uint32_t i = (a >> 6) & 1023u, f = a & 63u;
    unsigned q = i >> 8, k = i & 255u;
    int32_t s0, s1;
    if (q & 1u) { s0 = QSIN[256 - k]; s1 = QSIN[255 - k]; }
    else { s0 = QSIN[k]; s1 = QSIN[k + 1]; }
    int32_t s = s0 + (((s1 - s0) * (int32_t)f) >> 6);
    return q & 2u ? -s : s;
}
static int32_t icos(uint32_t a) { return isin(a + 16384u); }

static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static uint32_t next_random(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x ? x : 0x9e3779b9u;
}

static int32_t span(uint32_t *s, int32_t lo, int32_t hi)
{
    return lo + (int32_t)(next_random(s) % (uint32_t)(hi - lo + 1));
}

static inline __attribute__((always_inline)) int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static inline __attribute__((always_inline)) int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------- roll plan ---------- */

typedef struct { int16_t x, y; } slot_t;
static const slot_t SLOTS[3][3] = {
    {{112, 152}, {0, 0}, {0, 0}},
    {{70, 150}, {154, 158}, {0, 0}},
    {{60, 160}, {164, 160}, {112, 118}},
};
static const int16_t HALF[3] = {33, 27, 23};

void nabo_dice_plan(nabo_dice_plan_t *p, uint32_t seed, unsigned count)
{
    memset(p, 0, sizeof(*p));
    if (count < 1) count = 1;
    if (count > NABO_DICE_MAX) count = NABO_DICE_MAX;
    uint32_t s = seed ? seed : 0x6d2b79f5u;
    for (int i = 0; i < 3; ++i) next_random(&s);
    p->seed = seed;
    p->count = (uint8_t)count;
    p->half = HALF[count - 1];
    for (unsigned i = 0; i < count; ++i) {
        uint32_t r;
        do r = next_random(&s); while (r >= 4294967292u);   /* unbiased 1..6 */
        p->value[i] = (uint8_t)(1 + r % 6u);
        p->tx[i] = (int16_t)(SLOTS[count - 1][i].x + span(&s, -5, 5));
        p->ty[i] = (int16_t)(SLOTS[count - 1][i].y + span(&s, -3, 3));
        int32_t side = (int32_t)(next_random(&s) & 1u) ? 1 : -1;
        p->sx[i] = (int16_t)clamp(p->tx[i] + side * span(&s, 18, 46), p->half * 7 / 4 + 10, NABO_DICE_W - p->half * 7 / 4 - 10);
        p->sy[i] = (int16_t)(p->ty[i] + span(&s, -6, 8));
        p->yaw[i] = (uint16_t)((next_random(&s) & 3u) * 16384u +
                               (uint32_t)((next_random(&s) & 1u) ? 1 : -1) * (uint32_t)span(&s, 2700, 5600));
        /* Mostly horizontal axes read as tumbling end over end. */
        uint32_t heading = next_random(&s);
        int32_t ax = icos(heading), az = isin(heading), ay = span(&s, -5000, 5000);
        uint32_t len = isqrt32((uint32_t)(ax * ax + ay * ay + az * az));
        p->axis[i][0] = (int16_t)(ax * ONE / (int32_t)len);
        p->axis[i][1] = (int16_t)(ay * ONE / (int32_t)len);
        p->axis[i][2] = (int16_t)(az * ONE / (int32_t)len);
        p->spin[i] = span(&s, 78000, 140000);
        int32_t peak = span(&s, 66, 82) * (count == 1 ? 100 : count == 2 ? 90 : 80) / 100;
        /* Keep even a corner-up die below the tray's top rim at the apex. */
        int32_t low = p->ty[i] < p->sy[i] ? p->ty[i] : p->sy[i];
        int32_t reach = p->half * 7 / 4;
        int32_t room = (low - 8 - reach) * 1000 / 682 - reach;
        if (peak > room) peak = room;
        if (peak < 30) peak = 30;
        p->peak[i] = (uint16_t)peak;
        /* Same gravity for every die: duration grows with sqrt(height). */
        p->arc_ms[i] = (uint16_t)(560u * isqrt32(p->peak[i] * 65536u / 76u) / 256u);
        /* Thrown from 18% into the first arc; then 22%, 5% and 1.2% hops. */
        p->land_ms[i] = (uint16_t)(p->arc_ms[i] * 1623u / 1000u);
        if (p->land_ms[i] + 120u > p->end_ms) p->end_ms = (uint16_t)(p->land_ms[i] + 120u);
    }
}

unsigned nabo_dice_total(const nabo_dice_plan_t *p)
{
    unsigned total = 0;
    for (unsigned i = 0; i < p->count; ++i) total += p->value[i];
    return total;
}

/* Hop k runs from START_K[k] to START_K[k+1] (x1000 of the first arc). */
static const uint16_t HOP_START[5] = {0, 820, 1289, 1513, 1623};
static const uint16_t HOP_HEIGHT[4] = {1000, 220, 50, 12};   /* x1000 of peak */

/* Height above the table at local time t, px Q8. */
static int32_t height_q8(const nabo_dice_plan_t *p, unsigned i, int32_t t)
{
    if (t >= p->land_ms[i]) return 0;
    int32_t m = (int32_t)((int64_t)t * 1000 / p->arc_ms[i]);   /* x1000 of first arc */
    for (int k = 0; k < 4; ++k) {
        int32_t a = HOP_START[k], b = HOP_START[k + 1];
        if (m >= b) continue;
        if (k == 0) a = -180;                                   /* thrown mid-arc */
        int32_t len = b - a, tau = m - a;                       /* parabola 4H t(1-t) */
        int64_t h = (int64_t)4 * p->peak[i] * HOP_HEIGHT[k] * tau * (len - tau);
        return (int32_t)(h * 256 / ((int64_t)len * len * 1000));
    }
    return 0;
}

unsigned nabo_dice_impacts(const nabo_dice_plan_t *p, nabo_dice_impact_t *out, unsigned cap)
{
    static const uint16_t AMP[4] = {256, 120, 58, 28};   /* ~ landing speed */
    unsigned n = 0;
    uint32_t s = p->seed ^ 0xa5a5a5a5u;
    for (unsigned i = 0; i < p->count; ++i) {
        for (int k = 1; k < 5 && n < cap; ++k) {
            out[n].ms = (uint16_t)(p->arc_ms[i] * HOP_START[k] / 1000u);
            out[n].amp = (uint16_t)(AMP[k - 1] * span(&s, 82, 100) / 100);
            ++n;
            /* A die landing on a corner often ticks twice. */
            if (k == 1 && n < cap) {
                out[n].ms = (uint16_t)(out[n - 1].ms + span(&s, 14, 26));
                out[n].amp = (uint16_t)(out[n - 1].amp * 45 / 100);
                ++n;
            }
        }
    }
    /* Dice brushing past each other in the air. */
    for (unsigned i = 1; i < p->count && n < cap; ++i) {
        out[n].ms = (uint16_t)span(&s, 120, 300);
        out[n].amp = (uint16_t)span(&s, 40, 70);
        ++n;
    }
    for (unsigned a = 1; a < n; ++a)
        for (unsigned b = a; b && out[b - 1].ms > out[b].ms; --b) {
            nabo_dice_impact_t t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
        }
    return n;
}

/* ---------- pose ---------- */

typedef int32_t mat_t[9];

static void mat_mul(mat_t out, const mat_t a, const mat_t b)
{
    mat_t r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = (a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]) >> 14;
    memcpy(out, r, sizeof(r));
}

static void axis_angle(mat_t m, const int16_t *k, uint32_t angle)
{
    int64_t c = icos(angle), s = isin(angle), t = ONE - c;
    int64_t x = k[0], y = k[1], z = k[2];
    m[0] = (int32_t)(c + (t * x * x >> 28)); m[1] = (int32_t)((t * x * y >> 28) - (s * z >> 14)); m[2] = (int32_t)((t * x * z >> 28) + (s * y >> 14));
    m[3] = (int32_t)((t * x * y >> 28) + (s * z >> 14)); m[4] = (int32_t)(c + (t * y * y >> 28)); m[5] = (int32_t)((t * y * z >> 28) - (s * x >> 14));
    m[6] = (int32_t)((t * x * z >> 28) - (s * y >> 14)); m[7] = (int32_t)((t * y * z >> 28) + (s * x >> 14)); m[8] = (int32_t)(c + (t * z * z >> 28));
}

/* Signed permutations that put each value on top (+Y). Local faces:
 * 1 +Z, 6 -Z, 2 +X, 5 -X, 3 +Y, 4 -Y. */
static const int8_t TO_TOP[7][9] = {
    {1,0,0, 0,1,0, 0,0,1},
    {1,0,0, 0,0,1, 0,-1,0},
    {0,-1,0, 1,0,0, 0,0,1},
    {1,0,0, 0,1,0, 0,0,1},
    {1,0,0, 0,-1,0, 0,0,-1},
    {0,1,0, -1,0,0, 0,0,1},
    {1,0,0, 0,0,-1, 0,1,0},
};
/* Face value, outward axis (0..2) and sign, then u and v axes (a x b = n). */
typedef struct { uint8_t value, n; int8_t ns; uint8_t a, b; } face_def_t;
static const face_def_t FACES[6] = {
    {1, 2, 1, 0, 1}, {6, 2, -1, 1, 0}, {2, 0, 1, 1, 2},
    {5, 0, -1, 2, 1}, {3, 1, 1, 2, 0}, {4, 1, -1, 0, 2},
};

static int32_t ease_out(int32_t p, int power)   /* Q14 in and out */
{
    int32_t q = ONE - clamp(p, 0, ONE), r = ONE;
    for (int i = 0; i < power; ++i) r = r * q >> 14;
    return ONE - r;
}

static void shade(int32_t nx, int32_t ny, int32_t nz, int16_t out[3], int16_t *lit)
{
    static const int16_t IVORY[3] = {247, 242, 231};
    int32_t dif = (nx * LX + ny * LY + nz * LZ) >> 14;
    if (dif < 0) dif = 0;
    int32_t nh = (nx * HX + ny * HY + nz * HZ) >> 14;
    if (nh < 0) nh = 0;
    int32_t p = nh;
    for (int i = 0; i < 5; ++i) p = p * p >> 14;      /* (n.h)^32 */
    int32_t light = 6400 + dif * 11000 / ONE;          /* Q14: 0.39 + 0.67 d */
    int32_t spec = p * 7400 / ONE;                     /* Q14 */
    /* Cool the shadowed side a little, as a sky-lit room would. */
    static const int16_t COOL[3] = {-10, -3, 12};
    for (int c = 0; c < 3; ++c) {
        int32_t v = (IVORY[c] * 16 * light >> 14) + (255 * 16 * spec >> 14) +
                    COOL[c] * (ONE - dif) / ONE * 16 / 4;
        out[c] = (int16_t)clamp(v, 0, 4095);
    }
    if (lit) *lit = (int16_t)(light >> 6);
}

static void build_die(nabo_dice_die_t *d, const nabo_dice_plan_t *p, unsigned i, int32_t t)
{
    int32_t h = height_q8(p, i, t);
    int32_t prog = (int32_t)((int64_t)clamp(t, 0, p->land_ms[i]) * ONE / p->land_ms[i]);
    int32_t move = ease_out(prog, 3);
    int32_t turn = ease_out((int32_t)((int64_t)prog * 1100 / 1000), 2);
    int32_t tx = (p->sx[i] << 8) + (((p->tx[i] - p->sx[i]) << 8) * move >> 14);
    int32_t ty = (p->sy[i] << 8) + (((p->ty[i] - p->sy[i]) << 8) * move >> 14);
    uint32_t spin = (uint32_t)((int64_t)p->spin[i] * (ONE - turn) >> 14);

    mat_t top, yaw, world, tilt, view;
    for (int k = 0; k < 9; ++k) top[k] = TO_TOP[p->value[i]][k] * ONE;
    int32_t cy = icos(p->yaw[i]), sy = isin(p->yaw[i]);
    mat_t ry = {cy, 0, sy, 0, ONE, 0, -sy, 0, cy};
    memcpy(yaw, ry, sizeof(yaw));
    mat_mul(world, yaw, top);
    if (spin) {
        mat_t rot;
        axis_angle(rot, p->axis[i], spin);
        mat_mul(world, rot, world);
    }
    int32_t ce = icos(TILT), se = isin(TILT);
    mat_t rx = {ONE, 0, 0, 0, ce, -se, 0, se, ce};
    memcpy(tilt, rx, sizeof(tilt));
    mat_mul(view, tilt, world);

    int32_t s = p->half;
    /* Rest on the lowest corner while tumbling, never through the felt. */
    int32_t reach = (iabs(world[3]) + iabs(world[4]) + iabs(world[5])) * s;   /* Q14 px */
    int32_t lift = h + (reach >> 6);                                            /* Q8 */
    int32_t cx8 = tx, cy8 = ty - (lift * ce >> 14);

    int32_t ex = (iabs(view[0]) + iabs(view[1]) + iabs(view[2])) * s >> 14;
    int32_t ey = (iabs(view[3]) + iabs(view[4]) + iabs(view[5])) * s >> 14;
    d->bx0 = (int16_t)clamp((cx8 >> 8) - ex - 2, 0, NABO_DICE_W - 1);
    d->bx1 = (int16_t)clamp((cx8 >> 8) + ex + 2, 0, NABO_DICE_W - 1);
    d->by0 = (int16_t)clamp((cy8 >> 8) - ey - 2, 0, NABO_DICE_H - 1);
    d->by1 = (int16_t)clamp((cy8 >> 8) + ey + 2, 0, NABO_DICE_H - 1);

    /* Soft shadow: grows and fades as the die rises; light is up-left. */
    int32_t hp = h >> 8;
    int32_t rx8 = s * 256 * (138 + hp / 2) / 100;
    int32_t ry8 = rx8 * 50 / 100;
    d->shx = tx + (s * 256 * 34 / 100) + h * 30 / 100;
    d->shy = ty + (s * 256 * 30 / 100) + h * 8 / 100;
    d->shrx = (int32_t)((1 << 28) / rx8);
    d->shry = (int32_t)((1 << 28) / ry8);
    d->sha = (int16_t)(175 * 90 / (90 + hp));
    d->sx0 = (int16_t)clamp(((d->shx - rx8) >> 8) - 1, 0, NABO_DICE_W - 1);
    d->sx1 = (int16_t)clamp(((d->shx + rx8) >> 8) + 1, 0, NABO_DICE_W - 1);
    d->sy0 = (int16_t)clamp(((d->shy - ry8) >> 8) - 1, 0, NABO_DICE_H - 1);
    d->sy1 = (int16_t)clamp(((d->shy + ry8) >> 8) + 1, 0, NABO_DICE_H - 1);

    d->faces = 0;
    for (int f = 0; f < 6 && d->faces < 3; ++f) {
        const face_def_t *fd = &FACES[f];
        int32_t nx = view[fd->n] * fd->ns, ny = view[3 + fd->n] * fd->ns, nz = view[6 + fd->n] * fd->ns;
        if (nz <= VIS_Z) continue;
        int32_t ax = view[fd->a], ay = view[3 + fd->a], az = view[6 + fd->a];
        int32_t bx = view[fd->b], by = view[3 + fd->b], bz = view[6 + fd->b];
        /* Screen y grows downward. Edge vectors in Q8 px. */
        int32_t a2x = ax * s >> 6, a2y = -(ay * s >> 6);
        int32_t b2x = bx * s >> 6, b2y = -(by * s >> 6);
        int64_t det = (int64_t)a2x * b2y - (int64_t)a2y * b2x;   /* Q16 px^2 */
        if (det == 0) continue;
        nabo_dice_face_t *o = &d->face[d->faces++];
        o->value = fd->value;
        o->fcx = cx8 + (nx * s >> 6);
        o->fcy = cy8 - (ny * s >> 6);
        o->ux = (int32_t)(((int64_t)b2y << 24) / det);
        o->uy = (int32_t)((-(int64_t)b2x << 24) / det);
        o->vx = (int32_t)((-(int64_t)a2y << 24) / det);
        o->vy = (int32_t)(((int64_t)a2x << 24) / det);
        uint32_t gu = isqrt32((uint32_t)(((int64_t)o->ux * o->ux + (int64_t)o->uy * o->uy) >> 8));
        uint32_t gv = isqrt32((uint32_t)(((int64_t)o->vx * o->vx + (int64_t)o->vy * o->vy) >> 8));
        o->gu = gu ? (int32_t)((1u << 20) / gu) : 0;   /* |grad| is Q12: px per unit, Q8 */
        o->gv = gv ? (int32_t)((1u << 20) / gv) : 0;
        /* One pixel, in face units: how far anti-aliasing reaches past an edge. */
        o->mu = o->gu ? (int32_t)((1 << 24) / o->gu) : U1;
        o->mv = o->gv ? (int32_t)((1 << 24) / o->gv) : U1;
        /* Bevel band at least half a pixel wide: its inside never needs AA. */
        o->flat = (((U1 - BEVEL_START) >> 4) * o->gu >> 12) >= 128 &&
                  (((U1 - BEVEL_START) >> 4) * o->gv >> 12) >= 128;
        o->sil = (uint8_t)((az <= VIS_Z) | ((-az <= VIS_Z) << 1) | ((bz <= VIS_Z) << 2) | ((-bz <= VIS_Z) << 3));
        shade(nx, ny, nz, o->col, &o->lit);
        const int32_t e[4][3] = {{ax, ay, az}, {-ax, -ay, -az}, {bx, by, bz}, {-bx, -by, -bz}};
        for (int k = 0; k < 4; ++k) {
            int16_t mid[3];
            shade((nx + e[k][0]) * 11585 >> 14, (ny + e[k][1]) * 11585 >> 14,
                  (nz + e[k][2]) * 11585 >> 14, mid, NULL);
            for (int c = 0; c < 3; ++c) o->dcol[k][c] = (int16_t)(mid[c] - o->col[c]);
        }
        o->lu = (int16_t)((ax * LX + ay * LY + az * LZ) >> 14);
        o->lv = (int16_t)((bx * LX + by * LY + bz * LZ) >> 14);
    }
}

void nabo_dice_scene(nabo_dice_scene_t *scene, const nabo_dice_plan_t *p, int32_t t)
{
    memset(scene, 0, sizeof(*scene));
    scene->count = p->count;
    if (t < 0) t = 0;
    for (unsigned i = 0; i < p->count; ++i) {
        build_die(&scene->die[i], p, i, t);
        scene->order[i] = (uint8_t)i;
    }
    /* Further up the felt is further away: draw it first. */
    for (unsigned a = 1; a < p->count; ++a)
        for (unsigned b = a; b; --b) {
            const nabo_dice_die_t *x = &scene->die[scene->order[b - 1]], *y = &scene->die[scene->order[b]];
            if (x->shy <= y->shy) break;
            uint8_t k = scene->order[b]; scene->order[b] = scene->order[b - 1]; scene->order[b - 1] = k;
        }
}

bool nabo_dice_die_bounds(const nabo_dice_scene_t *scene, unsigned i, int *x0, int *y0, int *x1, int *y1)
{
    if (i >= scene->count) return false;
    const nabo_dice_die_t *d = &scene->die[i];
    *x0 = d->bx0 < d->sx0 ? d->bx0 : d->sx0;
    *y0 = d->by0 < d->sy0 ? d->by0 : d->sy0;
    *x1 = d->bx1 > d->sx1 ? d->bx1 : d->sx1;
    *y1 = d->by1 > d->sy1 ? d->by1 : d->sy1;
    return true;
}

bool nabo_dice_bounds(const nabo_dice_scene_t *scene, int *x0, int *y0, int *x1, int *y1)
{
    int ax = NABO_DICE_W, ay = NABO_DICE_H, bx = -1, by = -1;
    for (unsigned i = 0; i < scene->count; ++i) {
        const nabo_dice_die_t *d = &scene->die[i];
        if (d->bx0 < ax) ax = d->bx0;
        if (d->sx0 < ax) ax = d->sx0;
        if (d->by0 < ay) ay = d->by0;
        if (d->sy0 < ay) ay = d->sy0;
        if (d->bx1 > bx) bx = d->bx1;
        if (d->sx1 > bx) bx = d->sx1;
        if (d->by1 > by) by = d->by1;
        if (d->sy1 > by) by = d->sy1;
    }
    if (bx < ax || by < ay) return false;
    *x0 = ax; *y0 = ay; *x1 = bx; *y1 = by;
    return true;
}

/* ---------- pixels ---------- */

static void lerp3(int32_t c[3], const int16_t a[3], const int16_t b[3], int32_t t)   /* t Q8 */
{
    for (int i = 0; i < 3; ++i) c[i] = a[i] + ((b[i] - a[i]) * t >> 8);
}

/* Pixels this far inside the tray see only felt: no rim, wall shade or corner. */
#define FELT_INSET 24
#define VIG_X 11930                     /* Q16 / px^2: vignette ellipse */
#define VIG_Y 15500
#define VIG_CY 86

static inline __attribute__((always_inline)) void felt(int x, int y, int32_t vrow, int32_t c[3])
{
    int32_t dx = x - NABO_DICE_W / 2;
    int32_t v = (vrow + dx * dx * VIG_X) >> 16;                 /* Q12 */
    if (v > 4096) v = 4096;
    const int16_t *f = FELT[v >> 5];
    int32_t grain = GRAIN[((y & 15) << 4) | (x & 15)];
    c[0] = f[0] + grain; c[1] = f[1] + grain; c[2] = f[2] + grain;
}

/* Felt tray near its edge: wall shade, brass rim, black outside the corners. */
static void tray_edge(int x, int y, int32_t vrow, int32_t c[3])
{
    static const int16_t RIM_C[4][3] = {
        {64 * 16, 44 * 16, 20 * 16}, {232 * 16, 196 * 16, 124 * 16},
        {166 * 16, 124 * 16, 60 * 16}, {50 * 16, 34 * 16, 16 * 16}};
    const int32_t ex = (NABO_DICE_W / 2 - 1) * 16, ey = (NABO_DICE_H / 2 - 1) * 16;
    int32_t px = iabs((int32_t)(2 * x + 1 - NABO_DICE_W)) * 8;
    int32_t py = iabs((int32_t)(2 * y + 1 - NABO_DICE_H)) * 8;
    int32_t qx = px - (ex - TRAY_R * 16), qy = py - (ey - TRAY_R * 16);
    int32_t d;                                           /* signed distance, Q4 px */
    if (qx > 0 && qy > 0) d = (int32_t)isqrt32((uint32_t)(qx * qx + qy * qy)) - TRAY_R * 16;
    else d = (qx > qy ? qx : qy) - TRAY_R * 16;
    if (d >= 8) { c[0] = c[1] = c[2] = 0; return; }

    felt(x, y, vrow, c);
    int32_t depth = -d - RIM * 16;                               /* Q4 px into felt */
    if (depth < 13 * 16) {
        /* The rim's inner wall shades the felt next to it. */
        int32_t t = depth <= 0 ? 0 : depth * 256 / (13 * 16);
        int32_t f = 150 + (106 * t >> 8) * t / 256;
        for (int i = 0; i < 3; ++i) c[i] = c[i] * f >> 8;
    }
    if (depth >= 0) return;

    /* Rim: brass ridge, brighter on the lit upper edge. */
    int32_t across = -d;                                         /* 0 outer .. RIM*16 inner */
    if (across < 0) across = 0;
    int32_t t = across * 3 * 256 / (RIM * 16);
    int seg = t >> 8;
    if (seg > 2) seg = 2;
    int32_t rim[3];
    lerp3(rim, RIM_C[seg], RIM_C[seg + 1], t - (seg << 8));
    int32_t gain = 285 - y * 90 / NABO_DICE_H - x * 20 / NABO_DICE_W;
    for (int i = 0; i < 3; ++i) rim[i] = rim[i] * gain >> 8;
    int32_t blend = clamp(-depth * 256 / 16, 0, 256);            /* 1px soft join */
    for (int i = 0; i < 3; ++i) c[i] += (rim[i] - c[i]) * blend >> 8;
    if (d > -8) {                                                /* outer edge AA */
        int32_t cov = clamp((8 - d) * 16, 0, 256);
        for (int i = 0; i < 3; ++i) c[i] = c[i] * cov >> 8;
    }
}


/* Darken c with this face's pip ink where (u, v) falls in a pip. */
static inline __attribute__((always_inline)) void pip(const nabo_dice_face_t *f, int32_t u, int32_t v, int32_t c[3])
{
    int gx = u > PIP_SPLIT ? 2 : u < -PIP_SPLIT ? 0 : 1;
    int gy = v > PIP_SPLIT ? 2 : v < -PIP_SPLIT ? 0 : 1;
    if (PIPS[f->value] & (1u << (gy * 3 + gx))) {
        int32_t r = f->value == 1 ? PIP_R_ONE : PIP_R;
        int32_t pu = (u - (gx - 1) * PIP_STEP) >> 2, pv = (v - (gy - 1) * PIP_STEP) >> 2;   /* Q14 */
        int32_t d2 = pu * pu + pv * pv, outer = r + 1200;
        if (d2 < outer * outer) {
            int32_t inside = (r * r - d2) / (2 * r);                 /* Q14 units of u */
            int32_t px = (f->gu + f->gv) >> 1;
            int32_t pc = clamp((inside * px >> 14) + 128, 0, 256);
            if (pc) {
                bool red = f->value == 1 || f->value == 4;
                static const int16_t INK[2][3] = {{30 * 16, 30 * 16, 36 * 16}, {206 * 16, 26 * 16, 34 * 16}};
                /* A dimple: the wall nearer the light is in shade. */
                int32_t tilt = (pu * f->lu + pv * f->lv) >> 14;
                int32_t conc = clamp(256 - tilt * 110 / r, 150, 380);
                int32_t lit = f->lit * conc >> 8;
                for (int i = 0; i < 3; ++i) {
                    int32_t ink = INK[red][i] * lit >> 8;
                    c[i] += (ink - c[i]) * pc >> 8;
                }
            }
        }
    }
}

/* Colour of one die face pixel; returns coverage (Q8) or 0 if outside. */
static int32_t face_pixel(const nabo_dice_face_t *f, int32_t u, int32_t v, int32_t c[3])
{
    int32_t au = u < 0 ? -u : u, av = v < 0 ? -v : v;
    if (f->flat && au <= BEVEL_START && av <= BEVEL_START) {
        /* Inside the rounded edges: flat colour, fully covered. */
        c[0] = f->col[0]; c[1] = f->col[1]; c[2] = f->col[2];
        pip(f, u, v, c);
        return 256;
    }
    if (au > 2 * U1 || av > 2 * U1) return 0;
    int32_t du = ((U1 - au) >> 4) * f->gu >> 12;    /* Q8 px to the edge */
    int32_t dv = ((U1 - av) >> 4) * f->gv >> 12;
    if (du < -128 || dv < -128) return 0;
    int32_t cov = 256;
    if (f->sil & (u >= 0 ? 1 : 2)) cov = clamp(du + 128, 0, 256);
    if (f->sil & (v >= 0 ? 4 : 8)) cov = cov * clamp(dv + 128, 0, 256) >> 8;
    if (!cov) return 0;

    int32_t tu = au > BEVEL_START ? (au - BEVEL_START) * BEVEL_K >> 16 : 0;
    int32_t tv = av > BEVEL_START ? (av - BEVEL_START) * BEVEL_K >> 16 : 0;
    if (tu > 256) tu = 256;
    if (tv > 256) tv = 256;
    const int16_t *eu = f->dcol[u >= 0 ? 0 : 1], *ev = f->dcol[v >= 0 ? 2 : 3];
    for (int i = 0; i < 3; ++i) c[i] = f->col[i] + ((eu[i] * tu + ev[i] * tv) >> 8);

    pip(f, u, v, c);
    return cov;
}

/* Narrow [*lo, *hi] (die-box pixels) to where |w0 + k x| <= lim, plus one
 * pixel either side, so the per-pixel test only runs where a face can be. */
static void face_span(int32_t w0, int32_t k, int32_t lim, int32_t *lo, int32_t *hi)
{
    if (k == 0) {
        if (iabs(w0) > lim) { *lo = 1; *hi = 0; }
        return;
    }
    /* 32-bit: the C3 divides these in hardware; 64-bit division is a libcall. */
    int32_t a = (-lim - w0) / k, b = (lim - w0) / k;
    if (a > b) { int32_t t = a; a = b; b = t; }
    if (a - 1 > *lo) *lo = a - 1;
    if (b + 1 < *hi) *hi = b + 1;
}

static inline __attribute__((always_inline)) uint16_t pack(const int32_t c[3], int32_t b)
{
    int32_t r = clamp((c[0] + b * 8) >> 7, 0, 31);
    int32_t g = clamp((c[1] + b * 4) >> 6, 0, 63);
    int32_t bl = clamp((c[2] + b * 8) >> 7, 0, 31);
    return (uint16_t)(r << 11 | g << 5 | bl);
}

typedef struct {
    const nabo_dice_face_t *f;
    int32_t u0, v0, lo, hi, bx0;   /* u, v at x = bx0; face pixels lo..hi */
    int layer;                     /* draw order: higher is nearer */
} span_t;

/* One die-face pixel at x, or 0 coverage. */
static inline __attribute__((always_inline)) int32_t span_pixel(const span_t *s, int x, int32_t c[3])
{
    int32_t dx = x - s->bx0;
    return face_pixel(s->f, s->u0 + s->f->ux * dx, s->v0 + s->f->vx * dx, c);
}

void nabo_dice_draw_row(const nabo_dice_scene_t *scene, int y, int x0, int x1, uint16_t *dst)
{
    if (x0 < 0) x0 = 0;
    if (x1 >= NABO_DICE_W) x1 = NABO_DICE_W - 1;
    if (y < 0 || y >= NABO_DICE_H || x1 < x0) return;
    /* Per row, only the faces and shadows that touch it, nearest first. */
    span_t act[NABO_DICE_MAX * 3];
    const nabo_dice_die_t *shade_on[NABO_DICE_MAX];
    int n = 0, shades = 0;
    int32_t py = (y << 8) + 128, busy0 = NABO_DICE_W, busy1 = -1;
    for (unsigned i = 0; i < scene->count; ++i) {
        const nabo_dice_die_t *d = &scene->die[i];
        if (y >= d->sy0 && y <= d->sy1) {
            shade_on[shades++] = d;
            if (d->sx0 < busy0) busy0 = d->sx0;
            if (d->sx1 > busy1) busy1 = d->sx1;
        }
    }
    for (int o = (int)scene->count - 1; o >= 0; --o) {
        const nabo_dice_die_t *d = &scene->die[scene->order[o]];
        if (y < d->by0 || y > d->by1) continue;
        int32_t px = (d->bx0 << 8) + 128;
        for (unsigned k = 0; k < d->faces; ++k) {
            const nabo_dice_face_t *f = &d->face[k];
            int64_t ox = px - f->fcx, oy = py - f->fcy;
            span_t *s = &act[n];
            s->u0 = (int32_t)((f->ux * ox + f->uy * oy) >> 8);
            s->v0 = (int32_t)((f->vx * ox + f->vy * oy) >> 8);
            int32_t a = 0, b = d->bx1 - d->bx0;
            face_span(s->u0, f->ux, U1 + f->mu, &a, &b);
            face_span(s->v0, f->vx, U1 + f->mv, &a, &b);
            if (a > b) continue;
            s->f = f;
            s->bx0 = d->bx0;
            s->lo = d->bx0 + a;
            s->hi = d->bx0 + b;
            s->layer = o;
            if (s->lo < busy0) busy0 = s->lo;
            if (s->hi > busy1) busy1 = s->hi;
            ++n;
        }
    }
    const uint8_t *bayer = &BAYER[(y & 3) * 4];
    int32_t vrow = (y - VIG_CY) * (y - VIG_CY) * VIG_Y;
    bool inner_row = y >= FELT_INSET && y < NABO_DICE_H - FELT_INSET;
    for (int x = x0; x <= x1; ++x) {
        int32_t c[3], top[3], cover = 0;
        int hit = -1;
        bool busy = x >= busy0 && x <= busy1;
        if (busy) {
            /* Nearest first: an opaque face pixel needs no felt or shadow. */
            for (int j = 0; j < n; ++j) {
                if (x < act[j].lo || x > act[j].hi) continue;
                cover = span_pixel(&act[j], x, top);
                if (cover) { hit = j; break; }
            }
        }
        if (cover == 256) {
            c[0] = top[0]; c[1] = top[1]; c[2] = top[2];
        } else {
            if (inner_row && x >= FELT_INSET && x < NABO_DICE_W - FELT_INSET) felt(x, y, vrow, c);
            else tray_edge(x, y, vrow, c);
            if (busy) {
                for (int i = 0; i < shades; ++i) {
                    const nabo_dice_die_t *d = shade_on[i];
                    if (x < d->sx0 || x > d->sx1) continue;
                    int32_t nx = (((x << 8) + 128) - d->shx) * d->shrx >> 16;   /* Q12 */
                    int32_t ny = (py - d->shy) * d->shry >> 16;
                    int32_t q = (nx * nx + ny * ny) >> 12;
                    if (q >= 4096) continue;
                    int32_t f = 4096 - q;
                    int32_t a = d->sha * (f * f >> 12) >> 12;
                    for (int k = 0; k < 3; ++k) c[k] = c[k] * (256 - a) >> 8;
                }
                if (hit >= 0) {
                    /* Partly covered edge: blend dice beneath, farthest first,
                     * each with its first covering face, then the top one. */
                    for (int layer = 0; layer < act[hit].layer; ++layer) {
                        for (int j = hit + 1; j < n; ++j) {
                            if (act[j].layer != layer || x < act[j].lo || x > act[j].hi) continue;
                            int32_t fc[3], cov = span_pixel(&act[j], x, fc);
                            if (!cov) continue;
                            for (int k = 0; k < 3; ++k) c[k] += (fc[k] - c[k]) * cov >> 8;
                            break;
                        }
                    }
                    for (int k = 0; k < 3; ++k) c[k] += (top[k] - c[k]) * cover >> 8;
                }
            }
        }
        dst[x - x0] = pack(c, bayer[x & 3]);
    }
}

/* ---------- sound ---------- */

/* Damped resonators (Q14): a woody knock body, its overtones and a click. */
static const int16_t RES_A1[4] = {29399, 16476, -1273, 32020};
static const int16_t RES_A2[4] = {16291, 16199, 16046, 16316};
static const int16_t RES_GAIN[4] = {9, 12, 10, 4};

void nabo_dice_sound_init(nabo_dice_sound_t *s, const nabo_dice_plan_t *p, uint32_t start_frame)
{
    memset(s, 0, sizeof(*s));
    s->hits = (uint8_t)nabo_dice_impacts(p, s->hit, NABO_DICE_IMPACTS_MAX);
    s->noise = p->seed | 1u;
    s->at = start_frame;
    s->end = (uint32_t)(s->hits ? s->hit[s->hits - 1].ms : 0) * 16u + 4800u;
    while (s->next < s->hits && (uint32_t)s->hit[s->next].ms * 16u < start_frame) ++s->next;
}

size_t nabo_dice_sound_fill(nabo_dice_sound_t *s, int16_t *pcm, size_t n)
{
    if (s->at >= s->end) return 0;
    size_t out = 0;
    while (out < n && s->at < s->end) {
        while (s->next < s->hits && (uint32_t)s->hit[s->next].ms * 16u <= s->at) {
            s->burst = 64;                                      /* 4 ms of grit */
            s->burst_amp = s->hit[s->next].amp;
            ++s->next;
        }
        int32_t x = 0;
        if (s->burst > 0) {
            uint32_t r = next_random(&s->noise);
            int32_t env = s->burst_amp * s->burst / 64;         /* Q8 */
            x = ((int32_t)(r >> 20) - 2048) * env >> 4;         /* +-32K * amp */
            --s->burst;
        }
        int32_t mix = 0;
        for (int k = 0; k < 4; ++k) {
            int32_t y = ((RES_A1[k] * s->y[k][0] - RES_A2[k] * s->y[k][1]) >> 14) + (x >> 4);
            s->y[k][1] = s->y[k][0];
            s->y[k][0] = clamp(y, -(1 << 22), 1 << 22);
            mix += y * RES_GAIN[k];
        }
        mix = mix * 5 >> 6;
        /* Gentle soft clip keeps simultaneous landings from cracking. */
        if (mix > 24000) mix = 24000 + (mix - 24000) / 4;
        if (mix < -24000) mix = -24000 + (mix + 24000) / 4;
        pcm[out++] = (int16_t)clamp(mix, -32000, 32000);
        ++s->at;
    }
    return out;
}
