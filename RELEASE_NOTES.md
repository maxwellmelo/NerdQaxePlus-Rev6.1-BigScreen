# v1.0.37.2-bigscreen.1

First public hardware-tested release for the YYSLUPING NerdQaxe++ Rev 6.1
480 x 320 model.

## Highlights

- Native full-screen 480 x 320 layouts across the device UI.
- BOYA BY25Q256FS 32 MB flash support.
- Stock-compatible 32 MB partition map with 5 MB OTA slots.
- Readable whole-watt power display.
- Upstream display behavior remains unchanged unless the big-screen profile is
  explicitly selected.

## Tested Hardware

- YYSLUPING NerdQaxe++ Rev 6.1
- Reported board version: `501`
- Display: 480 x 320 ST7789-compatible i80 panel
- Flash: BOYA BY25Q256FS, JEDEC `0x684019`, 32 MB
- PSRAM: 8 MB

The release was tested on one physical unit under mining load. The web UI,
display pages, both fans, all ASICs, telemetry, and mining operation were
verified. Some panels may retain faint stock graphics; that is LCD image
retention and cannot be cleared by redrawing the interface.

## Install Warning

The attached `.bin` is an application/OTA image. Use the normal AxeOS firmware
update page after backing up the original firmware. Do not flash it at offset
`0x0`, do not erase the entire chip, and do not use it on other NerdQaxe++
display or flash variants.

## Integrity

`nerdqaxepp-rev61-bigscreen-v1.0.37.2-bigscreen.1.bin`

SHA-256:
`1D94AC66E3EA142F4912EF7FA77B6B1792F4D3B81408CE51D157A33D2AD3537A`

Built from upstream `v1.0.37.2-LTS` with ESP-IDF 5.3.3.
