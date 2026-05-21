use crate::{Effect, EffectContext, EffectId, EffectParams, Rgb};

pub struct RainbowEffect;

impl Effect for RainbowEffect {
    fn id(&self) -> EffectId {
        EffectId::Rainbow
    }

    fn render(
        &self,
        params: &EffectParams,
        ctx: &EffectContext,
        global_index: u16,
        time_ms: u64,
    ) -> Rgb {
        let speed = params.speed.max(1) as u64;
        let time_hue = ((time_ms * speed) / 20) as u32;
        let pos_hue = (global_index as u32 * 256) / ctx.total_leds.max(1) as u32;
        let hue = ((time_hue + pos_hue) & 0xff) as u8;
        Rgb::from_hsv(hue, 255, 255).scale(params.brightness)
    }
}
