#include "led_orchestra_config_store.h"

#include <inttypes.h>
#include <string.h>

#include <esp_log.h>
#include <esp_rom_crc.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

namespace {

static const char *TAG = "lo_cfg_store";

constexpr char kNamespace[] = "lo_cfg";
constexpr char kKey[] = "node";

// 'L''E''D''O' — distinguishes our record from any other blob under the key.
constexpr uint32_t kConfigMagic = 0x4C45444F;
// Append-only schema version. Bump and add a migration branch when fields change;
// never reorder existing fields (the LED Orchestra wire/storage contract is
// append-only). v1 is the initial durable layout.
constexpr uint16_t kConfigVersion = 1;

// Packed on-flash representation. Explicit fixed-width fields + reserved padding
// keep the layout stable across compilers; the trailing CRC covers everything
// before it.
struct __attribute__((packed)) ConfigRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved; // future flags / migration aid; written as 0
    uint16_t node_id;
    uint16_t segment_start;
    uint16_t segment_len;
    uint16_t total_leds;
    uint8_t led_gpio;
    uint8_t pad; // keep crc 4-byte aligned and layout deterministic
    uint32_t crc32;
};

uint32_t record_crc(const ConfigRecord &record)
{
    // CRC over every byte preceding the crc32 field.
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&record), offsetof(ConfigRecord, crc32));
}

LedOrchestraNodeConfig compiled_defaults()
{
    return LedOrchestraNodeConfig{
        .node_id = CONFIG_LED_ORCHESTRA_NODE_ID,
        .segment_start = CONFIG_LED_ORCHESTRA_SEGMENT_START,
        .segment_len = CONFIG_LED_ORCHESTRA_SEGMENT_LENGTH,
        .total_leds = CONFIG_LED_ORCHESTRA_TOTAL_LEDS,
        .led_gpio = CONFIG_LED_ORCHESTRA_LED_GPIO,
    };
}

bool record_is_sane(const ConfigRecord &record)
{
    if (record.magic != kConfigMagic) {
        ESP_LOGW(TAG, "stored config magic mismatch (0x%08" PRIX32 ")", record.magic);
        return false;
    }
    if (record.version != kConfigVersion) {
        // A newer/older firmware wrote a layout this build does not understand.
        // Fall back to defaults rather than guessing; add migration here later.
        ESP_LOGW(TAG, "stored config version %u unsupported (expected %u)", record.version, kConfigVersion);
        return false;
    }
    if (record.crc32 != record_crc(record)) {
        ESP_LOGW(TAG, "stored config CRC mismatch — ignoring corrupt record");
        return false;
    }
    if (record.segment_len == 0 || record.total_leds == 0) {
        ESP_LOGW(TAG, "stored config has empty segment/strip — ignoring");
        return false;
    }
    return true;
}

} // namespace

esp_err_t led_orchestra_config_load(LedOrchestraNodeConfig &out, bool &from_nvs)
{
    out = compiled_defaults();
    from_nvs = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted config namespace yet; using compiled defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    ConfigRecord record = {};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, kKey, &record, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted config record yet; using compiled defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s; using compiled defaults", esp_err_to_name(err));
        return ESP_OK; // defaults already in `out`; not a fatal boot error
    }
    if (size != sizeof(record) || !record_is_sane(record)) {
        ESP_LOGW(TAG, "persisted config unusable; using compiled defaults");
        return ESP_OK;
    }

    LedOrchestraNodeConfig loaded = {
        .node_id = record.node_id,
        .segment_start = record.segment_start,
        .segment_len = record.segment_len,
        .total_leds = record.total_leds,
        .led_gpio = record.led_gpio,
    };

    // The strip driver is initialized with the compile-time GPIO; a stored GPIO
    // that disagrees (e.g. firmware reflashed onto different wiring) would make
    // attributes lie about the real pin. Pin the GPIO to the compiled value and
    // surface the divergence rather than reporting an unused pin.
    if (loaded.led_gpio != CONFIG_LED_ORCHESTRA_LED_GPIO) {
        ESP_LOGW(TAG, "stored gpio %u != firmware gpio %u; pinning to firmware gpio", loaded.led_gpio,
                 CONFIG_LED_ORCHESTRA_LED_GPIO);
        loaded.led_gpio = CONFIG_LED_ORCHESTRA_LED_GPIO;
    }

    out = loaded;
    from_nvs = true;
    return ESP_OK;
}

esp_err_t led_orchestra_config_save(const LedOrchestraNodeConfig &config)
{
    ConfigRecord record = {};
    record.magic = kConfigMagic;
    record.version = kConfigVersion;
    record.reserved = 0;
    record.node_id = config.node_id;
    record.segment_start = config.segment_start;
    record.segment_len = config.segment_len;
    record.total_leds = config.total_leds;
    record.led_gpio = config.led_gpio;
    record.pad = 0;
    record.crc32 = record_crc(record);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(rw) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, kKey, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist config: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "config persisted: node=%u segment=[%u,%u) total=%u gpio=%u", config.node_id, config.segment_start,
             config.segment_start + config.segment_len, config.total_leds, config.led_gpio);
    return ESP_OK;
}
