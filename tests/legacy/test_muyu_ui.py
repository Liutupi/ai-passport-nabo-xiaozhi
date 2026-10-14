"""Actual C woodfish UI: edge requests, timing, lifetime, clipped strip decoder.

LVGL is represented by a small deterministic object/decoder fake. The UI C is
compiled unchanged and calls a core fake; no hardware, NVS, or audio is used.
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
typedef int lv_result_t;
#define LV_RESULT_INVALID 0
#define LV_RESULT_OK 1
typedef int lv_font_t;
typedef uint32_t lv_color_t;
typedef struct { int32_t x1,y1,x2,y2; } lv_area_t;
typedef struct { int magic,cf,w,h,stride; } lv_image_header_t;
typedef struct { lv_image_header_t header; const uint8_t *data; } lv_image_dsc_t;
typedef struct { lv_image_header_t header; uint8_t *data; } lv_draw_buf_t;
typedef struct { const void *src; void *user_data; lv_draw_buf_t *decoded; } lv_image_decoder_dsc_t;
typedef struct lv_obj { unsigned flags; char text[256]; int x,y,w,h; const void *src; } lv_obj_t;
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
void lv_obj_set_style_text_line_space(lv_obj_t*,int,int);
void lv_obj_remove_flag(lv_obj_t*,unsigned);
void lv_obj_add_flag(lv_obj_t*,unsigned);
void lv_obj_set_flag(lv_obj_t*,unsigned,bool);
void lv_obj_move_foreground(lv_obj_t*);
void lv_obj_invalidate(lv_obj_t*);
void lv_label_set_text(lv_obj_t*,const char*);
void lv_image_set_src(lv_obj_t*,const void*);
lv_color_t lv_color_hex(uint32_t);
lv_color_t lv_color_black(void);
lv_draw_buf_t *lv_draw_buf_create(int,int,int,int);
lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t*,int,int,int,int);
lv_image_decoder_t *lv_image_decoder_create(void);
void lv_image_decoder_set_info_cb(lv_image_decoder_t*,info_cb_t);
void lv_image_decoder_set_open_cb(lv_image_decoder_t*,open_cb_t);
void lv_image_decoder_set_get_area_cb(lv_image_decoder_t*,area_cb_t);
void lv_image_decoder_set_close_cb(lv_image_decoder_t*,close_cb_t);
"""

HEADERS = {
    "lvgl.h": LVGL_HEADER,
    "src/draw/lv_image_decoder_private.h": '#pragma once\n#include "lvgl.h"\n',
    "muse_menu.h": """#pragma once
typedef enum { MUSE_MENU_DOWN, MUSE_MENU_SELECT, MUSE_MENU_UP,
    MUSE_MENU_LEFT, MUSE_MENU_RIGHT, MUSE_MENU_BACK } muse_menu_key_t;
""",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
}

HARNESS = r"""#include "lvgl.h"
#include "nabo_muyu_ui.h"
#include "nabo_muyu.h"
#include "nabo_muyu_assets.h"
#include "nabo_muyu_codec.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t objects[16], parent;
static int object_count, foregrounds, image_updates, invalidates;
static const lv_image_dsc_t *base_source;
static lv_obj_t *sprite;
static lv_image_decoder_t decoder;
static lv_draw_buf_t buffers[2];
static uint8_t pixels[2][1056];
static int buffer_count, buffer_bytes, codec_fail;
static lv_image_decoder_dsc_t sessions[3];
static int64_t clock_us;
static uint64_t merit;
static bool wanted, sound, accept;
static int opens, closes, hits, sound_sets;
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; return &objects[object_count++]; }
lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
lv_obj_t *lv_image_create(lv_obj_t *p) { sprite=lv_obj_create(p); return sprite; }
void lv_obj_remove_style_all(lv_obj_t *o) { (void)o; }
void lv_obj_set_size(lv_obj_t *o,int w,int h) { o->w=w; o->h=h; }
void lv_obj_set_pos(lv_obj_t *o,int x,int y) { o->x=x; o->y=y; }
void lv_obj_align(lv_obj_t *o,int a,int x,int y) { (void)a; o->x=x; o->y=y; }
void lv_obj_set_style_bg_color(lv_obj_t *o,lv_color_t c,int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_bg_opa(lv_obj_t *o,int v,int s) { (void)o; (void)v; (void)s; }
void lv_obj_set_style_text_font(lv_obj_t *o,const lv_font_t *f,int s) { (void)o; (void)f; (void)s; }
void lv_obj_set_style_text_color(lv_obj_t *o,lv_color_t c,int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_text_align(lv_obj_t *o,int a,int s) { (void)o; (void)a; (void)s; }
void lv_obj_set_style_text_line_space(lv_obj_t *o,int a,int s) { (void)o; (void)a; (void)s; }
void lv_obj_remove_flag(lv_obj_t *o,unsigned f) { o->flags&=~f; }
void lv_obj_add_flag(lv_obj_t *o,unsigned f) { o->flags|=f; }
void lv_obj_set_flag(lv_obj_t *o,unsigned f,bool set) { if(set)o->flags|=f;else o->flags&=~f; }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; ++foregrounds; }
void lv_obj_invalidate(lv_obj_t *o) { (void)o; ++invalidates; }
void lv_label_set_text(lv_obj_t *o,const char *v) { snprintf(o->text,sizeof(o->text),"%s",v); }
void lv_image_set_src(lv_obj_t *o,const void *src) {
    o->src=src; if(!base_source)base_source=src; ++image_updates;
}
lv_color_t lv_color_hex(uint32_t c) { return c; }
lv_color_t lv_color_black(void) { return 0; }
lv_draw_buf_t *lv_draw_buf_create(int w,int h,int cf,int stride) {
    (void)stride; if(buffer_count>=2 || w*h*2>1024)return NULL;
    int i=buffer_count++;
    buffers[i].header=(lv_image_header_t){.w=w,.h=h,.cf=cf,.stride=w*2};
    buffers[i].data=pixels[i]; memset(pixels[i],0xcc,sizeof(pixels[i]));
    buffer_bytes+=w*h*2; return &buffers[i];
}
lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t *b,int cf,int w,int h,int stride) {
    (void)stride; if(w*h*2>1024)return NULL;
    b->header=(lv_image_header_t){.w=w,.h=h,.cf=cf,.stride=w*2}; return b;
}
lv_image_decoder_t *lv_image_decoder_create(void) { return &decoder; }
void lv_image_decoder_set_info_cb(lv_image_decoder_t *d,info_cb_t f) { d->info=f; }
void lv_image_decoder_set_open_cb(lv_image_decoder_t *d,open_cb_t f) { d->open=f; }
void lv_image_decoder_set_get_area_cb(lv_image_decoder_t *d,area_cb_t f) { d->area=f; }
void lv_image_decoder_set_close_cb(lv_image_decoder_t *d,close_cb_t f) { d->close=f; }
bool nabo_muyu_draw_row(unsigned f,unsigned y,int x0,int x1,uint16_t *dst,size_t cap) {
    if(codec_fail || f>=6 || y>=128 || x0<0 || x1>127 || x1<x0 || cap<(size_t)(x1-x0+1))return false;
    for(int x=x0;x<=x1;++x)dst[x-x0]=(uint16_t)(f*10000+y*128+(unsigned)x);
    return true;
}
int64_t esp_timer_get_time(void) { return clock_us; }
void nabo_muyu_init(void) {}
void nabo_muyu_open(void) { ++opens; wanted=true; }
void nabo_muyu_close(void) { ++closes; wanted=false; }
bool nabo_muyu_wanted(void) { return wanted; }
uint64_t nabo_muyu_count(void) { return merit; }
bool nabo_muyu_hit(void) { if(!wanted || !accept)return false; ++hits; ++merit; return true; }
bool nabo_muyu_sound_enabled(void) { return sound; }
void nabo_muyu_set_sound(bool enabled) { ++sound_sets; sound=enabled; }
bool nabo_muyu_take_hit(void) { return false; }
void nabo_muyu_idle_tick(void) {}
void nabo_muyu_save(void) {}

void fixture_init(void) { static const lv_font_t font=0; nabo_muyu_ui_build(&parent,&font); }
void fixture_reset(void) {
    for(int i=0;i<3;++i)if(sessions[i].user_data)decoder.close(&decoder,&sessions[i]);
    nabo_muyu_ui_close();
    clock_us=0; merit=0; wanted=false; sound=true; accept=true;
    opens=closes=hits=sound_sets=foregrounds=image_updates=invalidates=codec_fail=0;
    memset(sessions,0,sizeof(sessions));
}
void fixture_clock(int64_t us) { clock_us=us; }
void fixture_merit(uint64_t n) { merit=n; }
void fixture_accept(int n) { accept=n; }
void fixture_codec_fail(int n) { codec_fail=n; }
int fixture_frame(void) { return (int)((const lv_image_dsc_t*)sprite->src-base_source); }
int fixture_hits(void) { return hits; }
int fixture_opens(void) { return opens; }
int fixture_closes(void) { return closes; }
int fixture_sound_sets(void) { return sound_sets; }
int fixture_buffer_bytes(void) { return buffer_bytes; }
int fixture_updates(void) { return image_updates; }
int fixture_feedback_visible(void) { return !(objects[4].flags&LV_OBJ_FLAG_HIDDEN); }
const char *fixture_count_text(void) { return objects[2].text; }
const char *fixture_sound_text(void) { return objects[5].text; }
int fixture_info(int valid) {
    lv_image_decoder_dsc_t d={.src=valid?sprite->src:&parent}; lv_image_header_t h;
    return decoder.info(&decoder,&d,&h)==LV_RESULT_OK && h.w==128 && h.h==128;
}
int fixture_decoder_open(int slot) {
    sessions[slot].src=sprite->src;
    return decoder.open(&decoder,&sessions[slot]);
}
void fixture_decoder_close(int slot) { decoder.close(&decoder,&sessions[slot]); }
int fixture_decode(int slot,int x0,int y0,int x1,int y1,int frame,int expect_black) {
    lv_area_t full={x0,y0,x1,y1},area={0,LV_COORD_MIN,0,0}; int rows=0;
    while(decoder.area(&decoder,&sessions[slot],&full,&area)==LV_RESULT_OK) {
        if(area.x1!=x0 || area.x2!=x1 || area.y2-area.y1>=4)return -1;
        lv_draw_buf_t *buf=sessions[slot].decoded;
        for(int y=area.y1;y<=area.y2;++y)for(int x=x0;x<=x1;++x) {
            uint16_t got=((uint16_t*)buf->data)[(y-area.y1)*(buf->header.stride/2)+x-x0];
            uint16_t expected=expect_black?0:(uint16_t)(frame*10000+y*128+x);
            if(got!=expected)return -2;
        }
        rows+=area.y2-area.y1+1;
    }
    for(int i=0;i<2;++i)for(int j=1024;j<1056;++j)if(pixels[i][j]!=0xcc)return -3;
    return rows;
}
"""


class MuyuUIActualC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nabo-muyu-ui-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        for name, source in HEADERS.items():
            (tmp / name).parent.mkdir(parents=True, exist_ok=True)
            (tmp / name).write_text(source, encoding="utf-8")
        c = tmp / "fixture.c"
        c.write_text(HARNESS, encoding="utf-8")
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and os.name == "nt":
            local = Path("gcc")
            if local.is_file(): compiler = str(local)
        if not compiler: raise RuntimeError("Set CC for the actual C woodfish UI tests")
        library = tmp / ("muyu-ui.dll" if os.name == "nt" else "muyu-ui.so")
        result = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
            "-Wl,--export-all-symbols" if os.name == "nt" else "-fPIC",
            "-I", str(tmp), "-I", str(ROOT / "firmware"), str(c),
            str(ROOT / "firmware/nabo_muyu_ui.c"), "-o", str(library)
        ], capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stderr)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.fixture_clock.argtypes = [ctypes.c_int64]
        cls.lib.fixture_merit.argtypes = [ctypes.c_uint64]
        cls.lib.nabo_muyu_count.restype = ctypes.c_uint64
        cls.lib.nabo_muyu_ui_open.restype = ctypes.c_bool
        cls.lib.nabo_muyu_ui_key.argtypes = [ctypes.c_int]
        cls.lib.nabo_muyu_ui_key.restype = ctypes.c_bool
        cls.lib.nabo_muyu_ui_active.restype = ctypes.c_bool
        cls.lib.fixture_count_text.restype = ctypes.c_char_p
        cls.lib.fixture_sound_text.restype = ctypes.c_char_p
        cls.lib.fixture_init()

    def setUp(self):
        self.lib.fixture_reset()
        self.assertTrue(self.lib.nabo_muyu_ui_open())

    def text(self): return self.lib.fixture_count_text().decode("utf-8")
    def key(self, key): return self.lib.nabo_muyu_ui_key(key)
    def clock(self, us):
        self.lib.fixture_clock(us)
        self.lib.nabo_muyu_ui_tick()

    def test_open_loads_count_idle_frame_and_sound_without_automatic_hit(self):
        self.assertEqual(self.text(), "累计功德\n0")
        self.assertEqual(self.lib.fixture_frame(), 0)
        self.assertEqual(self.lib.fixture_hits(), 0)
        self.assertEqual(self.lib.fixture_sound_text().decode(), "音效：开")
        self.assertTrue(self.lib.nabo_muyu_ui_active())

    def test_one_edge_adds_once_and_hold_time_does_not_repeat(self):
        self.assertTrue(self.key(SELECT))
        self.assertEqual(self.text(), "累计功德\n1")
        for step in range(1, 101): self.clock(step * 50000)
        self.assertEqual(self.lib.nabo_muyu_count(), 1)
        self.assertEqual(self.lib.fixture_hits(), 1)

    def test_every_new_edge_counts_and_restarts_animation(self):
        self.key(SELECT)
        self.clock(160000)
        self.assertEqual(self.lib.fixture_frame(), 4)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_frame(), 1)
        self.assertEqual(self.lib.nabo_muyu_count(), 2)
        self.clock(160001)
        self.assertEqual(self.lib.nabo_muyu_count(), 2)

    def test_animation_uses_five_hit_frames_then_idle_and_ends_feedback(self):
        self.key(SELECT)
        for frame in range(1, 6):
            self.clock((frame - 1) * 50000)
            self.assertEqual(self.lib.fixture_frame(), frame)
        self.clock(250000)
        self.assertEqual(self.lib.fixture_frame(), 0)
        self.assertTrue(self.lib.fixture_feedback_visible())
        self.clock(450000)
        self.assertFalse(self.lib.fixture_feedback_visible())

    def test_down_toggles_independent_sound_without_counting(self):
        self.key(DOWN)
        self.assertEqual(self.lib.fixture_sound_text().decode(), "音效：关")
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_muyu_count(), 1)
        self.key(DOWN)
        self.assertEqual(self.lib.fixture_sound_text().decode(), "音效：开")
        self.assertEqual(self.lib.fixture_sound_sets(), 2)

    def test_up_back_left_request_return_without_hit_or_sound_change(self):
        for key in (UP, BACK, LEFT): self.assertFalse(self.key(key))
        self.assertEqual(self.lib.fixture_hits(), 0)
        self.assertEqual(self.lib.fixture_sound_sets(), 0)

    def test_close_is_idempotent_and_inactive_keys_do_nothing(self):
        self.lib.nabo_muyu_ui_close()
        self.lib.nabo_muyu_ui_close()
        self.assertEqual(self.lib.fixture_closes(), 1)
        self.assertFalse(self.lib.nabo_muyu_ui_active())
        self.assertFalse(self.key(SELECT))
        self.assertEqual(self.lib.fixture_hits(), 0)

    def test_reopen_keeps_core_count_but_resets_animation(self):
        self.key(SELECT)
        self.lib.nabo_muyu_ui_close()
        self.assertTrue(self.lib.nabo_muyu_ui_open())
        self.assertEqual(self.text(), "累计功德\n1")
        self.assertEqual(self.lib.fixture_frame(), 0)
        self.assertFalse(self.lib.fixture_feedback_visible())

    def test_full_64bit_counter_including_maximum_displays_exactly(self):
        for count in (2**32 + 7, 2**63 + 9, 2**64 - 1):
            self.lib.fixture_merit(count)
            self.lib.nabo_muyu_ui_close()
            self.lib.nabo_muyu_ui_open()
            self.assertEqual(self.text(), f"累计功德\n{count}")

    def test_rejected_core_hit_does_not_animate_or_show_plus_one(self):
        self.lib.fixture_accept(0)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_frame(), 0)
        self.assertFalse(self.lib.fixture_feedback_visible())
        self.assertEqual(self.lib.nabo_muyu_count(), 0)

    def test_idle_ticks_do_not_repeatedly_redraw_sprite(self):
        updates = self.lib.fixture_updates()
        for i in range(100): self.clock(i * 10000)
        self.assertEqual(self.lib.fixture_updates(), updates)
        self.assertEqual(self.lib.fixture_buffer_bytes(), 2048)

    def test_decoder_accepts_only_own_sources_and_caps_simultaneous_strips(self):
        self.assertTrue(self.lib.fixture_info(1))
        self.assertFalse(self.lib.fixture_info(0))
        self.assertTrue(self.lib.fixture_decoder_open(0))
        self.assertTrue(self.lib.fixture_decoder_open(1))
        self.assertFalse(self.lib.fixture_decoder_open(2))
        self.lib.fixture_decoder_close(0)
        self.assertTrue(self.lib.fixture_decoder_open(2))
        self.lib.fixture_decoder_close(1)
        self.lib.fixture_decoder_close(2)

    def test_decoder_streams_full_frame_in_four_row_strips(self):
        self.assertTrue(self.lib.fixture_decoder_open(0))
        self.assertEqual(self.lib.fixture_decode(0, 0, 0, 127, 127, 0, 0), 128)
        self.lib.fixture_decoder_close(0)

    def test_decoder_clips_to_partial_rectangle_and_pins_open_frame(self):
        self.assertTrue(self.lib.fixture_decoder_open(0))
        self.key(SELECT)
        self.assertTrue(self.lib.fixture_decoder_open(1))
        self.clock(150000)
        self.assertEqual(self.lib.fixture_decode(0, 31, 3, 87, 19, 0, 0), 17)
        self.assertEqual(self.lib.fixture_decode(1, 110, 123, 127, 127, 1, 0), 5)
        self.lib.fixture_decoder_close(0)
        self.lib.fixture_decoder_close(1)

    def test_failed_codec_row_is_black_and_invalid_rectangle_is_rejected(self):
        self.assertTrue(self.lib.fixture_decoder_open(0))
        self.lib.fixture_codec_fail(1)
        self.assertEqual(self.lib.fixture_decode(0, 0, 0, 127, 4, 0, 1), 5)
        self.assertEqual(self.lib.fixture_decode(0, -1, 0, 127, 4, 0, 1), 0)
        self.assertEqual(self.lib.fixture_decode(0, 0, 0, 128, 4, 0, 1), 0)
        self.lib.fixture_decoder_close(0)


class MuyuUIInstallation(unittest.TestCase):
    def test_menu_indices_edge_keys_timeout_and_idempotence(self):
        sdk = Path(os.environ.get("NABO_TEST_SDK", str(ROOT.parent / "muse-gadget-sdk")))
        baseline = subprocess.run([
            "git", "-C", str(sdk), "show", "812c46f9fe42b69d916613a9dc702cbac5ff7b54:esp32/components/muse/muse_menu.c"
        ], capture_output=True, check=True).stdout.decode("utf-8")
        with tempfile.TemporaryDirectory(prefix="nabo-muyu-install-") as directory:
            esp = Path(directory) / "esp32"
            component = esp / "components/muse"
            component.mkdir(parents=True)
            menu = component / "muse_menu.c"
            menu.write_text(baseline, encoding="utf-8")
            (component / "CMakeLists.txt").write_text(
                '        list(APPEND srcs "nabo_pcm_cache.c")\n'
                '    bt esp-tls mbedtls esp_app_format esp_http_client\n', encoding="utf-8")

            def replace(path, before, after):
                value = path.read_text(encoding="utf-8")
                if after in value: return
                self.assertEqual(value.count(before), 1, before)
                path.write_text(value.replace(before, after), encoding="utf-8")

            install_wifi_menu(ROOT, esp, replace)
            install_radio_ui(ROOT, esp, replace)
            install_muyu_ui(ROOT, esp, replace)
            first = menu.read_bytes()
            install_muyu_ui(ROOT, esp, replace)
            self.assertEqual(menu.read_bytes(), first)
            value = menu.read_text(encoding="utf-8")
            enum = value.split("typedef enum {", 1)[1].split("} item_t;", 1)[0]
            items = [line.strip().rstrip(",") for line in enum.splitlines() if line.strip().startswith("ITEM_")]
            self.assertEqual(items.index("ITEM_PHONE"), 5)
            self.assertEqual(items.index("ITEM_WIFI"), 6)
            self.assertEqual(items.index("ITEM_WIFI_ADD"), 7)
            self.assertEqual(items.index("ITEM_RADIO"), 8)
            self.assertEqual(items.index("ITEM_MUYU"), 9)
            self.assertIn('    [ITEM_MUYU] = "功德木鱼",', value)
            self.assertIn("if (!nabo_muyu_ui_key(key)) show(VIEW_LIST);", value)
            self.assertIn("s_last_key = now;\n        muse_state_poke();\n        nabo_muyu_ui_tick();", value)
            self.assertLess(value.index("nabo_muyu_ui_tick();"), value.index("if (now - s_last_key > IDLE_CLOSE_S)"))
            self.assertIn("if (s_view == VIEW_MUYU) nabo_muyu_ui_close();", value)
            for name in ("nabo_muyu_ui.c", "nabo_muyu_ui.h"):
                self.assertEqual((component / name).read_bytes(), (ROOT / "firmware" / name).read_bytes())


if __name__ == "__main__": unittest.main()
