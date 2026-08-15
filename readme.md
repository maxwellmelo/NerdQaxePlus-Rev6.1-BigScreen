# NerdQaxe++ Rev 6.1 Big-Screen Firmware

This community fork adds a native 480 x 320 interface and 32 MB BOYA flash
support for the YYSLUPING NerdQaxe++ Rev 6.1 big-screen model. It keeps the
upstream miner, AxeOS, pool, and tuning behavior while making the built-in
display readable and fully usable at its actual resolution.

<img width="348" height="220" alt="Screenshot 2026-08-15 024342" src="https://github.com/user-attachments/assets/6f572a84-873e-4f29-ba31-b4f74aca2dc8" />
<img width="540" height="361" alt="Screenshot 2026-08-15 024138" src="https://github.com/user-attachments/assets/0498dfab-f589-42b0-9780-c231306d6242" />
<img width="542" height="363" alt="Screenshot 2026-08-15 024058" src="https://github.com/user-attachments/assets/af71895b-082b-4116-9344-b7adbfb2a1b2" />


## Release

The hardware-tested release is
[`v1.0.37.2-bigscreen.1`](https://github.com/XTVDDICT/NerdQaxePlus-Rev6.1-BigScreen/releases/tag/v1.0.37.2-bigscreen.1).

- Firmware: `nerdqaxepp-rev61-bigscreen-v1.0.37.2-bigscreen.1.bin`
- SHA-256: `1D94AC66E3EA142F4912EF7FA77B6B1792F4D3B81408CE51D157A33D2AD3537A`
- Image size: 2,995,216 bytes
- Base firmware: upstream `v1.0.37.2-LTS`

### Compatibility

Use this build only with the YYSLUPING NerdQaxe++ Rev 6.1 big-screen unit that
reports board version `501`, has a 480 x 320 ST7789-compatible display, a BOYA
BY25Q256FS 32 MB flash chip (JEDEC `0x684019`), and 8 MB PSRAM. It is not a
generic NerdQaxe++ display update.

The release binary is an application/OTA image, not a full-flash image. Back up
your original firmware first, then install it through the normal AxeOS firmware
update page. Do not write it at flash offset `0x0`, and do not erase the device.
Existing NVS configuration is preserved by a normal OTA update, but keeping a
backup is still strongly recommended.

This release was built with ESP-IDF 5.3.3 and tested on one physical Rev 6.1
unit under mining load. The display, web UI, both fans, all ASICs, temperature,
power, hashrate, and whole-watt power formatting were exercised successfully.
Faint remnants of the stock graphics on some panels are LCD image retention,
not content drawn by this firmware.

## Big-Screen Build

Start from a clean build directory and explicitly select both the NerdQaxe++
board and YYSLUPING display profile:

```powershell
$env:BOARD = 'NERDQAXEPLUS2'
$env:DISPLAY_PROFILE = 'YYSLUPING_480X320'
idf.py -B build-yysluping `
  "-DSDKCONFIG=$PWD\build-yysluping\sdkconfig" `
  "-DSDKCONFIG_DEFAULTS=$PWD\sdkconfig.defaults;$PWD\sdkconfig.yysluping.defaults" `
  set-target esp32s3
idf.py -B build-yysluping `
  "-DSDKCONFIG=$PWD\build-yysluping\sdkconfig" `
  "-DSDKCONFIG_DEFAULTS=$PWD\sdkconfig.defaults;$PWD\sdkconfig.yysluping.defaults" `
  build
```

Leaving `DISPLAY_PROFILE` unset preserves the upstream 320 x 170 display path.
The profile is rejected at configure time for boards other than
`NERDQAXEPLUS2`.

## Changes In This Fork

- Adds an opt-in `YYSLUPING_480X320` display profile.
- Rebuilds the mining, settings, market, network, setup, splash, QR, error, and
  shutdown screens for 480 x 320.
- Adds exact-ID support for the BOYA BY25Q256FS 32 MB SPI flash.
- Matches the unit's stock 32 MB partition layout and 5 MB OTA slots.
- Displays power as whole watts on the big-screen layout so the value remains
  legible.
- Leaves upstream behavior unchanged when the big-screen profile is not chosen.

## Upstream And License

This project is based on
[`shufps/ESP-Miner-NerdQAxePlus`](https://github.com/shufps/ESP-Miner-NerdQAxePlus).
Thanks to its maintainers and the Bitaxe, NerdAxe, and NerdQaxe contributors.
The source remains licensed under GPL-3.0; see [LICENSE](LICENSE).

---

[![](https://dcbadge.vercel.app/api/server/3E8ca2dkcC)](https://discord.gg/3E8ca2dkcC)

# ESP-Miner-Nerdaxe version

| Supported Targets | ESP32-S3              |
| ----------------- | --------------------- |
| Required Platform | >= ESP-IDF v5.3.X       |
| ----------------- | --------------------- |

This is a forked version from the NerdAxe miner that was modified for using on the [NerdQAxe+](https://github.com/shufps/qaxe).

Credits to the devs:
- BitAxe devs on OSMU: @skot/ESP-Miner, @ben and @jhonny
- NerdAxe dev @BitMaker


## How to flash/update firmware

The newest releases are always here:

https://github.com/shufps/ESP-Miner-NerdQAxePlus/releases

### Recommended Method: The Webflasher

The [Webflasher](https://shufps.github.io/nerdqaxe-web-flasher/) (modified fork of the great [Bitaxe Webflasher](https://github.com/bitaxeorg/bitaxe-web-flasher) by [Wantclue](https://github.com/WantClue)) is the easiest method of updating all Nerd*axe variants.

[<img src="https://github.com/user-attachments/assets/4168f23a-bfe7-4536-91e3-7af6df9a203a" style="border:5px solid red;width:200px">](https://shufps.github.io/nerdqaxe-web-flasher/)

It uses the official releases published on this repository and is always up-to-date.

### Other Methods

#### Clone repository and prepare config

First you need to clone the repository and create a local copy of the config file:

```bash
# clone repository
git clone https://github.com/shufps/ESP-Miner-NerdQAxePlus

# change into the cloned repository
cd ESP-Miner-NerdQAxePlus

# copy the example config
cp config.cvs.example config.cvs
```

Then you can edit the fields like `stratumurl` and so on.

#### Bitaxetool

After the changes on the `config.cvs` files are done, you use the `bitaxetool` to flash factory binary and the config onto the device.

To switch it into bootload mode, reset the device with presset `boot` button.

```
bitaxetool --config ./config.cvs --firmware esp-miner-factory-NERDQAXEPLUS-v1.0.10.bin

```


## How to build firmware

### Using Docker

Docker containers allow to use the toolchain without installing `esp-idf` or `Node 20.x` on the system.

#### 0. TL;DR - `esp-miner.bin`, `www.bin`
```bash

# only once
cd docker
./build_docker.sh
cd ..

export BOARD="NERDQAXEPLUS2"
./docker/idf.sh set-target esp32-s3

# after each change on the source code
./docker/idf.sh build
```

Afterwards you will have a `esp-miner.bin` and `www.bin` in your `build` directory.


#### 1. First build the docker container

```bash
cd docker
./build_docker.sh
```

#### 2. How to use it

There are several scripts in the `docker` directory but what is most flexible is to just start the container as bash via

```bash
./docker/idf-shell.sh
```

You will get a new terminal that provides tools like:
- `idf.py`
- `bitaxetool`
- `esptool.py`
- `nvs_partition_gen.py`

The current repository will be mounted to `/home/builder/project`.

The default `builder` user has `uid:gid = 1000:1000` (like the main user on *buntu/Mint)

#### 3. Compiling & Flashing using the shell

#### 3.1. Just flashing with dockered `bitaxetool` with factory binary

(no `idf-shell.sh` version)

```bash
./docker/bitaxetool.sh --config config.cvs --firmware esp-miner-factory-NERDQAXEPLUS-v1.0.10.bin -p /dev/ttyACM0
```

##### 3.2. Compiling & Flashing using BitAxe tool

(inside of `idf-shell.sh`)

```bash
# start idf-shell
./docker/idf-shell.sh

# set board
export BOARD="NERDQAXEPLUS2"

# set target and build the binaries
idf.py set-target esp32s3
idf.py build

# merge all partitions including config into a single binary
./merge_bin.sh nerdqaxe+.bin

bitaxetool --config config.cvs --firmware esp-miner-factory-nerdqaxe+.bin  -p /dev/ttyACM0
```

#### 3.3. All manual steps for building and flashing

(inside of `idf-shell.sh`)

```bash
# start idf-shell
./docker/idf-shell.sh

# set board
export BOARD="NERDQAXEPLUS2"

# set target and build the binaries
idf.py set-target esp32s3

# optional if you want to change the sdkconfig
idf.py menuconfig

# build the binaries
idf.py build

# creat config.bin nvm partition from config.cvs
nvs_partition_gen.py generate config.cvs config.bin 12288

# merge all partitions including config into a single binary
./merge_bin_with_config.sh nerdqaxe+.bin

# flash using esptool
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before=default_reset --after=hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 nerdqaxe+.bin
```


When done just `exit` the shell.


### Without Docker

Install bitaxetool from pip. pip is included with Python 3.4 but if you need to install it check <https://pip.pypa.io/en/stable/installation/>

```
pip install --upgrade bitaxetool
```

## Grafana Monitoring

<img src="https://github.com/user-attachments/assets/3c485428-5e48-4761-9717-bd88579a747d" width="600px">

The NerdQaxe+ firmware supports Influx and the repository provides an installation with Grafana dashboard that can be started with a few bash commands: https://github.com/shufps/ESP-Miner-NerdQAxePlus/tree/master/monitoring


