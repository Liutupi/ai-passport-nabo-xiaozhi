/* One small, temporary sender task; Link remains the sole NimBLE owner.
 * All LVGL-facing operations are bounded. Pairing keeps Muse's existing
 * authenticated encryption. No credentials, audio or text cross this service. */
#include "nabo_keys.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "os/os_mbuf.h"
#include "nvs.h"
#include "esp_log.h"
#include "nabo_pc.h"
#include "nabo_radio.h"

#define DEFAULT_MAP UINT16_MAX
static atomic_bool s_wanted, s_running, s_stop, s_secure, s_suspended, s_scroll_reverse;
static atomic_uint s_conn=ATOMIC_VAR_INIT(BLE_HS_CONN_HANDLE_NONE), s_generation, s_subscribed;
static atomic_uint s_os, s_revision, s_result;
static atomic_bool s_protocol=ATOMIC_VAR_INIT(true);
static atomic_ushort s_maps[NABO_KEYS_OS_COUNT][NABO_KEY_COUNT];
static atomic_bool s_loaded;
static QueueHandle_t s_queue;
static StaticQueue_t s_queue_control;
typedef struct { uint32_t generation; nabo_key_packet_t packet; } key_request_t;
static uint8_t s_queue_data[4*sizeof(key_request_t)];
static uint16_t s_keyboard, s_consumer, s_mouse, s_boot, s_battery;
static const char *TAG="nabo.keys";

/* HID keyboard (report 1), Consumer controls (report 2) and a wheel mouse
 * (report 3). Report IDs are carried by each GATT Report Reference, not
 * prepended to notifications. Hosts cache this map: after it changes, remove
 * the bonded device on the computer and pair again. */
static const uint8_t report_map[]={
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x75,0x08,0x95,0x01,0x81,0x01,
    0x19,0x00,0x29,0x73,0x15,0x00,0x25,0x73,0x75,0x08,0x95,0x06,0x81,0x00,
    0x05,0x08,0x19,0x01,0x29,0x05,0x75,0x01,0x95,0x05,0x91,0x02,
    0x75,0x03,0x95,0x01,0x91,0x01,0xc0,
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,
    0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0,
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x03,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,
    0x95,0x01,0x75,0x03,0x81,0x01,
    0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x03,0x81,0x06,
    0xc0,0xc0
};
static const ble_uuid16_t hid_uuid=BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t info_uuid=BLE_UUID16_INIT(0x2a4a), map_uuid=BLE_UUID16_INIT(0x2a4b);
static const ble_uuid16_t control_uuid=BLE_UUID16_INIT(0x2a4c), report_uuid=BLE_UUID16_INIT(0x2a4d);
static const ble_uuid16_t protocol_uuid=BLE_UUID16_INIT(0x2a4e), boot_in_uuid=BLE_UUID16_INIT(0x2a22);
static const ble_uuid16_t boot_out_uuid=BLE_UUID16_INIT(0x2a32), ref_uuid=BLE_UUID16_INIT(0x2908);
static const ble_uuid16_t dis_uuid=BLE_UUID16_INIT(0x180a), pnp_uuid=BLE_UUID16_INIT(0x2a50);
static const ble_uuid16_t bas_uuid=BLE_UUID16_INIT(0x180f), battery_uuid=BLE_UUID16_INIT(0x2a19);
enum { ACCESS_INFO, ACCESS_MAP, ACCESS_CONTROL, ACCESS_PROTOCOL, ACCESS_KEYBOARD, ACCESS_CONSUMER, ACCESS_LEDS,
       ACCESS_PNP, ACCESS_BATTERY, ACCESS_MOUSE };

static int battery_level(void)
{
    int pct=nabo_pc_battery_level();
    return pct>=0 && pct<=100 ? pct : -1;
}

static int access_value(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)conn; (void)attr;
    unsigned kind=(uintptr_t)arg;
    uint8_t b[8]={0}; const void *data=b; size_t len=0;
    if (ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        switch(kind) {
        case ACCESS_INFO: b[0]=0x11;b[1]=0x01;b[3]=0x02;len=4;break;
        case ACCESS_MAP:data=report_map;len=sizeof(report_map);break;
        case ACCESS_PROTOCOL:b[0]=atomic_load(&s_protocol);len=1;break;
        case ACCESS_KEYBOARD:len=8;break;
        case ACCESS_CONSUMER:len=2;break;
        case ACCESS_MOUSE:len=4;break;
        case ACCESS_LEDS:len=1;break;
        /* Experimental Bluetooth vendor ID, not an assigned USB vendor ID. */
        case ACCESS_PNP:b[0]=1;b[1]=0xff;b[2]=0xff;b[3]=1;b[6]=1;len=7;break;
        case ACCESS_BATTERY: {
            int pct=battery_level();
            if (pct<0) return BLE_ATT_ERR_UNLIKELY;
            b[0]=(uint8_t)pct;len=1;break;
        }
        default:return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        return os_mbuf_append(ctx->om,data,len)==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctx->op==BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (OS_MBUF_PKTLEN(ctx->om)!=1 || os_mbuf_copydata(ctx->om,0,1,b)!=0)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (kind==ACCESS_LEDS) return 0;
        if (b[0]>1) return BLE_ATT_ERR_UNLIKELY;
        if (kind==ACCESS_CONTROL) atomic_store(&s_suspended,b[0]==0);
        else if (kind==ACCESS_PROTOCOL) atomic_store(&s_protocol,b[0]!=0);
        else return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static int access_ref(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)conn;(void)attr;
    uint8_t ref[2]={(uintptr_t)arg&255,((uintptr_t)arg>>8)&255};
    return os_mbuf_append(ctx->om,ref,2)==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
#define READ_ENC (BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN)
#define WRITE_ENC (BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN)
#define REFERENCE(id,type) (struct ble_gatt_dsc_def[]){{.uuid=&ref_uuid.u,.att_flags=BLE_ATT_F_READ,.access_cb=access_ref,.arg=(void *)((type)*256+(id))},{0}}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&hid_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        /* Gate discovery too: macOS can abandon HID creation if it reads the
         * map before the first passkey pairing has finished encrypting. */
        {.uuid=&info_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_INFO,.flags=READ_ENC},
        {.uuid=&map_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_MAP,.flags=READ_ENC},
        {.uuid=&control_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_CONTROL,.flags=WRITE_ENC},
        {.uuid=&protocol_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_PROTOCOL,.flags=READ_ENC|WRITE_ENC},
        {.uuid=&report_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_KEYBOARD,.flags=READ_ENC|BLE_GATT_CHR_F_NOTIFY,
         .val_handle=&s_keyboard,.descriptors=REFERENCE(1,1)},
        {.uuid=&report_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_CONSUMER,.flags=READ_ENC|BLE_GATT_CHR_F_NOTIFY,
         .val_handle=&s_consumer,.descriptors=REFERENCE(2,1)},
        {.uuid=&report_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_MOUSE,.flags=READ_ENC|BLE_GATT_CHR_F_NOTIFY,
         .val_handle=&s_mouse,.descriptors=REFERENCE(3,1)},
        {.uuid=&report_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_LEDS,.flags=READ_ENC|WRITE_ENC|BLE_GATT_CHR_F_WRITE,
         .descriptors=REFERENCE(1,2)},
        {.uuid=&boot_in_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_KEYBOARD,.flags=READ_ENC|BLE_GATT_CHR_F_NOTIFY,.val_handle=&s_boot},
        {.uuid=&boot_out_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_LEDS,.flags=READ_ENC|WRITE_ENC|BLE_GATT_CHR_F_WRITE},
        {0}}},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&dis_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&pnp_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_PNP,.flags=BLE_GATT_CHR_F_READ},
        {0}}},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&bas_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&battery_uuid.u,.access_cb=access_value,.arg=(void *)ACCESS_BATTERY,
         .flags=READ_ENC|BLE_GATT_CHR_F_NOTIFY,.val_handle=&s_battery},
        {0}}},
    {0}
};

bool nabo_keys_wanted(void) { return atomic_load(&s_wanted); }
void nabo_keys_apply(void) { nabo_pc_request_apply(); }
nabo_keys_os_t nabo_keys_os(void) { return (nabo_keys_os_t)atomic_load(&s_os); }
void nabo_keys_open(void)
{
    atomic_store(&s_result,0);
    atomic_store(&s_wanted,true);
    nabo_pc_request_apply();
}
void nabo_keys_close(void)
{
    atomic_store(&s_stop,true);
    atomic_store(&s_wanted,false);
    nabo_pc_request_apply();
}
void nabo_keys_toggle_os(void)
{
    atomic_store(&s_os,nabo_keys_os()==NABO_KEYS_MAC ? NABO_KEYS_WINDOWS : NABO_KEYS_MAC);
    atomic_fetch_add(&s_revision,1);
}
void nabo_keys_resolve(nabo_key_t key, nabo_key_packet_t *out)
{
    nabo_keys_os_t os=nabo_keys_os();
    if ((unsigned)key>=NABO_KEY_COUNT || !out) return;
    uint16_t encoded=s_loaded ? atomic_load(&s_maps[os][key]) : DEFAULT_MAP;
    if (encoded==DEFAULT_MAP || !nabo_keys_override_packet(encoded,out)) nabo_keys_packet(os,key,out);
    if (atomic_load(&s_scroll_reverse)) nabo_keys_reverse_scroll(out);
}
bool nabo_keys_scroll_reversed(void) { return atomic_load(&s_scroll_reverse); }
void nabo_keys_toggle_scroll(void)
{
    atomic_store(&s_scroll_reverse,!atomic_load(&s_scroll_reverse));
    atomic_fetch_add(&s_revision,1);
}
static void load_preferences(void)
{
    if (s_loaded) return;
    uint16_t maps[NABO_KEYS_OS_COUNT][NABO_KEY_COUNT];
    memset(maps,255,sizeof(maps));
    nvs_handle_t h; uint8_t os=NABO_KEYS_WINDOWS, reverse=0;
    if (nvs_open("nabo_keys",NVS_READONLY,&h)==ESP_OK) {
        size_t len=sizeof(maps);
        if (nvs_get_blob(h,"maps_v2",maps,&len)!=ESP_OK || len!=sizeof(maps)) {
            /* v1.0-v1.2 table: keep overrides of the actions that remain. */
            uint16_t old[NABO_KEYS_OS_COUNT][NABO_KEYS_V1_COUNT];
            memset(maps,255,sizeof(maps));
            len=sizeof(old);
            if (nvs_get_blob(h,"maps_v1",old,&len)==ESP_OK && len==sizeof(old))
                for (int o=0;o<NABO_KEYS_OS_COUNT;++o) for (int k=0;k<NABO_KEYS_V1_COUNT;++k) {
                    nabo_key_t now=nabo_keys_from_v1(k);
                    if (now<NABO_KEY_COUNT) maps[o][now]=old[o][k];
                }
        }
        nvs_get_u8(h,"os",&os);
        nvs_get_u8(h,"scroll_rev",&reverse);
        nvs_close(h);
    }
    nabo_key_packet_t p;
    for (int o=0;o<NABO_KEYS_OS_COUNT;++o) for(int k=0;k<NABO_KEY_COUNT;++k)
        atomic_store(&s_maps[o][k],nabo_keys_override_packet(maps[o][k],&p) ? maps[o][k] : DEFAULT_MAP);
    if (!atomic_load(&s_revision)) {
        atomic_store(&s_os,os<NABO_KEYS_OS_COUNT ? os : 0);
        atomic_store(&s_scroll_reverse,reverse==1);
    }
    s_loaded=true;
}
static bool save_preferences(void)
{
    uint16_t maps[NABO_KEYS_OS_COUNT][NABO_KEY_COUNT];
    for (int o=0;o<NABO_KEYS_OS_COUNT;++o) for(int k=0;k<NABO_KEY_COUNT;++k) maps[o][k]=atomic_load(&s_maps[o][k]);
    nvs_handle_t h;
    if (nvs_open("nabo_keys",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t e=nvs_set_blob(h,"maps_v2",maps,sizeof(maps));
    if(e==ESP_OK) e=nvs_set_u8(h,"os",nabo_keys_os());
    if(e==ESP_OK) e=nvs_set_u8(h,"scroll_rev",atomic_load(&s_scroll_reverse) ? 1 : 0);
    if(e==ESP_OK) e=nvs_commit(h);
    nvs_close(h);
    return e==ESP_OK;
}
void nabo_keys_prepare(void)
{
    load_preferences();
    nabo_radio_stop();
}
void nabo_keys_register(void)
{
    atomic_store(&s_conn,BLE_HS_CONN_HANDLE_NONE);
    atomic_store(&s_secure,false); atomic_store(&s_subscribed,0);
    atomic_store(&s_suspended,false); atomic_store(&s_protocol,true);
    int rc=ble_svc_gap_device_appearance_set(nabo_keys_wanted() ? 0x03c1 : 0);
    if (!rc) rc=ble_gatts_count_cfg(services);
    if (!rc) rc=ble_gatts_add_svcs(services);
    if (rc) { ESP_LOGE(TAG,"HID registration rc=%d",rc); atomic_store(&s_result,3);atomic_store(&s_wanted,false);nabo_pc_request_apply(); }
}
void nabo_keys_gap(struct ble_gap_event *ev)
{
    if (ev->type==BLE_GAP_EVENT_CONNECT && ev->connect.status==0) {
        atomic_fetch_add(&s_generation,1);atomic_store(&s_conn,ev->connect.conn_handle);
        atomic_store(&s_secure,false);atomic_store(&s_subscribed,0);
        atomic_store(&s_protocol,true);atomic_store(&s_suspended,false);
        if (nabo_keys_wanted()) ble_gap_security_initiate(ev->connect.conn_handle);
    } else if (ev->type==BLE_GAP_EVENT_DISCONNECT) {
        atomic_fetch_add(&s_generation,1);atomic_store(&s_conn,BLE_HS_CONN_HANDLE_NONE);
        atomic_store(&s_secure,false);atomic_store(&s_subscribed,0);
        atomic_store(&s_result,0);
    } else if (ev->type==BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc d;
        bool secure=ev->enc_change.status==0 && ble_gap_conn_find(ev->enc_change.conn_handle,&d)==0 &&
            d.sec_state.encrypted && d.sec_state.authenticated;
        atomic_store(&s_secure,secure);
    } else if (ev->type==BLE_GAP_EVENT_SUBSCRIBE) {
        uint16_t a=ev->subscribe.attr_handle;
        unsigned mask=a==s_keyboard ? 1 : a==s_consumer ? 2 : a==s_boot ? 4 : a==s_battery ? 8 :
            a==s_mouse ? 16 : 0;
        if (ev->subscribe.cur_notify) atomic_fetch_or(&s_subscribed,mask);
        else atomic_fetch_and(&s_subscribed,~mask);
    }
}
static bool ready(uint8_t report)
{
    /* Consumer and mouse reports exist only in Report protocol mode. */
    unsigned mask=report==2 ? 2 : report==3 ? 16 : (atomic_load(&s_protocol) ? 1 : 4);
    return nabo_keys_wanted() && atomic_load(&s_running) && !atomic_load(&s_stop) &&
        atomic_load(&s_secure) && !atomic_load(&s_suspended) && (atomic_load(&s_subscribed)&mask) &&
        (report==1 || atomic_load(&s_protocol));
}
static int notify(const nabo_key_packet_t *p, uint32_t generation, uint16_t handle, bool release)
{
    if (atomic_load(&s_generation)!=generation || !atomic_load(&s_secure)) return BLE_HS_ENOTCONN;
    uint8_t zero[8]={0};
    struct os_mbuf *om=ble_hs_mbuf_from_flat(release ? zero : p->data,p->len);
    return om ? ble_gatts_notify_custom(atomic_load(&s_conn),handle,om) : BLE_HS_ENOMEM;
}
static int notify_battery(int previous, uint32_t generation)
{
    if (!atomic_load(&s_secure) || atomic_load(&s_suspended) ||
        !(atomic_load(&s_subscribed)&8)) return previous;
    int pct=battery_level();
    if (pct<0 || pct==previous) return previous;
    uint8_t value=(uint8_t)pct;
    struct os_mbuf *om=ble_hs_mbuf_from_flat(&value,1);
    if (!om) return previous;
    /* The sender is joined before the host is stopped, as for key reports. */
    if (atomic_load(&s_generation)!=generation) {
        os_mbuf_free_chain(om);return previous;
    }
    return ble_gatts_notify_custom(atomic_load(&s_conn),s_battery,om)==0 ? pct : previous;
}
static void sender(void *arg)
{
    (void)arg;
    unsigned saved=0;
    unsigned battery_tick=0, battery_generation=atomic_load(&s_generation);
    int last_battery=-1;
    while (!atomic_load(&s_stop) && nabo_keys_wanted()) {
        unsigned revision=atomic_load(&s_revision);
        if (revision!=saved && save_preferences()) saved=revision;
        unsigned generation_now=atomic_load(&s_generation);
        if (generation_now!=battery_generation) {
            battery_generation=generation_now;last_battery=-1;
        }
        /* About once a second while idle; no extra task or timer. */
        if (battery_tick++%25==0) last_battery=notify_battery(last_battery,battery_generation);
        key_request_t request;
        if (xQueueReceive(s_queue,&request,pdMS_TO_TICKS(40))!=pdTRUE) continue;
        if (request.generation!=atomic_load(&s_generation)) continue;
        nabo_key_packet_t p=request.packet;
        if (!p.report) {
            unsigned c=atomic_load(&s_conn);
            if(c!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(c,BLE_ERR_REM_USER_CONN_TERM);
            continue;
        }
        if (!ready(p.report)) { atomic_store(&s_result,2);continue; }
        uint32_t generation=request.generation;
        uint16_t handle=p.report==2 ? s_consumer : p.report==3 ? s_mouse :
            (atomic_load(&s_protocol) ? s_keyboard : s_boot);
        int rc=notify(&p,generation,handle,false);
        if (!rc) {
            vTaskDelay(pdMS_TO_TICKS(45));
            for (int i=0;i<3;++i) {
                rc=notify(&p,generation,handle,true);
                if (!rc || rc==BLE_HS_ENOTCONN) break;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            /* A failed key-up must not leave Cmd/Ctrl held on the computer. */
            if (rc && atomic_load(&s_generation)==generation) ble_gap_terminate(atomic_load(&s_conn),BLE_ERR_REM_USER_CONN_TERM);
        }
        atomic_store(&s_result,rc ? 2 : 1);
        vTaskDelay(pdMS_TO_TICKS(35));
    }
    if (atomic_load(&s_revision)!=saved && !save_preferences()) atomic_store(&s_result,4);
    xQueueReset(s_queue);
    atomic_store(&s_running,false);
    vTaskDelete(NULL);
}
void nabo_keys_start(void)
{
    if (!nabo_keys_wanted() || atomic_load(&s_running)) return;
    if (!s_queue) s_queue=xQueueCreateStatic(4,sizeof(key_request_t),s_queue_data,&s_queue_control);
    xQueueReset(s_queue);
    atomic_store(&s_stop,false);atomic_store(&s_running,true);
    if (xTaskCreate(sender,"nabo_keys",3072,NULL,4,NULL)!=pdPASS) {
        atomic_store(&s_running,false);atomic_store(&s_wanted,false);atomic_store(&s_result,3);
        nabo_pc_request_apply();
    }
}
bool nabo_keys_stop(void)
{
    atomic_store(&s_stop,true);
    for (int i=0;i<150 && atomic_load(&s_running);++i) vTaskDelay(pdMS_TO_TICKS(10));
    return !atomic_load(&s_running);
}
bool nabo_keys_send(nabo_key_t key)
{
    key_request_t request={.generation=atomic_load(&s_generation)};
    if ((unsigned)key>=NABO_KEY_COUNT) return false;
    nabo_keys_resolve(key,&request.packet);
    bool ok=ready(request.packet.report) && xQueueSend(s_queue,&request,0)==pdTRUE;
    /* A computer paired before v1.3 cached the keyboard-only report map:
     * keys work but the wheel was never subscribed. Say how to fix it. */
    if (!ok) atomic_store(&s_result,request.packet.report==3 && ready(1) &&
                         !(atomic_load(&s_subscribed)&16) ? 5 : 2);
    return ok;
}
bool nabo_keys_reconnect(void)
{
    key_request_t request={.generation=atomic_load(&s_generation)};
    return nabo_keys_wanted() && atomic_load(&s_running) && xQueueSend(s_queue,&request,0)==pdTRUE;
}
void nabo_keys_status(char *out,size_t cap)
{
    unsigned result=atomic_load(&s_result);
    nabo_pc_status_t b; nabo_pc_status(&b);
    if (b.passkey) snprintf(out,cap,"配对码：%06u",(unsigned)b.passkey);
    else if (result==3) snprintf(out,cap,"内存不足，请退出重试");
    else if (result==4) snprintf(out,cap,"设置未保存，请重试");
    else if (result==5) snprintf(out,cap,"滑动需在电脑上删除\n本设备后重新配对");
    else if (!nabo_keys_wanted()) snprintf(out,cap,"正在恢复小智");
    else if (!atomic_load(&s_running)) snprintf(out,cap,"正在切换蓝牙");
    else if (atomic_load(&s_conn)==BLE_HS_CONN_HANDLE_NONE) snprintf(out,cap,"等待电脑连接");
    else if (!atomic_load(&s_secure)) snprintf(out,cap,"请完成电脑蓝牙配对");
    else if (atomic_load(&s_suspended)) snprintf(out,cap,"电脑已暂停键盘");
    else if (!ready(1)) snprintf(out,cap,"等待键盘就绪");
    else snprintf(out,cap,"%s / %s",nabo_keys_os()==NABO_KEYS_MAC ? "Mac" : "Windows",result==1 ? "已发送" : (result==2 ? "未发送，请重试" : "已连接"));
}
bool nabo_keys_command(char *line)
{
    if (!strcmp(line,"keys.status")) {
        char status[96];nabo_keys_status(status,sizeof(status));printf("%s\n",status);return true;
    }
    if (strncmp(line,"keys.map=",9)) return false;
    /* Exact decimal USB format: OS,index,modifier-byte,keyboard-usage. A
     * 65535 usage resets that action. No commands/macros/text are executed. */
    unsigned values[4]; char *at=line+9;
    for (int i=0;i<4;++i) {
        char *end;
        if (*at<'0' || *at>'9') { puts("keys.map invalid");return true; }
        errno=0;unsigned long value=strtoul(at,&end,10);
        if (errno || value>65535 || (i<3 ? *end!=',' : *end!=0)) { puts("keys.map invalid");return true; }
        values[i]=(unsigned)value;at=end+1;
    }
    unsigned o=values[0], k=values[1], m=values[2], u=values[3];
    if (o>=NABO_KEYS_OS_COUNT || k>=NABO_KEY_COUNT || m>255) { puts("keys.map invalid");return true; }
    nabo_key_packet_t p;
    uint16_t encoded=(uint16_t)((m<<8)|u);
    if (u!=65535 && (u>0x73 || !nabo_keys_override_packet(encoded,&p))) { puts("keys.map invalid");return true; }
    /* The keeper loads first. Console updates atomics; the sender alone
     * persists changes, without blocking button callbacks or LVGL. */
    if (!s_loaded || !nabo_keys_wanted()) { puts("keys.map open keyboard page first");return true; }
    atomic_store(&s_maps[o][k],u==65535 ? DEFAULT_MAP : encoded);
    atomic_fetch_add(&s_revision,1);
    puts("keys.map queued");return true;
}
