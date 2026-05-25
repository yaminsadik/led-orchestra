#include "led_orchestra_cluster.h"

#include <inttypes.h>
#include <string.h>

#include <app/data-model/Decode.h>
#include <app/server/Server.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_attribute.h>
#include <esp_matter_command.h>

#include "led_orchestra_matter.h"
#include "led_orchestra_renderer.h"

using namespace chip::app;
using namespace esp_matter;

extern uint16_t light_endpoint_id;

namespace {

static const char *TAG = "lo_cluster";
static constexpr uint16_t kClusterRevision = 1;
static char kFirmwareVersion[] = "0.1.0";

esp_err_t expect_target(const ConcreteCommandPath &path, uint32_t command_id)
{
    if (path.mEndpointId != light_endpoint_id || path.mClusterId != led_orchestra::matter::kClusterId ||
        path.mCommandId != command_id) {
        ESP_LOGE(TAG, "unexpected command endpoint=%u cluster=0x%" PRIX32 " command=0x%" PRIX32,
                 path.mEndpointId, path.mClusterId, path.mCommandId);
        return ESP_FAIL;
    }
    return ESP_OK;
}

template <typename T>
esp_err_t decode_value(TLVReader &reader, T &value)
{
    CHIP_ERROR err = DataModel::Decode(reader, value);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "failed to decode TLV value: %" CHIP_ERROR_FORMAT, err.Format());
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t enter_struct(TLVReader &reader, chip::TLV::TLVType &outer)
{
    if (reader.GetType() != chip::TLV::kTLVType_Structure) {
        ESP_LOGE(TAG, "command payload is not a TLV struct");
        return ESP_FAIL;
    }
    if (reader.EnterContainer(outer) != CHIP_NO_ERROR) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void update_u8(uint32_t attr_id, uint8_t value)
{
    esp_matter_attr_val_t val = esp_matter_uint8(value);
    attribute::update(light_endpoint_id, led_orchestra::matter::kClusterId, attr_id, &val);
}

void update_u16(uint32_t attr_id, uint16_t value)
{
    esp_matter_attr_val_t val = esp_matter_uint16(value);
    attribute::update(light_endpoint_id, led_orchestra::matter::kClusterId, attr_id, &val);
}

void update_u32(uint32_t attr_id, uint32_t value)
{
    esp_matter_attr_val_t val = esp_matter_uint32(value);
    attribute::update(light_endpoint_id, led_orchestra::matter::kClusterId, attr_id, &val);
}

esp_err_t handle_set_scene(const ConcreteCommandPath &path, TLVReader &tlv_data)
{
    ESP_RETURN_ON_ERROR(expect_target(path, led_orchestra::matter::command::kSetScene), TAG, "wrong command target");

    LedOrchestraScene scene = led_orchestra_get_scene();
    chip::TLV::TLVType outer;
    ESP_RETURN_ON_ERROR(enter_struct(tlv_data, outer), TAG, "bad SetScene payload");

    CHIP_ERROR err = CHIP_NO_ERROR;
    while ((err = tlv_data.Next()) == CHIP_NO_ERROR) {
        chip::TLV::Tag tag = tlv_data.GetTag();
        if (!chip::TLV::IsContextTag(tag)) {
            return ESP_FAIL;
        }

        switch (chip::TLV::TagNumFromTag(tag)) {
        case led_orchestra::matter::set_scene_tag::kEffect:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.effect), TAG, "bad effect");
            break;
        case led_orchestra::matter::set_scene_tag::kRed:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.red), TAG, "bad red");
            break;
        case led_orchestra::matter::set_scene_tag::kGreen:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.green), TAG, "bad green");
            break;
        case led_orchestra::matter::set_scene_tag::kBlue:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.blue), TAG, "bad blue");
            break;
        case led_orchestra::matter::set_scene_tag::kSpeed:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.speed), TAG, "bad speed");
            break;
        case led_orchestra::matter::set_scene_tag::kBrightness:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.brightness), TAG, "bad brightness");
            break;
        case led_orchestra::matter::set_scene_tag::kSequence:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.sequence), TAG, "bad sequence");
            break;
        case led_orchestra::matter::set_scene_tag::kScheduledStartMs:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, scene.scheduled_start_ms), TAG, "bad scheduled start");
            break;
        default:
            ESP_LOGW(TAG, "ignoring unknown SetScene tag %u", static_cast<unsigned>(chip::TLV::TagNumFromTag(tag)));
            break;
        }
    }

    if (err != CHIP_END_OF_TLV || tlv_data.ExitContainer(outer) != CHIP_NO_ERROR) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(led_orchestra_set_scene(scene), TAG, "failed to apply scene");
    update_u8(led_orchestra::matter::attribute::kCurrentScene, scene.effect);
    update_u32(led_orchestra::matter::attribute::kLastSequence, scene.sequence);
    return ESP_OK;
}

esp_err_t handle_set_node_config(const ConcreteCommandPath &path, TLVReader &tlv_data)
{
    ESP_RETURN_ON_ERROR(expect_target(path, led_orchestra::matter::command::kSetNodeConfig), TAG, "wrong command target");

    LedOrchestraNodeConfig config = led_orchestra_get_node_config();
    chip::TLV::TLVType outer;
    ESP_RETURN_ON_ERROR(enter_struct(tlv_data, outer), TAG, "bad SetNodeConfig payload");

    CHIP_ERROR err = CHIP_NO_ERROR;
    while ((err = tlv_data.Next()) == CHIP_NO_ERROR) {
        chip::TLV::Tag tag = tlv_data.GetTag();
        if (!chip::TLV::IsContextTag(tag)) {
            return ESP_FAIL;
        }

        switch (chip::TLV::TagNumFromTag(tag)) {
        case led_orchestra::matter::set_node_config_tag::kNodeId:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, config.node_id), TAG, "bad node id");
            break;
        case led_orchestra::matter::set_node_config_tag::kSegmentStart:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, config.segment_start), TAG, "bad segment start");
            break;
        case led_orchestra::matter::set_node_config_tag::kSegmentLength:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, config.segment_len), TAG, "bad segment length");
            break;
        case led_orchestra::matter::set_node_config_tag::kTotalLeds:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, config.total_leds), TAG, "bad total leds");
            break;
        case led_orchestra::matter::set_node_config_tag::kLedGpio:
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, config.led_gpio), TAG, "bad led gpio");
            break;
        default:
            ESP_LOGW(TAG, "ignoring unknown SetNodeConfig tag %u",
                     static_cast<unsigned>(chip::TLV::TagNumFromTag(tag)));
            break;
        }
    }

    if (err != CHIP_END_OF_TLV || tlv_data.ExitContainer(outer) != CHIP_NO_ERROR) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(led_orchestra_set_node_config(config), TAG, "failed to apply node config");
    update_u16(led_orchestra::matter::attribute::kSegmentStart, config.segment_start);
    update_u16(led_orchestra::matter::attribute::kSegmentLength, config.segment_len);
    update_u16(led_orchestra::matter::attribute::kTotalLeds, config.total_leds);
    update_u8(led_orchestra::matter::attribute::kLedGpio, config.led_gpio);
    return ESP_OK;
}

esp_err_t handle_sync_clock(const ConcreteCommandPath &path, TLVReader &tlv_data)
{
    ESP_RETURN_ON_ERROR(expect_target(path, led_orchestra::matter::command::kSyncClock), TAG, "wrong command target");

    uint64_t controller_time_ms = 0;
    chip::TLV::TLVType outer;
    ESP_RETURN_ON_ERROR(enter_struct(tlv_data, outer), TAG, "bad SyncClock payload");

    CHIP_ERROR err = CHIP_NO_ERROR;
    while ((err = tlv_data.Next()) == CHIP_NO_ERROR) {
        chip::TLV::Tag tag = tlv_data.GetTag();
        if (!chip::TLV::IsContextTag(tag)) {
            return ESP_FAIL;
        }
        if (chip::TLV::TagNumFromTag(tag) == led_orchestra::matter::sync_clock_tag::kControllerTimeMs) {
            ESP_RETURN_ON_ERROR(decode_value(tlv_data, controller_time_ms), TAG, "bad controller time");
        }
    }

    if (err != CHIP_END_OF_TLV || tlv_data.ExitContainer(outer) != CHIP_NO_ERROR) {
        return ESP_FAIL;
    }

    led_orchestra_sync_clock(controller_time_ms);
    return ESP_OK;
}

esp_err_t command_callback(const ConcreteCommandPath &path, TLVReader &tlv_data, void *)
{
    switch (path.mCommandId) {
    case led_orchestra::matter::command::kSetScene:
        return handle_set_scene(path, tlv_data);
    case led_orchestra::matter::command::kSetNodeConfig:
        return handle_set_node_config(path, tlv_data);
    case led_orchestra::matter::command::kSyncClock:
        return handle_sync_clock(path, tlv_data);
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

} // namespace

esp_err_t led_orchestra_cluster_create(esp_matter::endpoint_t *endpoint)
{
    ESP_RETURN_ON_FALSE(endpoint != nullptr, ESP_ERR_INVALID_ARG, TAG, "endpoint is null");

    cluster_t *custom_cluster = cluster::create(endpoint, led_orchestra::matter::kClusterId, CLUSTER_FLAG_SERVER);
    ESP_RETURN_ON_FALSE(custom_cluster != nullptr, ESP_FAIL, TAG, "failed to create custom cluster");

    LedOrchestraScene scene = led_orchestra_get_scene();
    LedOrchestraNodeConfig config = led_orchestra_get_node_config();

    cluster::global::attribute::create_cluster_revision(custom_cluster, kClusterRevision);
    cluster::global::attribute::create_feature_map(custom_cluster, 0);
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kCurrentScene, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint8(scene.effect));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kSegmentStart, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint16(config.segment_start));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kSegmentLength, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint16(config.segment_len));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kTotalLeds, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint16(config.total_leds));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kLedGpio, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint8(config.led_gpio));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kFirmwareVersion, ATTRIBUTE_FLAG_NONE,
                      esp_matter_char_str(kFirmwareVersion, strlen(kFirmwareVersion)), sizeof(kFirmwareVersion));
    attribute::create(custom_cluster, led_orchestra::matter::attribute::kLastSequence, ATTRIBUTE_FLAG_NONE,
                      esp_matter_uint32(scene.sequence));

    command::create(custom_cluster, led_orchestra::matter::command::kSetScene, COMMAND_FLAG_ACCEPTED | COMMAND_FLAG_CUSTOM,
                    command_callback);
    command::create(custom_cluster, led_orchestra::matter::command::kSetNodeConfig,
                    COMMAND_FLAG_ACCEPTED | COMMAND_FLAG_CUSTOM, command_callback);
    command::create(custom_cluster, led_orchestra::matter::command::kSyncClock, COMMAND_FLAG_ACCEPTED | COMMAND_FLAG_CUSTOM,
                    command_callback);

    ESP_LOGI(TAG, "custom cluster 0x%" PRIX32 " added to endpoint %u", led_orchestra::matter::kClusterId,
             endpoint::get_id(endpoint));
    return ESP_OK;
}
