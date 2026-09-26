# Font licenses

This lists every source TTF under `tools/fonts/ttf/` and `tools/fonts/instanced/`,
its family, designer/foundry, and license. The generated bitmap fonts under
`main/displays/fonts/*.c` are derived from these TTFs (converted to LVGL's
compressed 4bpp bitmap format with `lv_font_conv`, see `tools/fonts/README.md`)
and carry the same license as their source font.

All of these are distributed through [Google Fonts](https://fonts.google.com/).

## `tools/fonts/ttf/`

| File | Family | Designer / foundry | License |
|---|---|---|---|
| `Anton-Regular.ttf` | [Anton](https://fonts.google.com/specimen/Anton) | Vernon Adams | SIL Open Font License 1.1 |
| `Archivo-VF.ttf` | [Archivo](https://fonts.google.com/specimen/Archivo) (variable) | Omnibus-Type | SIL Open Font License 1.1 |
| `CourierPrime-Bold.ttf` | [Courier Prime](https://fonts.google.com/specimen/Courier+Prime) | Alan Dague-Greene / Quote-Unquote Apps | SIL Open Font License 1.1 |
| `CourierPrime-Regular.ttf` | [Courier Prime](https://fonts.google.com/specimen/Courier+Prime) | Alan Dague-Greene / Quote-Unquote Apps | SIL Open Font License 1.1 |
| `Fraunces-VF.ttf` | [Fraunces](https://fonts.google.com/specimen/Fraunces) (variable) | Undercase Type (Sturdy, Coppins, Rocha) | SIL Open Font License 1.1 |
| `IBMPlexMono-Bold.ttf` | [IBM Plex Mono](https://fonts.google.com/specimen/IBM+Plex+Mono) | IBM / Bold Monday | SIL Open Font License 1.1 |
| `InstrumentSerif-Regular.ttf` | [Instrument Serif](https://fonts.google.com/specimen/Instrument+Serif) | Instrument | SIL Open Font License 1.1 |
| `SairaCondensed-Bold.ttf` | [Saira Condensed](https://fonts.google.com/specimen/Saira+Condensed) | Omnibus-Type (Häkkinen) | SIL Open Font License 1.1 |
| `SairaCondensed-SemiBold.ttf` | [Saira Condensed](https://fonts.google.com/specimen/Saira+Condensed) | Omnibus-Type (Häkkinen) | SIL Open Font License 1.1 |
| `SpecialElite-Regular.ttf` | [Special Elite](https://fonts.google.com/specimen/Special+Elite) | Nate Piekos / Blambot | Apache License 2.0 |

## `tools/fonts/instanced/`

These are static instances generated from the variable fonts above (`Archivo-VF.ttf`,
`Fraunces-VF.ttf`) with `fontTools.varLib.instancer` (see `instance-archivo.py`),
because `lv_font_conv` needs a single static weight/width, not a variable-font
axis space. Each one carries the same SIL Open Font License 1.1 as its parent
variable font -- instancing does not change the license.

| File | Source | Instanced axes |
|---|---|---|
| `Archivo-Bold-wdth100.ttf` | Archivo-VF.ttf | wght=700, wdth=100 |
| `Archivo-Medium-wdth82.ttf` | Archivo-VF.ttf | wght=500, wdth=82 |
| `Archivo-Medium-wdth88.ttf` | Archivo-VF.ttf | wght=500, wdth=88 |
| `Archivo-Regular-wdth88.ttf` | Archivo-VF.ttf | wght=400, wdth=88 |
| `Fraunces-Bold-opsz72.ttf` | Fraunces-VF.ttf | wght=700, opsz=72 |

## SIL Open Font License 1.1

Full text: <https://openfontlicense.org/documents/OFL.txt>

Permits use, study, modification and redistribution, embedded in a product
(such as this firmware's compiled bitmap fonts) or standalone, free of
charge, including for commercial products, provided the font itself is never
sold on its own and any modified version is not distributed under the
original font name.

## Apache License 2.0

Full text: <https://www.apache.org/licenses/LICENSE-2.0>

Permits use, modification and redistribution (including in a compiled/derived
form such as this firmware's bitmap font), with attribution, for any purpose
including commercial use.
