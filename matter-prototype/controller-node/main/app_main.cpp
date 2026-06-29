#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_utils.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include "esp_ot_config.h"
#endif

#include "controller_wifi_ingress.h"
#include "led_orchestra_console.h"
#include "led_orchestra_ota_provider.h"

#if CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#endif

static const char *TAG = "lo_controller";

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

static void app_event_cb(const ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "commissioning session stopped");
        break;
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "interface IP address changed");
        break;
#if CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        // This node is the commissioner + OTA provider, never a commissionable
        // device. With the Matter server enabled (for the OTA provider cluster),
        // esp_matter::start() auto-opens a commissioning window and advertises
        // CHIPoBLE; that BLE peripheral role blocks the commissioner's BLE-central
        // scan, so `pairing ble-thread` connects to nothing. Close it so the
        // commissioner role owns BLE. The provider stays reachable over the
        // operational fabric (CASE), which does not need this window.
        ESP_LOGI(TAG, "closing self commissioning window (commissioner+provider, not commissionable)");
        chip::Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();
        break;
#endif
    default:
        break;
    }
}

extern "C" void app_main()
{
    // The build sets the default runtime log level to WARN so the monitor stays
    // readable. Re-raise the LED Orchestra tags so operator breadcrumbs (Wi-Fi
    // ingress, commissioning events, console command results) remain visible.
    esp_log_level_set("lo_controller", ESP_LOG_INFO);
    esp_log_level_set("lo_wifi_ingress", ESP_LOG_INFO);
    esp_log_level_set("lo_console", ESP_LOG_INFO);
    esp_log_level_set("lo_heap", ESP_LOG_INFO);
    esp_log_level_set("lo_ota", ESP_LOG_INFO);
    esp_log_level_set("ota_provider", ESP_LOG_INFO);
    esp_log_level_set("groupsettings", ESP_LOG_WARN);

    // Keep the noisy wifi:/OPENTHREAD: drivers at WARN, but surface Matter
    // commissioning progress so `pairing ble-thread` is debuggable: BLE link,
    // PASE/CASE secure sessions, controller stages, and operational discovery.
    // Lower these back toward WARN once commissioning is reliable.
    esp_log_level_set("chip[CTL]", ESP_LOG_INFO);
    esp_log_level_set("chip[DIS]", ESP_LOG_INFO);
    esp_log_level_set("chip[SC]", ESP_LOG_INFO);
    esp_log_level_set("chip[BLE]", ESP_LOG_INFO);
    esp_log_level_set("chip[DL]", ESP_LOG_INFO);

    // Group bring-up no longer needs verbose IM/DataModel logs on every boot.
    // Raise these temporarily when debugging KeySetWrite/GroupKeyMap/ACL issues.
    esp_log_level_set("chip[TOO]", ESP_LOG_WARN);
    esp_log_level_set("chip[DMG]", ESP_LOG_WARN);
    esp_log_level_set("chip[IM]", ESP_LOG_WARN);
    esp_log_level_set("chip[ZCL]", ESP_LOG_WARN);
    esp_log_level_set("chip[BDX]", ESP_LOG_INFO);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    // Offline OTA Provider scaffold (Phase 7). Node/cluster creation must happen
    // before esp_matter::start() sets up the data model. No-op unless
    // CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER is set.
    ESP_ERROR_CHECK(led_orchestra_ota_provider_init());

    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));
    ESP_ERROR_CHECK(led_orchestra_ota_provider_bind_delegate());

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    // TEMP DIAGNOSTIC (provider-on controller.init crash, 2026-06-28): capture heap
    // right before the controller's DeviceControllerFactory allocates its own
    // transport/session/exchange/CASE stack. If init fails with CHIP No memory
    // (0x1b), these numbers explain it. Remove once root cause is confirmed.
    ESP_LOGW(TAG, "pre-controller.init heap: free=%u min_free=%u largest=%u",
             (unsigned) esp_get_free_heap_size(),
             (unsigned) esp_get_minimum_free_heap_size(),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    auto &controller = esp_matter::controller::matter_controller_client::get_instance();
    ESP_ERROR_CHECK(controller.init(112233, 1, 5580));
    ESP_ERROR_CHECK(controller.setup_commissioner());
#if CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
    esp_matter::controller::set_fabric_index(controller.get_fabric_index());
#endif
    // LED nodes are always-on devices, not ICD/sleepy devices; skip the extra
    // StayActive commissioning stage that can time out before CommissioningComplete.
    esp_matter::controller::pairing_command::get_instance().set_icd_registration(false);
#if CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
    ESP_ERROR_CHECK(led_orchestra_ota_provider_bind_controller_exchange());
    // Production OTA requires the provider (hosted on the chip::Server data model)
    // to be reachable on the same operational fabric the commissioner pairs LED
    // nodes onto. The DeviceControllerFactory keeps a fabric table separate from
    // chip::Server's, so confirm the commissioner fabric is visible in the server
    // fabric table. If present_in_server_table=NO (or server_fabric_count=0), a
    // node's CASE-to-provider rides a different fabric than the cluster is hosted
    // on. Index match alone is weak — the compressed fabric id must also match.
    {
        auto &server_fabrics = chip::Server::GetInstance().GetFabricTable();
        chip::FabricIndex ctrl_fi = controller.get_fabric_index();
        const chip::FabricInfo *sf = server_fabrics.FindFabricWithIndex(ctrl_fi);
        ESP_LOGW(TAG,
                 "OTA fabric check: commissioner_fabric_index=%u server_fabric_count=%u present_in_server_table=%s",
                 (unsigned) ctrl_fi, (unsigned) server_fabrics.FabricCount(), sf ? "yes" : "NO");
    }
#endif
    esp_matter::lock::chip_stack_unlock();
#endif

    // Start Wi-Fi AP after the Matter stack is fully initialized. esp_wifi_init()
    // consumes a large heap block; starting it before esp_matter::start() exhausts
    // heap before the Matter main task stack can be allocated. esp-matter has
    // already called esp_netif_init() and esp_event_loop_create_default() by this
    // point, so controller_wifi_ingress_start()'s ensure_netif_and_events() is a
    // no-op and only the actual Wi-Fi init + AP start runs here.
    ESP_ERROR_CHECK(controller_wifi_ingress_start());

#if CONFIG_ENABLE_CHIP_SHELL
    ESP_ERROR_CHECK(led_orchestra_console_register_commands());
    ESP_ERROR_CHECK(led_orchestra_ota_provider_register_commands());
    esp_matter::console::init();
#endif

    xTaskCreate(heap_stats_task, "heap_stats", 3072, nullptr, tskIDLE_PRIORITY + 1, nullptr);

    ESP_LOGI(TAG, "LED Orchestra controller node ready");
}
