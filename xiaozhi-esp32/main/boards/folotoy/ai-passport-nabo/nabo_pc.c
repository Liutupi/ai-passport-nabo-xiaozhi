/* The local-app worker is the sole NimBLE lifecycle owner. HID remains the
 * reviewed, encrypted keyboard service imported from the user's NABO project. */
#include "nabo_pc.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nabo_keys.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);
static atomic_bool requested, running;
/* Owned only by the local-app worker; a reset retry must not stop the host twice. */
static bool host_stopped;
static atomic_uint passkey;
static uint8_t addr_type;

static void advertise(void);
static int gap(struct ble_gap_event* ev, void* arg) {
    (void)arg;
    nabo_keys_gap(ev);
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status)
                advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            atomic_store(&passkey, 0);
            if (nabo_keys_wanted())
                advertise();
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
                struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP,
                                       .passkey = 100000 + esp_random() % 900000};
                atomic_store(&passkey, io.passkey);
                ble_sm_inject_io(ev->passkey.conn_handle, &io);
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            atomic_store(&passkey, 0);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc desc;
            if (!ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc))
                ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            break;
    }
    return 0;
}
static void advertise(void) {
    if (!nabo_keys_wanted() || ble_gap_adv_active())
        return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03c1;
    fields.appearance_is_present = 1;
    static const ble_uuid16_t hid = BLE_UUID16_INIT(0x1812);
    fields.uuids16 = (ble_uuid16_t*)&hid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    const char* name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                        .disc_mode = BLE_GAP_DISC_MODE_GEN};
    if (!rc)
        rc = ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &params, gap, NULL);
    ESP_LOGI("NABO-PC", "keyboard advertising rc=%d", rc);
}
static void sync_host(void) {
    if (!ble_hs_id_infer_auto(0, &addr_type))
        advertise();
}
static void host_task(void* arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
void nabo_pc_request_apply(void) { atomic_store(&requested, true); }
bool nabo_pc_running(void) { return atomic_load(&running); }
void nabo_pc_status(nabo_pc_status_t* out) { out->passkey = atomic_load(&passkey); }
void nabo_pc_apply(void) {
    if (!atomic_exchange(&requested, false))
        return;
    if (nabo_keys_wanted() && !nabo_pc_running()) {
        nabo_keys_prepare();
        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE("NABO-PC", "init failed: %d", err);
            nabo_keys_close();
            return;
        }
        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_svc_gap_device_name_set("NABO-Xiaozhi-6D68");
        ble_hs_cfg.sync_cb = sync_host;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_mitm = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_store_config_init();
        nabo_keys_register();
        host_stopped = false;
        atomic_store(&running, true);
        nabo_keys_start();
        nimble_port_freertos_init(host_task);
    } else if ((!nabo_keys_wanted() || host_stopped) && nabo_pc_running()) {
        if (!nabo_keys_stop()) {
            atomic_store(&requested, true);
            return;
        }
        if (!host_stopped) {
            ble_gap_adv_stop();
            int rc = nimble_port_stop();
            if (rc) {
                ESP_LOGW("NABO-PC", "stop retry: %d", rc);
                atomic_store(&requested, true);
                return;
            }
            host_stopped = true;
        }
        /* IDF's dynamic ATT pool is freed before ble_gatts_stop() during
         * deinit. Return the registered attributes first, after GAP/peers stop,
         * so runtime-mempool teardown does not assert with live blocks. */
        int rc = ble_gatts_reset();
        if (rc) {
            ESP_LOGW("NABO-PC", "GATT reset retry: %d", rc);
            atomic_store(&requested, true);
            return;
        }
        esp_err_t err = nimble_port_deinit();
        if (err != ESP_OK) {
            ESP_LOGE("NABO-PC", "deinit failed: %d", err);
            return;
        }
        host_stopped = false;
        atomic_store(&running, false);
        atomic_store(&passkey, 0);
        if (nabo_keys_wanted())
            atomic_store(&requested, true);
        ESP_LOGI("NABO-PC", "keyboard stopped");
    }
}
