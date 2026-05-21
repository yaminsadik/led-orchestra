//! Per-node hardcoded config for Phase 1.
//!
//! Phase 1: one ESP32-C3 drives one strip; this node *is* the whole virtual
//! strip. Phase 3 will load `NodeConfig` from flash (provisioned over WiFi)
//! so a single firmware image can run on all boards.

use led_orchestra_shared::{NodeConfig, SegmentConfig};

pub const LED_COUNT: usize = 60;

pub const NODE: NodeConfig = NodeConfig {
    node_id: 1,
    // ESP32-C3 dev boards typically expose GPIO2 next to the on-board RGB LED;
    // change this to whatever pin your strip's DIN is wired to.
    led_gpio_pin: 2,
    segment: SegmentConfig {
        segment_id: 1,
        segment_start: 0,
        segment_len: LED_COUNT as u16,
    },
    total_leds: LED_COUNT as u16,
};
