# GoFlockYourself

**Passive Flock Safety camera detector** for the **ESP32-2432S028R** (Cheap Yellow Display / CYD).

Modern dark UI on the built-in 2.8″ ILI9341 touchscreen. Primary detection is **WiFi promiscuous mode** with full OUI matching, wildcard probe requests, and channel hopping. Optional BLE, piezo alert, RGB LED, and microSD logging.

> Receive-only. No association, no deauth, no transmit of attack frames. Field research / privacy awareness tool.

---

## Safety & disclaimer

This is a hobby project, shared as-is for anyone who wants to build their own version. A few things to know before you do:

* **LiPo batteries:** incorrect wiring, reverse polarity, or physical damage to a LiPo cell can cause fire. Double-check polarity yourself before powering anything on, use a charging module rated for the cell you're using, and never leave a charging LiPo unattended.
* **RF hardware:** the radio modules in this project operate according to their respective regulatory approvals (e.g. CE) for their intended region — verify this applies to your location before use.
* **No warranty:** this repository (firmware, wiring diagrams, enclosures, scripts, and this README) is provided without any warranty, express or implied. Verify every connection yourself before applying power — don't assume the diagrams or code here are free of mistakes.
* **Use at your own risk:** building and operating this project is entirely your own responsibility. The author assumes no liability for damage, injury, or loss resulting from building or using this project.

If you spot an error in the docs, wiring, or code, please open an issue — corrections are welcome.

---

## Documentation

- [Technical documentation (Markdown)](docs/GoFlockYourself_Technical_Documentation.md)
- [Technical documentation (PDF)](docs/GoFlockYourself_Technical_Documentation.pdf)
- [Page previews](docs/preview/)

---

## Hardware

| Item | Detail |
|------|--------|
| Board | ESP32-2432S028R (CYD) — ESP32-WROOM-32, no PSRAM |
| Display | 2.8″ ILI9341 240×320 |
| Touch | XPT2046 resistive |
| RGB LED | Onboard (GPIO 4 / 16 / 17, active-low) |
| Buzzer | Optional passive piezo on **GPIO 22** (CN1 / P3) |
| microSD | Onboard slot (optional CSV log) |

### Buzzer wiring

```
Piezo +  →  GPIO 22  (CN1 pin)
Piezo −  →  GND
```

For a louder active buzzer, use a transistor; `tone()` drives a passive piezo.

---

## Build & flash (PlatformIO)

```bash
cd GoFlockYourself
pio run -e cyd -t upload
pio device monitor -b 115200
```

Requirements: [PlatformIO](https://platformio.org/) Core or VS Code extension. The build is pinned to **Arduino-ESP32 2.0.17** (`espressif32 @ 6.10.0`). Arduino 3.x / pioarduino is not supported (`SPIFFS.h` missing, NimBLE-Arduino 1.4.3 will not compile). If `pio run -e cyd` pulls a `2024.*` platform, you have pioarduino installed — use the pin in `platformio.ini`, then `pio run -e cyd -t clean && pio run -e cyd`.

Libraries (pulled automatically):

- `bodmer/TFT_eSPI`
- `paulstoffregen/XPT2046_Touchscreen`
- `h2zero/NimBLE-Arduino` (BLE optional)

To shrink the binary and free RAM, disable BLE at build time:

```ini
; in platformio.ini build_flags:
-DGFY_ENABLE_BLE=0
```

---

## How it works

### Detection (highest confidence first)

1. **Wildcard probe + OUI** — management Probe Request, SSID IE length 0, transmitter matches Flock OUI list
2. **LiteOn vendor IE** — tag 221 / OUI `50:6F:9A` on a probe from a known OUI
3. **Probe + OUI** — any probe request from a known OUI (STA hunting uplink)
4. **SSID keywords** — `flock`, `flck`, `raven`, etc.
5. **OUI RX (addr1)** — frames addressed *to* a Flock MAC (AP talking to camera)
6. **Broad OUI TX** — any frame from known OUI (off by default; noisier)
7. **BLE** — XUNTONG mfg `0x09C8`, OUI, names, Raven service UUIDs (off by default)

~41 OUI prefixes are compiled in (`include/oui_list.h`), combining high-confidence research prefixes with lower-confidence related hardware.

### Channel hopping

| Plan | Channels | Use |
|------|----------|-----|
| **Primary** (default) | 1, 6, 11 | Fast cover of busy 2.4 GHz |
| **Full** | 1–13 | Maximum catch |
| **Asc** | 1–11 ascending | Matches observed camera hop direction |

Default dwell: **280 ms** per channel (tunable in `config.h` / UI).

### Alerts

- Full-screen red flash with method, MAC, OUI, RSSI, channel, confidence
- Piezo multi-tone pattern
- RGB LED red flash
- Per-MAC **4 s cooldown** to avoid spam
- Serial line + optional microSD CSV append

---

## On-screen UI

| Screen | Content |
|--------|---------|
| **Home** | LIVE/IDLE, channel, methods, hit counters, last detection, uptime, free heap |
| **Menu** | Start/Stop, Methods, Alerts, History, About |
| **Methods** | Toggle OUI TX/RX, wildcard, broad OUI, SSID keys, BLE |
| **Alerts** | Audio / LED / SD, channel plan |
| **History** | Last 40 detections (paged) |
| **About** | Version + research credits |

Tap the title bar or bottom **MENU >** on the home screen to open the menu.

---

## Serial output

```
[HIT] WILD PROBE AA:BB:CC:DD:EE:FF ch=6 rssi=-62 conf=100 WiFi <wildcard>
[hb] ch=6 any=12040 mgmt=8900 probe=210 wild=12 oui=3 hits=1/1 heap=145000 scan=1
```

If `any` stays 0 for 15 s, RF is not receiving (antenna / init). Non-zero frames with zero hits means the radio is healthy and no Flock signature has been seen yet.

---

## microSD log

Path: `/gfy_log.csv`

A **FAT32** card is probed shortly after boot (deferred so touch stays alive). If the volume mounts, logging turns **on** automatically. The Alerts **SD logging** toggle is saved across reboots; you do **not** need to stop scanning to enable it.

| Home `SD:` | Meaning |
|---|---|
| `ready` | FAT32 card, logging on |
| `off` | Card may be fine; logging toggled off (saved) |
| `none` | No card |
| `format` | Card present but not FAT32 (exFAT/NTFS/unformatted) |
| `error` | Mounted but a write failed |
| `idle` | Still checking |

```
millis,uptime_s,mac,oui,method,protocol,rssi,channel,confidence,label
```

---

## Project layout

```
GoFlockYourself/
  platformio.ini          # CYD + TFT_eSPI pins
  partitions.csv
  README.md
  LICENSE                 # MIT
  docs/                   # technical PDF + page previews
  include/
    config.h              # pins, defaults, timing
    oui_list.h            # Flock OUI table
    types.h               # events, settings, stats
    detection.h  radio.h  ui.h  logger.h  hardware.h
  src/
    main.cpp              # setup / loop
    detection.cpp         # queue, dedupe, history
    radio.cpp             # promiscuous sniffer + hop + BLE
    ui.cpp                # TFT + touch UI
    logger.cpp            # Serial + SD
    hardware.cpp          # RGB + buzzer
```

---

## Display invert (CYD panel lots)

Some ESP32-2432S028R batches invert colors, so the dark UI looks washed-out white. The controller cannot detect this (invert happens after the pixel buffer).

On **first boot**, tap whichever half of the split screen looks **dark**. That choice is stored on the board. Change it later from **Main menu → Invert display** (also under Alerts).

---

## Touch calibration

If taps feel offset, edit in `include/config.h`:

```c
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3700
#define TOUCH_Y_MIN  240
#define TOUCH_Y_MAX  3800
```

Some CYD batches need X/Y swap — adjust `read_touch()` in `ui.cpp` if inverted.

---

## Memory notes

- No PSRAM required
- Static history (40), static OUI table, FreeRTOS queue for hits
- Sniffer callback only parses + enqueues (no Serial/TFT/malloc)
- Heap printed on home screen and `[hb]` lines

---

## Research credits

- **@NitekryDPaul** — OUI list + addr1 receiver technique
- **Michael / DeFlockJoplin** — wildcard probe signature, field validation
- **colonelpanichacks / flock-you** — promiscuous pipeline reference
- **GainSec** — Raven BLE UUID research
- IEEE MA-L **B4:1E:52** registered to Flock Safety

---

## License

MIT — see [LICENSE](LICENSE).

---

## Disclaimer

Passive reception of publicly broadcast 802.11 / BLE advertisements for security research, privacy auditing, and education. Comply with local law. This firmware does not jam, deauth, or connect to camera infrastructure.
