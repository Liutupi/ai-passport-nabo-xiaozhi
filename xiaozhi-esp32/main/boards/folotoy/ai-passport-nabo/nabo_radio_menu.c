#include "nabo_radio_menu.h"
#include "nabo_radio.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum { PAGE_PLAYER, PAGE_DIRECTORY, PAGE_STATIONS } page_t;
enum { FILTER_ALL = -1, FILTER_FAVORITES = -2 };
enum { PLAY, STOP, PREVIOUS, NEXT, DIRECTORY, FAVORITE, BACK, ACTION_COUNT };
static page_t s_page;
static int s_action, s_directory, s_station;
static int s_filter = FILTER_ALL;

static bool matches(int station)
{
    return s_filter == FILTER_ALL ||
           (s_filter == FILTER_FAVORITES ? nabo_radio_favorite(station) :
            nabo_radio_station_category(station) == s_filter);
}

static int station_count(void)
{
    int count = 0;
    for (int i = 0; i < nabo_radio_station_count(); ++i) if (matches(i)) ++count;
    return count;
}

static int station_at(int index)
{
    for (int i = 0; i < nabo_radio_station_count(); ++i) {
        if (matches(i) && index-- == 0) return i;
    }
    return -1;
}

static int station_index(int station)
{
    int index = 0;
    for (int i = 0; i < nabo_radio_station_count(); ++i) {
        if (!matches(i)) continue;
        if (i == station) return index;
        ++index;
    }
    return -1;
}

static const char *filter_name(void)
{
    if (s_filter == FILTER_ALL) return "全部电台";
    if (s_filter == FILTER_FAVORITES) return "我的收藏";
    const char *name = nabo_radio_category_name(s_filter);
    return name ? name : "电台分类";
}

static const char *directory_name(int item)
{
    if (item == 0) return "全部电台";
    if (item == 1) return "我的收藏";
    if (item == nabo_radio_category_count() + 2) return "返回播放器";
    const char *name = nabo_radio_category_name(item - 2);
    return name ? name : "电台分类";
}

static const char *action_name(int item)
{
    static const char *const names[ACTION_COUNT] = {
        "播放", "停止", "上一台", "下一台", "电台目录", "收藏当前台", "返回"
    };
    if (item == PLAY) return nabo_radio_wanted() ? "暂停" : "播放";
    if (item == FAVORITE && nabo_radio_favorite(nabo_radio_current_station()))
        return "取消当前收藏";
    return names[item];
}

static void clamp_selection(void)
{
    int directories = nabo_radio_category_count() + 3;
    if (s_directory < 0 || s_directory >= directories) s_directory = 0;
    int count = station_count();
    if (s_station < 0 || s_station > count) s_station = count;
}

void nabo_radio_menu_open(void)
{
    s_page = PAGE_PLAYER;
    s_action = 0;
    clamp_selection();
}

void nabo_radio_menu_close(void)
{
    /* The worker and playback intent outlive the 30-second menu timeout. */
    s_page = PAGE_PLAYER;
    s_action = 0;
}

static void step_station(int direction)
{
    int count = station_count();
    if (!count || !nabo_radio_available()) return;
    int index = station_index(nabo_radio_current_station());
    if (index < 0) index = direction < 0 ? count - 1 : 0;
    else index = (index + (direction < 0 ? count - 1 : 1)) % count;
    nabo_radio_select_station(station_at(index));
}

bool nabo_radio_menu_key(muse_menu_key_t key)
{
    clamp_selection();
    if (key == MUSE_MENU_BACK || key == MUSE_MENU_LEFT) {
        if (s_page == PAGE_PLAYER) return false;
        s_page = s_page == PAGE_STATIONS ? PAGE_DIRECTORY : PAGE_PLAYER;
        return true;
    }
    if (s_page == PAGE_PLAYER) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            s_action = (s_action + (key == MUSE_MENU_UP ? ACTION_COUNT - 1 : 1)) % ACTION_COUNT;
        } else if (key == MUSE_MENU_SELECT) {
            switch (s_action) {
            case PLAY:
                if (!nabo_radio_available()) break;
                if (nabo_radio_current_station() < 0) step_station(1);
                else nabo_radio_toggle_play();
                break;
            case STOP: nabo_radio_stop(); break;
            case PREVIOUS: step_station(-1); break;
            case NEXT: step_station(1); break;
            case DIRECTORY: s_page = PAGE_DIRECTORY; break;
            case FAVORITE:
                if (nabo_radio_current_station() >= 0)
                    nabo_radio_toggle_favorite(nabo_radio_current_station());
                break;
            case BACK: return false;
            }
        }
    } else if (s_page == PAGE_DIRECTORY) {
        int total = nabo_radio_category_count() + 3;
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            s_directory = (s_directory + (key == MUSE_MENU_UP ? total - 1 : 1)) % total;
        } else if (key == MUSE_MENU_SELECT) {
            if (s_directory == total - 1) s_page = PAGE_PLAYER;
            else {
                s_filter = s_directory == 0 ? FILTER_ALL :
                           (s_directory == 1 ? FILTER_FAVORITES : s_directory - 2);
                s_station = station_index(nabo_radio_current_station());
                if (s_station < 0) s_station = 0;
                s_page = PAGE_STATIONS;
            }
        }
    } else {
        int count = station_count(), total = count + 1;
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            s_station = (s_station + (key == MUSE_MENU_UP ? total - 1 : 1)) % total;
        } else if (key == MUSE_MENU_SELECT) {
            if (s_station == count) s_page = PAGE_DIRECTORY;
            else if (nabo_radio_available()) {
                int station = station_at(s_station);
                if (station >= 0) nabo_radio_select_station(station);
                s_page = PAGE_PLAYER;
                s_action = 0;
            }
        }
    }
    return true;
}

static void append(char *out, size_t cap, const char *format, ...)
{
    size_t used = strlen(out);
    if (used >= cap - 1) return;
    va_list args;
    va_start(args, format);
    vsnprintf(out + used, cap - used, format, args);
    va_end(args);
}

/* A smaller caller buffer must not turn a truncated Chinese glyph into junk. */
static void finish_utf8(char *out)
{
    size_t size = strlen(out);
    if (!size) return;
    size_t start = size - 1;
    while (start && ((unsigned char)out[start] & 0xc0) == 0x80) --start;
    unsigned char lead = (unsigned char)out[start];
    unsigned int needed = lead < 0x80 ? 1 :
                          ((lead & 0xe0) == 0xc0 ? 2 : ((lead & 0xf0) == 0xe0 ? 3 : 4));
    if (size - start < needed) out[start] = '\0';
}

void nabo_radio_menu_text(char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = '\0';
    clamp_selection();
    if (s_page == PAGE_PLAYER) {
        int station = nabo_radio_current_station();
        const char *name = station >= 0 ? nabo_radio_station_name(station) : NULL;
        const char *state = nabo_radio_state_text();
        append(out, cap, "%s\n%s%s\n\n", name ? name : "尚未选择电台",
               state ? state : "已停止", nabo_radio_favorite(station) ? " 已收藏" : "");
        for (int i = 0; i < ACTION_COUNT; ++i)
            append(out, cap, "%s%s%s", i == s_action ? "> " : "  ", action_name(i),
                   i == ACTION_COUNT - 1 ? "" : "\n");
    } else if (s_page == PAGE_DIRECTORY) {
        int total = nabo_radio_category_count() + 3;
        int first = s_directory > 3 ? s_directory - 3 : 0;
        if (first + 5 > total) first = total > 5 ? total - 5 : 0;
        append(out, cap, "电台目录 %d/%d\n\n", s_directory + 1, total);
        for (int i = first; i < total && i < first + 5; ++i)
            append(out, cap, "%s%s\n", i == s_directory ? "> " : "  ", directory_name(i));
        append(out, cap, "\n上/下：选择  OK：进入");
    } else {
        int count = station_count();
        if (s_station == count) {
            append(out, cap, "%s\n%d 个电台\n\n> 返回电台目录\n\n上/下：选台  OK：返回",
                   filter_name(), count);
            if (!count) append(out, cap, "\n尚无电台，可返回其他分类");
        } else {
            int station = station_at(s_station);
            const char *name = nabo_radio_station_name(station);
            append(out, cap, "%s %d/%d\n\n%s\n%s\n\n上/下：选台\nOK：播放\n返回位于列表末尾",
                   filter_name(), s_station + 1, count, name ? name : "未知电台",
                   nabo_radio_favorite(station) ? "已收藏" : "未收藏");
        }
    }
    finish_utf8(out);
}

const char *nabo_radio_menu_action(void)
{
    if (s_page == PAGE_PLAYER) return action_name(s_action);
    if (s_page == PAGE_DIRECTORY)
        return s_directory == nabo_radio_category_count() + 2 ? "返回" : "进入";
    return s_station == station_count() ? "返回" : "播放";
}

bool nabo_radio_menu_waiting(void)
{
    /* Buffering does not extend the menu timeout or prevent returning to NABO. */
    return false;
}
