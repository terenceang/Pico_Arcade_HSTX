# Hardware Documentation - Pico Arcade HSTX

## Board Overview

* **Board Name**: Raspberry Pi Pico 2
* **Microcontroller**: Raspberry Pi RP2350A (Dual ARM Cortex-M33 / Hazard3 RISC-V)
* **Flash Memory**: 4 MB NOR Flash
* **Video Output**: HDMI / DVI-D TMDS differential signal output via RP2350 Hardware HSTX
* **Form Factor**: Standard Raspberry Pi Pico DIP footprint

---

## DVI Pin Mapping & Interface Configuration

The DVI/HDMI video engine utilizes the RP2350's hardware **HSTX peripheral** and **DMA channels** to drive TMDS differential pairs directly over GPIO pins.

> [!IMPORTANT]
> **DVI Sock Pinout Notice (Adafruit vs DIY / Other Versions)**
> - **Default Configuration (Adafruit DVI Sock)**: This firmware is preconfigured for the **Adafruit DVI Sock / Breakout** pinout (detailed in the table below).
> - **DIY / Pimoroni / Other DVI Socks**: Other DVI boards (e.g. original Pico DVI Sock DIY, Pimoroni Pico DV Demo Base, Waveshare) use different lane and polarity assignments across GPIO 12-19.
> - **How to Adjust for Your Board**: If using a different DVI sock or custom wiring, update the `user_pinout` struct in [`src/video/dvi_display.c`](../src/video/dvi_display.c#L129-L136) to match your board's clock and data lane assignments.

### Adafruit DVI Sock HSTX Pinout Table (Default)

| Signal Channel | Positive Pin (+) | Negative Pin (-) | Description |
| :--- | :--- | :--- | :--- |
| **Data 0 (Blue / Sync)** | **GPIO 12** | **GPIO 13** | TMDS Data Lane 0 (GP12 / GP13) |
| **Clock (CLK)** | **GPIO 14** | **GPIO 15** | TMDS Clock Pair (GP14 / GP15) |
| **Data 2 (Red)** | **GPIO 16** | **GPIO 17** | TMDS Data Lane 2 (GP16 / GP17) |
| **Data 1 (Green)** | **GPIO 18** | **GPIO 19** | TMDS Data Lane 1 (GP18 / GP19) |

> [!NOTE]
> Differential pairs occupy 2 consecutive GPIO pins ($N$ and $N+1$). For example, setting `.positive_gpio = 14, .negative_gpio = 15` configures GP14 as positive and GP15 as negative.

---

## Input

The onboard BOOTSEL button is wired up as a coin/select input (`src/platform/input/host_input.c`).
A SNES controller provides directional and action input via a PIO state machine on
**GPIO 26 (Clock) / 27 (Latch) / 28 (Data)** (`src/platform/input/snes_controller.{c,h,pio}`).

Unlike the original SNES driver (removed in an earlier investigation - see `CLAUDE.md`'s "HSTX
sync-loss caveat" - and re-added here), the PIO program is gated rather than free-running: the
state machine sits idle with zero GPIO activity until Core 0 explicitly triggers a read, once per
frame, via a non-blocking pipelined request/collect protocol. This avoids the recurring
async-to-frame-timing PIO burst pattern the original design had, without needing to touch the
video pipeline. Button mapping (D-pad, B=fire, Start, Select=coin, etc.) is documented in
`snes_controller.c`. Soak-tested on real hardware with the pad actively held/pressed
throughout and confirmed not to reintroduce the HDMI sync-loss bug - see `CLAUDE.md`'s "HSTX
sync-loss caveat" (narrowing #10).

A `DEBUG_CONTROLLER_TESTCARD` diagnostic screen (`src/video/controller_testcard.c`) draws a
live SNES pad diagram - D-pad cross on the left, four round face buttons on the right in
their real Super Famicom colors (Y=green, X=blue, A=red, B=yellow) - lighting up on press
with a short keypress beep, for verifying wiring/mapping without a serial console.

---

## Operating Parameters & Clock Setup

| Parameter | Value | Details |
| :--- | :--- | :--- |
| **Target Architecture** | `rp2350-arm-s` | ARM Cortex-M33 |
| **Core Voltage ($V_{REG}$)** | `1.10V` | RP2350 power-on default (`VREG_VOLTAGE_1_10`) |
| **System Clock ($f_{SYS}$)** | `126.000 MHz` | System clock for 25.2 MHz pixel clock via HSTX divider |
| **Video Timing** | 640x480 @ 60Hz | CEA-861 DVI standard timing (25.2 MHz pixel clock) |
| **Framebuffer Resolution** | 320x240 @ 8bpp | 8-bit palette-indexed (75 KB SRAM), scaled 2x to 640x480 |
| **Color Format** | 8bpp Indexed | 256-entry 24-bit RGB palette mapped to RGB565 scanlines |

---

## System Resource Allocation

### Dual-Core & Hardware Allocation
* **Core 0**:
  * Initializes system clock (126 MHz), voltage regulator (1.10V), and stdio (USB CDC only - UART is disabled, see CMakeLists.txt).
  * Executes the active game plugin's CPU emulation (`plugin->run_frame()`) - Space Invaders' 8080 ROM runs on the shared Z80 core, Pac-Man's on its own Z80 emulation.
  * Renders a full 8bpp frame directly into a write buffer from
    `dvi_display_get_write_buffer()` (`plugin->render_frame()`), then calls
    `dvi_display_present_frame()` once the frame is complete.
  * Generates 48 kHz PCM audio samples and pushes HDMI Data Islands to
    `pico_hdmi` (`audio_engine_feed_queue()`), called repeatedly through the
    frame rather than as a single once-per-frame burst.
  * Hardware-VSYNC-genlocked loop (`dvi_display_wait_for_vsync()`), not a wall-clock timer - see `CLAUDE.md`'s "HSTX sync-loss caveat" for why the earlier `sleep_us()`-based design was replaced.
* **HSTX Engine & DMA (`pico_hdmi`)**:
  * `dvi_scanline_fill_cb` (a scanline *fill* callback) does the 8bpp->RGB565
    palette lookup and horizontal doubling itself, per line, reading from
    whichever of `dvi_display.c`'s two 8bpp framebuffers Core 0 isn't
    currently writing (`fb_buffers[2]`, flipped by `dvi_display_present_frame()`)
    - matches `pico_hdmi`'s own reference example (`examples/bouncing_box`),
    rather than an earlier design that handed Core 1 a raw pointer into a
    single, non-double-buffered pre-converted array - see `CLAUDE.md`'s
    "HSTX sync-loss caveat" for why that was a real cross-core race.
  * Hardware TMDS encoding via RP2350 HSTX peripheral.
  * DMA streams scanline buffers and HDMI Data Island packets (Audio samples, InfoFrames, ACR) during H-blanking/sync.

### Hardware Peripherals Used
* **HSTX**: Drives DVI TMDS differential clock and data signals over GPIO 12-19 using hardware encoders.
* **DMA**: Transfers scanline buffers to HSTX FIFO and injects Data Island packets.
* **USB CDC**: Stdio telemetry goes out over USB serial only - UART stdio is explicitly disabled (see `CMakeLists.txt`'s `pico_enable_stdio_uart(... 0)`) to keep a second interrupt-driven driver off the board while the time-critical HSTX DMA IRQ is running.

---

## Serial Terminal Diagnostics Output

Connecting a serial terminal (such as VS Code Serial Monitor or TeraTerm) over the USB CDC port displays hardware status. UART stdio is disabled (see CMakeLists.txt) to keep a second interrupt-driven driver off the board while the time-critical HSTX DMA IRQ is running.

```text
==================================================
  Pico Arcade HSTX  v1.2.0
==================================================
[DEBUG] Microcontroller: RP2350 (Cortex-M33)
[DEBUG] Core Voltage   : 1.10V
[DEBUG] System Clock   : 126000000 Hz (Requested: 126000 kHz)
[DEBUG] --- HSTX HDMI Pinout Configuration ---
[DEBUG] HSTX Pins       : GPIO 12 - 19 (RP2350 Hardware HSTX)
[DEBUG] pico_hdmi HSTX driver initialized successfully.
```
