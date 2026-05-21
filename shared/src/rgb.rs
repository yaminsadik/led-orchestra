#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    pub const BLACK: Rgb = Rgb { r: 0, g: 0, b: 0 };
    pub const WHITE: Rgb = Rgb {
        r: 255,
        g: 255,
        b: 255,
    };
    pub const RED: Rgb = Rgb { r: 255, g: 0, b: 0 };
    pub const GREEN: Rgb = Rgb { r: 0, g: 255, b: 0 };
    pub const BLUE: Rgb = Rgb { r: 0, g: 0, b: 255 };
    pub const YELLOW: Rgb = Rgb {
        r: 255,
        g: 220,
        b: 0,
    };

    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Rgb { r, g, b }
    }

    pub fn scale(self, brightness: u8) -> Self {
        let b = brightness as u16;
        Rgb {
            r: ((self.r as u16 * b) / 255) as u8,
            g: ((self.g as u16 * b) / 255) as u8,
            b: ((self.b as u16 * b) / 255) as u8,
        }
    }

    /// Integer HSV → RGB. h, s, v all in `0..=255`.
    pub fn from_hsv(h: u8, s: u8, v: u8) -> Self {
        if s == 0 {
            return Rgb::new(v, v, v);
        }
        let region = h / 43;
        let remainder = (h - region * 43) * 6;
        let p = ((v as u16 * (255 - s as u16)) / 255) as u8;
        let q = ((v as u16 * (255 - ((s as u16 * remainder as u16) / 255))) / 255) as u8;
        let t = ((v as u16 * (255 - ((s as u16 * (255 - remainder as u16)) / 255))) / 255) as u8;
        match region {
            0 => Rgb::new(v, t, p),
            1 => Rgb::new(q, v, p),
            2 => Rgb::new(p, v, t),
            3 => Rgb::new(p, q, v),
            4 => Rgb::new(t, p, v),
            _ => Rgb::new(v, p, q),
        }
    }
}
