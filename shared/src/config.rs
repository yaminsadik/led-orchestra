use crate::{EffectId, EffectParams};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SegmentConfig {
    pub segment_id: u16,
    pub segment_start: u16,
    pub segment_len: u16,
}

/// Per-node config. Phase 1: baked into the firmware binary.
/// Phase 3: loaded from flash, provisioned over WiFi so a single firmware
/// image runs on all boards.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NodeConfig {
    pub node_id: u16,
    pub led_gpio_pin: u8,
    pub segment: SegmentConfig,
    /// Total LEDs across the whole virtual strip.
    /// Phase 1: equals `segment.segment_len` (one node = the whole strip).
    pub total_leds: u16,
}

/// Resolved scene the node should render.
/// Phase 5+: controller resolves the override priority chain
/// (emergency > segment > group > global) before producing this.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ActiveScene {
    pub effect: EffectId,
    pub params: EffectParams,
}
