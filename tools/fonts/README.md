# LVGL font pipeline

Downloads OFL TTFs from Google Fonts and generates LVGL v8 bitmap fonts
(`.c`, 4bpp) into `main/displays/fonts/`. Everything is local to this
folder - `npm install` only installs into `tools/fonts/node_modules`,
nothing globally.

See `FONT-LICENSES.md` for the license and source of every TTF used here.

## Reproducing from scratch

```powershell
cd tools\fonts

# 1) download the TTFs (github.com/google/fonts, OFL license, ofl/<family>)
.\download-fonts.ps1

# 2) install lv_font_conv locally (node_modules in this folder)
npm install

# 3) instance static weights/widths of Archivo (it ships only as a variable
#    font: one file with wght/wdth axes, no separate static cuts)
python instance-archivo.py

# 4) generate the .c font files directly into ../../main/displays/fonts/
.\generate-fonts.ps1
```

Everything here also runs fine from a portable Docker container instead of a
local Node/Python install, e.g.:

```bash
docker run --rm -v "$PWD/..":/project -w /project/tools/fonts node:20 \
  bash -c "npm install && npx lv_font_conv --help"
```

## What each step does

1. **`download-fonts.ps1`** - file names under `ofl/<family>` are not
   predictable (they vary per family: some ship only a Regular, others a
   full static-weight family, others a single variable-font file), so they
   are discovered through
   `https://api.github.com/repos/google/fonts/contents/ofl/<family>` rather
   than guessed.

2. **`instance-archivo.py`** - `ofl/archivo` only ships
   `Archivo[wdth,wght].ttf` (a variable font, `wght` axis 100-900 default
   600, `wdth` axis 62-125 default 100) plus its italic. `lv_font_conv` only
   reads the *default* instance of a variable font (it has no axis-selection
   flag), so without instancing first, every generated size would come out
   at weight 600 / width 100% - not the actual weights/widths this project
   needs (400/500/700, and condensed widths 82-88%). The script uses
   `fontTools.varLib.instancer` to produce the needed static TTFs.

   `ofl/sairacondensed`, by contrast, already ships static per-weight files
   in the current Google Fonts repo (Regular/Medium/SemiBold/Bold/Black/...)
   - it is not a variable font there, so that family skips instancing and
   downloads the right static `.ttf` directly.

3. **`generate-fonts.ps1`** - runs `npx lv_font_conv --format lvgl --bpp 4`
   once per family/weight/size/glyph-set combination. Glyphs are passed as
   `--range` (hex codepoints), not `--symbols` with literal accented
   characters, to avoid depending on the console's code page when the
   arguments reach the child `node` process. Two glyph sets are used:
   - `NUM` = digits 0-9, comma, dot, hyphen, %, ° - for fields that only
     ever show numbers (hashrate, MHz, °C, averages).
   - `FULL` = printable ASCII + Latin-1 accented letters + ° - for fields
     that show words.

   The `lv_font_conv` output format (guards `#ifdef
   LV_LVGL_H_INCLUDE_SIMPLE` / `#include "lvgl.h"` else `#include
   "lvgl/lvgl.h"`) matches what the rest of the project's generated fonts
   already use, so no extra `--lv-include` flag is needed.

## Font/weight/size choices

| File | Family/weight | px | Glyphs | Used by |
|---|---|---|---|---|
| `font_instrument_serif_hero.c` | Instrument Serif Regular (only weight available) | 188 | NUM | 19-zen hero number |
| `font_saira_condensed_bold_108.c` | Saira Condensed Bold | 108 | NUM | 01-painel hashrate hero |
| `font_saira_condensed_bold_28.c` | Saira Condensed Bold | 28 | NUM | tape center reading |
| `font_saira_condensed_semibold_24.c` | Saira Condensed SemiBold | 24 | FULL | footer (includes governor state words) |
| `font_saira_condensed_semibold_15.c` | Saira Condensed SemiBold | 15 | NUM | tape tick marks/numeric labels |
| `font_archivo_regular_15.c` | Archivo Regular, wdth 88% | 15 | FULL | small labels (zen footer; painel titles/unit/footer) |
| `font_archivo_regular_19.c` | Archivo Regular, wdth 88% | 19 | FULL | 19-zen hero unit |
| `font_archivo_medium_15.c` | Archivo Medium, wdth 82% | 15 | FULL | tabs/axis/average labels in 14-hashrate |
| `font_archivo_medium_17.c` | Archivo Medium, wdth 88% | 17 | FULL | insight sentence in 14-hashrate |
| `font_archivo_bold_20.c` | Archivo Bold, wdth 100% | 20 | FULL | 14-hashrate screen title |
| `font_archivo_bold_24.c` | Archivo Bold, wdth 100% | 24 | NUM | average values in 14-hashrate |

`NUM` = `0123456789,.-%°` · `FULL` = printable ASCII + Latin-1 accented
letters + `°`.

Every screen added after this original 3-screen pilot declares its own new
font locally in its own `.cpp` file (see `main/displays/screens/`) rather
than adding to `fonts.h`, following the same generation pipeline.

## Known limitations

- **Saira Condensed weight 400 (Regular) was not generated**: nothing in
  the pilot screens uses weight 400 of that family - every use is either
  weight 500 (rounded to the nearest static cut, 600) or 700. Generating an
  unused weight would only cost extra flash for nothing.
- **Archivo's `tnum` (tabular figures) could not be selected**:
  `lv_font_conv` does not expose an OpenType feature flag, so the tabular
  variant cannot be forced from the command line. In practice Archivo
  already uses monospaced digit widths by default in most weights, so the
  visual impact should be small, though it was not checked pixel by pixel.
- **File size on disk is not the linked firmware cost**: the `.c` file size
  is the size of the hex/text C source, not the actual size after
  compiling and linking (typically smaller, since hex text becomes compact
  binary bytes). The real cost is only known after a build.
