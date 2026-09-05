# GoFlockYourself Technical Documentation

<!-- page 1 -->
Receive-only  ·  Open hardware research  ·  Privacy awareness tool
GoFlockYourself
Passive Flock Safety Camera Detector
for the Cheap Yellow Display (ESP32-2432S028R)
Technical Documentation
Version 1.1.0
5 September 2026
By @OffGridPete
A production-oriented firmware and field guide for passively detecting Flock Safety and related automatic license
plate reader (ALPR) infrastructure using WiFi promiscuous mode, OUI fingerprinting, wildcard probe requests,
and optional Bluetooth Low Energy scanning — all on a low-cost ESP32 touchscreen board.


---

<!-- page 2 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
2
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Table of Contents
1. Background & Research
2. Limitations
3. Hardware Requirements
4. Getting Started / How to Use
5. User Interface & Navigation
6. Detection Methods
7. Alerts & Sensitivity
8. Logging & microSD Card
9. Best Practices & Tips
10. Credits & References
Appendix A — Confidence Scoring
Appendix B — Serial Diagnostics
Appendix C — File Map


---

<!-- page 3 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
3
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
1. Background & Research
1.1 What Are Flock Safety Cameras?
Flock Safety manufactures networked automatic license plate reader (ALPR) systems deployed by municipalities, law
enforcement agencies, homeowners associations, and private security clients primarily across the United States (and
to a lesser extent Canada). Units are commonly mounted on utility poles, streetlights, or dedicated stands near
roadways. Product families observed in the field include Falcon and Sparrow style pole cameras, as well as related
surveillance hardware from adjacent vendors in the broader ALPR ecosystem.
These systems capture vehicle images and plate data, often correlated with time, location, and vehicle attributes, and
upload that data to vendor cloud services. Because deployments can be dense, opaque, and operated with limited
public notice, journalists, civil liberties advocates, municipal watchdogs, and privacy-conscious residents have
developed independent methods to map where such infrastructure exists.
1.2 Why Detect Them?
Independent detection does not jam, disable, or interfere with cameras. It enables:
 Transparency — Cross-check public records and vendor maps against RF signatures observed on the ground.
 Privacy research — Document the density and placement of always-on ALPR infrastructure in a community.
 Field auditing — Verify whether a reported camera location is active and radiating 2.4 GHz management traffic.
 Education — Teach how commodity WiFi/BLE receivers can reveal device classes without network association.
Legal and ethical framing
GoFlockYourself is a receive-only tool. It does not transmit deauthentication frames, does not associate with
camera networks, and does not attempt to access camera data. Users remain responsible for complying with local
law regarding radio reception and surveillance mapping.
1.3 How Passive Detection Became Possible
ALPR cameras that use 2.4 GHz WiFi for configuration, management, or uplink discovery emit standard IEEE 802.11
frames. Even when they do not host a visible access point, they may act as stations (STAs) searching for a hidden
management SSID. Those emissions are public over the air and can be observed with a WiFi radio in promiscuous
(monitor) mode — the primary technique used by GoFlockYourself.
Promiscuous WiFi mode
In promiscuous mode, the ESP32 radio delivers management and data frames from the air to firmware without joining
a network. The detector hops across 2.4 GHz channels (typically 1, 6, and 11, or the full 1–13 set) and inspects each
frame’s addresses and information elements (IEs).
OUI fingerprinting
Every MAC address begins with a 24-bit Organizationally Unique Identifier (OUI). Researchers compiled prefixes
repeatedly associated with Flock Safety hardware, including modules from suppliers such as LiteOn and related
silicon vendors. GoFlockYourself ships a compiled table of approximately 41 OUI prefixes, including the IEEE MA-L
block B4:1E:52 registered to Flock Safety (9 May 2024).
addr2 (transmitter) and addr1 (receiver) matching


---

<!-- page 4 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
4
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
In 802.11 headers, addr2 is typically the transmitter address and addr1 the receiver. Matching a known OUI on addr2
catches cameras that are actively transmitting. Matching on addr1 catches nearby access points or peers that are
talking to a Flock MAC — a technique highlighted in NitekryDPaul’s research even when the camera itself is quiet on
that hop.
Wildcard probe requests
A probe request with an empty SSID Information Element (tag 0, length 0) is a wildcard probe: the station is asking
any AP to respond. Field research (notably DeFlockJoplin / Michael) observed Flock cameras emitting wildcard
probes while hunting uplink networks, often on a rapid channel sequence. Combined with a known OUI on the
transmitter, this is currently among the highest-confidence passive signatures available.
1.4 Evolution of Camera RF Behavior
Camera firmware and backhaul strategy have changed over time. Operators and researchers should treat historical
detection recipes as provisional:
 Earlier generations often exposed more continuous WiFi AP or management beacons; some community tooling
targeted those SSID patterns directly.
 ~Late 2025 reports describe reduced reliance on always-on WiFi management APs, with devices more often in
STA mode probing for hidden uplinks.
 Cellular (LTE) backhaul is increasingly common. Units that primarily use LTE may emit little or no 2.4 GHz WiFi
for long periods.
 BLE advertising that once helped detect Raven/Flock-adjacent gear has been reported as less reliable after
vendor updates.
 Locally administered MACs (U/L bit set) appear on some units; the firmware still matches exact OUI triples from
research lists (for example 82:6B:F2) rather than blanket-rejecting all local-admin addresses.
GoFlockYourself therefore prioritizes WiFi promiscuous OUI + wildcard probe detection, treats BLE as a
secondary interleaved method, and documents honesty about miss rates for cellular-first deployments.
1.5 Research Credits (Summary)
This project stands on community research rather than inventing detection from scratch. Primary credits include
@NitekryDPaul (OUI corpus and addr1 technique), Michael / DeFlockJoplin (wildcard probe signature and field
validation), colonelpanichacks / flock-you (promiscuous pipeline reference), GainSec (Raven BLE UUID research),
and the broader DeFlock mapping community. See Section 10 for the full acknowledgments list.


---

<!-- page 5 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
5
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
2. Limitations
Read this section carefully
GoFlockYourself is a useful field aid, not a guaranteed detector. Absence of an alert does not prove absence of a
camera. Presence of an alert is a probabilistic signal that must be interpreted with context (RSSI, method, location,
repeatability).
2.1 Not 100% Reliable
No passive RF tool can claim perfect recall against a product line that can silence or relocate its radio stack via OTA
updates. Detection depends on the camera emitting something observable on 2.4 GHz WiFi or BLE during the brief
windows the receiver is listening on the correct channel.
2.2 Cellular-First and Quiet Radios
Newer installations frequently prefer LTE / cellular backhaul. Those units may:
 Emit little or no WiFi for hours or days while operating normally.
 Only brief WiFi activity during install, recovery, or rare reconfiguration.
 Produce zero BLE advertisements of interest.
In those cases, public records, visual survey, and crowdsourced maps remain essential complements to RF
detection.
2.3 Shared Manufacturer OUIs and False Positives
Many OUIs on the research list are allocated to component vendors (for example WiFi module manufacturers) rather
than exclusively to Flock Safety. Devices unrelated to ALPR can share those prefixes. Wildcard-probe + OUI
combinations raise confidence, but broad OUI matching alone is noisier and is off by default for that reason.
2.4 Duty Cycle and Sleep
Cameras spend much of their life not probing. A drive-by on a single channel plan may simply miss a quiet interval.
Longer dwell, full channel plans, repeat passes, and stationary observation improve odds but never eliminate misses.
2.5 Range and Environment
Practical 2.4 GHz range for management frames depends on antenna, orientation, vehicle cabin attenuation,
multipath, and interference. Expect tens of meters in favorable line-of-sight conditions and substantially less inside
dense urban canyons or from a sealed metal vehicle with a poorly placed board.
2.6 Hardware Constraints
The ESP32-2432S028R has no PSRAM on most units, limited free GPIO, and a single 2.4 GHz radio shared
between WiFi and BLE. BLE scanning therefore time-shares the radio (WiFi fully stops during short BLE windows).
The interface remains usable, but simultaneous full-duty WiFi+BLE is not possible.
Limitation
Impact
Mitigation
LTE-only backhaul
No RF signature
Do not rely on RF alone; use maps/visuals
Shared OUIs
False positives
Prefer wildcard+OUI; disable Broad OUI


---

<!-- page 6 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
6
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Limitation
Impact
Mitigation
Sleep / quiet periods
Missed cameras
Repeat routes; full channel plan
Short range
Miss at speed
Slower pass; careful antenna orientation
BLE vs WiFi radio share
Coverage gaps
Leave BLE off unless needed


---

<!-- page 7 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
7
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
3. Hardware Requirements
3.1 Primary Board
GoFlockYourself targets the ESP32-2432S028R, commonly known as the Cheap Yellow Display (CYD):
Component
Specification
MCU
Espressif ESP32-WROOM-32 (dual-core, 240 MHz, WiFi + BT)
Display
2.8″ ILI9341 TFT, 240 × 320 portrait (some lots invert; see 4.3 / 5.4)
Touch
XPT2046 resistive touchscreen
Storage slot
Onboard microSD (SPI)
Status LED
Onboard RGB LED (active-low; GPIO 4 / 16 / 17)
Memory
No PSRAM on most units — firmware is static-allocation oriented
Flash
Typically 4 MB; firmware uses a custom partition table
3.2 Recommended Additions
Item
Purpose
Notes
Passive piezo buzzer
Audible hit alert
Wire + to GPIO 22 (CN1/P3), − to GND
microSD card
CSV detection log
Must be FAT32; see Section 8
USB power bank
Field power
Stable 5 V; avoid weak cables
Non-conductive mount
Dash / handlebar
Keep display readable; free USB port
3.3 Power
Power the CYD via its USB port from a computer for flashing, or from a quality USB power bank in the field.
Brownouts can reset the ESP32 mid-scan; if the device reboots under load, try a shorter cable and a higher-capacity
pack.
3.4 Optional Buzzer Wiring
Piezo + → GPIO 22 (CN1 header)
Piezo − → GND
For a louder active buzzer, drive a small NPN/MOSFET from GPIO 22 rather than powering the buzzer
directly from the pin.
microSD requirement
SD card logging works only when a microSD card formatted as FAT32 is inserted in the onboard slot. Without a
card, the firmware still operates fully; detections appear on-screen, in history, and on the USB serial console.


---

<!-- page 8 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
8
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
4. Getting Started / How to Use
4.1 Prerequisites
 A computer with USB and the project source tree (GoFlockYourself/).
 PlatformIO Core or the PlatformIO IDE extension for VS Code.
 A data-capable USB cable (charge-only cables will power the board but not flash it).
4.2 Flashing the Firmware
From a terminal:
cd GoFlockYourself
pio run -e cyd -t upload
pio device monitor -b 115200
On macOS, the serial device is typically of the form /dev/cu.usbserial-*. If upload fails with “Failed to connect,”
hold the board’s BOOT button, tap RST, release BOOT, and retry. Close any other serial monitor that may hold the
port.
4.3 First Boot
After a successful flash the board should:
 On a new board, show TAP THE DARK SIDE (split black/white). Tap whichever half looks dark. CYD
panel lots disagree on invert; the controller cannot detect this. The choice is saved in NVS for that board.
 Show a brief GoFlockYourself splash screen (v1.1.0).
 Enter the dark-themed Home / Status screen with LIVE scanning enabled.
 Pulse the RGB LED in a soft blue scanning pattern.
 Print a boot banner and configuration summary on USB serial at 115200 baud.
 Auto-detect a FAT32 microSD card if present and enable CSV logging (unless you previously saved logging off).
If the whole UI looks washed-out white, open Main menu → Invert display (or Alerts) and toggle it. If the display
remains blank, press RST once. Confirm serial output contains lines such as [wifi] SCAN start and a periodic
[hb] heartbeat with a non-zero frame counter when near any 2.4 GHz WiFi activity.
4.4 Basic Operation
 Leave the device scanning while walking or driving through an area of interest.
 Watch the Home screen for channel hops, hit counters, and last detection details.
 On a hit: full-screen alert, optional buzzer pattern, RGB flash, history entry, serial line, and SD append (if enabled).
 Use the touch menu to stop/start scanning, toggle methods, adjust alerts, and review history.
4.5 Power Recommendations
Use case
Recommendation
Bench / flash
Computer USB port; keep serial monitor available
Walking survey
10,000 mAh+ power bank; secure the board; avoid pocket RF shielding
Vehicle
Dedicated USB outlet or power bank; mount with display visible to a passenger
Long soak
Mains USB adapter; ensure ventilation; SD logging for unattended capture


---

<!-- page 9 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
9
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
5. User Interface & Navigation
5.1 Design Overview
The UI is a custom dark theme on the 240×320 ILI9341 panel using TFT_eSPI, with resistive touch via XPT2046. The
design prioritizes field readability: high contrast, large hit counters, and a prominent detection alert. Touch targets are
full-width rows where possible to compensate for resistive imprecision. Some CYD panel lots invert black/white; first
boot calibration and the Invert display toggle keep the theme dark on every board.
5.2 Home / Status Screen
The Home screen is the primary operational view. It shows:
Element
Meaning
Title + version
Project identity (GoFlockYourself / v1.1.0)
LIVE / IDLE
Whether WiFi promiscuous scanning is active
Channel
Current 2.4 GHz hop channel
Mode
Channel plan: Primary 1/6/11, Full 1–13, or Asc 1–11
Methods
Compact flags for enabled methods (OUI, WILD, BLE, …)
Frames
Total frames seen (proves the radio is alive)
SD:status
microSD state: ready / off / none / format / error / idle
HITS / UNIQUE
Total alerted events vs unique MACs in history
Last detection
Method, OUI, RSSI, channel, time ago
Uptime / Heap
Runtime and free heap for health monitoring
MENU >
Touch affordance to open the main menu
Tap the title bar or the lower MENU > region to open the main menu. Status fields refresh periodically while
remaining on Home so channel and counters stay live without full-screen redraws.
5.3 Touch Navigation
 Use firm, deliberate presses; resistive panels need pressure, not a hover.
 Most sub-screens provide a < BACK control in the header (top-right).
 If taps feel offset, calibrate raw min/max constants in include/config.h (TOUCH_X/Y_MIN/MAX).
 A short debounce (~220 ms) reduces double-triggers.
5.4 Main Menu
Menu item
Function
Start / Stop Scanning
Toggle promiscuous scan; LED returns to idle green when stopped
Detection Methods
Enable/disable individual RF detection paths
Alerts & Sensitivity
Audio, LED, SD logging, invert, channel plan
View Log / History
Paged on-device history of recent detections
About / Credits
Version, author, research credits, OUI count
Invert display
ON/OFF — corrects white-on-light panels; saved per board


---

<!-- page 10 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
10
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
5.5 Detection Methods Screen
Each row is a toggle (ON/OFF). Changes take effect immediately for subsequent frames. Rows include: WiFi OUI
(TX), WiFi OUI (RX), Wildcard Probe, Broad OUI, SSID Keywords, and BLE Scanning (when compiled in). Channel
plan summary is shown at the bottom for context.
5.6 Alerts & Sensitivity Screen
Toggles for Audio (buzzer), RGB LED alert, SD logging, and Invert display. Live card status reports FAT32 ready,
not inserted, not FAT32, or write error. Logging may be enabled while scanning. Channel plan buttons select Primary
(1/6/11), Full (1–13), or Ascending (1–11). See Section 7 for recommended profiles.
5.7 History Screen
Shows up to 40 recent detections in RAM (most recent first), six per page. Each row lists method, full MAC, RSSI,
and relative time. Prev appears only when a previous page exists; Next only when another page exists.
5.8 About Screen
Displays project name, version, author line By @OffGridPete, hardware target, a short capability summary, research
credits, and the number of compiled OUI prefixes.
5.9 Detection Alert Screen
On a meaningful (de-duplicated) hit, a full-screen alert flashes with method name, MAC, OUI, RSSI, channel, and
confidence percentage. Tap to dismiss early, or wait for the automatic timeout (~5.5 s). Audio and LED accompany
the visual alert when enabled.


---

<!-- page 11 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
11
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
6. Detection Methods
Methods are evaluated inside a lean WiFi sniffer callback that only parses and enqueues work; heavy UI, SD, and
serial I/O run on the main loop. Confidence scores rank methods from 0–100 for display and triage (see Appendix A).
What works best in the field today
Highest practical confidence: Wildcard Probe + known OUI (TX), optionally strengthened by LiteOn-style vendor IE
fingerprints. Next: Probe Request + OUI and OUI TX on management activity. OUI RX (addr1) is valuable when
nearby APs talk to a camera. BLE is secondary and off by default on the CYD. Disable Broad OUI unless you accept
higher false-positive rates.
6.1 WiFi OUI Matching — Transmitter (addr2)
How it works
For management and (when broad mode is on) data frames, the firmware extracts addr2 (transmitter / source). The
first three octets are compared against the compiled Flock-related OUI table. Multicast and all-zero / all-ones
addresses are ignored. Hits are scored and enqueued with method OUI TX or, if the frame is a probe request,
higher-tier probe methods.
Effectiveness / confidence
Medium–high when the camera is actually transmitting. Confidence is lower than wildcard-probe combinations
because many module vendors share OUIs. Baseline score for pure OUI TX is moderate (~60), elevated when
coupled with probe subtypes.
When it works best
 Camera is in STA mode and actively probing or exchanging management frames.
 You pass within solid 2.4 GHz range during an active radio window.
 OUI list includes the module family used by that deployment.
Limitations
 False positives from non-ALPR devices using the same module OUI.
 Silent LTE-first units may never transmit on WiFi during your pass.
 Requires correct channel hop timing to be on-channel during emission.
Best practices
 Leave OUI TX enabled for general surveys.
 Triage hits by RSSI and repeatability before treating them as confirmed cameras.
 Combine with Wildcard Probe for higher confidence.
6.2 WiFi OUI Matching — Receiver (addr1)
How it works
Some frames are addressed to a camera even when the camera is not the transmitter. The firmware matches known
OUIs on addr1 (receiver / destination) for management frames, skipping multicast destinations. This is the
NitekryDPaul addr1 technique: nearby infrastructure talking to a Flock MAC reveals presence indirectly.


---

<!-- page 12 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
12
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Effectiveness / confidence
Medium. Extremely useful in dense WiFi environments near a managed uplink, but more context-dependent than
transmitter probes. Score is lower than probe-based methods (~50) to reflect indirect evidence.
When it works best
 Camera has a WiFi relationship with a nearby AP that is actively sending frames.
 You are close enough to hear the AP’s transmissions to that MAC.
Limitations
 Does not fire if nothing addresses the camera on-air during your listen window.
 Can confuse triage if multiple devices share related prefixes.
Best practices
 Keep enabled for thorough surveys; correlate with TX/probe hits when possible.
 Use history MAC patterns and geography to discard one-off weak RX hits.
6.3 Wildcard Probe Request Detection
How it works
The sniffer identifies IEEE 802.11 Probe Request management frames (type 0, subtype 4), walks Information
Elements after the MAC header, and checks for SSID IE (tag 0) with length 0. When the transmitter OUI is also on
the research list, the event is classified as WILD PROBE — the top-tier signature. Related probe paths also detect
directed probes from known OUIs and optional LiteOn vendor IE fingerprints (tag 221 / OUI 50:6F:9A).
Effectiveness / confidence
Highest among stock methods (confidence ~100 for wildcard + OUI; high 80s for general probe + OUI). Field reports
from DeFlockJoplin-style testing describe strong capture rates when cameras are in uplink-hunt behavior, with far
fewer false positives than OUI-only matching.
When it works best
 Camera is actively enumerating networks (STA uplink discovery).
 Channel plan covers the hop sequence (Primary or Full/Asc as needed).
 Dwell time is short enough to intersect rapid probe bursts (~280 ms default).
Limitations
 No emission → no detection, regardless of method quality.
 Non-Flock devices can also send wildcard probes (OUI gate is essential).
 Requires being on the right channel during the burst.
Best practices
 Leave Wildcard Probe ON for virtually all field work.
 Prefer Primary 1/6/11 for speed; switch to Full if you suspect off-channel activity.
 Treat WILD PROBE hits as high priority for mapping and verification.
6.4 BLE Scanning (Secondary)


---

<!-- page 13 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
13
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
How it works
When enabled, the firmware periodically pauses WiFi completely, brings up NimBLE, runs a short blocking scan (~2
s), parses advertisements for manufacturer ID 0x09C8 (XUNTONG), Flock-related OUIs, name keywords, and
Raven-related service UUID bases, then tears down BLE and restarts WiFi promiscuous mode. WiFi-only slices of
~12 s separate BLE windows so the primary detector remains dominant.
Effectiveness / confidence
Variable and often lower than WiFi probe methods on current deployments. Useful as a secondary net for devices
that still advertise distinctive BLE data, but community reports indicate BLE has become less reliable after vendor
changes.
When it works best
 Target hardware still emits XUNTONG manufacturer data or Raven UUID patterns.
 Stationary or slow surveys where a 2-second WiFi gap is acceptable.
Limitations
 Classic ESP32 cannot sniff WiFi and BLE at the same instant.
 UI may freeze briefly during BLE slices.
 Init cost and memory pressure on no-PSRAM CYD hardware.
Best practices
 Leave BLE OFF for high-speed driving surveys focused on WiFi probes.
 Enable BLE for walking / soak tests when hunting older or Raven-class signatures.
 If stability problems appear, disable BLE; WiFi remains the primary detector.
6.5 Additional Supporting Paths
Path
Default
Role
SSID keywords
ON
Matches fragments such as flock, flck, raven in probe/beacon SSIDs
Broad OUI
OFF
Any frame from known OUI — noisy; experts only
LiteOn vendor IE
automatic
Raises confidence when probe carries IE OUI 50:6F:9A
6.6 Channel Hopping Plans
Plan
Channels
Dwell (default)
Best for
Primary
1, 6, 11
280 ms
Driving; fast cover of busy spectrum
Full
1–13
280 ms
Maximum catch; slower revisit rate
Ascending
1–11
280 ms
Align with observed ascending probe hops


---

<!-- page 14 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
14
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
7. Alerts & Sensitivity
7.1 Settings Overview
Setting
What it does
Audio (buzzer)
Plays a multi-tone pattern on de-duplicated hits (GPIO 22 piezo)
RGB LED alert
Flashes red on hit; soft blue pulse while scanning; green when idle
SD logging
Appends CSV rows when a FAT32 card is mounted (see Section 8)
Invert display
Flips panel invert so the dark UI stays dark on both CYD lots (saved in NVS)
Channel plan
Primary / Full / Asc — changes hop set immediately
7.2 De-duplication / Cooldown
Raw RF can deliver dozens of identical frames per second from one device. The firmware keeps a small per-MAC
cooldown table (default 4 seconds). Repeat frames from the same MAC within the window still update statistics as
appropriate, but they do not re-trigger full-screen alert, buzzer, or LED spam. This keeps the device usable in a
vehicle without constant alarms when parked near a talkative unit.
7.3 RSSI Floor
Frames weaker than the configured floor (default −95 dBm) are ignored in the sniffer path. Raising the floor (for
example to −80 dBm) reduces distant noise at the cost of range; lowering it increases sensitivity and multipath clutter.
Adjust in include/config.h (RSSI_FLOOR) for advanced tuning.
7.4 Recommended Profiles
Scenario
Methods
Channel plan
BLE
Audio
Notes
Driving (default)
OUI TX/RX + Wildcard +
SSID
Primary 1/6/11
Off
On
Maximize WiFi time; short
dwell
Walking survey
Same as driving
Primary or Full
Optional
On
Slower speed → better
catch
Stationary soak
All WiFi methods
Full or Asc
Optional
Optional
Log to SD; longer
observation
Low false positives
Wildcard + OUI TX; RX
optional
Primary
Off
On
Broad OUI off; triage by
conf.
Aggressive
research
All + Broad OUI
Full
On
On
Expect noise; verify
manually
7.5 Reducing False Positives
 Keep Broad OUI disabled unless you are actively researching.
 Prefer hits with method WILD PROBE / high confidence scores.
 Require geographic sense: a single weak OUI RX hit mid-highway may be multipath or coincidence.
 Use SD logs later to cluster by MAC and time rather than reacting to every beep.
 Update OUIs carefully — adding overly broad prefixes increases noise.


---

<!-- page 15 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
15
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
8. Logging & microSD Card
8.1 Enabling Logging
Shortly after boot (after the UI is up, so the shared SPI bus does not freeze touch) the firmware probes the onboard
microSD slot. If a readable FAT32 volume is found and the user has not previously turned logging off, logging turns
ON automatically and the Home screen shows SD:ready. The Alerts SD logging toggle is persisted in NVS across
reboots. Turning it on re-probes the slot (hot-plug friendly) and does not require stopping the scan.
Format requirement
The microSD card must be formatted as FAT32. exFAT/NTFS are not supported by the Arduino SD stack used
here. Cards that are empty or newly purchased should be formatted FAT32 on a computer before first use. A card
that answers SPI but has no FAT volume shows SD:format in red on Home and a “not FAT32” line on Alerts.
8.2 Log Location and Format
File path on the card: /gfy_log.csv (appears as gfy_log.csv in the card root on a desktop OS).
Header row:
millis,uptime_s,mac,oui,method,protocol,rssi,channel,confidence,label
Column
Description
millis
Device uptime timestamp in milliseconds at write
uptime_s
Same time in whole seconds (handy for spreadsheets)
mac
Full six-octet MAC (colon-separated hex)
oui
First three octets only
method
Human method name (e.g. WILD PROBE, OUI RX)
protocol
wifi or ble
rssi
Signal strength in dBm
channel
2.4 GHz channel of the capture
confidence
0–100 score from method ranking
label
SSID snippet, <wildcard>, BLE name, etc. (commas stripped)
8.3 Retrieving Logs
 Stop scanning if desired, power down, and remove the microSD card.
 Open gfy_log.csv in any spreadsheet or text editor.
 Sort by confidence, filter method == WILD PROBE, or pivot by OUI for analysis.
 Combine with GPS tracks from a separate phone app using approximate timestamps (the device log is relative to
boot millis, not wall clock unless you note start time).
8.4 Tips for Reliable Logging
 Use reputable name-brand cards; tiny/no-name cards fail more often on SPI buses.
 Eject safely from the OS after copying; avoid pulling the card during a write.
 If Home shows SD:none, reseat the card and toggle SD logging or reboot.
 If Home shows SD:format, reformat the card FAT32 (not exFAT) on a computer and reinsert.


---

<!-- page 16 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
16
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
 Serial will print [log] SD wrote hit #N on success.
 Touch and SD share SPI hardware resources on the CYD; the firmware re-claims the bus around each write —
still, avoid very rapid forced probes while touching the UI.


---

<!-- page 17 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
17
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
9. Best Practices & Tips
9.1 Mounting and Antenna Considerations
 The ESP32-WROOM module antenna is PCB/metal-can based — avoid covering it with hands, metal plates, or
dense carbon dash mats.
 Orient the board so the module antenna faces outward (toward glass), not into a metal console.
 Do not enclose the CYD in a fully metal box without an external antenna modification (advanced; not covered by
stock firmware).
 Keep USB power leads short and well-seated to prevent brownout resets.
9.2 Driving vs Walking Strategies
Mode
Strategy
Driving
Primary plan 1/6/11; BLE off; audio on; watch for clusters of high-confidence hits near poles
Driving (dense urban)
Slightly lower speed on first pass; consider Full plan on a second loop
Walking
Full or Asc plan; optional BLE; pause near suspected poles for 30–60 s
Stationary
Full plan + SD logging; note wall-clock start time for later correlation
9.3 Interpreting Results
 WILD PROBE + strong RSSI — Highest priority; treat as likely nearby ALPR-class radio.
 PROBE OUI — Strong candidate; confirm with geography and repeats.
 OUI RX only — Indirect; look for accompanying TX/probe evidence.
 Broad OUI / weak RSSI one-offs — Log for research; do not over-claim.
 Frames rising, hits zero — Radio is healthy; no signature match yet (common and OK).
9.4 Updating the OUI List
OUIs live in include/oui_list.h as a static array of three-byte prefixes with a confidence tier. To update:
 Add new high-confidence prefixes only from documented research or your own verified captures.
 Keep lower-confidence prefixes marked with higher conf tier values so scoring can demote them.
 Rebuild and flash: pio run -e cyd -t upload.
 Record why each OUI was added (source, date) in your research notes — future firmware churn will otherwise
make the list opaque.
9.5 Operational Safety
 Do not operate a touchscreen or react to alerts in a way that distracts from driving.
 Prefer a passenger operator or review SD logs after the route.
 Obey traffic laws; this tool is for observation, not confrontation.


---

<!-- page 18 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
18
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
10. Credits & References
10.1 Project
GoFlockYourself firmware and CYD interface — @OffGridPete and contributors. Version documented: 1.1.0 (5
September 2026).
10.2 Research and Prior Art
Contributor
Contribution
@NitekryDPaul
Core WiFi OUI corpus; addr1 receiver-side detection technique
Michael / DeFlockJoplin
Wildcard probe signature; field validation; OUI 82:6B:F2
colonelpanichacks / flock-you
Promiscuous detection pipeline reference implementation
GainSec
Raven BLE service UUID research
DeFlock community
Crowdsourced ALPR mapping methodologies and public datasets
IEEE registration
MA-L B4:1E:52 allocated to Flock Safety (2024-05-09)
Pintor & Atzori (2022)
Academic work on probe-request IE fingerprinting (context for IE methods)
10.3 Software Stack
 Espressif Arduino-ESP32 core and WiFi promiscuous APIs
 Bodmer TFT_eSPI — ILI9341 display driver
 Paul Stoffregen XPT2046_Touchscreen — resistive touch
 h2zero NimBLE-Arduino — optional BLE scanning
 PlatformIO — build and flash workflow
10.4 Disclaimer
This documentation and firmware are provided for security research, privacy auditing, journalism, and education.
Authors and contributors assume no liability for misuse. The reception of broadcast 802.11/BLE advertisements does
not include authorization to access private networks, tamper with infrastructure, or violate local regulations. Always
verify legal context in your jurisdiction.
 End of main documentation — appendices follow.


---

<!-- page 19 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
19
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Appendix A — Confidence Scoring
Each detection method maps to a base confidence score used in the UI and CSV log. Lower-confidence OUI table
entries slightly demote the score at enqueue time.
Method
Base score
Notes
WILD PROBE
100
Known OUI + empty SSID probe
LITEON IE
95
Vendor IE 50:6F:9A on probe
PROBE OUI
85
Any probe request from known OUI
SSID KEY
80
Keyword match in SSID
BLE UUID
75
Raven-class service UUID
BLE MFG
70
XUNTONG manufacturer ID 0x09C8
BLE NAME
65
Name keyword match
OUI TX
60
Transmitter OUI (non-probe / broad paths)
BLE OUI
55
BLE address OUI only
OUI RX
50
Receiver addr1 OUI


---

<!-- page 20 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
20
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Appendix B — Serial Diagnostics
USB serial runs at 115200 baud. Useful patterns:
[wifi] SCAN start plan=0 dwell=280ms ch=1
[hb] ch=6 any=12040 mgmt=8900 probe=210 wild=12 oui=3 hits=1/1 heap=145000 scan=1
[HIT] WILD PROBE AA:BB:CC:DD:EE:FF ch=6 rssi=-62 conf=100 WiFi <wildcard>
[log] SD wrote hit #3 → /gfy_log.csv
Observation
Interpretation
any stays 0
Radio not receiving — check init, antenna, environment
any > 0, hits = 0
Healthy RF; no Flock signatures yet
heap falling hard
Investigate BLE on/off; reboot if unstable
SD re-mount failed
Reseat card; confirm FAT32; try another card


---

<!-- page 21 -->
GoFlockYourself  ·  Technical Documentation  ·  v1.1.0
21
Passive Flock Safety Camera Detector
ESP32-2432S028R (CYD)
Appendix C — Source File Map
Path
Role
platformio.ini
CYD environment, TFT_eSPI pins, libraries
partitions.csv
Flash layout (app + SPIFFS region)
include/config.h
Pins, defaults, timing constants
include/oui_list.h
Compiled OUI table and keyword lists
include/types.h
Events, settings, method names/scores
src/main.cpp
setup/loop orchestration
src/radio.cpp
Promiscuous sniffer, hop, BLE handoff
src/detection.cpp
Queue, cooldown, history
src/ui.cpp
Touch UI screens and alerts
src/logger.cpp
Serial + microSD CSV logging
src/hardware.cpp
RGB LED and piezo helpers
README.md
Quick-start companion to this document
Document control
Title: GoFlockYourself Technical Documentation · Version 1.1.0 · Date: 5 September 2026 · Audience: makers, privacy
researchers, and field auditors · Classification: public research aid.
