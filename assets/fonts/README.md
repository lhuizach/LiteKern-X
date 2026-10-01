# The GUI font

`ui-font.png` (the glyphs) and `ui-font.json` (their metrics) are **Ubuntu Sans** (version 1.006) rasterised by `tools/font-rasterize.py`, at the sizes and weights the GUI uses (`kernel/text.h`). The build turns them into C (`tools/font2c.py`). The console keeps the video BIOS's 8×16 bitmap font.

- **Font:** Ubuntu Sans, Copyright 2011, 2022, 2023 Canonical Ltd. Designed by Dalton Maag Ltd.
- **Licence:** the Ubuntu Font Licence 1.0, <https://ubuntu.com/legal/font-licence>. It allows the font to be used, bundled and redistributed with software, provided its copyright notice and licence go with it.
- "Ubuntu" and "Canonical" are registered trademarks of Canonical Ltd. LiteKern X is not affiliated with either.

**Licence text:** `LICENCE-ubuntu-font.txt` in this folder (from the Ubuntu Sans repository). Before publishing (Phase 4), also mention the font in the release notes.

To regenerate, for example after changing a size in the tool's `FACES` (needs Pillow):
```
python tools/font-rasterize.py "path/to/UbuntuSans[wdth,wght].ttf" assets/fonts
```
In WSL the font is at `/usr/share/fonts/truetype/ubuntu/`.
