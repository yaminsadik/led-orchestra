use crate::Rgb;

/// Stable identifier for an effect. Wire format.
/// To add a new effect: 1) new file under `effects/`, 2) variant here,
/// 3) register in `EffectRegistry`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EffectId {
    Off,
    Solid,
    Rainbow,
}

impl EffectId {
    /// Stable numeric identifier used by the wire protocol.
    pub const fn wire_id(self) -> u8 {
        match self {
            EffectId::Off => 0,
            EffectId::Solid => 1,
            EffectId::Rainbow => 2,
        }
    }

    /// Converts a protocol effect id back into the shared enum.
    pub const fn from_wire_id(id: u8) -> Option<Self> {
        match id {
            0 => Some(EffectId::Off),
            1 => Some(EffectId::Solid),
            2 => Some(EffectId::Rainbow),
            _ => None,
        }
    }

    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "off" => Some(EffectId::Off),
            "solid" => Some(EffectId::Solid),
            "rainbow" => Some(EffectId::Rainbow),
            _ => None,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            EffectId::Off => "off",
            EffectId::Solid => "solid",
            EffectId::Rainbow => "rainbow",
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct EffectParams {
    /// Primary color (used by solid, accents, etc.).
    pub color: Rgb,
    /// Animation speed multiplier; 128 = nominal, 0 = stopped, 255 = fastest.
    pub speed: u8,
    /// Final per-pixel brightness, 0..=255.
    pub brightness: u8,
}

/// What an effect needs to know about the virtual strip and its current segment.
#[derive(Clone, Copy, Debug)]
pub struct EffectContext {
    pub total_leds: u16,
    pub segment_start: u16,
    pub segment_len: u16,
}

/// A pure function from `(global_index, time_ms)` to color.
/// `time_ms` is the *synchronized* time across all nodes once Phase 4 lands.
pub trait Effect {
    fn id(&self) -> EffectId;
    fn render(
        &self,
        params: &EffectParams,
        ctx: &EffectContext,
        global_index: u16,
        time_ms: u64,
    ) -> Rgb;
}
