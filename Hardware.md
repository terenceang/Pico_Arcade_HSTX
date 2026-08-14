# Hardware Documentation - Space Invader PICO

## Board Overview

* **Board Name**: Raspberry Pi Pico 2
* **Microcontroller**: Raspberry Pi RP2350A (Dual ARM Cortex-M33 / Hazard3 RISC-V)
* **Flash Memory**: 4 MB NOR Flash
* **Video Output**: HDMI / DVI-D TMDS differential signal output via RP2350 Hardware HSTX
* **Form Factor**: Standard Raspberry Pi Pico DIP footprint

---

## DVI Pin Mapping & Interface Configuration

The DVI/HDMI video engine utilizes the RP2350's hardware **HSTX peripheral** and **DMA channels** to drive TMDS differential pairs directly over GPIO pins. On the Raspberry Pi Pico 2, standard Pico DVI sock adapters route TMDS signals to **GPIO 12-19**.

### Custom Board DVI / HSTX Pinout Table

| Signal Channel | Positive Pin (+) | Negative Pin (-) | Description |
| :--- | :--- | :--- | :--- |
| **Data 0 (Blue / Sync)** | **GPIO 12** | **GPIO 13** | TMDS Data Lane 0 (GP12 / GP13) |
| **Clock (CLK)** | **GPIO 14** | **GPIO 15** | TMDS Clock Pair (GP14 / GP15) |
| **Data 2 (Red)** | **GPIO 16** | **GPIO 17** | TMDS Data Lane 2 (GP16 / GP17) |
| **Data 1 (Green)** | **GPIO 18** | **GPIO 19** | TMDS Data Lane 1 (GP18 / GP19) |

> [!NOTE]
> Differential pairs occupy 2 consecutive GPIO pins ($N$ and $N+1$). For example, setting `.pins_clk = 12` configures GPIO 12 as positive and GPIO 13 as negative.

---

## Input

No input device is currently wired up. A SNES controller driver used to read a controller
via a PIO state machine on GPIO 2/3/4, but was removed after being implicated in an
unresolved HDMI sync-loss investigation - see `CLAUDE.md`'s "HSTX sync-loss caveat". The
game currently just idles on the attract screen.

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
  * Executes Intel 8080 CPU emulation.
  * Renders 8bpp scanlines directly into a write buffer from
    `dvi_display_get_write_buffer()` (`game_render_scanline()`), then calls
    `dvi_display_present_frame()` once the frame is complete.
  * Generates 48 kHz PCM audio samples and pushes HDMI Data Islands to
    `pico_hdmi` (`audio_i2s_feed_queue()`), called repeatedly through the
    frame rather than as a single once-per-frame burst.
  * Microsecond wall-clock anchored loop (`sleep_until()`).
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
  Space Invader PICO  v1.0.0
==================================================
[DEBUG] Microcontroller: RP2350 (Cortex-M33)
[DEBUG] Core Voltage   : 1.10V
[DEBUG] System Clock   : 126000000 Hz (Requested: 126000 kHz)
[DEBUG] --- HSTX HDMI Pinout Configuration ---
[DEBUG] HSTX Pins       : GPIO 12 - 19 (RP2350 Hardware HSTX)
[DEBUG] pico_hdmi HSTX driver initialized successfully.
```
