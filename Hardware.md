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

## SNES Controller Pin Mapping

`src/input/snes_controller.c` reads a standard SNES controller via a PIO state machine (`src/input/snes_controller.pio`):

| Signal | GPIO | 40-pin header position | Description |
| :--- | :--- | :--- | :--- |
| **LATCH** | **GPIO 2** | Physical pin 4 | Latch pulse output (12 µs pulse) |
| **CLOCK** | **GPIO 3** | Physical pin 5 | Shift clock output (500 kHz) |
| **DATA** | **GPIO 4** | Physical pin 6 | Serial data input (internal pull-up enabled) |
| **VCC** | **3.3V / 5V** | Physical pin 36 or 39 | Controller power supply |
| **GND** | **GND** | Physical pin 3, 8, 13, 18, 23, 28, 33, or 38 | Common ground |

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
  * Executes Intel 8080 CPU emulation & SNES controller input decoding.
  * Renders 8bpp scanlines directly into `fb` (`game_render_scanline()`).
  * Converts the whole 8bpp frame to pre-packed RGB565 once per frame
    (`dvi_display_convert_frame()`) - deliberately kept off Core 1's ISR,
    which used to do this lookup per pixel and blew HDMI mode's per-line
    timing budget (Data Island construction shares that budget) - see
    Video.md's pipeline overview.
  * Generates 48 kHz PCM audio samples and pushes HDMI Data Islands to
    `pico_hdmi` (`audio_i2s_feed_queue()`), called repeatedly through the
    frame rather than as a single once-per-frame burst.
  * Microsecond wall-clock anchored loop (`sleep_until()`).
* **HSTX Engine & DMA (`pico_hdmi`)**:
  * `dvi_scanline_ptr_cb` (a scanline *pointer* callback) returns the
    address of the frame Core 0 already converted - no per-pixel work on
    this time-critical path.
  * Hardware TMDS encoding via RP2350 HSTX peripheral.
  * DMA streams scanline buffers and HDMI Data Island packets (Audio samples, InfoFrames, ACR) during H-blanking/sync.

### Hardware Peripherals Used
* **HSTX**: Drives DVI TMDS differential clock and data signals over GPIO 12-19 using hardware encoders.
* **PIO**: Runs the SNES controller driver state machine (`src/input/snes_controller.pio`).
* **DMA**: Transfers scanline buffers to HSTX FIFO and injects Data Island packets.
* **USB CDC + UART**: Stdio telemetry is mirrored to both USB serial and UART0 (GPIO0 TX / GPIO1 RX, `115200` baud).

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
