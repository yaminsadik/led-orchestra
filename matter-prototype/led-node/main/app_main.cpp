#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/thread.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include "led_orchestra_cluster.h"
#include "led_orchestra_renderer.h"
#include "openthread_config.h"

static const char *TAG = "lo_led_node";
uint16_t light_endpoint_id = 0;

// Stage D/E instrumentation: emit the two quantitative gate metrics (min free
// heap, largest free block) on a fixed cadence so heap drift is greppable across
// power-cycle and soak runs. See the Pass/Fail Metrics table in
// docs/controller-topology-validation.md.
static void heap_stats_task(void *)
{
    for (;;) {
        ESP_LOGI("lo_heap", "free=%u min_free=%u largest=%u",
                 (unsigned) esp_get_free_heap_size(),
                 (unsigned) esp_get_minimum_free_heap_size(),
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// OTA rollback health gate. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE makes a
// freshly-applied OTA image boot in PENDING_VERIFY; until the app confirms it,
// the bootloader will revert to the previous slot on the next reset. We only
// confirm once the node proves it is operational — OpenThread attached to the
// mesh. If it never attaches inside the window the image is bad/unreachable, so
// we deliberately roll back and reboot. For a node bolted to a wall with no USB
// access this is the difference between a recoverable bad update and a brick.
// (esp-matter's OTA requestor may also confirm the image; mark-valid is
// idempotent, so this app-level gate is a safe belt-and-suspenders.)
static void ota_rollback_health_task(void *)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        // Factory boot or an already-confirmed image: nothing to verify.
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGW(TAG, "running a PENDING_VERIFY OTA image; awaiting Thread attach before confirming");
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5 * 60 * 1000);
    bool attached = false;
    while (xTaskGetTickCount() < deadline) {
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
        if (esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
            otDeviceRole role = otThreadGetDeviceRole(esp_openthread_get_instance());
            esp_openthread_lock_release();
            if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
                role == OT_DEVICE_ROLE_LEADER) {
                attached = true;
                break;
            }
        }
#else
        attached = true;
        break;
#endif
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (attached && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "OTA image confirmed valid (Thread attached); rollback cancelled");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGE(TAG, "new OTA image did not become operational; rolling back to previous slot");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // Unreachable if rollback is enabled; the call reboots into the prior slot.
    vTaskDelete(nullptr);
}

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "interface IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "commissioning session stopped");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto timeout = chip::System::Clock::Seconds16(300);
            if (!mgr.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(timeout,
                                                                  chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "failed to reopen commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *)
{
    ESP_LOGI(TAG, "identify type=%u endpoint=%u effect=%u variant=%u", type, endpoint_id, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t, uint16_t, uint32_t, uint32_t,
                                         esp_matter_attr_val_t *, void *)
{
    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_log_level_set("lo_heap", ESP_LOG_INFO);
    xTaskCreate(heap_stats_task, "heap_stats", 3072, nullptr, tskIDLE_PRIORITY + 1, nullptr);

    ESP_ERROR_CHECK(led_orchestra_renderer_start());

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ESP_ERROR_CHECK(node == nullptr ? ESP_FAIL : ESP_OK);

    on_off_light::config_t light_config;
    endpoint_t *endpoint = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    ESP_ERROR_CHECK(endpoint == nullptr ? ESP_FAIL : ESP_OK);

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "LED Orchestra Matter endpoint id=%u", light_endpoint_id);

    ESP_ERROR_CHECK(led_orchestra_cluster_create(endpoint));

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    // Confirm or roll back a freshly-applied OTA image once Thread is up.
    xTaskCreate(ota_rollback_health_task, "ota_health", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
