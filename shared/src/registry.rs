use crate::effects::{OffEffect, RainbowEffect, SolidEffect};
use crate::{Effect, EffectId};

pub struct EffectRegistry;

impl EffectRegistry {
    pub fn get(id: EffectId) -> &'static dyn Effect {
        match id {
            EffectId::Off => &OffEffect,
            EffectId::Solid => &SolidEffect,
            EffectId::Rainbow => &RainbowEffect,
        }
    }

    pub fn all() -> &'static [EffectId] {
        &[EffectId::Off, EffectId::Solid, EffectId::Rainbow]
    }
}
