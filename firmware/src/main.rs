#![no_std]
#![no_main]

mod node_config;

use esp_backtrace as _;
use esp_hal::{delay::Delay, main, rmt::Rmt, time::Rate};
use esp_hal_smartled::{smart_led_buffer, SmartLedsAdapter};
use smart_leds::{SmartLedsWrite, RGB8};

use led_orchestra_shared::{
    ActiveScene, EffectContext, EffectId, EffectParams, EffectRegistry, Rgb,
};

use node_config::{LED_COUNT, NODE};

esp_bootloader_esp_idf::esp_app_desc!();

// 60Hz target. WS2812B RMT write takes ~30µs per LED — 60 LEDs ≈ 1.8 ms,
// well under the 16 ms frame budget.
const FRAME_PERIOD_MS: u32 = 16;

// Phase 1: scene is baked in. Phase 5+ replaces this with the resolved
// scene that comes back from the controller after the override-priority
// chain is applied (emergency > segment > group > global).
const INITIAL_SCENE: ActiveScene = ActiveScene {
    effect: EffectId::Rainbow,
    params: EffectParams {
        color: Rgb::new(0, 0, 0),
        speed: 128,
        // Keep dim by default so a USB-powered bench setup doesn't pull
        // more current than the 5 V rail can give.
        brightness: 40,
    },
};

#[main]
fn main() -> ! {
    let p = esp_hal::init(esp_hal::Config::default());
    esp_println::logger::init_logger_from_env();

    log::info!("led-orchestra node {} booting", NODE.node_id);
    log::info!(
        "segment id={} range=[{}, {}) total_leds={}",
        NODE.segment.segment_id,
        NODE.segment.segment_start,
        NODE.segment.segment_start + NODE.segment.segment_len,
        NODE.total_leds
    );

    let rmt = Rmt::new(p.RMT, Rate::from_mhz(80)).expect("RMT init failed");
    let mut rmt_buffer = smart_led_buffer!(LED_COUNT);

    // The HAL exposes each GPIO as a distinct typed singleton, so we can't
    // pick one at runtime from a `u8`. Phase 3 will introduce per-board build
    // features (or a typed wrapper) so `NODE.led_gpio_pin` actually drives
    // this choice. For Phase 1 we hardcode GPIO2 and document the override.
    let mut strip = SmartLedsAdapter::new(rmt.channel0, p.GPIO2, &mut rmt_buffer);

    let ctx = EffectContext {
        total_leds: NODE.total_leds,
        segment_start: NODE.segment.segment_start,
        segment_len: NODE.segment.segment_len,
    };

    let scene = INITIAL_SCENE;
    let effect = EffectRegistry::get(scene.effect);
    log::info!("running effect '{}'", scene.effect.name());

    let delay = Delay::new();
    let mut pixels: [RGB8; LED_COUNT] = [RGB8::default(); LED_COUNT];
    // Phase 4: replace this counter with a monotonic clock that is offset to
    // match the controller's wall clock so pure-function effects render
    // identical pixels on every node at every tick.
    let mut time_ms: u64 = 0;

    loop {
        for local_index in 0..NODE.segment.segment_len {
            let global_index = NODE.segment.segment_start + local_index;
            let c = effect.render(&scene.params, &ctx, global_index, time_ms);
            pixels[local_index as usize] = RGB8 { r: c.r, g: c.g, b: c.b };
        }
        let _ = strip.write(pixels.iter().copied());
        delay.delay_millis(FRAME_PERIOD_MS);
        time_ms = time_ms.wrapping_add(FRAME_PERIOD_MS as u64);
    }
}
