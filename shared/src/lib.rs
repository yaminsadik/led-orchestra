#![no_std]

pub mod config;
pub mod effect;
pub mod effects;
pub mod protocol;
pub mod registry;
pub mod rgb;

pub use config::{ActiveScene, NodeConfig, SegmentConfig};
pub use effect::{Effect, EffectContext, EffectId, EffectParams};
pub use protocol::{ProtocolError, SetScenePacket, TARGET_BROADCAST};
pub use registry::EffectRegistry;
pub use rgb::Rgb;
