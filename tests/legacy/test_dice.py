"""Actual C dice: renderer, roll plan, synthesized clatter, core hand-off and page.

nabo_dice_render.c, nabo_dice.c and nabo_dice_ui.c are compiled unchanged with
small deterministic fakes for LVGL, FreeRTOS and esp_random. No hardware is used.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOWN, SELECT, UP, LEFT, RIGHT, BACK = range(6)
W, H = 224, 196


def compiler():
    cc = os.environ.get("CC") or shutil.which("gcc")
    if not cc and os.name == "nt":
        local = Path("gcc")
        if local.is_file(): cc = str(local)
    if not cc: raise RuntimeError("Set CC for the actual C dice tests")
    return cc


def build(tmp, name, sources, headers):
    for path, text in headers.items():
        (tmp / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp / path).write_text(text, encoding="utf-8")
    library = tmp / (name + (".dll" if os.name == "nt" else ".so"))
    result = subprocess.run([
        compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
        "-Wl,--export-all-symbols" if os.name == "nt" else "-fPIC",
        "-I", str(tmp), "-I", str(ROOT / "firmware"), *map(str, sources), "-o", str(library)
    ], capture_output=True, text=True)
    if result.returncode: raise RuntimeError(result.stderr)
    return ctypes.CDLL(str(library))


RENDER_HARNESS = r"""#include "nabo_dice_render.h"
#include <string.h>
static nabo_dice_plan_t plan;
static nabo_dice_scene_t scene;
static nabo_dice_sound_t sound;
int fixture_plan(unsigned seed, unsigned count) { nabo_dice_plan(&plan, seed, count); return plan.count; }
int fixture_value(int i) { return plan.value[i]; }
int fixture_end(void) { return plan.end_ms; }
int fixture_land(int i) { return plan.land_ms[i]; }
void fixture_scene(int t) { nabo_dice_scene(&scene, &plan, t); }
int fixture_bounds(int *out) { return nabo_dice_bounds(&scene, &out[0], &out[1], &out[2], &out[3]); }
int fixture_die_box(int i, int *out) {
    const nabo_dice_die_t *d = &scene.die[i];
    out[0]=d->bx0; out[1]=d->by0; out[2]=d->bx1; out[3]=d->by1; return d->faces;
}
/* The face drawn highest on screen is the one facing up. */
int fixture_top_value(int i) {
    const nabo_dice_die_t *d = &scene.die[i]; int best = -1; int top = 0x7fffffff;
    for (unsigned k = 0; k < d->faces; ++k) if (d->face[k].fcy < top) { top = d->face[k].fcy; best = d->face[k].value; }
    return best;
}
void fixture_row(int y, int x0, int x1, unsigned short *dst) { nabo_dice_draw_row(&scene, y, x0, x1, dst); }
int fixture_impacts(unsigned short *ms, unsigned short *amp, int cap) {
    nabo_dice_impact_t hit[32]; unsigned n = nabo_dice_impacts(&plan, hit, (unsigned)cap);
    for (unsigned i = 0; i < n; ++i) { ms[i] = hit[i].ms; amp[i] = hit[i].amp; } return (int)n;
}
long fixture_sound(unsigned start, short *pcm, long cap) {
    nabo_dice_sound_init(&sound, &plan, start); long n = 0; size_t k;
    while (n < cap && (k = nabo_dice_sound_fill(&sound, pcm + n, (size_t)(cap - n < 320 ? cap - n : 320)))) n += (long)k;
    return n;
}
"""


class DiceRenderActualC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nabo-dice-render-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        (tmp / "fixture.c").write_text(RENDER_HARNESS, encoding="utf-8")
        cls.lib = build(tmp, "dice-render", [tmp / "fixture.c", ROOT / "firmware/nabo_dice_render.c"], {})
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.fixture_sound.restype = ctypes.c_long
        cls.lib.fixture_sound.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_short), ctypes.c_long]

    def quad(self, fn, *args):
        out = (ctypes.c_int * 4)()
        result = fn(*args, out)
        return result, list(out)

    def row(self, y, x0=0, x1=W - 1):
        buf = (ctypes.c_ushort * W)()
        self.lib.fixture_row(y, x0, x1, buf)
        return list(buf[:x1 - x0 + 1])

    def test_values_are_deterministic_and_uniform(self):
        counts = [0] * 7
        for seed in range(1, 20001):
            self.lib.fixture_plan(seed, 3)
            for i in range(3): counts[self.lib.fixture_value(i)] += 1
        self.assertEqual(counts[0], 0)
        expected = 60000 / 6
        chi = sum((c - expected) ** 2 / expected for c in counts[1:])
        self.assertLess(chi, 20.5, counts)            # p ~ 0.001 for 5 dof
        self.lib.fixture_plan(1234, 2)
        first = [self.lib.fixture_value(i) for i in range(2)]
        self.lib.fixture_plan(1234, 2)
        self.assertEqual(first, [self.lib.fixture_value(i) for i in range(2)])

    def test_count_is_clamped(self):
        self.assertEqual(self.lib.fixture_plan(5, 0), 1)
        self.assertEqual(self.lib.fixture_plan(5, 9), 3)

    def test_every_resting_die_shows_its_value_on_top(self):
        for count in (1, 2, 3):
            for seed in range(1, 401):
                self.lib.fixture_plan(seed, count)
                self.lib.fixture_scene(self.lib.fixture_end())
                for i in range(count):
                    self.assertEqual(self.lib.fixture_top_value(i), self.lib.fixture_value(i), (seed, count, i))

    def test_roll_lands_quickly_and_stays_inside_the_tray(self):
        for count in (1, 2, 3):
            for seed in range(1, 121):
                self.lib.fixture_plan(seed, count)
                end = self.lib.fixture_end()
                self.assertTrue(700 <= end <= 1400, end)
                for t in range(0, end + 41, 40):
                    self.lib.fixture_scene(t)
                    ok, (x0, y0, x1, y1) = self.quad(self.lib.fixture_bounds)
                    self.assertTrue(ok)
                    self.assertTrue(0 <= x0 <= x1 < W and 0 <= y0 <= y1 < H)
                    for i in range(count):
                        faces, (a, b, c, d) = self.quad(self.lib.fixture_die_box, i)
                        self.assertTrue(1 <= faces <= 3)
                        # A die never leaves the felt (8 px rim and margin).
                        self.assertTrue(a >= 6 and c <= W - 7 and b >= 2 and d <= H - 7, (seed, count, t, a, b, c, d))

    def test_rest_is_still_and_matches_end_pose(self):
        self.lib.fixture_plan(77, 3)
        end = self.lib.fixture_end()
        self.lib.fixture_scene(end)
        rest = [self.row(y) for y in range(0, H, 7)]
        self.lib.fixture_scene(end + 5000)
        self.assertEqual(rest, [self.row(y) for y in range(0, H, 7)])

    def test_partial_rows_equal_slices_of_full_rows(self):
        self.lib.fixture_plan(2024, 3)
        for t in (0, 230, 480, 900, 5000):
            self.lib.fixture_scene(t)
            for y in range(0, H, 5):
                full = self.row(y)
                for x0, x1 in ((0, 0), (17, 90), (100, 223), (223, 223), (55, 56)):
                    self.assertEqual(self.row(y, x0, x1), full[x0:x1 + 1], (t, y, x0, x1))

    def test_out_of_range_requests_write_nothing(self):
        self.lib.fixture_plan(3, 1)
        self.lib.fixture_scene(100)
        buf = (ctypes.c_ushort * W)(*([0x1234] * W))
        for y, x0, x1 in ((-1, 0, 10), (H, 0, 10), (5, 30, 10)):
            self.lib.fixture_row(y, x0, x1, buf)
            self.assertEqual(set(buf), {0x1234})

    def test_tray_corners_are_black_and_felt_is_green(self):
        self.lib.fixture_plan(9, 1)
        self.lib.fixture_scene(5000)
        self.assertEqual(self.row(0, 0, 0), [0])
        self.assertEqual(self.row(H - 1, W - 1, W - 1), [0])
        felt = self.row(40, 40, 40)[0]
        r, g, b = felt >> 11, (felt >> 5) & 63, felt & 31
        self.assertTrue(g > 2 * r and g > b, hex(felt))

    def test_frame_update_area_stays_partial(self):
        for count in (1, 3):
            self.lib.fixture_plan(31, count)
            areas = []
            for t in range(0, self.lib.fixture_end(), 40):
                self.lib.fixture_scene(t)
                _, (x0, y0, x1, y1) = self.quad(self.lib.fixture_bounds)
                areas.append((x1 - x0 + 1) * (y1 - y0 + 1))
            self.assertLess(sum(areas) / len(areas), 0.75 * W * H, (count, max(areas)))

    def test_impacts_follow_the_bounces(self):
        ms = (ctypes.c_ushort * 32)(); amp = (ctypes.c_ushort * 32)()
        for count in (1, 2, 3):
            self.lib.fixture_plan(55, count)
            n = self.lib.fixture_impacts(ms, amp, 24)
            self.assertTrue(5 * count <= n <= 24)
            self.assertEqual(list(ms[:n]), sorted(ms[:n]))
            self.assertLessEqual(max(ms[:n]), self.lib.fixture_end())
            self.assertTrue(all(0 < a <= 256 for a in amp[:n]))
        self.lib.fixture_plan(55, 3)
        self.assertEqual(self.lib.fixture_impacts(ms, amp, 4), 4)

    def test_clatter_is_bounded_finite_and_deterministic(self):
        pcm = (ctypes.c_short * 64000)()
        for count in (1, 2, 3):
            self.lib.fixture_plan(4242, count)
            n = self.lib.fixture_sound(0, pcm, 64000)
            self.assertTrue(16000 * 0.8 < n < 16000 * 1.8, n)
            peak = max(abs(v) for v in pcm[:n])
            self.assertTrue(6000 < peak <= 32000, peak)
            self.assertEqual(max(abs(v) for v in pcm[n - 160:n]) < 400, True)
            first = list(pcm[:n])
            self.assertEqual(self.lib.fixture_sound(0, pcm, 64000), n)
            self.assertEqual(first, list(pcm[:n]))

    def test_late_start_skips_ahead_in_time(self):
        pcm = (ctypes.c_short * 64000)()
        self.lib.fixture_plan(4242, 1)
        whole = self.lib.fixture_sound(0, pcm, 64000)
        late = self.lib.fixture_sound(3200, pcm, 64000)
        self.assertEqual(whole - late, 3200)
        self.assertEqual(self.lib.fixture_sound(10 ** 6, pcm, 64000), 0)


LVGL_HEADER = r"""#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LV_OBJ_FLAG_HIDDEN 1u
#define LV_OBJ_FLAG_SCROLLABLE 2u
#define LV_OBJ_FLAG_CLICKABLE 4u
#define LV_OPA_COVER 255
#define LV_TEXT_ALIGN_CENTER 0
#define LV_ALIGN_TOP_MID 0
#define LV_ALIGN_BOTTOM_MID 1
#define LV_COLOR_FORMAT_RGB565 1
#define LV_STRIDE_AUTO 0
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COORD_MIN (-32768)
#define LV_FONT_MONTSERRAT_28 1
#define LV_SYMBOL_DOWN "v"
#define LV_SYMBOL_UP "^"
typedef int lv_result_t;
#define LV_RESULT_INVALID 0
#define LV_RESULT_OK 1
typedef int lv_font_t;
extern const lv_font_t lv_font_montserrat_28, lv_font_montserrat_20;
typedef uint32_t lv_color_t;
typedef struct { int32_t x1,y1,x2,y2; } lv_area_t;
typedef struct { int magic,cf,w,h,stride; } lv_image_header_t;
typedef struct { lv_image_header_t header; const uint8_t *data; } lv_image_dsc_t;
typedef struct { lv_image_header_t header; uint8_t *data; } lv_draw_buf_t;
typedef struct { const void *src; void *user_data; lv_draw_buf_t *decoded; } lv_image_decoder_dsc_t;
typedef struct lv_obj { unsigned flags; char text[96]; int x,y,w,h; const void *src; } lv_obj_t;
typedef struct lv_image_decoder lv_image_decoder_t;
typedef lv_result_t (*info_cb_t)(lv_image_decoder_t*,lv_image_decoder_dsc_t*,lv_image_header_t*);
typedef lv_result_t (*open_cb_t)(lv_image_decoder_t*,lv_image_decoder_dsc_t*);
typedef lv_result_t (*area_cb_t)(lv_image_decoder_t*,lv_image_decoder_dsc_t*,const lv_area_t*,lv_area_t*);
typedef void (*close_cb_t)(lv_image_decoder_t*,lv_image_decoder_dsc_t*);
struct lv_image_decoder { info_cb_t info; open_cb_t open; area_cb_t area; close_cb_t close; };
lv_obj_t *lv_obj_create(lv_obj_t*);
lv_obj_t *lv_label_create(lv_obj_t*);
lv_obj_t *lv_image_create(lv_obj_t*);
void lv_obj_remove_style_all(lv_obj_t*);
void lv_obj_set_size(lv_obj_t*,int,int);
void lv_obj_set_pos(lv_obj_t*,int,int);
void lv_obj_align(lv_obj_t*,int,int,int);
void lv_obj_set_style_bg_color(lv_obj_t*,lv_color_t,int);
void lv_obj_set_style_bg_opa(lv_obj_t*,int,int);
void lv_obj_set_style_text_font(lv_obj_t*,const lv_font_t*,int);
void lv_obj_set_style_text_color(lv_obj_t*,lv_color_t,int);
void lv_obj_set_style_text_align(lv_obj_t*,int,int);
void lv_obj_remove_flag(lv_obj_t*,unsigned);
void lv_obj_add_flag(lv_obj_t*,unsigned);
void lv_obj_move_foreground(lv_obj_t*);
void lv_obj_invalidate(lv_obj_t*);
void lv_obj_invalidate_area(lv_obj_t*,const lv_area_t*);
void lv_obj_get_coords(const lv_obj_t*,lv_area_t*);
void lv_label_set_text(lv_obj_t*,const char*);
void lv_image_set_src(lv_obj_t*,const void*);
lv_color_t lv_color_hex(uint32_t);
lv_color_t lv_color_black(void);
lv_draw_buf_t *lv_draw_buf_create(int,int,int,int);
lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t*,int,int,int,int);
void lv_draw_buf_destroy(lv_draw_buf_t*);
lv_image_decoder_t *lv_image_decoder_create(void);
void lv_image_decoder_set_info_cb(lv_image_decoder_t*,info_cb_t);
void lv_image_decoder_set_open_cb(lv_image_decoder_t*,open_cb_t);
void lv_image_decoder_set_get_area_cb(lv_image_decoder_t*,area_cb_t);
void lv_image_decoder_set_close_cb(lv_image_decoder_t*,close_cb_t);
"""

UI_HEADERS = {
    "lvgl.h": LVGL_HEADER,
    "src/draw/lv_image_decoder_private.h": '#pragma once\n#include "lvgl.h"\n',
    "muse_menu.h": """#pragma once
typedef enum { MUSE_MENU_DOWN, MUSE_MENU_SELECT, MUSE_MENU_UP,
    MUSE_MENU_LEFT, MUSE_MENU_RIGHT, MUSE_MENU_BACK } muse_menu_key_t;
""",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "esp_random.h": "#pragma once\n#include <stdint.h>\nuint32_t esp_random(void);\n",
    "esp_log.h": "#pragma once\n#define ESP_LOGI(tag, ...) ((void)(tag))\n",
    "freertos/FreeRTOS.h": """#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
""",
}

UI_HARNESS = r"""#include "lvgl.h"
#include "nabo_dice_ui.h"
#include "nabo_dice.h"
#include "nabo_dice_render.h"
#include <stdio.h>
#include <string.h>
const lv_font_t lv_font_montserrat_28 = 28, lv_font_montserrat_20 = 20;
static lv_obj_t objects[16], parent;
static int object_count, invalidates, area_invalidates, live_buffers, fail_alloc, created;
static lv_area_t last_area, union_area;
static lv_obj_t *sprite;
static lv_image_decoder_t decoder;
static lv_draw_buf_t buffers[4];
static uint16_t pixels[4][224 * 4];
static int used[4];
static lv_image_decoder_dsc_t sessions[3];
static int64_t clock_us;
static uint32_t next_seed = 1000;
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; return &objects[object_count++]; }
lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
lv_obj_t *lv_image_create(lv_obj_t *p) { sprite = lv_obj_create(p); return sprite; }
void lv_obj_remove_style_all(lv_obj_t *o) { (void)o; }
void lv_obj_set_size(lv_obj_t *o,int w,int h) { o->w=w; o->h=h; }
void lv_obj_set_pos(lv_obj_t *o,int x,int y) { o->x=x; o->y=y; }
void lv_obj_align(lv_obj_t *o,int a,int x,int y) { (void)a; o->x=x; o->y=y; }
void lv_obj_set_style_bg_color(lv_obj_t *o,lv_color_t c,int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_bg_opa(lv_obj_t *o,int v,int s) { (void)o; (void)v; (void)s; }
void lv_obj_set_style_text_font(lv_obj_t *o,const lv_font_t *f,int s) { (void)o; (void)f; (void)s; }
void lv_obj_set_style_text_color(lv_obj_t *o,lv_color_t c,int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_text_align(lv_obj_t *o,int a,int s) { (void)o; (void)a; (void)s; }
void lv_obj_remove_flag(lv_obj_t *o,unsigned f) { o->flags&=~f; }
void lv_obj_add_flag(lv_obj_t *o,unsigned f) { o->flags|=f; }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_invalidate(lv_obj_t *o) { (void)o; ++invalidates; }
void lv_obj_get_coords(const lv_obj_t *o,lv_area_t *a) { *a=(lv_area_t){o->x,o->y,o->x+o->w-1,o->y+o->h-1}; }
void lv_obj_invalidate_area(lv_obj_t *o,const lv_area_t *a) {
    (void)o; ++area_invalidates; last_area=*a;
    if (area_invalidates==1) union_area=*a;
    else {
        if(a->x1<union_area.x1)union_area.x1=a->x1;
        if(a->y1<union_area.y1)union_area.y1=a->y1;
        if(a->x2>union_area.x2)union_area.x2=a->x2;
        if(a->y2>union_area.y2)union_area.y2=a->y2;
    }
}
void lv_label_set_text(lv_obj_t *o,const char *v) { snprintf(o->text,sizeof(o->text),"%s",v); }
void lv_image_set_src(lv_obj_t *o,const void *src) { o->src=src; }
lv_color_t lv_color_hex(uint32_t c) { return c; }
lv_color_t lv_color_black(void) { return 0; }
lv_draw_buf_t *lv_draw_buf_create(int w,int h,int cf,int stride) {
    (void)stride; if (fail_alloc && created + 1 >= fail_alloc) { ++created; return NULL; }
    ++created;
    for (int i=0;i<4;++i) if(!used[i]) {
        if (w*h > 224*4) return NULL;
        used[i]=1; ++live_buffers;
        buffers[i].header=(lv_image_header_t){.w=w,.h=h,.cf=cf,.stride=w*2};
        buffers[i].data=(uint8_t*)pixels[i]; return &buffers[i];
    }
    return NULL;
}
lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t *b,int cf,int w,int h,int stride) {
    (void)stride; if(w*h>224*4)return NULL;
    b->header=(lv_image_header_t){.w=w,.h=h,.cf=cf,.stride=w*2}; return b;
}
void lv_draw_buf_destroy(lv_draw_buf_t *b) { for(int i=0;i<4;++i) if(b==&buffers[i] && used[i]) { used[i]=0; --live_buffers; } }
lv_image_decoder_t *lv_image_decoder_create(void) { return &decoder; }
void lv_image_decoder_set_info_cb(lv_image_decoder_t *d,info_cb_t f) { d->info=f; }
void lv_image_decoder_set_open_cb(lv_image_decoder_t *d,open_cb_t f) { d->open=f; }
void lv_image_decoder_set_get_area_cb(lv_image_decoder_t *d,area_cb_t f) { d->area=f; }
void lv_image_decoder_set_close_cb(lv_image_decoder_t *d,close_cb_t f) { d->close=f; }
int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { return next_seed++; }

void fixture_init(void) { static const lv_font_t font=16; nabo_dice_ui_build(&parent,&font); }
void fixture_reset(void) {
    for(int i=0;i<3;++i) if(sessions[i].user_data) decoder.close(&decoder,&sessions[i]);
    memset(sessions,0,sizeof(sessions));
    nabo_dice_ui_close();
    clock_us=0; invalidates=area_invalidates=fail_alloc=created=0;
}
void fixture_clock(int64_t us) { clock_us=us; nabo_dice_ui_tick(); }
void fixture_fail_alloc(int n) { fail_alloc=n; created=0; }
int fixture_live_buffers(void) { return live_buffers; }
int fixture_invalidates(void) { return invalidates; }
int fixture_area_invalidates(void) { return area_invalidates; }
void fixture_union(int *o) { o[0]=union_area.x1; o[1]=union_area.y1; o[2]=union_area.x2; o[3]=union_area.y2; }
void fixture_sprite(int *o) { o[0]=sprite->x; o[1]=sprite->y; o[2]=sprite->w; o[3]=sprite->h; }
const char *fixture_title(void) { return objects[1].text; }
const char *fixture_total(void) { return objects[3].text; }
const char *fixture_detail(void) { return objects[4].text; }
int fixture_panel_hidden(void) { return objects[0].flags & LV_OBJ_FLAG_HIDDEN; }
int fixture_take(unsigned *seed, unsigned *count) {
    uint32_t s; unsigned c; int64_t at; bool got = nabo_dice_take_throw(&s,&c,&at);
    *seed=s; *count=c; return got;
}
int fixture_status(char *out, int cap) { return (int)nabo_dice_status(out,(unsigned)cap); }
int fixture_open_session(int slot) { sessions[slot].src = sprite->src; return decoder.open(&decoder,&sessions[slot]); }
void fixture_close_session(int slot) { decoder.close(&decoder,&sessions[slot]); }
/* Decode an area through the page's decoder and compare to direct rendering. */
int fixture_decode(int slot, unsigned seed, unsigned count, int t, int x0, int y0, int x1, int y1) {
    nabo_dice_plan_t p; nabo_dice_scene_t s; nabo_dice_plan(&p, seed, count); nabo_dice_scene(&s, &p, t);
    lv_area_t full={x0,y0,x1,y1}, area={0,LV_COORD_MIN,0,0}; int rows=0; uint16_t want[224];
    while (decoder.area(&decoder,&sessions[slot],&full,&area)==LV_RESULT_OK) {
        if (area.x1!=x0 || area.x2!=x1 || area.y2-area.y1>=4) return -1;
        lv_draw_buf_t *b=sessions[slot].decoded;
        for (int y=area.y1;y<=area.y2;++y) {
            nabo_dice_draw_row(&s,y,x0,x1,want);
            if (memcmp(want,(uint16_t*)b->data+(y-area.y1)*(b->header.stride/2),(size_t)(x1-x0+1)*2)) return -2;
        }
        rows += area.y2-area.y1+1;
    }
    return rows;
}
"""


class DiceUIActualC(unittest.TestCase):
    """Each test loads its own copy of the library, so page state starts fresh."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nabo-dice-ui-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        (tmp / "fixture.c").write_text(UI_HARNESS, encoding="utf-8")
        first = build(tmp, "dice-ui", [tmp / "fixture.c", ROOT / "firmware/nabo_dice_ui.c",
                                       ROOT / "firmware/nabo_dice.c", ROOT / "firmware/nabo_dice_render.c"], UI_HEADERS)
        cls.built = Path(first._name)
        cls.copies = 0
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, first._handle)

    def setUp(self):
        cls = type(self)
        cls.copies += 1
        path = cls.built.with_name(f"dice-ui-{cls.copies}{cls.built.suffix}")
        shutil.copy2(cls.built, path)
        self.lib = lib = ctypes.CDLL(str(path))
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, lib._handle)
        lib.fixture_clock.argtypes = [ctypes.c_int64]
        for name in ("nabo_dice_ui_open", "nabo_dice_ui_key", "nabo_dice_ui_active", "nabo_dice_wanted"):
            getattr(lib, name).restype = ctypes.c_bool
        lib.nabo_dice_ui_key.argtypes = [ctypes.c_int]
        for name in ("fixture_title", "fixture_total", "fixture_detail"):
            getattr(lib, name).restype = ctypes.c_char_p
        lib.fixture_init()

    def text(self, name): return getattr(self.lib, "fixture_" + name)().decode("utf-8")
    def key(self, key): return self.lib.nabo_dice_ui_key(key)
    def take(self):
        seed, count = ctypes.c_uint(), ctypes.c_uint()
        return self.lib.fixture_take(ctypes.byref(seed), ctypes.byref(count)), seed.value, count.value
    def settle(self, start=0):
        for t in range(start, start + 1600001, 40000): self.lib.fixture_clock(t)

    def test_open_allocates_strips_only_while_visible(self):
        self.assertEqual(self.lib.fixture_live_buffers(), 0)
        self.assertTrue(self.lib.nabo_dice_ui_open())
        self.assertEqual(self.lib.fixture_live_buffers(), 2)
        self.assertTrue(self.lib.nabo_dice_wanted())
        self.assertFalse(self.lib.fixture_panel_hidden())
        self.assertEqual(self.text("detail"), "按 OK 摇一摇")
        self.assertEqual(self.lib.fixture_invalidates(), 1)
        self.lib.nabo_dice_ui_close()
        self.assertEqual(self.lib.fixture_live_buffers(), 0)
        self.assertFalse(self.lib.nabo_dice_wanted())
        self.assertTrue(self.lib.fixture_panel_hidden())

    def test_failed_allocation_leaves_everything_closed(self):
        self.lib.fixture_fail_alloc(2)
        self.assertFalse(self.lib.nabo_dice_ui_open())
        self.assertEqual(self.lib.fixture_live_buffers(), 0)
        self.assertFalse(self.lib.nabo_dice_wanted())
        self.assertFalse(self.lib.nabo_dice_ui_active())
        self.assertFalse(self.key(SELECT))

    def test_ok_throws_once_and_sound_gets_the_same_roll(self):
        self.assertTrue(self.lib.nabo_dice_ui_open())
        self.assertFalse(self.take()[0])
        self.assertTrue(self.key(SELECT))
        got, seed, count = self.take()
        self.assertTrue(got)
        self.assertEqual(count, 1)
        self.assertFalse(self.take()[0])
        self.assertEqual(self.text("total"), "")
        self.settle()
        self.assertTrue(self.text("total").isdigit())
        self.assertEqual(self.text("detail"), "点")

    def test_keys_wait_for_landing_and_up_back_left_return(self):
        self.lib.nabo_dice_ui_open()
        self.key(SELECT)
        self.take()
        self.lib.fixture_clock(300000)
        self.assertTrue(self.key(SELECT))
        self.assertTrue(self.key(DOWN))
        self.assertFalse(self.take()[0])
        self.assertIn("1 颗", self.text("title"))
        for key in (UP, BACK, LEFT): self.assertFalse(self.key(key))

    def test_down_cycles_one_two_three_and_throws(self):
        self.lib.nabo_dice_ui_open()
        clock = 0
        for want in (2, 3, 1, 2):
            self.assertTrue(self.key(DOWN))
            got, _, count = self.take()
            self.assertTrue(got)
            self.assertEqual(count, want)
            self.assertIn(f"{want} 颗", self.text("title"))
            self.settle(clock)
            clock += 2000000
            self.lib.fixture_clock(clock)
            total = self.text("total")
            detail = self.text("detail")
            if want == 1:
                self.assertEqual(detail, "点")
            else:
                parts = [int(v) for v in detail.split()[0:want * 2:2]]
                self.assertEqual(sum(parts), int(total))

    def test_each_frame_invalidates_only_inside_the_tray(self):
        self.lib.nabo_dice_ui_open()
        self.key(DOWN)
        self.key(DOWN)
        self.settle()
        self.assertGreater(self.lib.fixture_area_invalidates(), 20)
        area = (ctypes.c_int * 4)(); sprite = (ctypes.c_int * 4)()
        self.lib.fixture_union(area); self.lib.fixture_sprite(sprite)
        x, y, w, h = sprite
        self.assertTrue(x <= area[0] <= area[2] < x + w and y <= area[1] <= area[3] < y + h)

    def test_leaving_mid_roll_keeps_the_result_for_next_visit(self):
        self.lib.nabo_dice_ui_open()
        self.key(SELECT)
        self.lib.fixture_clock(200000)
        self.lib.nabo_dice_ui_close()
        buf = ctypes.create_string_buffer(96)
        self.lib.fixture_status(buf, 96)
        self.assertIn('"wanted":false', buf.value.decode())
        self.assertIn('"rolls":', buf.value.decode())
        self.assertTrue(self.lib.nabo_dice_ui_open())
        self.assertTrue(self.text("total").isdigit())
        self.assertEqual(self.lib.fixture_live_buffers(), 2)

    def test_decoder_streams_exact_rendered_strips(self):
        self.lib.nabo_dice_ui_open()
        self.key(SELECT)
        got, seed, count = self.take()
        self.lib.fixture_clock(360000)
        self.assertEqual(self.lib.fixture_open_session(0), 1)
        self.assertEqual(self.lib.fixture_open_session(1), 1)
        self.assertEqual(self.lib.fixture_open_session(2), 0)      # two strips only
        self.assertEqual(self.lib.fixture_decode(0, seed, count, 360, 10, 20, 200, 39), 20)
        self.assertEqual(self.lib.fixture_decode(1, seed, count, 360, 0, 190, 223, 195), 6)
        self.assertEqual(self.lib.fixture_decode(0, seed, count, 360, 0, 0, 224, 3), 0)  # outside
        self.lib.fixture_close_session(0)
        self.lib.fixture_close_session(1)


if __name__ == "__main__":
    unittest.main()
