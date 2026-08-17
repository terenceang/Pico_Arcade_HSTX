# Display Stability Analysis and System Hardening Guide

## Current baseline (v1.2.0)

- **Wire timing:** 640x480p60 (`DISPLAY_REFRESH_HZ = 60`) at a **25.2 MHz pixel clock** (800 total pixels/line, 525 total lines/frame).
- **System clock:** **151.2 MHz** (`DVI_SYS_CLK_KHZ 151200`), exact 6x integer multiple of 25.2 MHz - see §4 below (adopted from that section's own recommendation; §1-§3's worked numbers still use the original 126 MHz/5x baseline they were computed against, noted where relevant).
- **Execution mode:** **100% SRAM-resident** (`pico_set_binary_type(Pico_Arcade_HSTX copy_to_ram)`), eliminating all Flash QSPI XIP cache misses and bus stalls.
- **Logical framebuffer:** **320x240 8bpp double-buffered**, scaled 2x on both axes via Core 1's scanline fill callback (`dvi_scanline_fill_cb`).
- **Arcade source image:** **256x224 1bpp**, converted to 320x240 8bpp write buffer on Core 0.
- **Audio transport:** **48 kHz stereo LPCM over HDMI Data Islands** (4 samples/packet, 200 packets/frame target queue, exact integer ratio).
- **Frame pacing:** **Hardware VSYNC Genlocked** (`dvi_display_wait_for_vsync` + `__wfe()` / `__sev`), zero software clock drift and zero bus polling.
- **HDMI Header Acceleration:** **Precomposed Active Lines** (`PICO_HDMI_PRECOMPOSED_ACTIVE_LINES=ON`), reducing H-blank ISR execution time from ~5.0 µs to < 1.2 µs (> 75% speedup).
- **Bus isolation:** **Scratch-Y RAM relocation** (`PICO_HDMI_LINE_BUFFER_IN_SCRATCH_Y=ON`), moving Core 1's active-line DMA buffer out of regular SRAM to reduce Core 0/Core 1 bus contention - see §6 below.
- **Input:** **Gated, non-blocking PIO SNES controller driver** (`src/platform/input/snes_controller.{c,h,pio}`, GPIO 26/27/28), soak-tested clean - see §5 below.

---

## Timing Budget & Hardware Constraints

> **Note:** this section and §2/§3 below compute their cycle-count figures against the original
> **126 MHz** analysis baseline (labelled explicitly where it appears, e.g. "(126 MHz)"). The live
> system clock is now **151.2 MHz** as of v1.2.0 (see §4's update note) - wall-clock/µs/ms figures
> and percentages are unaffected (they're clock-independent), but any *cycle count* here understates
> the actual current budget by the 151.2/126 ≈ 1.2x the clock switch bought (e.g. the 800-cycle
> H-blank window below is 960 cycles today - see §4's "What going faster actually buys").

### Frame-Level Budget (640x480p60)

| Timing Parameter | Value | Details |
|---|---|---|
| **Pixel Clock** | **25.200 MHz** | Exact CEA/VESA standard clock |
| **Total Line Time** | **31.75 µs** | 800 pixel clocks (640 active + 160 blanking) |
| **Active Video Time** | **25.40 µs** | 640 pixel clocks |
| **H-Blank Window** | **6.35 µs** | 160 pixel clocks (Front porch, Sync, Back porch, Data Island) |
| **Total Frame Time** | **16.667 ms** | 525 lines (480 active + 45 vertical blanking) |
| **Frame Frequency** | **60.000 Hz** | Exact hardware-locked refresh |
| **CPU Cycles / Frame (126 MHz)** | **2,100,000 cycles** | 16.67 ms frame budget |
| **CPU Cycles / Line (126 MHz)** | **4,000 cycles** | 31.75 µs line budget |
| **H-Blank Cycles (126 MHz)** | **800 cycles** | 6.35 µs time-critical ISR window |

### The Critical Timing Vulnerability: The 6.35 µs H-Blank Window

In RP2350 HDMI mode, each active video line requires two chained DMA transfers:
1. **Header Transfer (6.35 µs):** Transmits sync tokens, preamble, Data Island audio packet (if scheduled), and video guard band.
2. **Active Pixel Data Transfer (25.40 µs):** Transmits 640 pixels (320 words of 16-bit packed RGB565).

The DMA interrupt for the active line fires at the start of the Header transfer. Core 1 has **strictly 6.35 µs (800 CPU cycles @ 126 MHz)** to:
- Take the interrupt and execute ISR prologues.
- Call the scanline fill callback (`dvi_scanline_fill_cb`).
- Fetch the audio packet from the Data Island ring buffer.
- Patch the Data Island into the header and arm the DMA controller for the upcoming Active Pixel Data transfer.

**Failure Mode:** If shared SRAM bus contention, Flash XIP cache stalls, or interrupt latency delays Core 1 by more than 6.35 µs, the HSTX 8-word hardware FIFO empties, the HSTX serializer loses synchronization with DMA, and DMA races unthrottled at maximum bus speed (~137–166 Hz), causing permanent video sync loss.

My recommendation: treat **320x240 logical / 640x480p60 wire** as the current ceiling for a stable shipping build unless hardware testing proves otherwise.

## 2. Frame rate: 50 Hz vs 60 Hz

### Why 60 Hz is the better default

- The game loop and interrupts are already modeled as **one real arcade frame at 60 Hz**.
- The current display path is genlocked to hardware VSYNC at **60.000 Hz**.
- **48,000 / 60 = 800 samples/frame**, which makes audio pacing very clean.
- 640x480p60 is a very common, display-friendly mode.

### What 50 Hz would buy

At first glance, 50 Hz gives more frame time:

- **20.00 ms/frame** instead of 16.67 ms/frame
- **48,000 / 50 = 960 audio samples/frame**
- **240 audio packets/frame** at 4 samples/packet

So Core 0 would get more time per frame.

### Why 50 Hz is not a free win

- It would no longer match the current 60 Hz emulation cadence.
- It would require retuning frame pacing, audio queue targets, and possibly game-time assumptions.
- A standard 50 Hz consumer-video alternative is typically **576p50-class timing**, which is actually **more total video work**, not less.
- A custom **640x480p50** mode may be electrically possible but is less interoperable with real displays than 640x480p60.

Recommendation: **keep 60 Hz for the mainline build**. Only investigate 50 Hz as an experiment if the goal is a PAL-style variant for specific displays, not as the primary stability fix.

## 3. Audio rate and scaling

### Source material: native 32 kHz

Space Invaders' original PCB produced sound through analog circuits, not digital PCM at any fixed rate.
This project's sound assets are synthesised or sampled PCM files stored at **32 kHz**
(`SOUND_SAMPLE_RATE_HZ 32000` in `src/audio/sound_data.h`).
That 32 kHz figure is *not* a hardware requirement from the arcade board — it is simply the rate the
asset files happen to be encoded at, chosen as a reasonable low-cost source rate that still captures the
frequency content of the retro sound effects (all of which are band-limited well below 8 kHz in practice).

The HDMI transport rate is a separate concern: `AUDIO_SAMPLE_RATE` (`src/platform/audio/audio_engine.h`)
controls what rate is declared to the HDMI sink and what rate the sample-rate converter targets.
`space_invaders_plugin.c` already contains a nearest-sample resampler (`voice_advance()`, fixed-point
fractional accumulator) that converts from `SOUND_SAMPLE_RATE_HZ` to the hardcoded 48000 Hz transport
rate on every call. Note this resampler always runs unconditionally - unlike the hypothetical
equal-rates fast path discussed below, there is currently no `#if`-gated shortcut for the rates-match
case. The two rates are decoupled by design.

### Baseline numbers used throughout this section

All figures are calculated for the current hardware: **RP2350 at 126 MHz**, **60 Hz frame rate**,
**4 samples per HDMI Data Island packet**, **up to 5 simultaneous voices** (4 one-shot + 1 loop,
all active = worst case), **10 sound effects averaging 0.3 s each** (representative asset set).

- Frame budget: `126 000 000 / 60 = 2 100 000 cycles = 16 667 µs`
- Resampler fast path (rates match): ~6 cycles per active voice per output sample — load + increment + compare
- Resampler interpolation path (rates differ): ~10 cycles per active voice per output sample — the extra frac-accumulator add + conditional subtract loop

These are conservative single-issue estimates; actual ARM Cortex-M33 pipelining makes the real cost slightly lower, but the *ratios* between options hold.

### Three-way comparison: 32 kHz vs 44.1 kHz vs 48 kHz

#### 32 kHz

| Dimension | Detail | Practical impact |
|---|---|---|
| **Packets / frame at 60 Hz** | `32000 / 4 / 60 = 133.33` — not an integer | **Bad:** the fractional 0.33 accumulates to a full missing packet every **3 frames (50 ms)**; `audio_engine_feed_queue()` needs an explicit running-remainder counter or the queue level drifts by ±1 packet continuously |
| **Mixer CPU cost** | `533 samples/frame × 5 voices × 6 cycles = ~20 000 cycles/frame` | **~158 µs / frame = 0.95% of the frame budget** — cheapest of all three options |
| **Saving vs 48 kHz** | 48 kHz costs ~46 000 cycles/frame; 32 kHz costs ~20 000 cycles/frame | **Saves ~26 000 cycles ≈ 190 µs ≈ 1.1% of the frame budget** — real but small on RP2350 at 126 MHz |
| **Resampling cost** | Zero if a `#if SOUND_SAMPLE_RATE_HZ == AUDIO_SAMPLE_RATE` fast path were added to `space_invaders_plugin.c`'s `voice_advance()` (not present today - see note above) | **Benefit:** eliminates ~4 extra cycles per active-voice per sample vs the resample path; saved instructions also reduce instruction-cache pressure slightly |
| **HDMI compliance** | 32 kHz is a valid CEA-861 audio sample rate | **Risk:** a minority of HDMI sinks (some older TVs, some AV receivers) silently mute 32 kHz audio or display a "no audio" error even though it is technically legal |
| **Flash / RAM for assets** | `10 effects × 0.3 s × 32 000 samples/s × 2 bytes = ~188 KB` | **Saves ~93 KB of flash vs 48 kHz source** — meaningful on a flash-constrained build |
| **Audio bandwidth** | Nyquist at 16 kHz | No audible difference: all Space Invaders sound effects are band-limited below 6 kHz |
| **Verdict** | Worth considering only if flash or CPU budget is critically tight | The 1.1% CPU saving and 93 KB flash saving are real; the fractional-packet accounting and HDMI compatibility risk are the trade-offs |

#### 44.1 kHz

| Dimension | Detail | Practical impact |
|---|---|---|
| **Packets / frame at 60 Hz** | `44100 / 4 / 60 = 183.75` — not an integer | **Worst of all three:** the 0.75 fractional part accumulates a full missing packet every **1.33 frames (22 ms)**; over 4 frames the drift is 3 whole packets — an audible stutter if uncompensated |
| **Mixer CPU cost** | `735 samples/frame × 5 voices × 10 cycles = ~42 262 cycles/frame` | **~335 µs / frame = 2.0% of the frame budget** — more expensive than 48 kHz for no benefit |
| **Cost vs 48 kHz** | 44.1 kHz is actually slightly *cheaper* than 48 kHz (735 vs 800 output samples), but the resample ratio `320:441` is harder to compute | **Saves only ~29 µs/frame (0.14%)** — negligible; offset by the fractional-packet accounting overhead that 44.1 kHz requires but 48 kHz does not |
| **Resampling cost** | Step ratio `32000/44100 = 320/441` — no small integer simplification | **Same ~10 cycles/voice/sample as 48 kHz**, but the non-power-of-two modulus means the fractional accumulator never "resets cleanly," giving a slightly irregular per-sample branch pattern |
| **HDMI compliance** | Valid CEA-861 rate, well-supported by most sinks | Slightly worse than 48 kHz in AV-receiver compatibility; no advantage over 48 kHz |
| **Debug tone LUT** | `44100 / 1000 = 44.1` — not an integer | **Bad:** the 1 kHz debug tone LUT must be rounded to 44 or 45 entries, producing a ~0.23% pitch error (inaudible but impure) |
| **Flash / RAM for assets** | `10 × 0.3 s × 44 100 × 2 bytes = ~258 KB` | **Uses 70 KB more flash than 32 kHz, 23 KB more than 48 kHz** with no audible improvement |
| **Audio bandwidth** | Nyquist at 22.05 kHz | Overkill: 6 kHz source content, no benefit |
| **Verdict** | **Avoid.** Gets the worst of both worlds: resampling cost without the clean-integer-packets property of 48 kHz, and fractional-packet drift worse than 32 kHz | |

#### 48 kHz (current)

| Dimension | Detail | Practical impact |
|---|---|---|
| **Packets / frame at 60 Hz** | `48000 / 4 / 60 = 200` — **exact integer** | **Best:** no rounding, no drift, no compensating bookkeeping; `AUDIO_QUEUE_TARGET_LEVEL = 200` is exact and stable forever |
| **Mixer CPU cost** | `800 samples/frame × 5 voices × 10 cycles = ~46 000 cycles/frame` | **~365 µs / frame = 2.19% of the frame budget** — highest absolute cost, but well within the ~14 600 µs idle tail every frame |
| **Extra cost vs 32 kHz** | 48 kHz costs ~26 000 more cycles/frame than 32 kHz | **Costs an extra 190 µs / frame = +1.1% of frame budget** — real but negligible on RP2350 at 126 MHz; equivalent to adding just ~26 000 / 2 100 000 = 1.1% more load on Core 0 |
| **Resampling cost** | Step ratio `32000/48000 = 2:3` — simplest possible non-trivial ratio | Every 3 output samples advance exactly 2 source samples; the fractional accumulator oscillates between 0 and 2 predictable values only, giving the most cache-friendly branch pattern of the resample path |
| **HDMI compliance** | **Primary** CEA-861 audio sample rate | **Best:** supported by every HDMI sink (TVs, monitors, AV receivers, capture cards) without exception |
| **Queue target constant** | `(48000 / 4) / 60 = 200` | Exact — `AUDIO_QUEUE_TARGET_LEVEL` in `audio_engine.h` requires zero correction logic now or in the future |
| **Debug tone LUT** | `48000 / 1000 = 48` entries | Exact integer; the 1 kHz tone is a perfect sine with zero pitch error |
| **Flash / RAM for assets** | `10 × 0.3 s × 48 000 × 2 bytes = ~281 KB` (if assets were authored at 48 kHz) | Assets are currently authored at 32 kHz and resampled at runtime, so **flash cost is actually the same 188 KB as the 32 kHz option** — only the runtime output rate differs |
| **Audio bandwidth** | Nyquist at 24 kHz | Overkill, but costs nothing extra |
| **Verdict** | **Preferred.** Every structural property favours it. The 1.1% extra CPU cost over 32 kHz is the only trade-off, and it is inconsequential at 126 MHz | |

### Summary comparison table

| Property | 32 kHz | 44.1 kHz | 48 kHz (current) |
|---|---|---|---|
| Packets/frame at 60 Hz | 133.33 (fractional) | 183.75 (fractional) | **200 (exact)** |
| Mixer CPU cost (5 voices) | **~20 000 cyc / ~158 µs / 0.95%** | ~42 262 cyc / ~335 µs / 2.0% | ~46 000 cyc / ~365 µs / 2.19% |
| vs 48 kHz (CPU) | **Saves ~26 000 cyc ≈ 190 µs ≈ 1.1%** | Saves ~3 738 cyc ≈ 30 µs ≈ 0.14% | baseline |
| Resampling from 32 kHz source | **None — fast path taken** | Yes, 320:441 ratio | Yes, 2:3 ratio |
| Queue drift if uncompensated | ±1 packet every 3 frames (50 ms) | ±1 packet every 1.3 frames (22 ms) | **Zero — never drifts** |
| Compensation code required | Yes — running-remainder counter | Yes — worse than 32 kHz | **No** |
| HDMI sink compatibility | Good (most sinks) | Very good | **Universal** |
| Flash for assets (10 × 0.3 s) | **~188 KB** | ~258 KB | **~188 KB** (assets stay at 32 kHz) |
| Debug tone LUT (1 kHz) | 32 entries (clean) | 44.1 — not integer (pitch error) | **48 entries (clean)** |
| Net recommendation | Viable only if CPU/flash is critically tight | **Avoid** | **Preferred** |

### Why 32 kHz is the only credible alternative

Switching `AUDIO_SAMPLE_RATE` from 48000 to 32000 saves **~26 000 cycles per frame (≈ 190 µs, ≈ 1.1% of
the 16.67 ms frame budget)** on RP2350 at 126 MHz, if a `#if SOUND_SAMPLE_RATE_HZ == AUDIO_SAMPLE_RATE`
fast path were added to `voice_advance()` (`space_invaders_plugin.c`) to drop to ~6 cycles/voice/sample
instead of the ~10 it costs unconditionally today.
It also saves **no flash** (assets are already stored at 32 kHz) but would save flash if the project were
ever ported to a platform where assets needed to be encoded at the output rate.

The trade-off is:
- **Fractional-packet bookkeeping**: the queue target must alternate between 133 and 134 packets, driven by
  a running-remainder counter, or the queue drifts by ±1 packet every 3 frames (50 ms) indefinitely.
- **Marginally reduced HDMI sink compatibility**: a minority of sinks misbehave at 32 kHz.

On RP2350 at 126 MHz, 1.1% of the frame budget is inconsequential — the CPU emulation alone takes far
more. This trade is not worth making unless targeting a much slower MCU.

44.1 kHz offers neither the zero-resampling benefit of 32 kHz nor the clean integer-packets benefit of
48 kHz. It also has the worst drift rate (±1 packet every 22 ms) and no audible quality advantage. It
should not be considered for this project.

### Audio-related suggestions

- Keep **48 kHz** as the transport rate.
- Sound assets may remain at 32 kHz — the 2:3 resampler is correct and cheap.
- If memory or CPU becomes tight, consider reducing the **content** complexity of the mixer (fewer simultaneous voices, shorter sample tables) before changing the HDMI sample rate.
- If experimenting with 50 Hz, update the queue target to match **48000 / 4 / 50 = 240 packets**.
- If ever switching to 32 kHz transport, add explicit fractional-packet compensation to `audio_engine_feed_queue()` to prevent cumulative queue-level drift.

## Main stability improvement opportunities

## High-confidence improvements

### 1. Add a documented performance budget table

Create a permanent table in the docs for:

- line time
- H-blank time
- active time
- logical pixels per even line
- effective palette lookups per frame
- audio packets per frame
- emulation time budget per frame

This makes later experiments easier to judge before trying them on hardware.

### 2. Keep Core 1 work fixed and content-independent

The strongest theme in the repository notes is that the display path is happiest when Core 1 does the same predictable amount of work every line.
Any new feature should prefer:

- work on **Core 0**, before `dvi_display_present_frame()`
- lookup-table driven rendering
- fixed-cost per-line operations
- no extra branching in the Core 1 fill callback

### 3. Treat the fill callback as a hard real-time boundary

Do not add to Core 1's callback unless the feature clearly pays for itself.
Prefer stability-friendly features that leave the callback unchanged.

### 4. Add compile-time quality tiers

Useful presets would be:

- **Stability-first**: current 320x240 path, minimal extras
- **Balanced**: current path plus simple overlays/effects
- **Experimental**: optional visual features that may reduce margin

That gives a safe baseline for hardware soak testing.

## Medium-risk experiments

### 5. Reduce Core 0 bus burstiness further

The notes suggest irregular Core 0 activity may still be triggering rare bad windows even after the major architectural fixes.
Ideas worth testing:

- spread non-urgent work more evenly across the frame
- avoid large temporary memory walks near present time
- keep render passes linear and cache-friendly
- avoid debug printing or other incidental interrupts during active gameplay

### 6. Explore simpler physical-wire modes only if supported by the library

If `pico_hdmi` can support a lower-throughput standard mode with strong monitor compatibility, it may be worth evaluating. But this should only be pursued if:

- the mode is standard and display-friendly
- the total pixel and blanking budget are genuinely easier than 640x480p60
- it does not make audio insertion harder

Based on the current project structure, this is a secondary path, not the first fix.

## Retro-gaming features that are interesting without hurting stability much

These are the most promising additions because they can mostly happen on Core 0 in the 320x240 buffer:

### 1. Cabinet/profile presets

Per-game or per-display presets for:

- rotation
- flip
- screen offset
- scale mode
- color overlay on/off

This adds usability without touching the fragile timing path.

### 2. Better CRT-style presentation, kept cheap

Low-cost options:

- optional scanline darkening in the **320x240 buffer** before present
- mild vignette or bezel-safe border treatment
- alternate phosphor/monitor palette presets
- optional monochrome variants (green, white, amber)

These are retro-friendly and can be implemented without increasing wire resolution.

### 3. Aspect-ratio presets

Offer presets such as:

- integer-like fit
- full-height fit
- wider fill for modern 4:3 panels
- original visible-area bias

This fits the existing scaling model in `display_config.h` and `Emulator.md`.

### 4. Per-title video personality

If this project grows into a small arcade platform later, keep the HSTX mode fixed but vary only:

- internal logical content size
- palette
- overlay bands
- bezel art
- scale/offset presets

That is a good way to support more retro content without reopening the most timing-sensitive part of the system.

### 5. Optional frame blending only as a toggle

A very light persistence effect could make motion feel more CRT-like, but it should stay optional because it increases memory traffic on Core 0.
It is more promising than raising output resolution, but should still be treated as a measured experiment.

## Recommended order of work

1. **Document the timing budget** in one place and keep it updated.
2. **Keep 640x480p60 + 320x240 logical + 48 kHz audio as the shipping baseline.**
3. Add only features that preserve the current Core 1 callback cost.
4. If chasing more stability, optimize for **less cross-core bus irregularity**, not for more visual resolution.
5. Add retro appeal through **presentation features** rather than more pixels.
6. Treat **50 Hz** and alternate wire timings as optional experiments, not the default direction.

## Bottom line

The current architecture is already close to the sensible physical limit for a stable HDMI build on this hardware:

- **640x480p60 wire output** is the right compatibility target.
- **320x240 logical rendering with 2x doubling** is the right stability/performance compromise.
- **48 kHz HDMI audio** is the right transport rate.
- The next wins are more likely to come from **predictability and presentation polish** than from pushing resolution or exotic timings.

If the goal is “more interesting retro gaming,” the best return is probably:

- stronger display presets
- cheap CRT-style presentation options
- better cabinet/orientation profiles
- optional visual polish that stays on Core 0

rather than trying to exceed the current wire-resolution strategy.

---

## 4. System clock speed investigation

> **Update (v1.2.0): this section's recommendation has been adopted.** `DVI_SYS_CLK_KHZ` in
> `dvi_display.c` is now `151200` (151.2 MHz), not the `126000` this section analyzes as "current."
> The reasoning, comparison table, and risk breakdown below are kept as the decision record for
> *why* 151.2 MHz was chosen and why higher candidates (176.4/201.6/252 MHz) were not - read
> "126 MHz" throughout as "the baseline this analysis was run from," not the current live setting.

### Why 126 MHz is the baseline

`DVI_SYS_CLK_KHZ 126000` in `dvi_display.c` is not an arbitrary choice. 640×480p60 requires a **25.2 MHz
pixel clock**, and the RP2350 HSTX serialiser needs the system clock to be an **exact integer multiple** of
that pixel clock. If the ratio is fractional, the HSTX serialiser's internal divider produces a slightly
wrong bit rate and the display loses sync permanently — the same desync failure mode documented in
`CLAUDE.md`. 126 MHz = **5 × 25.2 MHz** is the smallest clean multiple that keeps the PLL within the
RP2350's nominal operating range.

### Current frame budget at 126 MHz

At 126 MHz, each 60 Hz frame is **2 100 000 cycles = 16.67 ms**.

| Work item | Cycles / frame | % of budget | Wall time |
|---|---|---|---|
| 8080 emulation (33 280 T-states × ~10 ARM ops) | ~332 800 | **15.8%** | ~2.64 ms |
| Audio mixer (800 samples × 5 voices × ~10 cyc + packet build) | ~46 000 | **2.2%** | ~0.37 ms |
| Core 1 fill callback (120 even lines × 80 iter × ~6 cyc) | ~57 600 | **2.7%** | ~0.46 ms |
| **Total active work** | **~436 400** | **~20.8%** | **~3.46 ms** |
| **Idle / spare** | **~1 663 600** | **~79.2%** | **~13.2 ms** |

The chip spends about 80% of every frame doing nothing. There is no throughput problem to solve.

### Valid candidate clocks (integer multiples of 25.2 MHz only)

Only clocks that are **exact integer multiples of 25.2 MHz** are safe. The three common SDK presets —
150 MHz (5.952×), 200 MHz (7.937×), 250 MHz (9.921×) — are **not** clean multiples and **must not be
used**; they produce fractional HSTX bit rates and permanent display desync.

| MHz | Multiple | HSTX valid | Voltage | Frame budget | Emu % | vs 126 MHz | Risk |
|---|---|---|---|---|---|---|---|
| **126** | **5×** | **YES** | **1.10 V (default)** | 2 100 000 cyc | **15.8%** | baseline | none — current |
| **151.2** | **6×** | **YES** | 1.10 V (within spec) | 2 520 000 cyc | 13.2% | **+20% budget, –2.6 pp emu** | low |
| **176.4** | **7×** | **YES** | ~1.15 V needed | 2 940 000 cyc | 11.3% | +40% budget, –4.5 pp emu | medium |
| **201.6** | **8×** | **YES** | ~1.20 V needed | 3 360 000 cyc | 9.9% | +60% budget, –5.9 pp emu | medium–high |
| **252.0** | **10×** | **YES** | ~1.30 V needed | 4 200 000 cyc | 7.9% | +100% budget, –7.9 pp emu | high |
| 150 | ~5.95× | **NO** | — | — | — | — | **HSTX desync — do not use** |
| 200 | ~7.94× | **NO** | — | — | — | — | **HSTX desync — do not use** |
| 250 | ~9.92× | **NO** | — | — | — | — | **HSTX desync — do not use** |

"% emu" = fraction of the frame budget consumed by 8080 emulation alone. "pp" = percentage points.

### What going faster actually buys

**The idle time does not change in wall-clock terms.** The frame is always 16.67 ms (genlocked to
hardware VSYNC). Going faster means each ARM instruction finishes in fewer nanoseconds, so Core 0
completes its fixed workload sooner and sits idle longer. The practical effects are:

1. **The H-blank window gains more cycles.** At 126 MHz the H-blank is 800 cycles (~6.35 µs).
   At 151.2 MHz it grows to 960 cycles — 20% more headroom for Core 1's ISR before the deadline.
2. **The emulation fraction shrinks**, marginally reducing the probability that Core 0 is on the
   bus during Core 1's critical H-blank window.
3. **No new free wins.** The stability trigger is *irregular* Core 0 bus traffic, not how many
   cycles Core 0 uses. A faster clock reduces the odds of a bad timing coincidence slightly, but
   does not eliminate the irregularity.

### Risk breakdown by candidate

**151.2 MHz — the only low-risk increase**

- Still within practical operating range at 1.10 V. The RP2350 datasheet rates 133 MHz at 1.10 V;
  community and SDK testing confirms reliable operation at ~150 MHz at the same voltage.
  151.2 MHz is ~1.5% above that and well within typical silicon margin.
- No voltage change required — `VREG_VSEL` stays at `VREG_VOLTAGE_1_10`.
- Single constant change: `DVI_SYS_CLK_KHZ 126000 → 151200`.
- Verify `clock_get_hz(clk_sys)` in the debug print after flashing; if `set_sys_clock_khz` cannot
  find a PLL lock it silently stays at the old frequency.

**176.4 MHz — moderate risk**

- Requires raising `VREG_VSEL` to `VREG_VOLTAGE_1_15` (1.15 V).
- Higher voltage → faster HSTX signal edges → more potential ringing on the HDMI cable.
- Flash XIP latency increases relative to CPU clock; cache miss cost inside `invaders_machine_run_cycles()` (`z80_tick()` calls) rises.
- Higher idle current draw and board temperature.

**201.6 MHz and 252.0 MHz — high risk**

- Both require explicit voltage increases (1.20 V and 1.30 V respectively).
- Flash SPI clock divider (`PICO_FLASH_SPI_CLKDIV`) may need explicit adjustment or flash reads
  become unreliable.
- 252 MHz approaches the RP2350's soft community-tested ceiling (~300 MHz) in a chip-lot-dependent way.
- Neither addresses the known sync-loss root cause; both add new failure modes.

### Recommendation

~~Try **151.2 MHz first**.~~ **Done (v1.2.0)** - `DVI_SYS_CLK_KHZ` is `151200`. It was a single
constant change, cost nothing in voltage or complexity, and extends the H-blank cycle budget by
20%. It has been soak-tested on real hardware well beyond 30 minutes of active gameplay (see
`CLAUDE.md`'s "HSTX sync-loss caveat", narrowing #10 and the "Current status" note above it) with
no reproduction of the sync-loss bug - believed fixed, not formally proven, per that note.

---

## 5. PIO — what it can and cannot do for stability

### What PIO is on RP2350

PIO (Programmable I/O) state machines are small, independent processors running fixed 32-instruction
programs from dedicated instruction memory, with their own clock divider and direct GPIO access.
They are **fully decoupled from the ARM cores** — they consume no CPU cycles and generate no shared-bus
traffic for their own GPIO operations. The RP2350 has three PIO blocks (PIO0/1/2), each with four state
machines, for twelve in total. This project currently uses none.

**Important**: the HSTX peripheral is *not* PIO — it is a completely separate, dedicated hardware block.
It cannot be replaced by, combined with, or controlled through PIO.

### The stability problem PIO cannot solve

The confirmed root trigger is **irregular Core 0 bus traffic from real 8080 opcode dispatch** in
`invaders_machine_run_cycles()` (via the shared `z80_tick()` core - see CLAUDE.md's "HSTX sync-loss
caveat" for why Space Invaders no longer has its own separate `i8080_step()`). Every opcode fetches at a
different bus address, takes a different number of cycles, and
issues a different number of reads/writes to the emulated machine's SRAM. This irregularity exists inside
the ARM core's own instruction fetch/decode/execute pipeline. PIO state machines can observe GPIO pins;
they cannot observe or gate the system interconnect between the CPU and SRAM.

### Where PIO genuinely helps: a correctly-designed input driver

The previous SNES controller driver used PIO but was designed without `.wrap_target`/`.wrap`, so the
state machine ran free-running at ~1 MHz, pushing new samples every ~46 µs. When Core 0 drained the
FIFO once per frame, the SM burst back into ~184 µs of GPIO activity, creating a recurring PIO bus event
every 16.67 ms that the investigation temporarily suspected as a stability trigger.

A **correctly-designed PIO input driver** can be safe and beneficial:

- Use `.wrap_target`/`.wrap` so the SM idles waiting for an explicit command after each sample
  (a `pull block` or `wait irq`).
- Gate the latch/clock burst to happen only during the **dead frame tail** (after
  `dvi_display_present_frame()` but before the next VSYNC), when Core 1 is not filling the line buffer.
- Keep GPIO activity on pins well away from the HSTX differential pairs (GPIO 12–19) to minimise
  electromagnetic coupling.

Result: **zero CPU cycles** for input polling, **zero unpredictable PIO GPIO bursts during active video**,
and a clean sample already in the FIFO when Core 0 wants it.

### PIO applicability summary

| PIO application | Helps stability? | Notes |
|---|---|---|
| Gated input driver | **Yes** — removes irregular GPIO bursts during active video | **Implemented and soak-tested**: `src/platform/input/snes_controller.pio` now uses `.wrap_target`/`.wrap` so the SM idles until Core 0 explicitly triggers exactly one read per frame via a non-blocking pipelined request/collect protocol; confirmed on hardware not to reintroduce the sync-loss (CLAUDE.md's "HSTX sync-loss caveat", narrowing #10) |
| TMDS/HSTX replacement | **No** — PIO cannot run at TMDS bit rates on RP2350 | 252 Mbit/s per lane far exceeds PIO's ~63 MHz instruction rate; HSTX exists for this |
| Bus-traffic monitor (detect HSTX desync) | **No** — PIO cannot observe internal DMA/bus transactions | Only observable via GPIO pins |
| Core 0 execution pacer (injection of delays) | **No** — PIO cannot throttle ARM instruction fetch; CPU-based pacing attempt caused a crash | |
| Autonomous audio sample generation | **Marginal** — a SM could push silence/tone samples into a FIFO for Core 0 to read | Current CPU-side mixer costs only 2.2% of the frame budget; not worth the complexity |

**Bottom line on PIO**: it is not a solution to the HSTX sync-loss problem, but it is the right
implementation technology for an input device. The SNES driver's problem was its *free-running
design*, not PIO itself - this has since been corrected (see the table row above) rather than left as a
future recommendation.

---

## 6. Optimisation recommendations

The sections above establish a clear picture: Core 0 spends only ~21% of each frame doing real work,
the audio and video pipelines are already well-optimised, and the remaining instability comes from a
rare timing hazard in the HSTX DMA engine — not from a shortage of CPU cycles. Optimisations should
therefore target *predictability* and *bus isolation* first, raw throughput second.

The table below lists every actionable item across all topics discussed, ranked by confidence,
estimated implementation effort, and expected stability impact.

### Implemented & Proven Stability Pillars (v1.2.0)

| Pillar | Mechanism | File / Setting | Hardware Stability Impact |
|---|---|---|---|
| **1. 100% SRAM Execution** | `copy_to_ram` binary type | `CMakeLists.txt` | **Eliminates 100% of Flash QSPI XIP cache miss stalls** (which previously caused 50–80 cycle bus freezes during 8080 opcode jumps & sound reads). |
| **2. Precomposed Active Lines** | `PICO_HDMI_PRECOMPOSED_ACTIVE_LINES=ON` | `CMakeLists.txt`, `dvi_display.c` | **Shrinks H-blank ISR time from ~5.0 µs to < 1.2 µs**, providing > 5 µs of safety margin before the 6.35 µs H-blank deadline. |
| **3. Hardware Event Standby (`__wfe`)** | VSYNC callback + ARM WFE sleep | `dvi_display.c` (`dvi_display_wait_for_vsync`) | **Eliminates 13.5 ms of tight 126 MHz SRAM polling**, setting Core 0 bus traffic to 0 during the entire frame wait. |
| **4. Private Memory Bank Isolation** | Scratch X & Scratch Y placement | `dvi_display.c`, `video_output.c` | `palette_fast` in **Scratch X** and `line_buffer` in **Scratch Y** bypass the main AHB crossbar, guaranteeing zero bus contention for palette lookups. |
| **5. Decoupled Frame Emulation** | 2-half frame blocks (16,640 cyc $\to$ RST1 $\to$ 16,640 cyc $\to$ RST2) | `space_invaders_plugin.c` (`run_frame`) | Emulation finishes in ~2 ms, leaving the remaining ~14.6 ms of the frame free of shared bus traffic. |
| **6. Continuous 48 kHz Audio Delivery** | Uncapped queue feeding (200 packets/frame) | `audio_engine.c` (`audio_engine_feed_queue`) | Delivers exact 800 samples/frame, eliminating silence-packet underruns and audio distortion. |

---

## Remaining Actionable Hardening Steps

### Priority 1 — Low Effort, Immediate Headroom Boost

| # | Action | Files to Change | Expected Benefit |
|---|---|---|---|
| ~~**P1-A**~~ | ~~**Switch System Clock to 151.2 MHz**~~ (6× 25.2 MHz pixel clock) - **Done** | `dvi_display.c`: `DVI_SYS_CLK_KHZ` is now `151200` | **+20% H-blank cycle budget (800 → 960 cycles)**; emulation time drops from 2.64 ms to 2.20 ms. Operates at nominal 1.10 V with zero voltage increase. |
| **P1-B** | **Maintain 48 kHz Audio SSOT** | `audio_engine.h` | Exact 200 packets/frame integer ratio with zero drift. |
| **P1-C** | **Maintain 320×240 Double-Buffered Scaling** | `dvi_display.c`, `si_config.h` | Fixed 80-iteration scanline callback, minimal Core 1 overhead. |

### Priority 2 — Deep Architectural Hardening

| # | Action | Files to Change | Expected Benefit |
|---|---|---|---|
| **P2-A** | **Explicit SRAM Bank Striping for Framebuffers** | `dvi_display.c` (`fb_buffers` section attribute) | Place `fb_buffers[0]` in `sram4` and `fb_buffers[1]` in `sram5`. Completely separates Core 0 write bus port from Core 1 DMA read bus port on the RP2350 crossbar. |
| ~~**P2-B**~~ | ~~**Computed-Goto / Jump Table for `z80_tick()`**~~ - **Superseded** | `src/emu/z80.c`, `src/emu/z80.h` | The hand-rolled interpreter this targeted was replaced by the vendored `chips/z80.h` single-header core (`src/emu/z80.c` is now just `#define CHIPS_IMPL` + `#include "z80.h"`), which already dispatches via its own micro-step `switch(cpu->step)` table rather than a per-opcode switch - this specific recommendation no longer applies as written. |
| ~~**P2-C**~~ | ~~**Gated PIO Controller Input Driver**~~ - **Done (v1.2.0)** | `src/platform/input/snes_controller.{c,h,pio}` | Implemented as a request-gated (not vertical-blank-gated - triggered once per frame from the main loop, see §5) PIO driver; soak-tested clean, see §5 and `CLAUDE.md`'s "HSTX sync-loss caveat" (narrowing #10). |

---

### What to Avoid

| Anti-Pattern | Reason |
|---|---|
| **Non-Integer System Clocks (150, 200, 250 MHz)** | Not integer multiples of 25.2 MHz $\to$ Fractional HSTX bit rate $\to$ Permanent display loss of sync. |
| **Native 640×480 Logical Rendering** | Doubles palette lookups in the time-critical H-blank window. |
| **44.1 kHz Audio Transport** | 183.75 packets/frame causes severe fractional drift (±1 packet every 22 ms) and non-integer 1 kHz tone LUT (44.1 samples). |
| **Flash Execution without `copy_to_ram`** | Flash SPI cache misses cause 50–80 cycle bus freezes, starving the HSTX FIFO. |
| **Tight Polling in Main Loop** | Spinning on SRAM variables at 126 MHz congests the AHB crossbar. Always use `__wfe()` event standby. |
