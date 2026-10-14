#include "nabo_keys_model.h"
#include <stdio.h>
#include <string.h>

enum { CTRL=1, SHIFT=2, ALT=4, GUI=8, RIGHT_SHIFT=0x20, RIGHT_ALT=0x40 };
static const char *const names[] = {
    "听写 / 结束", "语音翻译", "语音提问",
    "发送 / 确认", "插入换行", "停止 / 取消", "接受建议", "上一条命令",
    "复制", "粘贴", "剪切", "撤销", "重做", "全选", "保存", "查找",
    "切换应用", "窗口总览", "系统搜索", "锁定电脑", "新建标签", "关闭标签",
    "恢复标签", "下一标签", "上一标签", "地址栏",
    "区域截图", "全屏截图", "播放 / 暂停", "上一曲", "下一曲", "音量增加", "音量降低", "静音"
};
_Static_assert(sizeof(names)/sizeof(*names)==NABO_KEY_COUNT, "shortcut names");
static const uint8_t ranges[][2]={{0,8},{8,8},{16,4},{20,6},{26,8}};
static const char *const groups[]={"Typeless 与 AI","文字编辑","窗口操作","浏览器","截图与媒体","连接与按键"};
const char *nabo_key_name(nabo_key_t key) { return (unsigned)key<NABO_KEY_COUNT ? names[key] : ""; }
const char *nabo_keys_group_name(int group) { return (unsigned)group<6 ? groups[group] : ""; }
int nabo_keys_group_count(int group) { return (unsigned)group<NABO_KEYS_GROUPS ? ranges[group][1] : 0; }
nabo_key_t nabo_keys_group_item(int group, int row)
{
    return (unsigned)group<NABO_KEYS_GROUPS && (unsigned)row<ranges[group][1] ?
        (nabo_key_t)(ranges[group][0]+row) : NABO_KEY_COUNT;
}
static void keyboard(nabo_key_packet_t *p, uint8_t mod, uint8_t key)
{ *p=(nabo_key_packet_t){.report=1,.len=8,.data={mod,0,key}}; }
bool nabo_keys_packet(nabo_keys_os_t os, nabo_key_t key, nabo_key_packet_t *out)
{
    if (!out || (unsigned)os>=NABO_KEYS_OS_COUNT || (unsigned)key>=NABO_KEY_COUNT) return false;
    uint8_t main=os==NABO_KEYS_MAC ? GUI : CTRL;
    uint8_t mod=0, usage=0;
    switch (key) {
    /* Windows follows the user's Typeless bindings. Keep the Mac defaults
     * and its existing NVS overrides independent. Right Alt is modifier bit
     * 6, not left Alt or a normal key-array usage. */
    case NABO_KEY_DICTATE:
        if (os==NABO_KEYS_WINDOWS) mod=RIGHT_ALT; else usage=0x68;
        break;
    case NABO_KEY_TRANSLATE:
        if (os==NABO_KEYS_WINDOWS) mod=RIGHT_ALT|RIGHT_SHIFT; else usage=0x69;
        break;
    case NABO_KEY_ASK:
        if (os==NABO_KEYS_WINDOWS) { mod=RIGHT_ALT; usage=0x2c; } else usage=0x6a;
        break;
    case NABO_KEY_ENTER: usage=0x28; break;
    case NABO_KEY_NEWLINE: mod=SHIFT; usage=0x28; break;
    case NABO_KEY_ESCAPE: usage=0x29; break;
    case NABO_KEY_TAB: usage=0x2b; break;
    case NABO_KEY_UP: usage=0x52; break;
    case NABO_KEY_COPY: mod=main; usage=0x06; break;
    case NABO_KEY_PASTE: mod=main; usage=0x19; break;
    case NABO_KEY_CUT: mod=main; usage=0x1b; break;
    case NABO_KEY_UNDO: mod=main; usage=0x1d; break;
    case NABO_KEY_REDO: mod=main|(os==NABO_KEYS_MAC ? SHIFT : 0); usage=os==NABO_KEYS_MAC ? 0x1d : 0x1c; break;
    case NABO_KEY_ALL: mod=main; usage=0x04; break;
    case NABO_KEY_SAVE: mod=main; usage=0x16; break;
    case NABO_KEY_FIND: mod=main; usage=0x09; break;
    case NABO_KEY_SWITCH: mod=os==NABO_KEYS_MAC ? GUI : ALT; usage=0x2b; break;
    case NABO_KEY_OVERVIEW: mod=os==NABO_KEYS_MAC ? CTRL : GUI; usage=os==NABO_KEYS_MAC ? 0x52 : 0x2b; break;
    case NABO_KEY_SEARCH: mod=GUI; usage=os==NABO_KEYS_MAC ? 0x2c : 0x16; break;
    case NABO_KEY_LOCK: mod=os==NABO_KEYS_MAC ? CTRL|GUI : GUI; usage=os==NABO_KEYS_MAC ? 0x14 : 0x0f; break;
    case NABO_KEY_NEW_TAB: mod=main; usage=0x17; break;
    case NABO_KEY_CLOSE_TAB: mod=main; usage=0x1a; break;
    case NABO_KEY_REOPEN_TAB: mod=main|SHIFT; usage=0x17; break;
    case NABO_KEY_NEXT_TAB: mod=CTRL; usage=0x2b; break;
    case NABO_KEY_PREV_TAB: mod=CTRL|SHIFT; usage=0x2b; break;
    case NABO_KEY_ADDRESS: mod=main; usage=0x0f; break;
    case NABO_KEY_SCREEN_AREA: mod=os==NABO_KEYS_MAC ? GUI|SHIFT : GUI|SHIFT; usage=os==NABO_KEYS_MAC ? 0x21 : 0x16; break;
    case NABO_KEY_SCREEN_FULL: mod=GUI|(os==NABO_KEYS_MAC ? SHIFT : 0); usage=os==NABO_KEYS_MAC ? 0x20 : 0x46; break;
    default: {
        static const uint16_t consumers[]={0xcd,0xb6,0xb5,0xe9,0xea,0xe2};
        uint16_t c=consumers[key-NABO_KEY_PLAY];
        *out=(nabo_key_packet_t){.report=2,.len=2,.data={(uint8_t)c,(uint8_t)(c>>8)}};
        return true;
    }
    }
    keyboard(out,mod,usage);
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
    case 0x2c:key="Space";break; case 0x46:key="PrtSc";break;case 0x52:key="Up";break;
    case 0:key="";break;default:snprintf(buf,sizeof(buf),"%02X",k);key=buf;break;
    }
    snprintf(out,cap,"%s%s%s%s%s",m&0x11 ? "C-" : "",m&0x22 ? "S-" : "",
             m&0x44 ? "A-" : "",m&0x88 ? (os==NABO_KEYS_MAC ? "Cmd-" : "Win-") : "",key);
    if (strlen(out)>10) snprintf(out,cap,"%02X:%02X",m,k);
}
