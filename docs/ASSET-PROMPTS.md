# LiteKern X — Asset Prompts (icons, cursors, UI glyphs)

Prompts for generating LiteKern X's visual assets with an AI that **writes SVG code** (ChatGPT, Claude, Gemini, …).

**Don't use image generators** (Midjourney, DALL·E, Stable Diffusion) for these. They produce blurry raster art, can't hit exact 16/24/32 px grids, and can't give you exact cursor hotspots.

**When these are used:** Phase 2 §2 needs the cursors. Phase 2 §4 (widgets) and Phase 3 §1 (the final icon/cursor set) need the rest. Generating them now is fine, because they're files, not features. Commit them under `assets/` so they're ready when those steps start.

---

## How to use this
1. Start a new chat and paste the **Rules block** (§1) first.
2. Then paste **one** asset prompt from §3–§6 per message. Asking for one file at a time gives much better results than asking for a whole set.
3. Save each answer as the file name the prompt asks for, under `assets/`.
4. Check each file against the **Review checklist** (§7) before committing it. If one fails, reply to the AI with the rule it broke.

## Where the files go
```
assets/
  cursors/      arrow.svg, hand.svg, text.svg, wait-0..3.svg, move.svg, not-allowed.svg,
                resize-h.svg, resize-v.svg, resize-d1.svg, resize-d2.svg
  icons/apps/   <app>.svg            one 48x48 master per app
  icons/ui/     close.svg, minimise.svg, check.svg, ... (16x16)
  cursors.json  hotspots and animation (see §3.3)
  palette.md    copy of §2, the only colours allowed
```

---

## 1. Rules block (paste this first in every new chat)

```text
You are designing icons and cursors for LiteKern X, a small from-scratch operating system
for a 2008 netbook (ASUS EeePC 1000HE: 1024x600 screen, 8.9" (~133 dpi), 32-bit colour,
Intel Atom CPU, no GPU acceleration). Everything you make will be rasterised to small
bitmaps, so crispness at 1x matters more than detail.

OUTPUT LANGUAGE AND FORMAT
- Output exactly one SVG file per answer, in a single ```svg code block, nothing else in
  the block. After the block, one line saying the hotspot (cursors only) and nothing more.
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
- No comments, no metadata, no editor namespaces (inkscape:, sodipodi:). Keep the file
  small and hand-readable.

PIXEL GRID RULES
- Design on the pixel grid of the requested size. All rect/line/polygon coordinates are
  whole numbers, or whole+0.5 for 1px strokes, so edges land on pixel boundaries.
- Minimum stroke width 1px. Minimum gap between shapes 1px. Nothing thinner than 1px.
- Keep a 1px empty margin inside the canvas unless I say otherwise.
- Simple, bold, geometric shapes. If a detail wouldn't survive at 16x16, leave it out.

COLOURS — use ONLY these 8 colours (plus fully transparent background):
  #0F1E33  ink        (outlines, darkest detail)
  #1E3A5F  navy       (system background colour; main dark fill)
  #2E5584  steel      (secondary dark fill, shading)
  #48A6E8  sky        (accent, the LiteKern blue; highlights, active state)
  #C8D0DC  mist       (light fill, text colour on navy)
  #FFFFFF  white      (cursor fill, brightest highlight)
  #E8A33D  amber      (warnings, one warm accent per icon at most)
  #B83232  red        (errors, close button, destructive actions only)
- No other colours, no opacity blends except 0.5 for a disabled state when I ask for one.
- The background of every file is transparent (no full-canvas rect).

STYLE
- Flat, geometric, friendly, "technical but warm". Think 2000s pixel-precise UI icons
  redrawn flat: no skeuomorphism, no 3D, no photo realism, no gloss.
- Light source is implied top-left: if you shade, the lighter tone is on the top/left.
- Every icon must read clearly on BOTH the navy desktop (#1E3A5F) and a light window
  (#C8D0DC): give light shapes an ink (#0F1E33) 1px outline where they touch the edge.
- No letters or words inside icons (no "A" for a text editor, no "Settings" labels).
- Consistent family: same corner radius, same outline weight, same palette in every icon.
```

---

## 2. Sizes, formats and naming at a glance

| Asset | Canvas (px) | Master file | Notes |
|---|---|---|---|
| Cursors | 32×32 | `assets/cursors/<name>.svg` | Visible shape about 20–24 px tall. Hotspot recorded in `cursors.json`. |
| Busy cursor | 32×32 × 4 frames | `assets/cursors/wait-0.svg` … `wait-3.svg` | Played at 150 ms per frame. |
| App icons | 48×48 master | `assets/icons/apps/<app>.svg` | Also used downscaled to 32 and 16. Ask for a hand-tuned 16×16 if the downscale looks muddy (§4.3). |
| UI glyphs | 16×16 | `assets/icons/ui/<name>.svg` | Title-bar buttons, checkboxes and similar |
| Logo (optional) | 128×128 | `assets/logo.svg` | For the Phase 4 website. It is **not** a boot splash, because the boot budget is ≤1000 ms. |

- **File names:** lowercase letters, digits and hyphens only (`resize-d1.svg`, `text-editor.svg`).
- **Build-time conversion** (Phase 2): the build will rasterise these SVGs to 32-bit ARGB bitmaps for the kernel. The strict SVG subset in §1 keeps that simple, and keeps it compatible with the SVG cursor rasteriser being ported from v1.

> ⚠ **Check this against v1 first.** v1 already had "SVG-spec cursors". If v1's rasteriser supports fewer features than §1 allows (for example no `A` arcs or no `C` curves), remove those from the Rules block before generating anything, so the new cursors stay compatible with the rasteriser being ported.

---

## 3. Cursors

### 3.1 Shared cursor rules (paste once, after the Rules block)
```text
CURSOR RULES (in addition to the rules above)
- Canvas 32x32. The visible cursor is 20-24px tall; the rest is transparent.
- Fill #FFFFFF with a 1px #0F1E33 outline on every edge, so it's visible on navy, on white
  and on sky-blue. No other colours unless the prompt says so.
- The hotspot (the exact pixel that "clicks") must be a real pixel of the shape's tip or
  centre, given as integer x,y. State it after the code block as: hotspot: x,y
- Shapes must be pixel-aligned with no anti-aliasing tricks. They must look sharp at 1x.
```

### 3.2 One prompt per cursor
| File | Prompt (paste after the rules) | Expected hotspot |
|---|---|---|
| `arrow.svg` | Default pointer: a classic left-leaning arrow pointing to the top-left, with a short tail. The tip is at the top-left of the visible shape. Visible height 21px. | 1,1 (the tip) |
| `hand.svg` | Link/button pointer: a hand with the index finger pointing straight up and the other fingers folded, drawn as simple rounded rectangles. The fingertip is the hotspot. Visible height 22px. | fingertip, about 10,1 |
| `text.svg` | Text I-beam: a vertical bar 1px wide and 17px tall, with small serifs (5px wide) at the top and bottom. Ink outline. | centre of the bar, about 16,16 |
| `wait-0.svg` … `wait-3.svg` | Busy cursor, 4 animation frames. The **arrow from arrow.svg**, plus a small 9×9 ring beside its bottom-right made of 4 segments. In frame N, segment N is sky #48A6E8 and the others are mist #C8D0DC. The arrow must be pixel-identical in all 4 frames. Output one frame per answer, starting with frame 0. | same as arrow |
| `move.svg` | Move: a plus shape with an arrowhead on each of its 4 ends, 21×21, centred. | 16,16 |
| `not-allowed.svg` | Not allowed: the arrow plus a small 10×10 circle with a diagonal bar at its bottom-right, in red #B83232 with a 1px ink outline. | same as arrow |
| `resize-h.svg` | Horizontal resize: a double-headed arrow pointing left and right, 21px wide, centred. | 16,16 |
| `resize-v.svg` | Vertical resize: a double-headed arrow pointing up and down, 21px tall, centred. | 16,16 |
| `resize-d1.svg` | Diagonal resize from top-left to bottom-right: a double-headed arrow, centred. | 16,16 |
| `resize-d2.svg` | Diagonal resize from top-right to bottom-left: a double-headed arrow, centred. Mirror of resize-d1. | 16,16 |

### 3.3 `assets/cursors.json` (write this yourself, or ask the AI to fill it in from its hotspot lines)
The **language here is JSON**, in the same spirit as `kerns.json`:
```json
{
  "version": 1,
  "size": 32,
  "cursors": {
    "arrow":       { "file": "arrow.svg",       "hotspot": [1, 1] },
    "hand":        { "file": "hand.svg",        "hotspot": [10, 1] },
    "text":        { "file": "text.svg",        "hotspot": [16, 16] },
    "wait":        { "frames": ["wait-0.svg", "wait-1.svg", "wait-2.svg", "wait-3.svg"],
                     "frame_ms": 150, "hotspot": [1, 1] },
    "move":        { "file": "move.svg",        "hotspot": [16, 16] },
    "not-allowed": { "file": "not-allowed.svg", "hotspot": [1, 1] },
    "resize-h":    { "file": "resize-h.svg",    "hotspot": [16, 16] },
    "resize-v":    { "file": "resize-v.svg",    "hotspot": [16, 16] },
    "resize-d1":   { "file": "resize-d1.svg",   "hotspot": [16, 16] },
    "resize-d2":   { "file": "resize-d2.svg",   "hotspot": [16, 16] }
  }
}
```
- **Rules:** hotspots are integers inside `0..size-1`, every file named must exist, and there are no extra keys.
- Replace each placeholder hotspot with the one the AI reported, after checking it on the actual shape.

---

## 4. App icons

### 4.1 Shared app-icon rules (paste once, after the Rules block)
```text
APP ICON RULES (in addition to the rules above)
- Canvas 48x48 with a 2px transparent margin, so the icon body fits in 44x44.
- Every app icon sits on the same base: a rounded square 44x44 at (2,2), rx=8, fill navy
  #1E3A5F, 1px ink #0F1E33 outline, and a 1px steel #2E5584 inner highlight line along
  the top edge only.
- The symbol on the base uses mist #C8D0DC and white #FFFFFF, with exactly ONE accent
  colour (sky #48A6E8, or amber #E8A33D if the prompt says so).
- The symbol fits in the central 28x28 area (from 10,10 to 38,38) and is readable when
  the whole icon is shrunk to 16x16. Use at most 3 main shapes.
- Same base, radius, outline and highlight in every app icon: these are a family.
```

### 4.2 Per-app prompts
Phase 3 calls for **1–3 finished apps**, and which apps is still open. Use the ones you pick, and keep the others for later. These are all plausible for a no-network, read-only-ramdisk OS:

| File | Symbol prompt (paste after the app-icon rules) |
|---|---|
| `notes.svg` | A notes app: a mist sheet of paper with its top-right corner folded, and 3 short horizontal sky lines on it. |
| `calculator.svg` | A calculator: a mist rectangle body, a sky display strip across the top, and a 3×3 grid of small white squares as keys. |
| `clock.svg` | A clock: a mist circle face with an ink outline, two ink hands showing about 10:10, and a single sky dot at the centre. |
| `sysinfo.svg` | A system information app: a mist chip shape (a square with 3 short pins on each side) with a sky square at its centre. |
| `paint.svg` | A drawing app: a white diagonal brush from bottom-left to top-right, with an amber tip (the amber accent). |
| `terminal.svg` | A console/log viewer: a mist window frame containing a sky ">" chevron and a short white underscore cursor. |
| `settings.svg` | Settings: a mist gear with 8 teeth and a navy hole, and one sky ring around the hole. |
| `launcher.svg` | The app launcher/home: a 2×2 grid of mist rounded squares, with the top-left one in sky. |

Template for a new app:
```text
[paste the Rules block and the App icon rules first]
Make assets/icons/apps/<name>.svg: an app icon for <what the app does, in one sentence>.
Symbol: <1-3 simple shapes and their colours>. Accent colour: <sky or amber>.
```

### 4.3 Hand-tuned 16×16 version (only if the downscale looks muddy)
```text
[Rules block first]
Redraw this 48x48 app icon as a 16x16 icon for a taskbar: <paste the 48x48 SVG>.
Canvas 16x16, base rounded square 14x14 at (1,1) rx=3, same colours. Simplify the symbol
to at most 2 shapes; every shape at least 2px wide. Save as <name>-16.svg.
```

---

## 5. UI glyphs (title bar and widgets)

```text
UI GLYPH RULES (in addition to the rules above)
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

Disabled state: don't generate separate files. The renderer draws any glyph at 50% opacity.

---

## 6. Logo (optional, for the Phase 4 website)
```text
[Rules block first]
Make assets/logo.svg, 128x128: the LiteKern X logo. A navy (#1E3A5F) rounded square
(rx=24) with a 2px ink outline. Inside it, a bold geometric "X" built from two thick
crossing bars: one bar sky (#48A6E8), the other mist (#C8D0DC). Where they cross, the
sky bar is on top, with a 2px navy gap separating the bars so it reads as layered.
Nothing else: no wordmark, no text, no shadows.
```
> The logo is the one place where an "X" letterform is allowed, because it's a geometric mark, not text in a font. It's not shown at boot, since the 1000 ms boot budget has no room for a splash.

---

## 7. Review checklist (before committing any asset)
- [ ] It opens in a browser and shows the expected shape. Check it at **100% zoom**, not zoomed in.
- [ ] `width`, `height` and `viewBox` match the required size exactly.
- [ ] It contains none of: `<text>`, `<image>`, `<style>`, `gradient`, `filter`, `mask`, `clipPath`, `<use>`, `<defs>`, `rotate(`, `scale(`, `matrix(`. Quick check from PowerShell in the repo:
  ```
  wsl grep -nE "text|image|style|gradient|filter|mask|clipPath|<use|<defs|rotate\(|scale\(|matrix\(" assets -r
  ```
- [ ] Every colour is one of the 8 hex codes in §1 (search for `#` and compare).
- [ ] It reads on **both** navy `#1E3A5F` and light `#C8D0DC`. Drop it into a page with each background colour to check.
- [ ] Cursors: the reported hotspot is actually on the tip or centre, and it's in `cursors.json`.
- [ ] Family check: put every app icon side by side at 48 px and at 16 px. Same base, same radius, same weight.
