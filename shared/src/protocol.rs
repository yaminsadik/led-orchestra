//! Fixed-width wire packets shared by the controller and firmware.
//!
//! Phase 2 uses UDP as the transport, but this module deliberately only knows
//! about bytes. That keeps the packet contract usable from both `std` host code
//! and `no_std` ESP32-C3 firmware.

use core::fmt;

use crate::{ActiveScene, EffectId, EffectParams, Rgb};

const MAGIC: [u8; 2] = *b"LO";
const VERSION: u8 = 1;
const KIND_SET_SCENE: u8 = 1;

/// Encoded length, in bytes, of a [`SetScenePacket`].
pub const SET_SCENE_PACKET_LEN: usize = 16;

/// Target node id used to address every listening node.
pub const TARGET_BROADCAST: u16 = 0;

/// Error returned while encoding or decoding a wire packet.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProtocolError {
    BufferTooSmall,
    InvalidLength { expected: usize, actual: usize },
    InvalidMagic,
    UnsupportedVersion(u8),
    UnsupportedKind(u8),
    UnknownEffect(u8),
}

impl fmt::Display for ProtocolError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            ProtocolError::BufferTooSmall => write!(f, "buffer is too small for packet"),
            ProtocolError::InvalidLength { expected, actual } => {
                write!(
                    f,
                    "invalid packet length: expected {expected}, got {actual}"
                )
            }
            ProtocolError::InvalidMagic => write!(f, "packet magic did not match LED Orchestra"),
            ProtocolError::UnsupportedVersion(version) => {
                write!(f, "unsupported protocol version {version}")
            }
            ProtocolError::UnsupportedKind(kind) => write!(f, "unsupported packet kind {kind}"),
            ProtocolError::UnknownEffect(effect) => write!(f, "unknown effect id {effect}"),
        }
    }
}

/// Command packet that replaces a node's active scene.
///
/// `target_node == TARGET_BROADCAST` applies to every node. Otherwise firmware
/// should only apply the packet when `target_node` matches its own node id.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SetScenePacket {
    /// Controller-generated sequence value. Phase 2 uses it for logs and
    /// duplicate detection can be added later if needed.
    pub sequence: u32,
    /// Destination node id, or [`TARGET_BROADCAST`] for all nodes.
    pub target_node: u16,
    /// Fully resolved scene parameters for the target node.
    pub scene: ActiveScene,
}

impl SetScenePacket {
    /// Creates an in-memory packet before encoding it onto the wire.
    pub const fn new(sequence: u32, target_node: u16, scene: ActiveScene) -> Self {
        Self {
            sequence,
            target_node,
            scene,
        }
    }

    /// Returns true when this packet should be applied by `node_id`.
    pub fn is_for_node(self, node_id: u16) -> bool {
        self.target_node == TARGET_BROADCAST || self.target_node == node_id
    }

    /// Encodes this packet into `out`.
    ///
    /// Returns the number of bytes written, currently always
    /// [`SET_SCENE_PACKET_LEN`] on success.
    pub fn encode(self, out: &mut [u8]) -> Result<usize, ProtocolError> {
        if out.len() < SET_SCENE_PACKET_LEN {
            return Err(ProtocolError::BufferTooSmall);
        }

        out[0..2].copy_from_slice(&MAGIC);
        out[2] = VERSION;
        out[3] = KIND_SET_SCENE;
        out[4..8].copy_from_slice(&self.sequence.to_be_bytes());
        out[8..10].copy_from_slice(&self.target_node.to_be_bytes());
        out[10] = self.scene.effect.wire_id();
        out[11] = self.scene.params.color.r;
        out[12] = self.scene.params.color.g;
        out[13] = self.scene.params.color.b;
        out[14] = self.scene.params.speed;
        out[15] = self.scene.params.brightness;

        Ok(SET_SCENE_PACKET_LEN)
    }

    /// Decodes one complete UDP payload into a set-scene packet.
    pub fn decode(packet: &[u8]) -> Result<Self, ProtocolError> {
        if packet.len() != SET_SCENE_PACKET_LEN {
            return Err(ProtocolError::InvalidLength {
                expected: SET_SCENE_PACKET_LEN,
                actual: packet.len(),
            });
        }
        if packet[0..2] != MAGIC {
            return Err(ProtocolError::InvalidMagic);
        }
        if packet[2] != VERSION {
            return Err(ProtocolError::UnsupportedVersion(packet[2]));
        }
        if packet[3] != KIND_SET_SCENE {
            return Err(ProtocolError::UnsupportedKind(packet[3]));
        }

        let sequence = u32::from_be_bytes([packet[4], packet[5], packet[6], packet[7]]);
        let target_node = u16::from_be_bytes([packet[8], packet[9]]);
        let effect =
            EffectId::from_wire_id(packet[10]).ok_or(ProtocolError::UnknownEffect(packet[10]))?;
        let params = EffectParams {
            color: Rgb::new(packet[11], packet[12], packet[13]),
            speed: packet[14],
            brightness: packet[15],
        };

        Ok(Self {
            sequence,
            target_node,
            scene: ActiveScene { effect, params },
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_set_scene_packet() {
        let packet = SetScenePacket::new(
            42,
            7,
            ActiveScene {
                effect: EffectId::Solid,
                params: EffectParams {
                    color: Rgb::new(12, 34, 56),
                    speed: 128,
                    brightness: 40,
                },
            },
        );

        let mut bytes = [0; SET_SCENE_PACKET_LEN];
        let len = packet.encode(&mut bytes).unwrap();

        assert_eq!(len, SET_SCENE_PACKET_LEN);
        assert_eq!(SetScenePacket::decode(&bytes), Ok(packet));
    }

    #[test]
    fn rejects_packets_with_unknown_effects() {
        let mut bytes = [0; SET_SCENE_PACKET_LEN];
        SetScenePacket::new(
            1,
            TARGET_BROADCAST,
            ActiveScene {
                effect: EffectId::Off,
                params: EffectParams::default(),
            },
        )
        .encode(&mut bytes)
        .unwrap();
        bytes[10] = 99;

        assert_eq!(
            SetScenePacket::decode(&bytes),
            Err(ProtocolError::UnknownEffect(99))
        );
    }

    #[test]
    fn broadcast_targets_every_node() {
        let packet = SetScenePacket::new(
            1,
            TARGET_BROADCAST,
            ActiveScene {
                effect: EffectId::Off,
                params: EffectParams::default(),
            },
        );

        assert!(packet.is_for_node(1));
        assert!(packet.is_for_node(20));
    }
}
