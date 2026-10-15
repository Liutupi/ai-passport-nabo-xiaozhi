#include "nabo_keys_model.h"
#include <stdio.h>
#include <string.h>

enum { CTRL = 1, SHIFT = 2, ALT = 4, GUI = 8, RIGHT_SHIFT = 0x20, RIGHT_ALT = 0x40 };
enum { MOUSE_LEFT = 1, MOUSE_RIGHT = 2 };
static const char* const names[] = {
    "向上滑",      "向下滑",      "返回上页", "鼠标单击", "鼠标右键", "确认 / 发送", "取消 / 退出",
    "复制",        "粘贴",        "撤销",     "切换应用", "显示桌面", "区域截图",    "锁定电脑",
    "播放 / 暂停", "快退",        "快进",     "音量增加", "音量降低", "静音",        "上一曲",
    "下一曲",      "听写 / 结束", "语音翻译", "语音提问", "向右翻页"};
_Static_assert(sizeof(names) / sizeof(*names) == NABO_KEY_COUNT, "shortcut names");
static const uint8_t ranges[NABO_KEYS_GROUPS][2] = {{0, 6}, {6, 9}, {15, 8}, {23, 3}};
static const nabo_key_t menu_order[] = {
    NABO_KEY_SCROLL_UP, NABO_KEY_SCROLL_DOWN,  NABO_KEY_RIGHT,  NABO_KEY_BACK,
    NABO_KEY_CLICK,     NABO_KEY_RIGHT_CLICK,  NABO_KEY_ENTER,  NABO_KEY_ESCAPE,
    NABO_KEY_COPY,      NABO_KEY_PASTE,        NABO_KEY_UNDO,   NABO_KEY_SWITCH,
    NABO_KEY_DESKTOP,   NABO_KEY_SCREEN_AREA,  NABO_KEY_LOCK,   NABO_KEY_PLAY,
    NABO_KEY_SEEK_BACK, NABO_KEY_SEEK_FORWARD, NABO_KEY_VOL_UP, NABO_KEY_VOL_DOWN,
    NABO_KEY_MUTE,      NABO_KEY_PREVIOUS,     NABO_KEY_NEXT,   NABO_KEY_DICTATE,
    NABO_KEY_TRANSLATE, NABO_KEY_ASK};
_Static_assert(sizeof(menu_order) / sizeof(*menu_order) == NABO_KEY_COUNT, "group ranges");
_Static_assert((int)NABO_KEY_RIGHT == (int)NABO_KEYS_V2_COUNT, "preserve v2 map indices");
static const char* const groups[NABO_KEYS_GROUPS] = {"鼠标与翻页", "常用操作", "影音播放",
                                                     "Typeless 与 AI"};
const char* nabo_key_name(nabo_key_t key) {
    return (unsigned)key < NABO_KEY_COUNT ? names[key] : "";
}
const char* nabo_keys_group_name(int group) {
    return (unsigned)group < NABO_KEYS_GROUPS ? groups[group] : "";
}
int nabo_keys_group_count(int group) {
    return (unsigned)group < NABO_KEYS_GROUPS ? ranges[group][1] : 0;
}
nabo_key_t nabo_keys_group_item(int group, int row) {
    return (unsigned)group < NABO_KEYS_GROUPS && (unsigned)row < ranges[group][1]
               ? menu_order[ranges[group][0] + row]
               : NABO_KEY_COUNT;
}
nabo_key_t nabo_keys_from_v1(int v1)
{
    /* Only keyboard actions that survived the trim can carry an override. */
    switch (v1) {
    case 0: return NABO_KEY_DICTATE;
    case 1: return NABO_KEY_TRANSLATE;
    case 2: return NABO_KEY_ASK;
    case 3: return NABO_KEY_ENTER;
    case 5: return NABO_KEY_ESCAPE;
    case 8: return NABO_KEY_COPY;
    case 9: return NABO_KEY_PASTE;
    case 11: return NABO_KEY_UNDO;
    case 16: return NABO_KEY_SWITCH;
    case 19: return NABO_KEY_LOCK;
    case 26: return NABO_KEY_SCREEN_AREA;
    default: return NABO_KEY_COUNT;
    }
}
static void keyboard(nabo_key_packet_t *p, uint8_t mod, uint8_t key)
{ *p=(nabo_key_packet_t){.report=1,.len=8,.data={mod,0,key}}; }
static void mouse(nabo_key_packet_t *p, uint8_t buttons, int8_t wheel)
{ *p=(nabo_key_packet_t){.report=3,.len=4,.data={buttons,0,0,(uint8_t)wheel}}; }
void nabo_keys_reverse_scroll(nabo_key_packet_t *p)
{
    if (p && p->report==3 && p->data[3]) p->data[3]=(uint8_t)(-(int8_t)p->data[3]);
}
bool nabo_keys_packet(nabo_keys_os_t os, nabo_key_t key, nabo_key_packet_t* out) {
    if (!out || (unsigned)os >= NABO_KEYS_OS_COUNT || (unsigned)key >= NABO_KEY_COUNT)
        return false;
    bool mac = os == NABO_KEYS_MAC;
    uint8_t main = mac ? GUI : CTRL;
    uint8_t mod = 0, usage = 0;
    switch (key) {
        /* "Up" means the page moves toward its top, like the physical key.
         * macOS ships with natural scrolling, which inverts the wheel; the
         * connection page can reverse either platform. */
        case NABO_KEY_SCROLL_UP:
            mouse(out, 0, mac ? -NABO_SCROLL_STEP : NABO_SCROLL_STEP);
            return true;
        case NABO_KEY_SCROLL_DOWN:
            mouse(out, 0, mac ? NABO_SCROLL_STEP : -NABO_SCROLL_STEP);
            return true;
        case NABO_KEY_CLICK:
            mouse(out, MOUSE_LEFT, 0);
            return true;
        case NABO_KEY_RIGHT_CLICK:
            mouse(out, MOUSE_RIGHT, 0);
            return true;
        /* Browser / Finder / Explorer back: Cmd+[ or Alt+Left. */
        case NABO_KEY_BACK:
            mod = mac ? GUI : ALT;
            usage = mac ? 0x2f : 0x50;
            break;
        case NABO_KEY_ENTER:
            usage = 0x28;
            break;
        case NABO_KEY_ESCAPE:
            usage = 0x29;
            break;
        case NABO_KEY_COPY:
            mod = main;
            usage = 0x06;
            break;
        case NABO_KEY_PASTE:
            mod = main;
            usage = 0x19;
            break;
        case NABO_KEY_UNDO:
            mod = main;
            usage = 0x1d;
            break;
        case NABO_KEY_SWITCH:
            mod = mac ? GUI : ALT;
            usage = 0x2b;
            break;
        /* Win+D; macOS "Show Desktop" defaults to F11. */
        case NABO_KEY_DESKTOP:
            mod = mac ? 0 : GUI;
            usage = mac ? 0x44 : 0x07;
            break;
        case NABO_KEY_SCREEN_AREA:
            mod = GUI | SHIFT;
            usage = mac ? 0x21 : 0x16;
            break;
        case NABO_KEY_LOCK:
            mod = mac ? CTRL | GUI : GUI;
            usage = mac ? 0x14 : 0x0f;
            break;
        /* Web players (YouTube, Bilibili) seek with the arrow keys. */
        case NABO_KEY_SEEK_BACK:
            usage = 0x50;
            break;
        case NABO_KEY_SEEK_FORWARD:
        case NABO_KEY_RIGHT:
            usage = 0x4f;
            break;
        /* Windows follows the user's Typeless bindings. Keep the Mac defaults
         * and its existing NVS overrides independent. Right Alt is modifier bit
         * 6, not left Alt or a normal key-array usage. */
        case NABO_KEY_DICTATE:
            if (!mac)
                mod = RIGHT_ALT;
            else
                usage = 0x68;
            break;
        case NABO_KEY_TRANSLATE:
            if (!mac)
                mod = RIGHT_ALT | RIGHT_SHIFT;
            else
                usage = 0x69;
            break;
        case NABO_KEY_ASK:
            if (!mac) {
                mod = RIGHT_ALT;
                usage = 0x2c;
            } else
                usage = 0x6a;
            break;
        default: {
            static const uint16_t consumers[] = {0xcd, 0, 0, 0xe9, 0xea, 0xe2, 0xb6, 0xb5};
            uint16_t c = consumers[key - NABO_KEY_PLAY];
            *out =
                (nabo_key_packet_t){.report = 2, .len = 2, .data = {(uint8_t)c, (uint8_t)(c >> 8)}};
            return true;
        }
    }
    keyboard(out, mod, usage);
    return true;
}
bool nabo_keys_override_packet(uint16_t encoded, nabo_key_packet_t *out)
{
    uint8_t mod=encoded>>8, key=encoded&255;
    if (!out || key>0x73 || (key && key<4) || (!key && !mod)) return false;
    keyboard(out,mod,key);
    return true;
}
void nabo_keys_chord(const nabo_key_packet_t *p, nabo_keys_os_t os, char *out, size_t cap)
{
    if (!out || !cap) return;
    if (!p) { out[0]=0; return; }
    if (p->report==2) { snprintf(out,cap,"MEDIA"); return; }
    if (p->report==3) {
        int8_t wheel=(int8_t)p->data[3];
        snprintf(out,cap,"%s",wheel>0 ? "Wheel Up" : wheel<0 ? "Wheel Dn" :
                 p->data[0]==MOUSE_RIGHT ? "R-Click" : "Click");
        return;
    }
    const char *key=""; char buf[12]; uint8_t k=p->data[2], m=p->data[0];
    /* Distinguish Typeless's right-side modifiers in the narrow value column. */
    if ((m==RIGHT_ALT && (k==0 || k==0x2c || k==0x10)) ||
        (m==(RIGHT_ALT|RIGHT_SHIFT) && k==0)) {
        snprintf(out,cap,"%s",m==(RIGHT_ALT|RIGHT_SHIFT) ? "RAlt+RSh" :
                 (k==0x2c ? "RAlt+Spc" : (k==0x10 ? "RAlt+M" : "RAlt")));
        return;
    }
    if (k>=4 && k<=0x1d) { buf[0]='A'+k-4; buf[1]=0; key=buf; }
    else if (k>=0x1e && k<=0x26) { buf[0]='1'+k-0x1e; buf[1]=0; key=buf; }
    else if (k>=0x68 && k<=0x73) { snprintf(buf,sizeof(buf),"F%d",13+k-0x68); key=buf; }
    else if (k>=0x3a && k<=0x45) { snprintf(buf,sizeof(buf),"F%d",1+k-0x3a); key=buf; }
    else switch(k) {
    case 0x28:key="Enter";break; case 0x29:key="Esc";break;case 0x2b:key="Tab";break;
    case 0x2c:key="Space";break; case 0x2f:key="[";break;case 0x46:key="PrtSc";break;
    case 0x4f:key="Right";break; case 0x50:key="Left";break;case 0x52:key="Up";break;
    case 0:key="";break;default:snprintf(buf,sizeof(buf),"%02X",k);key=buf;break;
    }
    snprintf(out,cap,"%s%s%s%s%s",m&0x11 ? "C-" : "",m&0x22 ? "S-" : "",
             m&0x44 ? "A-" : "",m&0x88 ? (os==NABO_KEYS_MAC ? "Cmd-" : "Win-") : "",key);
    if (strlen(out)>10) snprintf(out,cap,"%02X:%02X",m,k);
}
