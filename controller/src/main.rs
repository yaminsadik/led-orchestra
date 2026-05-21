use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::time::{SystemTime, UNIX_EPOCH};

use anyhow::{anyhow, ensure, Context, Result};
use clap::{Parser, Subcommand};
use led_orchestra_shared::{
    protocol, ActiveScene, EffectId, EffectParams, EffectRegistry, Rgb, SetScenePacket,
    TARGET_BROADCAST,
};

const DEFAULT_BRIGHTNESS: u8 = 40;
const DEFAULT_SPEED: u8 = 128;

#[derive(Parser, Debug)]
#[command(name = "loctl", version, about = "LED Orchestra controller")]
struct Cli {
    /// Address of the controller / node bus (Phase 2+).
    #[arg(short, long, default_value = "udp://239.42.0.1:4242", global = true)]
    bus: String,

    /// Target node id. 0 broadcasts to every listening node.
    #[arg(long, default_value_t = TARGET_BROADCAST, global = true)]
    target_node: u16,

    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Debug)]
enum Command {
    /// Apply an effect or color to every node (global layer).
    #[command(subcommand)]
    All(AllCmd),

    /// Per-segment commands (overrides global + group).
    Segment {
        segment_id: u16,
        #[command(subcommand)]
        action: SegmentAction,
    },

    /// Per-group commands (overrides global, lower than segment).
    Group {
        group_id: u16,
        #[command(subcommand)]
        action: GroupAction,
    },

    /// List known nodes (with online/offline status).
    Nodes,

    /// List known segments and their virtual-strip ranges.
    Segments,

    /// List effects known to the firmware via shared/.
    Effects,

    /// Live status: scene, mode, per-node heartbeat.
    Status,

    /// Walk a deterministic test sequence for wiring verification.
    TestSequence,

    /// Scene save/load.
    #[command(subcommand)]
    Scene(SceneCmd),

    /// OTA firmware update (Phase 7 — placeholder).
    Firmware {
        #[arg(long)]
        image: String,
        #[arg(long)]
        target: Option<String>,
    },
}

#[derive(Subcommand, Debug)]
enum AllCmd {
    Solid {
        hex: String,
        #[arg(long, default_value_t = DEFAULT_BRIGHTNESS)]
        brightness: u8,
    },
    Effect {
        name: String,
        #[arg(long, default_value_t = DEFAULT_SPEED)]
        speed: u8,
        #[arg(long, default_value_t = DEFAULT_BRIGHTNESS)]
        brightness: u8,
        #[arg(long, default_value = "000000")]
        color: String,
    },
    Off,
}

#[derive(Subcommand, Debug)]
enum SegmentAction {
    Solid {
        hex: String,
    },
    Effect {
        name: String,
    },
    Off,
    /// Blink this segment so the operator can locate it physically.
    Identify,
    /// Clear any segment-level override; segment falls back to group/global.
    ClearOverride,
}

#[derive(Subcommand, Debug)]
enum GroupAction {
    Solid { hex: String },
    Effect { name: String },
    ClearOverride,
}

#[derive(Subcommand, Debug)]
enum SceneCmd {
    Save { name: String },
    Load { name: String },
    List,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    let bus = cli.bus;
    let target_node = cli.target_node;

    match cli.command {
        Command::Effects => {
            println!("Effects registered in shared/:");
            for id in EffectRegistry::all() {
                println!("  - {}", id.name());
            }
        }
        Command::Nodes | Command::Segments | Command::Status | Command::TestSequence => {
            eprintln!(
                "[phase2] Controller transport not online yet. Configured bus: {}",
                bus
            );
        }
        Command::All(cmd) => send_all_command(&bus, target_node, cmd)?,
        Command::Segment { segment_id, action } => {
            not_implemented(&format!("segment {segment_id} {:?}", action))
        }
        Command::Group { group_id, action } => {
            not_implemented(&format!("group {group_id} {:?}", action))
        }
        Command::Scene(cmd) => not_implemented(&format!("scene {:?}", cmd)),
        Command::Firmware { image, target } => {
            not_implemented(&format!("firmware --image {image} --target {:?}", target))
        }
    }
    Ok(())
}

fn send_all_command(bus: &str, target_node: u16, cmd: AllCmd) -> Result<()> {
    let scene = scene_from_all_command(cmd)?;
    send_scene(bus, target_node, scene)
}

fn scene_from_all_command(cmd: AllCmd) -> Result<ActiveScene> {
    match cmd {
        AllCmd::Solid { hex, brightness } => Ok(ActiveScene {
            effect: EffectId::Solid,
            params: EffectParams {
                color: parse_hex_color(&hex)?,
                speed: DEFAULT_SPEED,
                brightness,
            },
        }),
        AllCmd::Effect {
            name,
            speed,
            brightness,
            color,
        } => {
            let normalized = name.to_ascii_lowercase();
            let effect = EffectId::from_name(&normalized).with_context(|| {
                format!("unknown effect '{name}'. Run `loctl effects` to list valid names")
            })?;
            Ok(ActiveScene {
                effect,
                params: EffectParams {
                    color: parse_hex_color(&color)?,
                    speed,
                    brightness,
                },
            })
        }
        AllCmd::Off => Ok(ActiveScene {
            effect: EffectId::Off,
            params: EffectParams {
                color: Rgb::BLACK,
                speed: 0,
                brightness: 0,
            },
        }),
    }
}

fn send_scene(bus: &str, target_node: u16, scene: ActiveScene) -> Result<()> {
    let destination = udp_destination(bus)?;
    let bind_addr = if destination.is_ipv4() {
        "0.0.0.0:0"
    } else {
        "[::]:0"
    };
    let socket = UdpSocket::bind(bind_addr)
        .with_context(|| format!("failed to bind UDP send socket at {bind_addr}"))?;

    if let SocketAddr::V4(addr) = destination {
        if addr.ip().is_multicast() {
            socket
                .set_multicast_ttl_v4(1)
                .context("failed to set multicast TTL")?;
        }
    }

    let packet = SetScenePacket::new(packet_sequence(), target_node, scene);
    let mut bytes = [0; protocol::SET_SCENE_PACKET_LEN];
    let len = packet.encode(&mut bytes).map_err(|err| anyhow!("{err}"))?;
    let sent = socket
        .send_to(&bytes[..len], destination)
        .with_context(|| format!("failed to send UDP packet to {destination}"))?;
    ensure!(
        sent == len,
        "short UDP send: wrote {sent} bytes, expected {len}"
    );

    println!(
        "sent set-scene seq={} target={} effect={} bytes={} bus={}",
        packet.sequence,
        target_label(target_node),
        packet.scene.effect.name(),
        sent,
        bus
    );

    Ok(())
}

fn udp_destination(bus: &str) -> Result<SocketAddr> {
    let address = bus.strip_prefix("udp://").unwrap_or(bus);
    ensure!(
        !address.trim().is_empty(),
        "bus address must be udp://host:port or host:port"
    );
    address
        .to_socket_addrs()
        .with_context(|| format!("failed to resolve UDP bus '{bus}'"))?
        .next()
        .with_context(|| format!("UDP bus '{bus}' did not resolve to any socket address"))
}

fn parse_hex_color(hex: &str) -> Result<Rgb> {
    let value = hex.trim();
    let value = value.strip_prefix('#').unwrap_or(value);
    let value = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .unwrap_or(value);

    ensure!(
        value.len() == 6 && value.bytes().all(|byte| byte.is_ascii_hexdigit()),
        "expected RGB hex color like ff8800 or #ff8800"
    );

    let r = parse_hex_byte(value, 0)?;
    let g = parse_hex_byte(value, 2)?;
    let b = parse_hex_byte(value, 4)?;
    Ok(Rgb::new(r, g, b))
}

fn parse_hex_byte(value: &str, offset: usize) -> Result<u8> {
    u8::from_str_radix(&value[offset..offset + 2], 16)
        .with_context(|| format!("invalid hex byte '{}'", &value[offset..offset + 2]))
}

fn packet_sequence() -> u32 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u32)
        .unwrap_or(0)
}

fn target_label(target_node: u16) -> String {
    if target_node == TARGET_BROADCAST {
        "all".to_string()
    } else {
        target_node.to_string()
    }
}

fn not_implemented(what: &str) {
    eprintln!("[phase2] not yet implemented: {what}");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_hex_color_forms() {
        assert_eq!(parse_hex_color("ff8800").unwrap(), Rgb::new(255, 136, 0));
        assert_eq!(parse_hex_color("#00AA11").unwrap(), Rgb::new(0, 170, 17));
        assert_eq!(parse_hex_color("0x0000ff").unwrap(), Rgb::BLUE);
    }

    #[test]
    fn rejects_invalid_hex_color() {
        assert!(parse_hex_color("fff").is_err());
        assert!(parse_hex_color("#gg0000").is_err());
    }

    #[test]
    fn parses_udp_bus_addresses() {
        let socket = udp_destination("udp://127.0.0.1:4242").unwrap();
        assert_eq!(socket.to_string(), "127.0.0.1:4242");
    }
}
