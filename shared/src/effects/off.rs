use crate::{Effect, EffectContext, EffectId, EffectParams, Rgb};

pub struct OffEffect;

impl Effect for OffEffect {
    fn id(&self) -> EffectId {
        EffectId::Off
    }

    fn render(
        &self,
        _params: &EffectParams,
        _ctx: &EffectContext,
        _global_index: u16,
        _time_ms: u64,
    ) -> Rgb {
        Rgb::BLACK
    }
}
