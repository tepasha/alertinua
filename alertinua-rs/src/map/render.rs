//! Растеризація карти областей України у framebuffer.
//! Логіка 1:1 портована з components/map/map_render.c.
//!
//! На відміну від C-версії, тут framebuffer зберігається у "звичайному"
//! (не байт-свопнутому) RGB565 — байтовий порядок під SPI/ST7789 бере на
//! себе `embedded_graphics::image::ImageRawBE` під час виводу на екран
//! (див. src/display.rs), тож ручний `swap16()` тут більше не потрібен.

use super::ukraine_map_data::{MAP_DISPLAY_H, MAP_DISPLAY_W, MAP_NUM_REGIONS, MAP_POINTS, MAP_REGIONS};

pub const FB_LEN: usize = (MAP_DISPLAY_W * MAP_DISPLAY_H) as usize;

#[inline]
pub const fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    (((r & 0xF8) as u16) << 8) | (((g & 0xFC) as u16) << 3) | ((b >> 3) as u16)
}

#[inline]
fn set_px(fb: &mut [u16], x: i32, y: i32, color: u16) {
    if x < 0 || x >= MAP_DISPLAY_W || y < 0 || y >= MAP_DISPLAY_H {
        return;
    }
    fb[(y * MAP_DISPLAY_W + x) as usize] = color;
}

fn clear(fb: &mut [u16], color: u16) {
    fb.fill(color);
}

fn draw_line(fb: &mut [u16], x0: i32, y0: i32, x1: i32, y1: i32, color: u16) {
    let (mut x0, mut y0) = (x0, y0);
    let dx = (x1 - x0).abs();
    let sx = if x0 < x1 { 1 } else { -1 };
    let dy = -(y1 - y0).abs();
    let sy = if y0 < y1 { 1 } else { -1 };
    let mut err = dx + dy;
    loop {
        set_px(fb, x0, y0, color);
        if x0 == x1 && y0 == y1 {
            break;
        }
        let e2 = 2 * err;
        if e2 >= dy {
            err += dy;
            x0 += sx;
        }
        if e2 <= dx {
            err += dx;
            y0 += sy;
        }
    }
}

/// Точки одного регіону як (x, y) у i32, для зручності растеризації.
fn region_points(point_offset: u16, point_count: u8) -> impl Iterator<Item = (i32, i32)> {
    let start = point_offset as usize;
    let count = point_count as usize;
    MAP_POINTS[start..start + count]
        .iter()
        .map(|p| (p.x as i32, p.y as i32))
}

/// Заливка багатокутника скануванням рядків ("непарний-парний"),
/// коректно обробляє й опуклі, й неопуклі контури областей.
fn fill_polygon(fb: &mut [u16], pts: &[(i32, i32)], color: u16) {
    let n = pts.len();
    if n < 3 {
        return;
    }
    let ymin = pts.iter().map(|p| p.1).min().unwrap();
    let ymax = pts.iter().map(|p| p.1).max().unwrap();

    for y in ymin..=ymax {
        let mut xs: heapless::Vec<i32, 48> = heapless::Vec::new();
        for i in 0..n {
            let j = (i + 1) % n;
            let (_, y0) = pts[i];
            let (_, y1) = pts[j];
            if y0 == y1 {
                continue;
            }
            if (y >= y0 && y < y1) || (y >= y1 && y < y0) {
                let t = (y - y0) as f32 / (y1 - y0) as f32;
                let x = pts[i].0 + (t * (pts[j].0 - pts[i].0) as f32) as i32;
                let _ = xs.push(x);
            }
        }
        xs.sort_unstable();
        let mut a = 0;
        while a + 1 < xs.len() {
            for x in xs[a]..=xs[a + 1] {
                set_px(fb, x, y, color);
            }
            a += 2;
        }
    }
}

fn draw_polygon_outline(fb: &mut [u16], pts: &[(i32, i32)], color: u16) {
    let n = pts.len();
    for i in 0..n {
        let j = (i + 1) % n;
        draw_line(fb, pts[i].0, pts[i].1, pts[j].0, pts[j].1, color);
    }
}

/// Те саме, що fill_polygon, але замість суцільної заливки лишає тільки
/// діагональні лінії (крок `period` пікселів, нахил 45°) — імітація
/// штрихування Pip-Boy/терміналів Fallout на монохромному дисплеї.
fn fill_polygon_hatched(fb: &mut [u16], pts: &[(i32, i32)], color: u16, period: i32) {
    let n = pts.len();
    if n < 3 {
        return;
    }
    let ymin = pts.iter().map(|p| p.1).min().unwrap();
    let ymax = pts.iter().map(|p| p.1).max().unwrap();

    for y in ymin..=ymax {
        let mut xs: heapless::Vec<i32, 48> = heapless::Vec::new();
        for i in 0..n {
            let j = (i + 1) % n;
            let (_, y0) = pts[i];
            let (_, y1) = pts[j];
            if y0 == y1 {
                continue;
            }
            if (y >= y0 && y < y1) || (y >= y1 && y < y0) {
                let t = (y - y0) as f32 / (y1 - y0) as f32;
                let x = pts[i].0 + (t * (pts[j].0 - pts[i].0) as f32) as i32;
                let _ = xs.push(x);
            }
        }
        xs.sort_unstable();
        let mut a = 0;
        while a + 1 < xs.len() {
            for x in xs[a]..=xs[a + 1] {
                let diag = ((x - y) % period + period) % period; // коректний mod для від'ємних x-y
                if diag == 0 {
                    set_px(fb, x, y, color);
                }
            }
            a += 2;
        }
    }
}

fn region_pts_vec(idx: usize) -> heapless::Vec<(i32, i32), 48> {
    let r = &MAP_REGIONS[idx];
    region_points(r.point_offset, r.point_count).collect()
}

/// Штрихована Pip-Boy-стилізована карта з підсвіченим обраним регіоном.
pub fn render_map(fb: &mut [u16], selected: Option<usize>) {
    clear(fb, rgb565(3, 10, 5));

    let hatch_col = rgb565(25, 90, 30);
    for i in 0..MAP_NUM_REGIONS {
        fill_polygon_hatched(fb, &region_pts_vec(i), hatch_col, 3);
    }

    let border_col = rgb565(100, 255, 100); // фосфорно-зелені межі
    for i in 0..MAP_NUM_REGIONS {
        draw_polygon_outline(fb, &region_pts_vec(i), border_col);
    }

    if let Some(sel) = selected {
        if sel < MAP_NUM_REGIONS {
            let hl = rgb565(255, 255, 150); // янтарний Pip-Boy акцент
            draw_polygon_outline(fb, &region_pts_vec(sel), hl);
        }
    }
}

/// Кольорова карта (кожна область власним RGB565-кольором з даних).
pub fn render_map_multicolor(fb: &mut [u16], selected: Option<usize>) {
    clear(fb, rgb565(10, 12, 22));

    for i in 0..MAP_NUM_REGIONS {
        fill_polygon(fb, &region_pts_vec(i), MAP_REGIONS[i].color);
    }

    let border_col = rgb565(255, 210, 0);
    for i in 0..MAP_NUM_REGIONS {
        draw_polygon_outline(fb, &region_pts_vec(i), border_col);
    }

    if let Some(sel) = selected {
        if sel < MAP_NUM_REGIONS {
            let hl = rgb565(255, 255, 255);
            draw_polygon_outline(fb, &region_pts_vec(sel), hl);
        }
    }
}

pub fn mark_region_red(fb: &mut [u16], region_index: usize) {
    if region_index >= MAP_NUM_REGIONS {
        return;
    }
    let red_fill = rgb565(200, 20, 20);
    let red_edge = rgb565(255, 80, 80);
    let pts = region_pts_vec(region_index);
    fill_polygon(fb, &pts, red_fill);
    draw_polygon_outline(fb, &pts, red_edge);
}

pub fn mark_regions_red(fb: &mut [u16], region_indices: &[usize]) {
    for &idx in region_indices {
        mark_region_red(fb, idx);
    }
}

/// Власні блочні гліфи 8x8 лише для 'E' і 'R' (усе, що потрібно для "ERR") —
/// той самий формат, що й у поширених 8x8-шрифтах: bit0 = лівий стовпчик.
const GLYPH_E: [u8; 8] = [0xFF, 0x01, 0x01, 0x3F, 0x01, 0x01, 0x01, 0xFF];
const GLYPH_R: [u8; 8] = [0x3F, 0x41, 0x41, 0x3F, 0x09, 0x11, 0x21, 0x41];

fn draw_glyph(fb: &mut [u16], x0: i32, y0: i32, glyph: &[u8; 8], color: u16, scale: i32) {
    for row in 0..8 {
        let bits = glyph[row as usize];
        for col in 0..8 {
            if bits & (1 << col) != 0 {
                for sy in 0..scale {
                    for sx in 0..scale {
                        set_px(fb, x0 + col * scale + sx, y0 + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

/// Банер помилки "ERR" по центру екрана (Wi-Fi/API недоступні тощо).
pub fn draw_err_banner(fb: &mut [u16]) {
    let scale = 3;
    let glyph_w = 8 * scale;
    let glyph_h = 8 * scale;
    let gap = 4;
    let text_w = glyph_w * 3 + gap * 2;
    let pad = 8;
    let box_w = text_w + pad * 2;
    let box_h = glyph_h + pad * 2;
    let box_x = (MAP_DISPLAY_W - box_w) / 2;
    let box_y = (MAP_DISPLAY_H - box_h) / 2;

    let black = rgb565(0, 0, 0);
    let red_bright = rgb565(255, 40, 40);
    let red_dim = rgb565(120, 15, 15);

    // чорна підкладка під панель
    for y in box_y..box_y + box_h {
        for x in box_x..box_x + box_w {
            set_px(fb, x, y, black);
        }
    }

    // подвійна червона рамка — зовнішня яскрава, внутрішня тьмяніша
    for x in box_x..box_x + box_w {
        set_px(fb, x, box_y, red_bright);
        set_px(fb, x, box_y + box_h - 1, red_bright);
    }
    for y in box_y..box_y + box_h {
        set_px(fb, box_x, y, red_bright);
        set_px(fb, box_x + box_w - 1, y, red_bright);
    }
    for x in box_x + 2..box_x + box_w - 2 {
        set_px(fb, x, box_y + 2, red_dim);
        set_px(fb, x, box_y + box_h - 3, red_dim);
    }

    // кутові HUD-засічки, що стирчать за межі панелі
    let tick = 4;
    draw_line(fb, box_x - tick, box_y - tick, box_x, box_y, red_bright);
    draw_line(fb, box_x + box_w + tick, box_y - tick, box_x + box_w, box_y, red_bright);
    draw_line(fb, box_x - tick, box_y + box_h + tick, box_x, box_y + box_h, red_bright);
    draw_line(
        fb,
        box_x + box_w + tick,
        box_y + box_h + tick,
        box_x + box_w,
        box_y + box_h,
        red_bright,
    );

    // сам напис "ERR" по центру панелі
    let text_x = box_x + pad;
    let text_y = box_y + pad;
    draw_glyph(fb, text_x, text_y, &GLYPH_E, red_bright, scale);
    draw_glyph(fb, text_x + (glyph_w + gap), text_y, &GLYPH_R, red_bright, scale);
    draw_glyph(fb, text_x + (glyph_w + gap) * 2, text_y, &GLYPH_R, red_bright, scale);
}
