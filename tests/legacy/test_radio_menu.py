"""Compile the production radio menu C and exercise three-button navigation.

The fakes record core requests; no audio, Wi-Fi, NVS, credentials, or serial
ports are used. Installer tests run on a temporary pristine SDK menu only.
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

HEADERS = {
    "muse_menu.h": """#pragma once
#include <stdbool.h>
typedef enum { MUSE_MENU_DOWN, MUSE_MENU_SELECT, MUSE_MENU_UP,
    MUSE_MENU_LEFT, MUSE_MENU_RIGHT, MUSE_MENU_BACK } muse_menu_key_t;
""",
    "nabo_radio.h": """#pragma once
#include <stdbool.h>
int nabo_radio_station_count(void);
const char *nabo_radio_station_name(int);
int nabo_radio_station_category(int);
int nabo_radio_category_count(void);
const char *nabo_radio_category_name(int);
int nabo_radio_current_station(void);
bool nabo_radio_favorite(int);
void nabo_radio_toggle_favorite(int);
void nabo_radio_select_station(int);
void nabo_radio_toggle_play(void);
void nabo_radio_stop(void);
bool nabo_radio_wanted(void);
bool nabo_radio_active(void);
const char *nabo_radio_state_text(void);
bool nabo_radio_available(void);
""",
}

HARNESS = r"""#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nabo_radio_menu.h"
#include "nabo_radio.h"
static const char *const names[] = {
    "中央广播中国之声", "北京音乐广播", "体育之声", "故事广播",
    "地方交通电台", "中央广播经典音乐", "中央广播经济之声完整名称"
};
static const char *const categories[] = { "中央电台", "音乐电台", "体育电台", "故事电台", "地方电台" };
static int cats[] = { 0, 1, 2, 3, 4, 0, 0 };
static int current, count, wanted, available, selects, toggles, stops, favorites;
static unsigned int favorite_bits;
static char state[80];
int nabo_radio_station_count(void) { return count; }
const char *nabo_radio_station_name(int i) { return i >= 0 && i < count ? names[i] : NULL; }
int nabo_radio_station_category(int i) { return i >= 0 && i < count ? cats[i] : -1; }
int nabo_radio_category_count(void) { return 5; }
const char *nabo_radio_category_name(int i) { return i >= 0 && i < 5 ? categories[i] : NULL; }
int nabo_radio_current_station(void) { return current; }
bool nabo_radio_favorite(int i) { return i >= 0 && i < count && (favorite_bits & (1u << i)); }
void nabo_radio_toggle_favorite(int i) {
    if (i >= 0 && i < count) { ++favorites; favorite_bits ^= (1u << i); }
}
void nabo_radio_select_station(int i) {
    if (i >= 0 && i < count) { ++selects; current = i; wanted = 1; }
}
void nabo_radio_toggle_play(void) { ++toggles; wanted ^= 1; }
void nabo_radio_stop(void) { ++stops; wanted = 0; }
bool nabo_radio_wanted(void) { return wanted; }
bool nabo_radio_active(void) { return wanted; }
const char *nabo_radio_state_text(void) { return state; }
bool nabo_radio_available(void) { return available; }
void fixture_reset(void) {
    current = -1; count = 7; wanted = 0; available = 1;
    selects = toggles = stops = favorites = 0; favorite_bits = 0;
    snprintf(state, sizeof(state), "已停止");
}
void fixture_current(int i) { current = i; }
void fixture_wanted(int n) { wanted = n; }
void fixture_count(int n) { count = n; }
void fixture_available(int n) { available = n; }
void fixture_favorites(unsigned int n) { favorite_bits = n; }
void fixture_state(const char *text) { snprintf(state, sizeof(state), "%s", text); }
int fixture_selects(void) { return selects; }
int fixture_toggles(void) { return toggles; }
int fixture_stops(void) { return stops; }
int fixture_favorite_calls(void) { return favorites; }
"""


class RadioMenuC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nabo-radio-menu-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        for name, source in HEADERS.items():
            (tmp / name).write_text(source, encoding="utf-8")
        fixture = tmp / "fixture.c"
        fixture.write_text(HARNESS, encoding="utf-8")
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and os.name == "nt":
            local = Path("gcc")
            if local.is_file():
                compiler = str(local)
        if not compiler:
            raise RuntimeError("Set CC to a host C compiler for the actual C radio menu tests")
        library = tmp / ("radio-menu.dll" if os.name == "nt" else "radio-menu.so")
        result = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
            "-Wl,--export-all-symbols" if os.name == "nt" else "-fPIC",
            "-I", str(tmp), "-I", str(ROOT / "firmware"), str(fixture),
            str(ROOT / "firmware/nabo_radio_menu.c"), "-o", str(library),
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == "nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.nabo_radio_menu_text.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.nabo_radio_menu_key.argtypes = [ctypes.c_int]
        cls.lib.nabo_radio_menu_key.restype = ctypes.c_bool
        cls.lib.nabo_radio_menu_action.restype = ctypes.c_char_p
        cls.lib.nabo_radio_menu_waiting.restype = ctypes.c_bool
        cls.lib.fixture_state.argtypes = [ctypes.c_char_p]
        cls.lib.nabo_radio_favorite.argtypes = [ctypes.c_int]
        cls.lib.nabo_radio_favorite.restype = ctypes.c_bool

    def setUp(self):
        self.lib.fixture_reset()
        self.lib.nabo_radio_menu_open()
        self.choose_directory(0)
        self.key(BACK)
        self.key(BACK)
        self.lib.nabo_radio_menu_open()

    def text(self, cap=1024):
        out = ctypes.create_string_buffer(cap)
        self.lib.nabo_radio_menu_text(out, cap)
        return out.value.decode("utf-8")

    def key(self, key, times=1):
        result = True
        for _ in range(times):
            result = self.lib.nabo_radio_menu_key(key)
        return result

    def action(self):
        return self.lib.nabo_radio_menu_action().decode("utf-8")

    def choose_directory(self, index):
        self.lib.nabo_radio_menu_open()
        self.key(DOWN, 4)
        self.key(SELECT)
        for _ in range(8):
            if self.text().startswith("电台目录 1/8"):
                break
            self.key(UP)
        else:
            self.fail("Directory did not wrap to All stations")
        self.key(DOWN, index)
        self.key(SELECT)

    def test_initial_player_is_stopped_without_selecting_or_playing(self):
        self.assertIn("尚未选择电台", self.text())
        self.assertIn("已停止", self.text())
        self.assertEqual(self.action(), "播放")
        self.assertEqual(self.lib.fixture_selects(), 0)
        self.assertEqual(self.lib.fixture_toggles(), 0)

    def test_seven_player_actions_wrap_on_three_buttons(self):
        expected = ["播放", "停止", "上一台", "下一台", "电台目录", "收藏当前台", "返回"]
        for item in expected:
            self.assertEqual(self.action(), item)
            self.key(DOWN)
        self.assertEqual(self.action(), "播放")
        self.key(UP)
        self.assertEqual(self.action(), "返回")
        self.assertFalse(self.key(SELECT))
        self.assertEqual(self.lib.fixture_stops(), 0)

    def test_first_play_selects_a_station_and_subsequent_select_toggles(self):
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 0)
        self.assertEqual(self.lib.fixture_selects(), 1)
        self.assertEqual(self.action(), "暂停")
        self.key(SELECT)
        self.assertEqual(self.action(), "播放")
        self.assertEqual(self.lib.fixture_toggles(), 1)
        self.assertEqual(self.lib.fixture_selects(), 1)

    def test_stop_requires_ok_and_keeps_current_station(self):
        self.lib.fixture_current(3)
        self.lib.fixture_wanted(1)
        self.key(DOWN)
        self.assertEqual(self.lib.fixture_stops(), 0)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_stops(), 1)
        self.assertEqual(self.lib.nabo_radio_current_station(), 3)
        self.assertEqual(self.action(), "停止")

    def test_all_station_previous_next_wrap_in_catalog_order(self):
        self.lib.fixture_current(0)
        self.key(DOWN, 2)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 6)
        self.key(DOWN)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 0)

    def test_directories_include_all_favorites_five_categories_and_return(self):
        self.key(DOWN, 4)
        self.key(SELECT)
        for index in range(8):
            self.assertTrue(self.text().startswith(f"电台目录 {index + 1}/8"))
            self.key(DOWN)
        self.assertTrue(self.text().startswith("电台目录 1/8"))
        self.key(UP)
        self.assertEqual(self.action(), "返回")
        self.key(SELECT)
        self.assertIn("尚未选择电台", self.text())

    def test_browsing_directory_or_stations_does_not_start_audio(self):
        self.choose_directory(2)
        self.assertIn("中央电台", self.text())
        self.key(DOWN, 3)
        self.assertEqual(self.lib.fixture_selects(), 0)
        self.assertEqual(self.lib.fixture_toggles(), 0)
        self.assertEqual(self.lib.fixture_stops(), 0)

    def test_category_selection_and_previous_next_remain_in_category(self):
        self.choose_directory(2)
        self.assertIn("中央电台 1/3", self.text())
        self.key(DOWN)
        self.assertIn("中央广播经典音乐", self.text())
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 5)
        self.assertEqual(self.action(), "暂停")
        self.key(DOWN, 3)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 6)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 0)
        self.key(UP)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 6)

    def test_station_list_has_back_entry_and_wraps_in_both_directions(self):
        self.choose_directory(0)
        self.assertIn("全部电台 1/7", self.text())
        self.key(UP)
        self.assertEqual(self.action(), "返回")
        self.assertIn("返回电台目录", self.text())
        self.key(DOWN)
        self.assertIn("全部电台 1/7", self.text())
        self.key(UP)
        self.key(SELECT)
        self.assertIn("电台目录 1/8", self.text())

    def test_empty_favorites_has_return_without_invalid_play_request(self):
        self.choose_directory(1)
        self.assertIn("我的收藏", self.text())
        self.assertIn("0 个电台", self.text())
        self.assertEqual(self.action(), "返回")
        self.key(UP)
        self.key(DOWN)
        self.key(SELECT)
        self.assertIn("电台目录 2/8", self.text())
        self.assertEqual(self.lib.fixture_selects(), 0)

    def test_favorites_selection_and_wrap_ignore_other_stations(self):
        self.lib.fixture_favorites((1 << 1) | (1 << 5))
        self.choose_directory(1)
        self.assertIn("我的收藏 1/2", self.text())
        self.assertIn("北京音乐广播", self.text())
        self.key(SELECT)
        self.key(DOWN, 2)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 5)
        self.key(DOWN)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 1)

    def test_favorite_action_marks_and_unmarks_current_station(self):
        self.lib.fixture_current(2)
        self.key(DOWN, 5)
        self.key(SELECT)
        self.assertTrue(self.lib.nabo_radio_favorite(2))
        self.assertIn("已收藏", self.text())
        self.assertEqual(self.action(), "取消当前收藏")
        self.key(SELECT)
        self.assertFalse(self.lib.nabo_radio_favorite(2))
        self.assertEqual(self.action(), "收藏当前台")
        self.assertEqual(self.lib.fixture_favorite_calls(), 2)

    def test_favorite_action_does_not_publish_invalid_station(self):
        self.key(DOWN, 5)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_favorite_calls(), 0)

    def test_removing_current_favorite_recovers_previous_next_filter(self):
        self.lib.fixture_favorites((1 << 1) | (1 << 5))
        self.choose_directory(1)
        self.key(SELECT)
        self.key(DOWN, 5)
        self.key(SELECT)
        self.assertFalse(self.lib.nabo_radio_favorite(1))
        self.key(UP, 2)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 5)

    def test_closed_menu_does_not_stop_playback_or_reset_filter(self):
        self.choose_directory(2)
        self.key(SELECT)
        self.lib.nabo_radio_menu_close()
        self.assertEqual(self.lib.fixture_stops(), 0)
        self.lib.nabo_radio_menu_open()
        self.assertEqual(self.action(), "暂停")
        self.key(DOWN, 3)
        self.key(SELECT)
        self.assertEqual(self.lib.nabo_radio_current_station(), 5)

    def test_back_and_left_navigate_levels_without_stopping_audio(self):
        for key in (BACK, LEFT):
            with self.subTest(key=key):
                self.choose_directory(0)
                self.assertTrue(self.key(key))
                self.assertIn("电台目录", self.text())
                self.assertTrue(self.key(key))
                self.assertIn("尚未选择电台", self.text())
                self.assertFalse(self.key(key))
        self.assertEqual(self.lib.fixture_stops(), 0)

    def test_right_never_acts_as_confirmation(self):
        self.key(RIGHT)
        self.assertEqual(self.lib.fixture_selects(), 0)
        self.choose_directory(0)
        self.key(RIGHT)
        self.assertEqual(self.lib.fixture_selects(), 0)
        self.assertIn("全部电台 1/7", self.text())

    def test_offline_waiting_status_still_accepts_play_intent(self):
        self.lib.fixture_current(0)
        self.lib.fixture_state("等待 Wi-Fi".encode())
        self.key(SELECT)
        self.assertIn("等待 Wi-Fi", self.text())
        self.assertEqual(self.lib.fixture_toggles(), 1)
        self.assertFalse(self.lib.nabo_radio_menu_waiting())

    def test_disabled_radio_or_empty_catalog_does_not_select_invalid_station(self):
        self.lib.fixture_available(0)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_selects(), 0)
        self.lib.fixture_available(1)
        self.lib.fixture_count(0)
        self.key(SELECT)
        self.assertEqual(self.lib.fixture_selects(), 0)

    def test_full_unicode_name_and_state_render_without_ascii_substitution(self):
        self.lib.fixture_current(6)
        self.lib.fixture_state("正在缓冲".encode())
        self.assertIn("中央广播经济之声完整名称", self.text())
        self.assertIn("正在缓冲", self.text())

    def test_every_small_buffer_preserves_utf8_and_stays_in_bounds(self):
        self.lib.fixture_current(6)
        for capacity in range(1, 300):
            with self.subTest(capacity=capacity):
                out = ctypes.create_string_buffer(b"X" * 512, 513)
                self.lib.nabo_radio_menu_text(out, capacity)
                self.assertIn(0, out.raw[:capacity])
                self.assertEqual(out.raw[capacity:512], b"X" * (512 - capacity))
                out.value.decode("utf-8")
        self.lib.nabo_radio_menu_text(None, 0)
        self.lib.nabo_radio_menu_text(None, 1)


class RadioMenuInstallation(unittest.TestCase):
    def test_sdk_menu_install_is_idempotent_and_preserves_old_item_indices(self):
        sdk = Path(os.environ.get("NABO_TEST_SDK", str(ROOT.parent / "muse-gadget-sdk")))
        baseline = subprocess.run([
            "git", "-C", str(sdk), "show", "812c46f9fe42b69d916613a9dc702cbac5ff7b54:esp32/components/muse/muse_menu.c"
        ], capture_output=True, check=True).stdout.decode("utf-8")
        with tempfile.TemporaryDirectory(prefix="nabo-radio-install-") as directory:
            esp = Path(directory) / "esp32"
            component = esp / "components/muse"
            component.mkdir(parents=True)
            menu = component / "muse_menu.c"
            menu.write_text(baseline, encoding="utf-8")
            (component / "CMakeLists.txt").write_text(
                '        list(APPEND srcs "nabo_pcm_cache.c")\n'
                '    bt esp-tls mbedtls esp_app_format esp_http_client\n', encoding="utf-8")

            def replace(path, before, after):
                text = path.read_text(encoding="utf-8")
                if after in text:
                    return
                self.assertEqual(text.count(before), 1, before)
                path.write_text(text.replace(before, after), encoding="utf-8")

            install_wifi_menu(ROOT, esp, replace)
            install_radio_ui(ROOT, esp, replace)
            first = menu.read_bytes()
            install_radio_ui(ROOT, esp, replace)
            self.assertEqual(menu.read_bytes(), first)
            text = menu.read_text(encoding="utf-8")
            enum = text.split("typedef enum {", 1)[1].split("} item_t;", 1)[0]
            items = [line.strip().rstrip(",") for line in enum.splitlines() if line.strip().startswith("ITEM_")]
            self.assertEqual(items.index("ITEM_PHONE"), 5)
            self.assertEqual(items.index("ITEM_WIFI"), 6)
            self.assertEqual(items.index("ITEM_WIFI_ADD"), 7)
            self.assertEqual(items.index("ITEM_RADIO"), 8)
            self.assertIn('set_text(s_title, "网络电台")', text)
            self.assertIn("if (s_view == VIEW_RADIO) nabo_radio_menu_close();", text)
            self.assertNotIn("nabo_radio_menu_waiting()", text)
            self.assertIn("font = muse_font_cjk_16;", text)
            self.assertIn("font.fallback = &lv_font_montserrat_14;", text)
            self.assertIn("font.kerning = LV_FONT_KERNING_NONE;", text)
            self.assertIn("font = lv_font_unscii_16; font.fallback = &muse_font_cjk_16;", text)
            self.assertIn(RADIO_STYLES, text)
            self.assertNotIn(OLD_RADIO_STYLES, text)
            self.assertIn("i == ITEM_RADIO ? nabo_radio_font() : font;", text)
            # An already installed first radio revision migrates without
            # rebuilding the old Wi-Fi font or changing other menu views.
            menu.write_text(text.replace(RADIO_STYLES, OLD_RADIO_STYLES).replace(
                "i == ITEM_RADIO ? nabo_radio_font() : font;",
                "i == ITEM_RADIO ? nabo_wifi_font() : font;"), encoding="utf-8")
            install_radio_ui(ROOT, esp, replace)
            self.assertEqual(menu.read_text(encoding="utf-8"), text)
            for name in ("nabo_radio_menu.c", "nabo_radio_menu.h"):
                self.assertEqual((component / name).read_bytes(), (ROOT / "firmware" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
