#![no_std]
#![no_main]

mod node_config;

use core::sync::atomic::{AtomicU8, Ordering};

use embassy_executor::Spawner;
use embassy_net::udp::{PacketMetadata, UdpSocket};
use embassy_net::{Runner, Stack, StackResources};
use embassy_time::{Duration, Timer};
use esp_backtrace as _;
use esp_hal::{
    interrupt::software::SoftwareInterruptControl, rmt::Rmt, rng::Rng, time::Rate,
    timer::timg::TimerGroup,
};
use esp_hal_smartled::{smart_led_buffer, SmartLedsAdapter};
use esp_radio::wifi::{ClientConfig, ModeConfig, WifiController, WifiDevice};
use smart_leds::{SmartLedsWrite, RGB8};
use static_cell::StaticCell;

use led_orchestra_shared::{
    ActiveScene, EffectContext, EffectId, EffectParams, EffectRegistry, Rgb, SetScenePacket,
};

use node_config::{LED_COUNT, NODE};

esp_bootloader_esp_idf::esp_app_desc!();
// 60Hz target. WS2812B RMT write takes ~30µs per LED — 60 LEDs ≈ 1.8 ms,
// well under the 16 ms frame budget.
const FRAME_PERIOD_MS: u64 = 16;
const UDP_PORT: u16 = 4242;
const NET_SOCKET_COUNT: usize = 3;

const WIFI_SSID: &str = match option_env!("LED_ORCHESTRA_WIFI_SSID") {
    Some(value) => value,
    None => "",
};
const WIFI_PASSWORD: &str = match option_env!("LED_ORCHESTRA_WIFI_PASSWORD") {
    Some(value) => value,
    None => "",
};

// Phase 1 fallback: if WiFi is not configured or drops, the node keeps rendering
// this local scene until the controller sends a valid Phase 2 packet.
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

static SCENE_VERSION: AtomicU8 = AtomicU8::new(0);
static SCENE_EFFECT: AtomicU8 = AtomicU8::new(INITIAL_SCENE.effect.wire_id());
static SCENE_R: AtomicU8 = AtomicU8::new(INITIAL_SCENE.params.color.r);
static SCENE_G: AtomicU8 = AtomicU8::new(INITIAL_SCENE.params.color.g);
static SCENE_B: AtomicU8 = AtomicU8::new(INITIAL_SCENE.params.color.b);
static SCENE_SPEED: AtomicU8 = AtomicU8::new(INITIAL_SCENE.params.speed);
static SCENE_BRIGHTNESS: AtomicU8 = AtomicU8::new(INITIAL_SCENE.params.brightness);

static RADIO: StaticCell<esp_radio::Controller<'static>> = StaticCell::new();
static NET_RESOURCES: StaticCell<StackResources<NET_SOCKET_COUNT>> = StaticCell::new();

#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    let p = esp_hal::init(esp_hal::Config::default());
    esp_alloc::heap_allocator!(size: 64 * 1024);
    esp_println::logger::init_logger_from_env();

    log::info!("led-orchestra node {} booting", NODE.node_id);
    log::info!(
        "segment id={} range=[{}, {}) total_leds={}",
        NODE.segment.segment_id,
        NODE.segment.segment_start,
        NODE.segment.segment_start + NODE.segment.segment_len,
        NODE.total_leds
    );

    let timg0 = TimerGroup::new(p.TIMG0);
    let software_interrupt = SoftwareInterruptControl::new(p.SW_INTERRUPT);
    esp_rtos::start(timg0.timer0, software_interrupt.software_interrupt0);

    let rmt = Rmt::new(p.RMT, Rate::from_mhz(80)).expect("RMT init failed");
    let mut rmt_buffer = smart_led_buffer!(LED_COUNT);

    // The HAL exposes each GPIO as a distinct typed singleton, so we can't
    // pick one at runtime from a `u8`. Phase 3 will introduce per-board build
    // features (or a typed wrapper) so `NODE.led_gpio_pin` actually drives
    // this choice. For now we hardcode GPIO2 and document the override.
    let mut strip = SmartLedsAdapter::new(rmt.channel0, p.GPIO2, &mut rmt_buffer);

    if wifi_configured() {
        let radio = RADIO.init(esp_radio::init().expect("radio init failed"));
        let (controller, interfaces) =
            esp_radio::wifi::new(radio, p.WIFI, Default::default()).expect("WiFi init failed");

        let seed = random_seed();
        let (stack, runner) = embassy_net::new(
            interfaces.sta,
            embassy_net::Config::dhcpv4(Default::default()),
            NET_RESOURCES.init(StackResources::new()),
            seed,
        );

        spawner.must_spawn(net_task(runner));
        spawner.must_spawn(wifi_task(controller));
        spawner.must_spawn(udp_scene_task(stack));
        log::info!("WiFi enabled; listening for set-scene packets on UDP {UDP_PORT}");
    } else {
        log::warn!(
            "WiFi disabled; set LED_ORCHESTRA_WIFI_SSID and LED_ORCHESTRA_WIFI_PASSWORD at build time"
        );
    }

    let ctx = EffectContext {
        total_leds: NODE.total_leds,
        segment_start: NODE.segment.segment_start,
        segment_len: NODE.segment.segment_len,
    };

    let mut pixels: [RGB8; LED_COUNT] = [RGB8::default(); LED_COUNT];
    // Phase 4: replace this local counter with a monotonic clock offset to the
    // controller so every node renders the same pure-function frame.
    let mut time_ms: u64 = 0;

    loop {
        let scene = active_scene();
        let effect = EffectRegistry::get(scene.effect);

        for local_index in 0..NODE.segment.segment_len {
            let global_index = NODE.segment.segment_start + local_index;
            let c = effect.render(&scene.params, &ctx, global_index, time_ms);
            pixels[local_index as usize] = RGB8 {
                r: c.r,
                g: c.g,
                b: c.b,
            };
        }
        let _ = strip.write(pixels.iter().copied());

        Timer::after(Duration::from_millis(FRAME_PERIOD_MS)).await;
        time_ms = time_ms.wrapping_add(FRAME_PERIOD_MS);
    }
}

#[embassy_executor::task]
async fn net_task(mut runner: Runner<'static, WifiDevice<'static>>) -> ! {
    runner.run().await
}

#[embassy_executor::task]
async fn wifi_task(mut controller: WifiController<'static>) -> ! {
    let client_config = ModeConfig::Client(
        ClientConfig::default()
            .with_ssid(WIFI_SSID.into())
            .with_password(WIFI_PASSWORD.into()),
    );

    loop {
        if let Err(err) = controller.set_config(&client_config) {
            log::error!("failed to configure WiFi: {:?}", err);
            Timer::after(Duration::from_secs(5)).await;
            continue;
        }

        if let Err(err) = controller.start_async().await {
            log::error!("failed to start WiFi: {:?}", err);
            Timer::after(Duration::from_secs(5)).await;
            continue;
        }

        loop {
            if !matches!(controller.is_connected(), Ok(true)) {
                log::info!("connecting to WiFi SSID '{}'", WIFI_SSID);
                match controller.connect_async().await {
                    Ok(()) => log::info!("WiFi connected"),
                    Err(err) => {
                        log::warn!("WiFi connect failed: {:?}", err);
                        Timer::after(Duration::from_secs(5)).await;
                    }
                }
            }

            Timer::after(Duration::from_secs(2)).await;
        }
    }
}

#[embassy_executor::task]
async fn udp_scene_task(stack: Stack<'static>) -> ! {
    let mut rx_meta = [PacketMetadata::EMPTY; 4];
    let mut rx_buffer = [0; 256];
    let mut tx_meta = [PacketMetadata::EMPTY; 1];
    let mut tx_buffer = [0; 64];
    let mut packet_buffer = [0; 64];

    stack.wait_config_up().await;
    log::info!("network config acquired: {:?}", stack.config_v4());

    let mut socket = UdpSocket::new(
        stack,
        &mut rx_meta,
        &mut rx_buffer,
        &mut tx_meta,
        &mut tx_buffer,
    );

    if let Err(err) = socket.bind(UDP_PORT) {
        log::error!("failed to bind UDP {UDP_PORT}: {:?}", err);
        core::future::pending::<()>().await;
    }

    log::info!("UDP scene receiver listening on port {UDP_PORT}");

    loop {
        match socket.recv_from(&mut packet_buffer).await {
            Ok((len, meta)) => match SetScenePacket::decode(&packet_buffer[..len]) {
                Ok(packet) if packet.is_for_node(NODE.node_id) => {
                    store_scene(packet.scene);
                    log::info!(
                        "accepted set-scene seq={} effect={} from {:?}",
                        packet.sequence,
                        packet.scene.effect.name(),
                        meta.endpoint
                    );
                }
                Ok(packet) => {
                    log::debug!(
                        "ignored set-scene seq={} for node {}",
                        packet.sequence,
                        packet.target_node
                    );
                }
                Err(err) => log::warn!("ignored invalid UDP scene packet: {}", err),
            },
            Err(err) => log::warn!("UDP receive error: {:?}", err),
        }
    }
}

fn wifi_configured() -> bool {
    !WIFI_SSID.is_empty() && !WIFI_PASSWORD.is_empty()
}

fn random_seed() -> u64 {
    let rng = Rng::new();
    ((rng.random() as u64) << 32) | rng.random() as u64
}

fn active_scene() -> ActiveScene {
    loop {
        let start = SCENE_VERSION.load(Ordering::Acquire);
        if start & 1 == 1 {
            continue;
        }

        let effect =
            EffectId::from_wire_id(SCENE_EFFECT.load(Ordering::Relaxed)).unwrap_or(EffectId::Off);
        let color = Rgb::new(
            SCENE_R.load(Ordering::Relaxed),
            SCENE_G.load(Ordering::Relaxed),
            SCENE_B.load(Ordering::Relaxed),
        );
        let speed = SCENE_SPEED.load(Ordering::Relaxed);
        let brightness = SCENE_BRIGHTNESS.load(Ordering::Relaxed);

        let end = SCENE_VERSION.load(Ordering::Acquire);
        if start == end && end & 1 == 0 {
            return ActiveScene {
                effect,
                params: EffectParams {
                    color,
                    speed,
                    brightness,
                },
            };
        }
    }
}

fn store_scene(scene: ActiveScene) {
    let start = SCENE_VERSION.load(Ordering::Relaxed).wrapping_add(1);
    SCENE_VERSION.store(start | 1, Ordering::Release);
    SCENE_EFFECT.store(scene.effect.wire_id(), Ordering::Relaxed);
    SCENE_R.store(scene.params.color.r, Ordering::Relaxed);
    SCENE_G.store(scene.params.color.g, Ordering::Relaxed);
    SCENE_B.store(scene.params.color.b, Ordering::Relaxed);
    SCENE_SPEED.store(scene.params.speed, Ordering::Relaxed);
    SCENE_BRIGHTNESS.store(scene.params.brightness, Ordering::Relaxed);
    SCENE_VERSION.store((start | 1).wrapping_add(1), Ordering::Release);
}

#[unsafe(no_mangle)]
unsafe extern "C" fn __esp_radio_misc_nvs_deinit() {}

#[unsafe(no_mangle)]
unsafe extern "C" fn __esp_radio_misc_nvs_init() -> i32 {
    0
}
