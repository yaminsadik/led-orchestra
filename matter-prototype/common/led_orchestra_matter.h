#pragma once

#include <stdint.h>

namespace led_orchestra {
namespace matter {

// Development vendor id 0xFFF1 plus manufacturer-specific cluster range 0xFC00.
// Replace the vendor id before any production or certified build.
static constexpr uint32_t kClusterId = 0xFFF1FC00;
static constexpr uint16_t kEndpointIdHint = 1;
static constexpr uint16_t kGroupAllNodes = 0x0001;

namespace attribute {
static constexpr uint32_t kCurrentScene = 0x00000000;
static constexpr uint32_t kSegmentStart = 0x00000001;
static constexpr uint32_t kSegmentLength = 0x00000002;
static constexpr uint32_t kTotalLeds = 0x00000003;
static constexpr uint32_t kLedGpio = 0x00000004;
static constexpr uint32_t kFirmwareVersion = 0x00000005;
static constexpr uint32_t kLastSequence = 0x00000006;
} // namespace attribute

namespace command {
static constexpr uint32_t kSetScene = 0x00000000;
static constexpr uint32_t kSetNodeConfig = 0x00000001;
static constexpr uint32_t kSyncClock = 0x00000002;
} // namespace command

namespace set_scene_tag {
static constexpr uint8_t kEffect = 0;
static constexpr uint8_t kRed = 1;
static constexpr uint8_t kGreen = 2;
static constexpr uint8_t kBlue = 3;
static constexpr uint8_t kSpeed = 4;
static constexpr uint8_t kBrightness = 5;
static constexpr uint8_t kSequence = 6;
static constexpr uint8_t kScheduledStartMs = 7;
} // namespace set_scene_tag

namespace set_node_config_tag {
static constexpr uint8_t kNodeId = 0;
static constexpr uint8_t kSegmentStart = 1;
static constexpr uint8_t kSegmentLength = 2;
static constexpr uint8_t kTotalLeds = 3;
static constexpr uint8_t kLedGpio = 4;
} // namespace set_node_config_tag

namespace sync_clock_tag {
static constexpr uint8_t kControllerTimeMs = 0;
} // namespace sync_clock_tag

enum EffectId : uint8_t {
    kEffectOff = 0,
    kEffectSolid = 1,
    kEffectRainbow = 2,
    // Per-pixel color whose R/G/B channels are consecutive Fibonacci numbers
    // (mod 256) along the virtual strip; the sequence scrolls down the strip
    // with speed. Append-only: never reorder or reuse effect ids.
    kEffectFibonacci = 3,
};

} // namespace matter
} // namespace led_orchestra
