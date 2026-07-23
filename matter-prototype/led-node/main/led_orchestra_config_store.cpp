#include "led_orchestra_config_store.h"

#include <inttypes.h>
#include <string.h>

#include <esp_log.h>
#include <esp_rom_crc.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

#include "led_orchestra_effects.h"

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

// ---------------------------------------------------------------------------
// Scene store — persists the last *immediate* (non-scheduled) active scene.
// Shares the "lo_cfg" NVS namespace; scene record uses a different key.
// ---------------------------------------------------------------------------

constexpr char kSceneKey[] = "scene";

// 'L''E''D''N' (scene mnemonic: N = "now").
constexpr uint32_t kSceneMagic = 0x4C45444E;
constexpr uint16_t kSceneVersion = 1;

struct __attribute__((packed)) SceneRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t effect;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t speed;
    uint8_t brightness;
    uint8_t pad[2];
    uint32_t sequence;
    uint32_t crc32;
};

static_assert(sizeof(SceneRecord) == 24, "SceneRecord layout changed");

uint32_t scene_record_crc(const SceneRecord &r)
{
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&r), offsetof(SceneRecord, crc32));
}

LedOrchestraScene scene_defaults()
{
    // Mirrors g_scene default in led_orchestra_renderer.cpp; kept in sync with
    // the compiled initial scene so a clean boot and a post-power-cycle boot
    // with no persisted scene are indistinguishable.
    return LedOrchestraScene{
        .effect = 2, // rainbow
        .red = 0, .green = 0, .blue = 0,
        .speed = 128, .brightness = 40,
        .sequence = 0, .scheduled_start_ms = 0,
    };
}

bool scene_record_is_sane(const SceneRecord &r)
{
    if (r.magic != kSceneMagic) {
        ESP_LOGW(TAG, "stored scene magic mismatch (0x%08" PRIX32 ")", r.magic);
        return false;
    }
    if (r.version != kSceneVersion) {
        ESP_LOGW(TAG, "stored scene version %u unsupported (expected %u)", r.version, kSceneVersion);
        return false;
    }
    if (r.crc32 != scene_record_crc(r)) {
        ESP_LOGW(TAG, "stored scene CRC mismatch — ignoring");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Calibration store — persists the last accepted SetCalibration (per-node field
// tuning). Shares the "lo_cfg" NVS namespace under its own key.
// ---------------------------------------------------------------------------

constexpr char kCalibKey[] = "calib";

// 'L''E''D''C' (calibration mnemonic: C = "calibration").
constexpr uint32_t kCalibMagic = 0x4C454443;
constexpr uint16_t kCalibVersion = 1;

struct __attribute__((packed)) CalibRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    int32_t time_offset_ms;
    uint8_t brightness_cap;
    uint8_t palette_override;
    uint8_t pad[2];
    uint32_t color_correction;
    uint32_t color_temperature;
    uint32_t crc32;
};

static_assert(sizeof(CalibRecord) == 28, "CalibRecord layout changed");

uint32_t calib_record_crc(const CalibRecord &r)
{
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&r), offsetof(CalibRecord, crc32));
}

LedOrchestraCalibration calib_defaults()
{
    // Identity: matches g_calibration defaults in led_orchestra_renderer.cpp so a
    // clean boot and a no-record boot are indistinguishable (no tuning applied).
    return LedOrchestraCalibration{
        .time_offset_ms = 0,
        .brightness_cap = 255,
        .palette_override = kCalibNoPaletteOverride,
        .color_correction = 0x00FFFFFF,
        .color_temperature = 0x00FFFFFF,
    };
}

bool calib_record_is_sane(const CalibRecord &r)
{
    if (r.magic != kCalibMagic) {
        ESP_LOGW(TAG, "stored calibration magic mismatch (0x%08" PRIX32 ")", r.magic);
        return false;
    }
    if (r.version != kCalibVersion) {
        ESP_LOGW(TAG, "stored calibration version %u unsupported (expected %u)", r.version, kCalibVersion);
        return false;
    }
    if (r.crc32 != calib_record_crc(r)) {
        ESP_LOGW(TAG, "stored calibration CRC mismatch — ignoring");
        return false;
    }
    if (r.palette_override != kCalibNoPaletteOverride && led_orchestra_palette_meta(r.palette_override) == nullptr) {
        ESP_LOGW(TAG, "stored calibration palette override %u unknown — ignoring", r.palette_override);
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

esp_err_t led_orchestra_scene_load(LedOrchestraScene &out, bool &from_nvs)
{
    out = scene_defaults();
    from_nvs = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted scene namespace yet; using scene defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for scene failed: %s", esp_err_to_name(err));
        return err;
    }

    SceneRecord record = {};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, kSceneKey, &record, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted scene record yet; using scene defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob(scene) failed: %s; using scene defaults", esp_err_to_name(err));
        return ESP_OK;
    }
    if (size != sizeof(record) || !scene_record_is_sane(record)) {
        ESP_LOGW(TAG, "persisted scene unusable; using scene defaults");
        return ESP_OK;
    }

    out = LedOrchestraScene{
        .effect = record.effect,
        .red = record.red,
        .green = record.green,
        .blue = record.blue,
        .speed = record.speed,
        .brightness = record.brightness,
        .sequence = record.sequence,
        .scheduled_start_ms = 0, // never restore a scheduled-start deadline
    };
    from_nvs = true;
    return ESP_OK;
}

esp_err_t led_orchestra_scene_save(const LedOrchestraScene &scene)
{
    SceneRecord record = {};
    record.magic = kSceneMagic;
    record.version = kSceneVersion;
    record.reserved = 0;
    record.effect = scene.effect;
    record.red = scene.red;
    record.green = scene.green;
    record.blue = scene.blue;
    record.speed = scene.speed;
    record.brightness = scene.brightness;
    record.pad[0] = 0;
    record.pad[1] = 0;
    record.sequence = scene.sequence;
    record.crc32 = scene_record_crc(record);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(rw) for scene failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, kSceneKey, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist scene: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "scene persisted: effect=%u rgb=%u,%u,%u speed=%u brightness=%u seq=%" PRIu32,
             scene.effect, scene.red, scene.green, scene.blue, scene.speed, scene.brightness, scene.sequence);
    return ESP_OK;
}

esp_err_t led_orchestra_calibration_load(LedOrchestraCalibration &out, bool &from_nvs)
{
    out = calib_defaults();
    from_nvs = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted calibration namespace yet; using identity defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for calibration failed: %s", esp_err_to_name(err));
        return err;
    }

    CalibRecord record = {};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, kCalibKey, &record, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted calibration record yet; using identity defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob(calib) failed: %s; using identity defaults", esp_err_to_name(err));
        return ESP_OK;
    }
    if (size != sizeof(record) || !calib_record_is_sane(record)) {
        ESP_LOGW(TAG, "persisted calibration unusable; using identity defaults");
        return ESP_OK;
    }

    out = LedOrchestraCalibration{
        .time_offset_ms = record.time_offset_ms,
        .brightness_cap = record.brightness_cap,
        .palette_override = record.palette_override,
        .color_correction = record.color_correction,
        .color_temperature = record.color_temperature,
    };
    from_nvs = true;
    return ESP_OK;
}

esp_err_t led_orchestra_calibration_save(const LedOrchestraCalibration &calibration)
{
    CalibRecord record = {};
    record.magic = kCalibMagic;
    record.version = kCalibVersion;
    record.reserved = 0;
    record.time_offset_ms = calibration.time_offset_ms;
    record.brightness_cap = calibration.brightness_cap;
    record.palette_override = calibration.palette_override;
    record.pad[0] = 0;
    record.pad[1] = 0;
    record.color_correction = calibration.color_correction;
    record.color_temperature = calibration.color_temperature;
    record.crc32 = calib_record_crc(record);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(rw) for calibration failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, kCalibKey, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist calibration: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG,
             "calibration persisted: time_offset_ms=%" PRId32 " brightness_cap=%u palette_override=%u "
             "color_correction=0x%06" PRIX32 " color_temperature=0x%06" PRIX32,
             calibration.time_offset_ms, calibration.brightness_cap, calibration.palette_override,
             calibration.color_correction, calibration.color_temperature);
    return ESP_OK;
}
