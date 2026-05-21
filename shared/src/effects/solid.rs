use crate::{Effect, EffectContext, EffectId, EffectParams, Rgb};

pub struct SolidEffect;

impl Effect for SolidEffect {
    fn id(&self) -> EffectId {
        EffectId::Solid
    }

    fn render(
        &self,
        params: &EffectParams,
        _ctx: &EffectContext,
        _global_index: u16,
        _time_ms: u64,
    ) -> Rgb {
        params.color.scale(params.brightness)
    }
}
