"""Actual C mappings, HID GATT contract, bounded sender and failure recovery."""
import ctypes
import os
from pathlib import Path
import tempfile
import unittest
from test_dice import build

ROOT=Path(__file__).resolve().parents[1]


class Packet(ctypes.Structure):
    _fields_=[("report",ctypes.c_ubyte),("length",ctypes.c_ubyte),("data",ctypes.c_ubyte*8)]


class KeysModel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix="nabo-keys-model-")
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.lib=build(Path(cls.tmp.name),"keys-model",[ROOT/"firmware/nabo_keys_model.c"],{})
        if os.name=="nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary,cls.lib._handle)
        cls.lib.nabo_keys_packet.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.POINTER(Packet)]
        cls.lib.nabo_keys_packet.restype=ctypes.c_bool
        cls.lib.nabo_keys_override_packet.argtypes=[ctypes.c_ushort,ctypes.POINTER(Packet)]
        cls.lib.nabo_keys_override_packet.restype=ctypes.c_bool
        cls.lib.nabo_key_name.restype=ctypes.c_char_p
        cls.lib.nabo_keys_group_name.restype=ctypes.c_char_p
        cls.lib.nabo_keys_reverse_scroll.argtypes=[ctypes.POINTER(Packet)]
        cls.lib.nabo_keys_chord.argtypes=[ctypes.POINTER(Packet),ctypes.c_int,ctypes.c_char_p,ctypes.c_size_t]

    def packet(self,os,key):
        p=Packet();self.assertTrue(self.lib.nabo_keys_packet(os,key,ctypes.byref(p)));return p

    def test_all_actions_are_bounded_valid_and_named(self):
        for os in range(2):
            for k in range(25):
                p=self.packet(os,k)
                self.assertIn(p.report,(1,2,3))
                self.assertEqual(p.length,{1:8,2:2,3:4}[p.report])
                self.assertTrue(self.lib.nabo_key_name(k))
                if p.report==1:self.assertEqual(list(p.data[3:]),[0]*5)

    def test_typeless_mac_defaults_remain_independent(self):
        for i,k in enumerate(range(22,25)):
            self.assertEqual(list(self.packet(0,k).data),[0,0,0x68+i,0,0,0,0,0])

    def test_typeless_windows_matches_right_side_bindings(self):
        for k,(modifier,usage) in zip(range(22,25),((0x40,0),(0x60,0),(0x40,0x2c))):
            self.assertEqual(list(self.packet(1,k).data),[modifier,0,usage,0,0,0,0,0])

    def test_typeless_labels_distinguish_right_modifiers_and_fit(self):
        for k,expected in zip(range(22,25),(b'RAlt',b'RAlt+RSh',b'RAlt+Spc')):
            label=ctypes.create_string_buffer(16)
            packet=self.packet(1,k)
            self.lib.nabo_keys_chord(ctypes.byref(packet),1,label,len(label))
            self.assertEqual(label.value,expected)
            small=ctypes.create_string_buffer(4)
            self.lib.nabo_keys_chord(ctypes.byref(packet),1,small,len(small))
            self.assertEqual(small.raw[-1],0)

    def test_every_value_label_fits_the_column(self):
        for os in range(2):
            for k in range(25):
                label=ctypes.create_string_buffer(32)
                packet=self.packet(os,k)
                self.lib.nabo_keys_chord(ctypes.byref(packet),os,label,len(label))
                self.assertTrue(label.value);self.assertLessEqual(len(label.value),10)

    def test_scroll_is_a_wheel_and_mac_follows_natural_scrolling(self):
        up,down=0,1
        self.assertEqual(list(self.packet(1,up).data[:4]),[0,0,0,3])
        self.assertEqual(list(self.packet(1,down).data[:4]),[0,0,0,0xfd])
        self.assertEqual(list(self.packet(0,up).data[:4]),[0,0,0,0xfd])
        self.assertEqual(list(self.packet(0,down).data[:4]),[0,0,0,3])
        p=self.packet(1,up);self.lib.nabo_keys_reverse_scroll(ctypes.byref(p))
        self.assertEqual(p.data[3],0xfd)
        click=self.packet(1,3);self.lib.nabo_keys_reverse_scroll(ctypes.byref(click))
        self.assertEqual(list(click.data[:4]),[1,0,0,0])
        self.assertEqual(list(self.packet(0,4).data[:4]),[2,0,0,0])

    def test_back_switch_desktop_and_seek_are_platform_specific(self):
        self.assertEqual(list(self.packet(0,2).data[:3]),[8,0,0x2f])
        self.assertEqual(list(self.packet(1,2).data[:3]),[4,0,0x50])
        self.assertEqual(list(self.packet(0,10).data[:3]),[8,0,0x2b])
        self.assertEqual(list(self.packet(1,10).data[:3]),[4,0,0x2b])
        self.assertEqual(list(self.packet(0,11).data[:3]),[0,0,0x44])
        self.assertEqual(list(self.packet(1,11).data[:3]),[8,0,0x07])
        for os in range(2):
            self.assertEqual(list(self.packet(os,15).data[:3]),[0,0,0x50])
            self.assertEqual(list(self.packet(os,16).data[:3]),[0,0,0x4f])

    def test_editing_uses_command_or_control(self):
        for k in (7,8,9):
            self.assertEqual(self.packet(0,k).data[0],8)
            self.assertEqual(self.packet(1,k).data[0],1)

    def test_screenshots_and_media_have_correct_usage_pages(self):
        self.assertEqual(list(self.packet(0,12).data[:3]),[10,0,0x21])
        self.assertEqual(list(self.packet(1,12).data[:3]),[10,0,0x16])
        media={14:0xcd,17:0xe9,18:0xea,19:0xe2,20:0xb6,21:0xb5}
        for k,usage in media.items():
            p=self.packet(0,k);self.assertEqual((p.report,p.data[0],p.data[1]),(2,usage,0))

    def test_groups_cover_every_action_exactly_once(self):
        seen=[]
        for g in range(4):
            seen.extend(self.lib.nabo_keys_group_item(g,r) for r in range(self.lib.nabo_keys_group_count(g)))
        self.assertEqual(seen,list(range(25)))
        self.assertEqual(self.lib.nabo_keys_group_item(4,0),25)
        self.assertEqual(self.lib.nabo_keys_group_item(-1,0),25)
        self.assertEqual(self.lib.nabo_keys_group_name(4),b'')

    def test_v1_overrides_map_to_surviving_actions_only(self):
        expected={0:22,1:23,2:24,3:5,5:6,8:7,9:8,11:9,16:10,19:13,26:12}
        for v1 in range(-1,35):
            self.assertEqual(self.lib.nabo_keys_from_v1(v1),expected.get(v1,25))

    def test_invalid_inputs_and_custom_key_validation(self):
        p=Packet()
        for os,k in ((-1,0),(2,0),(0,-1),(0,25)):
            self.assertFalse(self.lib.nabo_keys_packet(os,k,ctypes.byref(p)))
        for encoded in (0,1,2,3,0x74,0xffff):
            self.assertFalse(self.lib.nabo_keys_override_packet(encoded,ctypes.byref(p)))
        self.assertTrue(self.lib.nabo_keys_override_packet(0x4000,ctypes.byref(p)))
        self.assertEqual(list(p.data[:3]),[0x40,0,0])


PLATFORM=r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGE(tag,...) ((void)(tag))
typedef void *QueueHandle_t;
typedef int StaticQueue_t;
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdPASS 1
QueueHandle_t xQueueCreateStatic(unsigned,unsigned,uint8_t *,StaticQueue_t *);
int xQueueReset(QueueHandle_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
typedef struct { unsigned type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
#define BLE_UUID16_INIT(x) {.u={16},.value=(x)}
struct os_mbuf { unsigned len; uint8_t data[160]; };
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
typedef int (*access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_dsc_def { const ble_uuid_t *uuid; unsigned att_flags;access_fn access_cb;void *arg; };
struct ble_gatt_chr_def { const ble_uuid_t *uuid;access_fn access_cb;void *arg;unsigned flags;uint16_t *val_handle;struct ble_gatt_dsc_def *descriptors; };
struct ble_gatt_svc_def { int type;const ble_uuid_t *uuid;struct ble_gatt_chr_def *characteristics; };
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_READ 1
#define BLE_GATT_CHR_F_READ_ENC 2
#define BLE_GATT_CHR_F_READ_AUTHEN 4
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_WRITE_ENC 16
#define BLE_GATT_CHR_F_WRITE_AUTHEN 32
#define BLE_GATT_CHR_F_NOTIFY 64
#define BLE_GATT_CHR_F_WRITE_NO_RSP 128
#define BLE_ATT_F_READ 1
enum { BLE_GATT_ACCESS_OP_READ_CHR, BLE_GATT_ACCESS_OP_WRITE_CHR, BLE_ATT_ERR_READ_NOT_PERMITTED=20,
 BLE_ATT_ERR_WRITE_NOT_PERMITTED, BLE_ATT_ERR_INSUFFICIENT_RES, BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN, BLE_ATT_ERR_UNLIKELY };
enum { BLE_GAP_EVENT_CONNECT, BLE_GAP_EVENT_DISCONNECT, BLE_GAP_EVENT_ENC_CHANGE, BLE_GAP_EVENT_SUBSCRIBE };
struct ble_gap_event { int type;struct { int status;uint16_t conn_handle; } connect,enc_change;
struct { uint16_t attr_handle;bool cur_notify; } subscribe; };
struct ble_gap_conn_desc { struct { bool encrypted,authenticated; } sec_state; };
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_ENOTCONN 30
#define BLE_HS_ENOMEM 31
#define BLE_ERR_REM_USER_CONN_TERM 19
#define OS_MBUF_PKTLEN(om) ((om)->len)
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int os_mbuf_copydata(struct os_mbuf *,unsigned,unsigned,void *);
int os_mbuf_free_chain(struct os_mbuf *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,unsigned);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
int ble_gap_security_initiate(uint16_t);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_gap_terminate(uint16_t,unsigned);
int ble_svc_gap_device_appearance_set(uint16_t);
typedef struct { int battery_pct; } muse_power_t;
muse_power_t muse_state_power(void);
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *,unsigned,nvs_handle_t *);
int nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_u8(nvs_handle_t,const char *,uint8_t);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
typedef struct {unsigned passkey;} muse_ble_status_t;
void muse_ble_status(muse_ble_status_t *);
void muse_link_ble_apply(void);
void muse_hatch_turn_cancel(void);
void nabo_radio_stop(void);
void nabo_tts_cancel(void);
typedef struct { uint32_t passkey; } nabo_pc_status_t;
void nabo_pc_request_apply(void);
void nabo_pc_status(nabo_pc_status_t *);
int nabo_pc_battery_level(void);
'''

RUNTIME=r'''
#include "platform.h"
#include "nabo_keys.c"
#include <assert.h>
static key_request_t queue_items[4];static unsigned count;
static int kicks,terminations,notifies,fail_notify,fail_task,security,flipped;
static int battery_pct,appearance;
static bool auth=true,nv_have,nv_have_v1,nv_have_v2;static uint8_t nv_os,nv_rev;
static uint16_t nv_maps[2][NABO_KEY_COUNT];static uint16_t nv_v1[2][NABO_KEYS_V1_COUNT];
static nabo_key_packet_t sent[12];
QueueHandle_t xQueueCreateStatic(unsigned n,unsigned size,uint8_t *buf,StaticQueue_t *ctrl) {
 (void)buf;(void)ctrl;assert(n==4 && size==sizeof(key_request_t));return queue_items;
}
int xQueueReset(QueueHandle_t q) {(void)q;count=0;return 1;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned wait) {(void)q;assert(!wait);if(count==4)return 0;queue_items[count++]=*(key_request_t *)p;return 1;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned wait) {
 (void)q;(void)wait;if(!count){atomic_store(&s_stop,true);return 0;}
 *(key_request_t *)p=queue_items[0];memmove(queue_items,queue_items+1,--count*sizeof(*queue_items));return 1;
}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned stack,void *a,unsigned prio,void *h) {
 (void)fn;(void)n;(void)a;(void)prio;(void)h;assert(stack==3072);return !fail_task;
}
void vTaskDelete(void *arg){(void)arg;}
void vTaskDelay(unsigned n){if(n==45 && flipped){atomic_fetch_add(&s_generation,1);flipped=0;}}
int os_mbuf_append(struct os_mbuf *om,const void *p,size_t len){assert(om->len+len<=sizeof(om->data));memcpy(om->data+om->len,p,len);om->len+=len;return 0;}
int os_mbuf_copydata(struct os_mbuf *om,unsigned at,unsigned n,void *out){if(at+n>om->len)return -1;memcpy(out,om->data+at,n);return 0;}
int os_mbuf_free_chain(struct os_mbuf *om){(void)om;return 0;}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p,unsigned n){static struct os_mbuf om;om.len=0;os_mbuf_append(&om,p,n);return &om;}
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *p){(void)p;return 0;}
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *p){unsigned n=1;for(;p->uuid;++p)for(struct ble_gatt_chr_def *c=p->characteristics;c->uuid;++c)if(c->val_handle)*c->val_handle=n++;return 0;}
int ble_gatts_notify_custom(uint16_t conn,uint16_t handle,struct os_mbuf *om){
 assert(conn==7);assert(notifies<12);nabo_key_packet_t *p=&sent[notifies++];
 p->report=handle==s_battery ? 9 : handle==s_mouse ? 3 : (handle==s_consumer ? 2 : 1);p->len=om->len;memcpy(p->data,om->data,om->len);
 return fail_notify && (fail_notify==1 || notifies>1) ? BLE_HS_ENOMEM : 0;
}
int ble_gap_security_initiate(uint16_t c){assert(c==7);++security;return 0;}
int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc *d){(void)c;d->sec_state.encrypted=true;d->sec_state.authenticated=auth;return 0;}
int ble_gap_terminate(uint16_t c,unsigned reason){assert(c==7 && reason==19);++terminations;return 0;}
int ble_svc_gap_device_appearance_set(uint16_t value){appearance=value;return 0;}
muse_power_t muse_state_power(void){return (muse_power_t){.battery_pct=battery_pct};}
int nvs_open(const char *ns,unsigned mode,nvs_handle_t *h){assert(!strcmp(ns,"nabo_keys"));*h=1;return mode==NVS_READONLY && !nv_have ? -1 : 0;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *out){(void)h;*out=!strcmp(k,"os") ? nv_os : nv_rev;return 0;}
int nvs_get_blob(nvs_handle_t h,const char *k,void *out,size_t *len){
 (void)h;
 if(!strcmp(k,"maps_v2")){if(!nv_have_v2)return -1;assert(*len==sizeof(nv_maps));memcpy(out,nv_maps,*len);return 0;}
 assert(!strcmp(k,"maps_v1"));if(!nv_have_v1)return -1;assert(*len==sizeof(nv_v1));memcpy(out,nv_v1,*len);return 0;
}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t n){(void)h;if(!strcmp(k,"os"))nv_os=n;else{assert(!strcmp(k,"scroll_rev"));nv_rev=n;}return 0;}
int nvs_set_blob(nvs_handle_t h,const char *k,const void *in,size_t len){(void)h;assert(!strcmp(k,"maps_v2") && len==sizeof(nv_maps));memcpy(nv_maps,in,len);nv_have_v2=true;return 0;}
int nvs_commit(nvs_handle_t h){(void)h;nv_have=true;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
void muse_ble_status(muse_ble_status_t *b){b->passkey=0;}
void muse_link_ble_apply(void){++kicks;}
void muse_hatch_turn_cancel(void){}
void nabo_radio_stop(void){}
void nabo_tts_cancel(void){}
void nabo_pc_request_apply(void){++kicks;}
void nabo_pc_status(nabo_pc_status_t *b){b->passkey=0;}
int nabo_pc_battery_level(void){return battery_pct;}
void fixture_init(void){
 atomic_store(&s_wanted,false);atomic_store(&s_running,false);atomic_store(&s_loaded,false);
 atomic_store(&s_stop,false);atomic_store(&s_os,0);atomic_store(&s_revision,0);atomic_store(&s_result,0);
 kicks=terminations=notifies=fail_notify=fail_task=security=flipped=0;nv_have=nv_have_v1=nv_have_v2=false;nv_rev=0;nv_os=0;atomic_store(&s_scroll_reverse,false);auth=true;count=0;battery_pct=73;
 nabo_keys_prepare();atomic_store(&s_os,NABO_KEYS_MAC);nabo_keys_open();nabo_keys_register();nabo_keys_start();
 struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={0,7}};nabo_keys_gap(&e);
 e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=7;nabo_keys_gap(&e);
 e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.attr_handle=s_keyboard;e.subscribe.cur_notify=true;nabo_keys_gap(&e);
 e.subscribe.attr_handle=s_consumer;nabo_keys_gap(&e);e.subscribe.attr_handle=s_boot;nabo_keys_gap(&e);
 e.subscribe.attr_handle=s_mouse;nabo_keys_gap(&e);
 assert(security==1);
}
int fixture_case(int which){
 fixture_init();
 if(which==0){assert(nabo_keys_send(NABO_KEY_COPY));sender(NULL);assert(notifies==2);assert(sent[0].data[0]==8 && sent[0].data[2]==6);for(int i=0;i<8;++i)assert(!sent[1].data[i]);}
 if(which==1){assert(nabo_keys_send(NABO_KEY_PLAY));sender(NULL);assert(notifies==2 && sent[0].report==2 && sent[0].len==2 && sent[0].data[0]==0xcd && sent[1].data[0]==0);}
 if(which==2){for(int i=0;i<4;++i)assert(nabo_keys_send(NABO_KEY_DICTATE));assert(!nabo_keys_send(NABO_KEY_DICTATE));nabo_keys_close();sender(NULL);assert(!count && !notifies);}
 if(which==3){assert(nabo_keys_send(NABO_KEY_COPY));atomic_fetch_add(&s_generation,1);sender(NULL);assert(!notifies);}
 if(which==4){fail_notify=2;assert(nabo_keys_send(NABO_KEY_COPY));sender(NULL);assert(notifies==4 && terminations==1);}
 if(which==5){fail_notify=1;assert(nabo_keys_send(NABO_KEY_COPY));sender(NULL);assert(notifies==1 && terminations==0);}
 if(which==6){atomic_store(&s_secure,false);assert(!nabo_keys_send(NABO_KEY_COPY));atomic_store(&s_secure,true);atomic_store(&s_suspended,true);assert(!nabo_keys_send(NABO_KEY_COPY));atomic_store(&s_suspended,false);atomic_store(&s_protocol,false);assert(!nabo_keys_send(NABO_KEY_PLAY));assert(nabo_keys_send(NABO_KEY_COPY));sender(NULL);assert(notifies==2);}
 if(which==7){nabo_keys_toggle_os();char cmd[]="keys.map=1,22,64,0";assert(nabo_keys_command(cmd));sender(NULL);assert(nv_have && nv_os==1 && nv_maps[1][NABO_KEY_DICTATE]==0x4000);atomic_store(&s_loaded,false);atomic_store(&s_revision,0);load_preferences();nabo_key_packet_t p;nabo_keys_resolve(NABO_KEY_DICTATE,&p);assert(p.data[0]==64 && p.data[2]==0);}
 if(which==8){flipped=1;assert(nabo_keys_send(NABO_KEY_COPY));sender(NULL);assert(notifies==1 && terminations==0);}
 if(which==9){atomic_store(&s_running,false);fail_task=1;nabo_keys_start();assert(!nabo_keys_wanted() && !atomic_load(&s_running) && kicks>=2);}
 if(which==10){struct os_mbuf om={0};struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&om};assert(!access_value(7,0,&ctx,(void *)ACCESS_MAP));assert(om.len==sizeof(report_map));for(struct ble_gatt_chr_def *c=services[0].characteristics;c->uuid;++c)if(c->val_handle)assert((c->flags&READ_ENC)==READ_ENC);uint8_t bad=2;om.len=0;os_mbuf_append(&om,&bad,1);ctx.op=BLE_GATT_ACCESS_OP_WRITE_CHR;assert(access_value(7,0,&ctx,(void *)ACCESS_PROTOCOL)!=0);}
 if(which==11){auth=false;struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={0,7}};nabo_keys_gap(&e);assert(!nabo_keys_send(NABO_KEY_COPY));e.type=BLE_GAP_EVENT_DISCONNECT;nabo_keys_gap(&e);assert(atomic_load(&s_conn)==BLE_HS_CONN_HANDLE_NONE && !atomic_load(&s_subscribed));}
 if(which==12){char *commands[]={"keys.map=1,0,255,255","keys.map=2,0,1,6","keys.map=4294967296,0,1,6","keys.map=0,0,0,18446744073709551616","keys.map=0,0,1,6x","keys.map=0,-1,1,6"};for(unsigned i=0;i<sizeof(commands)/sizeof(*commands);++i)nabo_keys_command(commands[i]);assert(atomic_load(&s_maps[1][0])==DEFAULT_MAP && atomic_load(&s_revision)==0);char far[]="keys.map=0,25,1,6";nabo_keys_command(far);assert(atomic_load(&s_revision)==0);}
 if(which==13){
  assert(appearance==0x03c1);assert(((const ble_uuid16_t *)services[1].uuid)->value==0x180a);
  assert(((const ble_uuid16_t *)services[2].uuid)->value==0x180f && !services[3].uuid);
  struct os_mbuf om={0};struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&om};
  assert(!access_value(7,0,&ctx,(void *)ACCESS_PNP));uint8_t pnp[]={1,255,255,1,0,0,1};assert(om.len==7 && !memcmp(om.data,pnp,7));
  om.len=0;assert(!access_value(7,0,&ctx,(void *)ACCESS_BATTERY) && om.len==1 && om.data[0]==73);
  battery_pct=-1;assert(access_value(7,0,&ctx,(void *)ACCESS_BATTERY)!=0);battery_pct=101;assert(access_value(7,0,&ctx,(void *)ACCESS_BATTERY)!=0);
  nabo_keys_close();nabo_keys_register();assert(appearance==0);
 }
 if(which==14){
  unsigned generation=atomic_load(&s_generation);assert(notify_battery(-1,generation)==-1 && !notifies);
  struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={s_battery,true}};nabo_keys_gap(&e);
  assert(notify_battery(-1,generation)==73 && notifies==1 && sent[0].report==9 && sent[0].len==1 && sent[0].data[0]==73);
  assert(notify_battery(73,generation)==73 && notifies==1);battery_pct=72;
  assert(notify_battery(73,generation)==72 && notifies==2);
  atomic_store(&s_secure,false);assert(notify_battery(73,generation)==73 && notifies==2);
  atomic_store(&s_secure,true);atomic_fetch_add(&s_generation,1);assert(notify_battery(73,generation)==73 && notifies==2);
 }
 if(which==15){
  /* Metadata must trigger authentication before the host constructs HID,
   * rather than waiting for the first protected input-report read. */
  unsigned metadata=0;
  for(struct ble_gatt_chr_def *c=services[0].characteristics;c->uuid;++c){
   unsigned kind=(uintptr_t)c->arg;
   if(kind==ACCESS_INFO || kind==ACCESS_MAP){
    assert((c->flags&READ_ENC)==READ_ENC);++metadata;
   }
  }
  assert(metadata==2);
 }
 if(which==16){
  /* Windows wheel up then all-zero release on the mouse report. */
  atomic_store(&s_os,NABO_KEYS_WINDOWS);
  assert(nabo_keys_send(NABO_KEY_SCROLL_UP));sender(NULL);
  assert(notifies==2 && sent[0].report==3 && sent[0].len==4 && (int8_t)sent[0].data[3]==3);
  for(int i=0;i<4;++i){assert(!sent[1].data[i]);}
  assert(sent[1].len==4 && sent[1].report==3);
 }
 if(which==17){
  /* Reverse flips the wheel, persists, and survives a reload. */
  atomic_store(&s_os,NABO_KEYS_WINDOWS);nabo_keys_toggle_scroll();assert(nabo_keys_scroll_reversed());
  assert(nabo_keys_send(NABO_KEY_SCROLL_DOWN));sender(NULL);
  assert(notifies==2 && (int8_t)sent[0].data[3]==3 && nv_rev==1 && nv_have_v2);
  atomic_store(&s_loaded,false);atomic_store(&s_revision,0);atomic_store(&s_scroll_reverse,false);
  load_preferences();assert(nabo_keys_scroll_reversed());
  nabo_key_packet_t p;nabo_keys_resolve(NABO_KEY_CLICK,&p);assert(p.report==3 && p.data[0]==1 && !p.data[3]);
 }
 if(which==18){
  /* A v1.2 device keeps Typeless/Copy overrides; dropped actions vanish. */
  memset(nv_v1,255,sizeof(nv_v1));nv_v1[1][0]=0x4010;nv_v1[0][8]=0x0806;nv_v1[1][20]=0x0117;nv_have_v1=true;nv_have=true;nv_os=1;
  atomic_store(&s_loaded,false);atomic_store(&s_revision,0);load_preferences();
  assert(atomic_load(&s_maps[1][NABO_KEY_DICTATE])==0x4010 && atomic_load(&s_maps[0][NABO_KEY_COPY])==0x0806);
  for(int o=0;o<2;++o)for(int k=0;k<NABO_KEY_COUNT;++k)
   if(!(o==1&&k==NABO_KEY_DICTATE)&&!(o==0&&k==NABO_KEY_COPY))assert(atomic_load(&s_maps[o][k])==DEFAULT_MAP);
  assert(nabo_keys_os()==NABO_KEYS_WINDOWS);
  /* A saved v2 table wins over v1. */
  nv_maps[1][NABO_KEY_DICTATE]=0x4000;for(int o=0;o<2;++o)for(int k=0;k<NABO_KEY_COUNT;++k)if(!(o==1&&k==NABO_KEY_DICTATE))nv_maps[o][k]=0xffff;nv_have_v2=true;
  atomic_store(&s_loaded,false);load_preferences();assert(atomic_load(&s_maps[1][NABO_KEY_DICTATE])==0x4000 && atomic_load(&s_maps[0][NABO_KEY_COPY])==DEFAULT_MAP);
 }
 if(which==19){
  /* Computer paired with the old keyboard-only map: wheel never subscribed. */
  struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={s_mouse,false}};nabo_keys_gap(&e);
  assert(!nabo_keys_send(NABO_KEY_SCROLL_UP));char text[96];nabo_keys_status(text,sizeof(text));
  assert(strstr(text,"重新配对"));assert(nabo_keys_send(NABO_KEY_COPY));
 }
 if(which==20){
  /* Boot protocol carries keyboard only. */
  atomic_store(&s_protocol,false);assert(!nabo_keys_send(NABO_KEY_SCROLL_DOWN));assert(!nabo_keys_send(NABO_KEY_CLICK));
  assert(nabo_keys_send(NABO_KEY_ENTER));
  struct os_mbuf om={0};struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&om};
  assert(!access_value(7,0,&ctx,(void *)ACCESS_MOUSE) && om.len==4);
 }
 return 1;
}
'''


class KeysRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix="nabo-keys-runtime-");cls.addClassCleanup(cls.tmp.cleanup)
        tmp=Path(cls.tmp.name);(tmp/"runtime.c").write_text(RUNTIME)
        headers={"platform.h":PLATFORM}
        for name in ("freertos/FreeRTOS.h","freertos/queue.h","freertos/task.h","host/ble_hs.h","host/ble_gatt.h","services/gap/ble_svc_gap.h","os/os_mbuf.h","nvs.h","esp_log.h","muse_ble.h","muse_link.h","muse_chat.h","muse_state.h","nabo_radio.h","nabo_tts.h"):
            headers[name]='#include "platform.h"\n'
        # Runtime is included unchanged; the temporary copy keeps quoted
        # boundary headers on the fake platform instead of the SDK headers.
        for name in ("nabo_keys.c","nabo_keys.h","nabo_keys_model.h"):
            (tmp/name).write_bytes((ROOT/"firmware"/name).read_bytes())
        headers["nabo_pc.h"]='#include "platform.h"\n'
        cls.lib=build(tmp,"keys-runtime",[tmp/"runtime.c",ROOT/"firmware/nabo_keys_model.c"],headers)
        if os.name=="nt":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary,cls.lib._handle)


for i,name in enumerate(("keyboard_keyup","consumer_keyup","bounded_queue_exit","stale_connection","release_failure_disconnect","press_failure","suspend_boot","platform_map_persistence","disconnect_during_press","allocation_failure_restore","gatt_security_map","authentication_disconnect","invalid_usb_map","hogp_services_identity","battery_subscription_security_generation","encrypted_hid_discovery","mouse_wheel_keyup","scroll_reverse_persists","v1_override_migration","stale_pairing_hint","boot_protocol_keyboard_only")):
    def check(self,case=i):self.assertEqual(self.lib.fixture_case(case),1)
    setattr(KeysRuntime,"test_"+name,check)


if __name__=="__main__":unittest.main()
