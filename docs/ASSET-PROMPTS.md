# LiteKern X — Asset Rules and Prompts (cursors, icons, UI glyphs)

Two kinds of asset, made two ways:

| Asset | Format | How you make it |
|---|---|---|
| **Cursors** (§3) | **PNG images**, 32×32 | A text AI types a pixel grid that `tools/cursor-grid2png.py` converts, or a pixel editor (Piskel, Aseprite, GIMP) |
| **App icons** (§4) | **PNG images**, 48×48 | An image AI or a pixel editor, in the Adwaita style (§4.2) |
| UI glyphs, logo (§5–§6) | SVG | A text AI that writes SVG code (ChatGPT, Claude, Gemini), using the prompts below |

All of them are turned into bitmaps for the kernel **at build time**, by a script, so the kernel never parses PNG or SVG itself.

**When these are used:** Phase 2 §2 needs the cursors. Phase 2 §4 (widgets) and Phase 3 §1 (the final icon/cursor set) need the rest. Make them whenever you like and commit them under `assets/`.

---

## Where the files go
```
assets/
  cursors/      arrow.png, hand.png, text.png, wait-0..3.png, move.png, not-allowed.png,
                resize-h.png, resize-v.png, resize-d1.png, resize-d2.png
  cursors.json  hotspots and animation (see §3.4)
  icons/        <app>.png            one 48x48 PNG per app (§4)
  icons.json    which icons exist (§4.1)
  icons/ui/     close.svg, minimise.svg, check.svg, ... (16x16)
  logo.svg      optional, 128x128
```
**File names:** lowercase letters, digits and hyphens only (`resize-d1.png`, `text-editor.svg`).

---

## 1. The palette (every asset)
Use **only** these 8 colours, plus transparency:

| Hex | Name | Use |
|---|---|---|
| `#0F1E33` | ink | Outlines, darkest detail |
| `#1E3A5F` | navy | System background colour, main dark fill |
| `#2E5584` | steel | Secondary dark fill, shading |
| `#48A6E8` | sky | Accent (the LiteKern blue): highlights, active state |
| `#C8D0DC` | mist | Light fill, text colour on navy |
| `#FFFFFF` | white | Cursor fill, brightest highlight |
| `#E8A33D` | amber | Warnings. At most one warm accent per asset. |
| `#B83232` | red | Errors, the close button, destructive actions only |

- **Cursors are the exception:** they use pure black `#000000` and white `#FFFFFF` (§3.1), plus half-transparent versions of those two along curved edges.
- The screen is the EeePC 1000HE's: 1024×600, 8.9" (about 133 dpi), 32-bit colour. Assets are drawn **1:1, never scaled**, so what you see at 100% zoom is exactly what appears on screen.

---

## 2. Sizes and formats at a glance

| Asset | Canvas (px) | File | Notes |
|---|---|---|---|
| Cursors | **32×32** | `assets/cursors/<name>.png` | RGBA PNG. The visible shape is about 20–24 px tall. Hotspot goes in `cursors.json`. |
| Busy cursor | 32×32 × 4 frames | `assets/cursors/wait-0.png` … `wait-3.png` | Played at 150 ms per frame |
| App icons | **48×48** | `assets/icons/<app>.png` | RGBA PNG, Adwaita style (§4). |
| UI glyphs | 16×16 | `assets/icons/ui/<name>.svg` | Title-bar buttons, checkboxes and similar |
| Logo (optional) | 128×128 | `assets/logo.svg` | For the Phase 4 website, and it can be the boot splash (the boot budget is now ≤ 5 s, see `docs/BOOT-BUDGET.md`). |

---

## 3. Cursors (PNG images)

### 3.1 Rules
| Rule | Why |
|---|---|
| **PNG, 32-bit RGBA** (8 bits per channel plus alpha), with a **transparent background** | The kernel blends each pixel using its alpha |
| **Exactly 32×32 px**. The visible shape is 20–24 px tall; everything else is fully transparent (alpha 0). | Drawn 1:1. Anything larger is refused by the build. |
| **Draw it at 1×, pixel by pixel**, at 32×32. Don't draw it big and shrink it. | Shrinking makes a small cursor blurry and muddy |
| **Black body `#000000` with a white `#FFFFFF` edge, 1 px wide, all the way round** (decided 2026-09-29) | Visible on navy, on white (thanks to the black body) and on sky blue |
| **Rounded:** soft corners and curves instead of sharp points. The tip keeps a clear single hotspot pixel. | The LiteKern X look |
| Edge pixels: **fully opaque** (alpha 255). Optionally, a few **half-transparent pixels** (alpha about 64–192) on the *outside* of curves, to smooth them. | Smooth edges without a blurry shape |
| **No drop shadows, glows, gradients or textures** | They look muddy at this size and cost drawing time |
| Only black `#000000`, white `#FFFFFF`, **greys between them where black meets white on a curve** (smoothing), and the §1 colours for the few cursors in §3.3 that use sky or red | One look across the whole system |
| **Hotspot:** the one pixel that "clicks". It must be an opaque pixel of the tip, or the centre of a symmetric cursor. | Goes in `cursors.json` (§3.4) |
| Frames of an animation (`wait-*`) are **identical except for the part that animates** | So it doesn't jitter |
| Save **without colour profiles or metadata** (in GIMP: Export → untick "Save color profile") | Keeps the build conversion exact |

### 3.2 How to make one

**Option A: a text AI types it as a grid (easiest).** Paste the prompt below into ChatGPT or Claude, save the whole answer as a text file, then convert it:
```
wsl python3 tools/cursor-grid2png.py arrow-grid.txt assets/cursors/arrow.png
```
The script checks the size, the characters and the hotspot, and prints the `cursors.json` entry. Open the PNG to look at it. If it's not right, ask the AI to fix specific rows ("row 12 is too wide on the right"), then convert again.

```text
Draw a mouse cursor for a small operating system as a 32x32 pixel grid.

OUTPUT FORMAT (exactly this, nothing else):
- 32 lines, each exactly 32 characters, inside one ``` code block. Row 0 is the top,
  column 0 is the left.
- Characters: "." transparent, "K" black body, "W" white edge, "w" half-transparent white
  (only on the outside of curves, to smooth them), "k" half-transparent black (only where a
  curve of the body needs smoothing).
- After the code block, one line: hotspot: X,Y

DESIGN
- The default pointer: a classic arrow pointing up and to the left, with a short tail
  angled down and to the right. About 21 pixels tall and 14 wide. The tip sits near the
  top-left corner of the grid (around column 1, row 1).
- ROUNDED: soft corners everywhere. The tail's end is rounded, the notch where the tail
  meets the head is a smooth curve, and the two outer corners of the arrowhead are rounded.
  The tip is slightly softened but still comes to a clear point.
- A black body ("K") with a white edge ("W") exactly 1 pixel wide all the way round.
  Every black pixel touching the transparent area must have a white pixel between it and
  the transparent area. The edge never has gaps.
- Symmetric-looking and smooth: each row's width changes gradually, with no jagged steps.
- Nothing else in the grid: no shadow, no glow, no text.
- The hotspot is the tip: the top-left-most opaque pixel of the arrow. It must be "K" or "W".

Check before answering: exactly 32 rows of exactly 32 characters, only the characters
. K W w k, a continuous white edge, and a hotspot that is on an opaque pixel.
```

**Option B: draw it yourself (Piskel, free in the browser).**
1. Go to [piskelapp.com](https://www.piskelapp.com), click Create Sprite, then **Resize** the canvas to **32 × 32**.
2. Add black `#000000` and white `#FFFFFF` to the palette (plus `#48A6E8` or `#B83232` where needed).
3. Draw with the **pen at size 1**: the white edge first, then fill the body black. Leave everything else transparent.
4. Check it in the preview at **1×** (the small preview, not the zoomed canvas). That's its real size.
5. **Export → PNG** at scale 1 and save as `assets/cursors/<name>.png`.
6. Note the hotspot: hover over the tip pixel and read the x, y coordinates Piskel shows at the bottom. Put them in `cursors.json`.

GIMP or Aseprite work the same way: a 32×32 canvas, the pencil tool (not the brush, which is anti-aliased), and export as PNG.

### 3.3 The set

| File | What to draw | Hotspot |
|---|---|---|
| `arrow.png` | The default pointer: a classic arrow leaning left and pointing to the top-left, with a short tail, about 21 px tall. The tip sits at the top-left of the shape. | The tip, e.g. 1,1 |
| `hand.png` | Over a link or button: a hand with the index finger pointing straight up and the other fingers folded, about 22 px tall | The fingertip |
| `text.png` | Over text: an I-beam, a 1 px vertical bar about 17 px tall with 5 px serifs at the top and bottom | The centre of the bar |
| `wait-0.png` … `wait-3.png` | Busy: **the arrow** plus a small ring about 9×9 beside its bottom-right, made of 4 segments. In frame N, segment N is sky `#48A6E8` and the others are mist `#C8D0DC`. The arrow is pixel-identical in all 4. | Same as the arrow |
| `move.png` | Moving something: a plus shape with an arrowhead on each of its 4 ends, about 21×21, centred | The centre, 16,16 |
| `not-allowed.png` | Not allowed: the arrow plus a small circle (about 10×10) with a diagonal bar at its bottom-right, in red `#B83232` with an ink outline | Same as the arrow |
| `resize-h.png` | Resizing sideways: a double-headed arrow pointing left and right, about 21 px wide, centred | 16,16 |
| `resize-v.png` | Resizing up and down: a double-headed arrow, about 21 px tall, centred | 16,16 |
| `resize-d1.png` | Diagonal resize, top-left to bottom-right, centred | 16,16 |
| `resize-d2.png` | Diagonal resize, top-right to bottom-left. The mirror of `resize-d1`. | 16,16 |

**Start with `arrow.png`.** It's the only one Phase 2 needs straight away. The others can come later.

### 3.4 `assets/cursors.json`
This one is **JSON**, in the same spirit as `kerns.json`. Put the hotspot you noted for each file here:
```json
{
  "version": 1,
  "size": 32,
  "cursors": {
    "arrow":       { "file": "arrow.png",       "hotspot": [1, 1] },
    "hand":        { "file": "hand.png",        "hotspot": [10, 1] },
    "text":        { "file": "text.png",        "hotspot": [16, 16] },
    "wait":        { "frames": ["wait-0.png", "wait-1.png", "wait-2.png", "wait-3.png"],
                     "frame_ms": 150, "hotspot": [1, 1] },
    "move":        { "file": "move.png",        "hotspot": [16, 16] },
    "not-allowed": { "file": "not-allowed.png", "hotspot": [1, 1] },
    "resize-h":    { "file": "resize-h.png",    "hotspot": [16, 16] },
    "resize-v":    { "file": "resize-v.png",    "hotspot": [16, 16] },
    "resize-d1":   { "file": "resize-d1.png",   "hotspot": [16, 16] },
    "resize-d2":   { "file": "resize-d2.png",   "hotspot": [16, 16] }
  }
}
```
- Hotspots are whole numbers from 0 to 31. Only list cursors whose files exist; you can start with just `"arrow"`.
- The values above are placeholders. Replace each with the pixel you actually read off your image.

### 3.5 Checklist before committing a cursor
Run the checker first. It tests most of this list and writes a preview of the cursor on navy, white and sky, at 1× and 8×:
```
wsl python3 tools/cursor-check.py assets/cursors/arrow.png build/arrow-preview.png
```
- [ ] 32×32 px, PNG, with transparency (the background shows as a checkerboard in the editor)
- [ ] It looks right at **100% zoom**, which is its real size, not only when zoomed in
- [ ] It has a black body with a 1 px white edge all the way round, rounded corners, and no shadow or glow
- [ ] It's still visible when placed on navy `#1E3A5F`, white, and sky `#48A6E8`
- [ ] The hotspot in `cursors.json` is an opaque pixel on the tip or centre

---

## 4. App icons (PNG images, decided 2026-09-30)

App icons are **48×48 RGBA PNGs**, like the cursors, and are converted at build time by `tools/icons2c.py`. They follow the **Adwaita** icon style (GNOME/Fedora), to match the desktop (`kernel/theme.c`). The 8-colour palette in §1 doesn't apply to them.

`assets/icons/files.png` and `log.png` are placeholders drawn by `tools/icon-placeholders.py`. Replace either one by saving a PNG with the same name.

### 4.1 Rules
- **Canvas:** 48×48 RGBA PNG with a transparent background. The icon's body fits in about 40×40, centred, with 4 px of empty space around it.
- **Style, Adwaita:** flat, simple and friendly shapes with rounded corners. Light comes from the top: a slightly lighter top face and a darker lower edge are fine. No outlines, no 3D perspective, no text, no photos.
- **Soft edges are fine:** half-transparent pixels along curves are anti-aliasing and are kept.
- **Colours:** the GNOME palette. Blues `#62a0ea` `#3584e4` `#1c71d8`, greens `#57e389` `#2ec27e`, yellows `#f8e45c` `#f6d32d`, oranges `#ffa348` `#ff7800`, reds `#ed333b` `#c01c28`, purples `#c061cb` `#9141ac`, browns `#cdab8f` `#986a44`, light greys `#deddda` `#c0bfbc`, dark greys `#5e5c64` `#3d3846` `#241f31`.
- **Must read on the desktop background** (`#202634`, dark slate), at 100% zoom.
- **Registered in `assets/icons.json`:** `"<name>": "<name>.png"`. The build fails if a listed file is missing or isn't 48×48.

### 4.2 Prompt for an image AI or a pixel editor
```text
Make a 48x48 pixel app icon as a PNG with a transparent background, for LiteKern X, a
small operating system styled like GNOME (Adwaita). The icon is for <what the app does>.

Style: GNOME/Adwaita app icon. Flat, simple, rounded shapes; soft light from the top (a
slightly lighter top, a slightly darker bottom edge); no outlines, no text, no 3D
perspective, no photo textures. The shape fills about 40x40 pixels, centred, with a
4 pixel transparent margin. It must be crisp and readable at exactly 48x48 on a dark
background (#202634). Colours from the GNOME palette only: <pick 2-3, e.g. blue #62a0ea
#3584e4, light grey #deddda>.

Symbol: <1-3 simple shapes>.
```

| File | Symbol |
|---|---|
| `files.png` | A blue folder (`#3584e4` back and tab, `#62a0ea` front) with a light grey sheet of paper peeking out. |
| `log.png` | A dark terminal window (`#5e5c64` frame, `#241f31` screen) with a white `>` and `_`. |
| `notes.png` | A yellow note pad (`#f6d32d`) with 3 grey lines. |
| `settings.png` | A grey gear (`#9a9996`) with a darker centre. |
| `clock.png` | A white clock face with dark hands at 10:10 and a blue rim. |

### 4.3 Checklist
- [ ] 48×48, RGBA, with a transparent background.
- [ ] It reads clearly at 100% on `#202634`, next to the other icons.
- [ ] It's listed in `assets/icons.json`, and `wsl make` builds without an `icons2c:` error.

---

## 5. UI glyphs (SVG, title bar and widgets)

> These rules predate the Adwaita decision (2026-09-30) and use the §1 palette. Revisit them before making glyphs; the Phase 2 header-bar icons are drawn in code for now (`kernel/wm.c`).

### 5.0 Rules block (paste this first in every new chat for §5–§6)

```text
You are designing icons for LiteKern X, a small from-scratch operating system for a 2008
netbook (ASUS EeePC 1000HE: 1024x600 screen, 8.9" (~133 dpi), 32-bit colour, Intel Atom
CPU, no GPU acceleration). Everything you make will be rasterised to small bitmaps, so
crispness at 1x matters more than detail.

OUTPUT LANGUAGE AND FORMAT
- Output exactly one SVG file per answer, in a single ```svg code block, nothing else in
  the block.
- SVG 1.1, UTF-8. Root element: <svg xmlns="http://www.w3.org/2000/svg" width="W"
  height="H" viewBox="0 0 W H">. W and H are exactly the size I ask for.
- Allowed elements: <svg>, <g>, <path>, <rect>, <circle>, <ellipse>, <polygon>, <polyline>,
  <line>, <title>.
- Allowed attributes: fill, stroke, stroke-width, stroke-linecap, stroke-linejoin,
  fill-rule, opacity (only 1 or 0.5), x, y, width, height, rx, ry, cx, cy, r, d, points,
  x1, y1, x2, y2, transform (translate() only).
- Path commands allowed: M L H V C Q A Z (upper or lower case).
- FORBIDDEN: gradients, patterns, filters, blur, drop shadows, masks, clipPath, <text>,
  fonts, <image>, embedded bitmaps, <style>/CSS, class/id-based styling, <use>/<defs>,
  scripts, animation elements, rotate/scale/skew/matrix transforms, currentColor, named
  colours. Colours are 6-digit hex only.
- No comments, no metadata, no editor namespaces (inkscape:, sodipodi:).

PIXEL GRID RULES
- Design on the pixel grid of the requested size. All coordinates are whole numbers, or
  whole+0.5 for 1px strokes, so edges land on pixel boundaries.
- Minimum stroke width 1px. Minimum gap between shapes 1px.
- Keep a 1px empty margin inside the canvas unless I say otherwise.
- Simple, bold, geometric shapes. If a detail wouldn't survive at 16x16, leave it out.

COLOURS — use ONLY these 8 colours (plus fully transparent background):
  #0F1E33 ink, #1E3A5F navy, #2E5584 steel, #48A6E8 sky, #C8D0DC mist, #FFFFFF white,
  #E8A33D amber (one warm accent per icon at most), #B83232 red (errors/close only).
- The background of every file is transparent (no full-canvas rect).

STYLE
- Flat, geometric, friendly, "technical but warm". No skeuomorphism, 3D, photo realism
  or gloss. Implied light source top-left.
- No letters or words inside icons.
- Consistent family: same corner radius, same outline weight, same palette in every icon.
```

```text
UI GLYPH RULES (in addition to the Rules block)
- Canvas 16x16, 1px transparent margin. Strokes exactly 1px or 2px, with square caps.
- Default colour mist #C8D0DC (for use on navy). Only close-hover uses red #B83232.
- Each glyph is centred optically and symmetric where the shape allows.
```

| File | Prompt |
|---|---|
| `close.svg` | Window close: an "×" made of two 2px diagonal strokes, 8×8, centred. |
| `close-hover.svg` | Close hover: a 14×14 rounded square (rx=3) in red #B83232, with the close "×" on it in white. |
| `minimise.svg` | Minimise: one 2px horizontal line, 8px wide, in the lower third. |
| `maximise.svg` | Maximise: a 9×9 square outline, 1px, with a 2px top edge. |
| `check-off.svg` / `check-on.svg` | Checkbox, 12×12 box, 1px mist outline. The "on" version has a navy fill and a 2px sky check mark. |
| `radio-off.svg` / `radio-on.svg` | Radio button, 12×12 circle outline. The "on" version has a 6×6 sky dot at the centre. |
| `arrow-up.svg` / `arrow-down.svg` / `arrow-left.svg` / `arrow-right.svg` | Scroll/spin arrows: small solid triangles, 8px wide, mist. |
| `warning.svg` | Warning: an amber triangle with an ink "!" made of a rect and a square dot. It's a shape, not a font glyph. |
| `error.svg` | Error: a red circle with a white 2px "×". |
| `busy-dot.svg` | A 6×6 sky circle, used for small progress indicators. |

Disabled state: don't make separate files. The renderer draws any glyph at 50% opacity.

---

## 6. Logo (optional, SVG, for the Phase 4 website)
```text
[Rules block first]
Make assets/logo.svg, 128x128: the LiteKern X logo. A navy (#1E3A5F) rounded square
(rx=24) with a 2px ink outline. Inside it, a bold geometric "X" built from two thick
crossing bars: one bar sky (#48A6E8), the other mist (#C8D0DC). Where they cross, the
sky bar is on top, with a 2px navy gap separating the bars so it reads as layered.
Nothing else: no wordmark, no text, no shadows.
```
> The logo is the one place where an "X" letterform is allowed, because it's a geometric mark, not text in a font. It can also be shown at boot as the splash, now that the boot budget (≤ 5 s) has room for one.

---

## 7. Review checklist for SVG assets (§5–§6)
- [ ] It opens in a browser and shows the expected shape. Check it at **100% zoom**, not zoomed in.
- [ ] `width`, `height` and `viewBox` match the required size exactly.
- [ ] It contains none of: `<text>`, `<image>`, `<style>`, `gradient`, `filter`, `mask`, `clipPath`, `<use>`, `<defs>`, `rotate(`, `scale(`, `matrix(`. A quick check from PowerShell in the repo:
  ```
  wsl grep -nE "<text|<image|<style|gradient|filter|mask|clipPath|<use|<defs|rotate\(|scale\(|matrix\(" -r assets --include=*.svg
  ```
- [ ] Every colour is one of the 8 hex codes in §1.
- [ ] It reads on **both** navy `#1E3A5F` and light `#C8D0DC`.
- [ ] Family check: put every app icon side by side at 48 px and at 16 px. Same base, same radius, same weight.

Cursors have their own checklist in §3.5.
