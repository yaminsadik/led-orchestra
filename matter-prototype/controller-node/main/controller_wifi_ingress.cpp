#include "controller_wifi_ingress.h"

#include <cstring>

#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/inet.h>

static const char *TAG = "lo_wifi_ingress";

static esp_err_t ensure_netif_and_events()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return ESP_OK;
}

#if CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_AP
static esp_err_t start_private_ap()
{
    ESP_RETURN_ON_ERROR(ensure_netif_and_events(), TAG, "failed to init netif/event loop");

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "failed to init wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "failed to set wifi storage");

    wifi_config_t ap_config = {};
    const char *ssid = CONFIG_LED_ORCHESTRA_WIFI_AP_SSID;
    const char *password = CONFIG_LED_ORCHESTRA_WIFI_AP_PASSWORD;

    std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = std::strlen(reinterpret_cast<char *>(ap_config.ap.ssid));
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), password, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = CONFIG_LED_ORCHESTRA_WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = CONFIG_LED_ORCHESTRA_WIFI_AP_MAX_CONNECTIONS;
    ap_config.ap.pmf_cfg.required = false;

    if (std::strlen(password) >= 8) {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ap_config.ap.password[0] = '\0';
        ESP_LOGW(TAG, "controller AP is open because password is shorter than 8 characters");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "failed to set wifi AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "failed to configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start AP");

    ESP_LOGI(TAG, "operator Wi-Fi ingress AP started: ssid=%s channel=%u max_clients=%u",
             reinterpret_cast<const char *>(ap_config.ap.ssid),
             static_cast<unsigned>(ap_config.ap.channel),
             static_cast<unsigned>(ap_config.ap.max_connection));
    return ESP_OK;
}
#endif

#if CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_STA
static void station_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "operator Wi-Fi station disconnected; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "operator Wi-Fi station got IPv4 address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t start_station()
{
    if (std::strlen(CONFIG_LED_ORCHESTRA_WIFI_STA_SSID) == 0) {
        ESP_LOGW(TAG, "operator Wi-Fi station mode selected without CONFIG_LED_ORCHESTRA_WIFI_STA_SSID; Wi-Fi ingress disabled");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_netif_and_events(), TAG, "failed to init netif/event loop");

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "failed to init wifi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "failed to set wifi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &station_event_handler, nullptr),
                        TAG, "failed to register wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &station_event_handler, nullptr),
                        TAG, "failed to register ip event handler");

    wifi_config_t sta_config = {};
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.ssid), CONFIG_LED_ORCHESTRA_WIFI_STA_SSID,
                 sizeof(sta_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.password), CONFIG_LED_ORCHESTRA_WIFI_STA_PASSWORD,
                 sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set wifi station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "failed to configure station");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start station");

    ESP_LOGI(TAG, "operator Wi-Fi ingress station starting: ssid=%s",
             reinterpret_cast<const char *>(sta_config.sta.ssid));
    return ESP_OK;
}
#endif

esp_err_t controller_wifi_ingress_start()
{
#if CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_AP
    return start_private_ap();
#elif CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_STA
    return start_station();
#else
    ESP_LOGI(TAG, "operator Wi-Fi ingress disabled; USB serial remains available");
    return ESP_OK;
#endif
}
