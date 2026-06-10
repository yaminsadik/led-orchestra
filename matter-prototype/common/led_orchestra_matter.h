#pragma once

#include <stdint.h>

namespace led_orchestra {
namespace matter {

// Development vendor id 0xFFF1 plus manufacturer-specific cluster range 0xFC00.
// Replace the vendor id before any production or certified build.
static constexpr uint32_t kClusterId = 0xFFF1FC00;
static constexpr uint16_t kEndpointIdHint = 1;

// Default "all LED nodes" application group. An application group id is a plain
// 16-bit id (0x0001..0xFEFF); it is encoded into a Matter group NodeId with
// chip::NodeIdFromGroupId(group) (= 0xFFFFFFFFFFFF0000 | group) before a
// groupcast invoke. Group `0x0001` is the conventional all-nodes group for the
// orchestra. Group ids are an append-only namespace: never reuse a retired one.
static constexpr uint16_t kGroupAllNodes = 0x0001;

// Standard Matter Groups cluster (0x0004), used unicast on each LED endpoint to
// enroll that endpoint into an application group. Once an endpoint is a group
// member, a groupcast invoke of the LED Orchestra custom cluster reaches it.
// These are SDK/spec ids, restated here so the controller console does not pull
// the full zap-generated cluster objects.
namespace groups_cluster {
static constexpr uint32_t kClusterId = 0x00000004;
static constexpr uint32_t kCommandAddGroup = 0x00000000;     // AddGroup(group_id, group_name)
static constexpr uint32_t kCommandRemoveGroup = 0x00000003;  // RemoveGroup(group_id)
namespace add_group_tag {
static constexpr uint8_t kGroupId = 0;
static constexpr uint8_t kGroupName = 1;
} // namespace add_group_tag
} // namespace groups_cluster

// Group key material defaults for the development fabric. The controller must
// install a group keyset and bind it to the application group before groupcast
// custom-cluster commands are accepted by member nodes (see
// `lo-show-group-help` and docs/console.md). These are dev/test values only;
// production rotates real epoch keys through the Kubernetes control plane.
namespace group_key {
static constexpr uint16_t kDefaultKeysetId = 0x0042;
// Key policy 0 = TrustFirst (CacheAndSync requires synced time). 16-byte epoch
// key as a 32-char hex octet string for `controller group-settings add-keyset`.
static constexpr uint8_t kDefaultKeyPolicy = 0;
} // namespace group_key

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
    // Soft overlapping RGB waves with a global breathe curve. Designed to avoid
    // hard primary-only bands on sparse/short bench strips.
    kEffectAuroraBreathe = 4,
};

} // namespace matter
} // namespace led_orchestra
