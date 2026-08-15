# Display Stability Analysis and Optimisation Ideas

## Current baseline

- **Wire timing today:** 640x480p60 (`DISPLAY_REFRESH_HZ = 60`) at a **25.2 MHz pixel clock**.
- **System clock today:** **126 MHz** (`DVI_SYS_CLK_KHZ 126000`), which is a clean 5x multiple of 25.2 MHz.
- **Logical framebuffer today:** **320x240 8bpp**, doubled in both axes by Core 1's scanline fill callback.
- **Arcade source image:** **256x224 1bpp**, converted to the 320x240 framebuffer on Core 0.
- **Audio today:** **48 kHz LPCM over HDMI Data Islands**, with **4 samples per packet** and a **200-packet target queue**.

This is a sensible baseline because it matches a very common display mode, uses exact integer relationships, and avoids the known failure mode of trying to do too much work inside the Core 1 HSTX path.

## Timing budget

### Frame-level budget

For 640x480p60, the full CEA/VESA-style wire timing is effectively:

- **800 pixels/line** total
- **525 lines/frame** total
- **25.2 million pixels/second**
- **16.67 ms/frame**
- **31.77 us/line**

### Blank/active split

The repository notes a practical **H-blank window of about 6.35 us** per line.
That leaves about **25.4 us active video** per line.

That 6.35 us is the dangerous window for HDMI mode because the ISR still has to keep the HSTX/DMA command stream correct while audio Data Islands are being injected.

### Why 320x240 is the safe operating point

The current design only performs palette lookup for **320 source pixels** per logical row, then packs each color into both halves of a 32-bit word for horizontal doubling.
Because the callback reads **4 source pixels per 32-bit load**, each even scanline needs only **80 loop iterations**. Odd scanlines reuse the already-filled line buffer.

That is a major reason the current design is viable:

- **Native 640x480 logical rendering** would double the per-line lookup work.
- The comment in `src/video/display_config.h` already says that **640 independent palette lookups per scanline fit in pure DVI mode but not comfortably in HDMI mode**.
- So the current **320x240 -> 640x480** scheme is not just a cosmetic scaling choice; it is a stability choice.

## What is physically realistic on this hardware

## 1. Resolution

### Safest practical target

- **640x480p60 wire timing** with a **320x240 logical framebuffer** is the safest practical target for this project.

### Likely still viable

- **256x224 or 256x240 logical internal rendering** with scaling into the existing 320x240 buffer is also safe, because it does not increase HSTX-side work.
- Small visual additions that happen on **Core 0 before present** are likely fine if they do not force Core 1 to do more per-pixel work.

### Higher-risk / poor return

- **Native 640x480 logical rendering** is high risk in HDMI mode because it roughly doubles fill-callback work at exactly the place the project has already found timing sensitivity.
- **Higher wire modes** such as 800x600 or 720p are not good near-term targets. Even if electrically possible with RP2350/HSTX, they would increase total pixel throughput, reduce timing margin, increase memory pressure, and likely worsen the same class of stability issues.

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

The HDMI transport rate is a separate concern: `AUDIO_SAMPLE_RATE` (`src/audio/audio_i2s.h`) controls
what rate is declared to the HDMI sink and what rate the sample-rate converter targets.
`audio_i2s.c` already contains a linear-interpolation resampler that converts from
`SOUND_SAMPLE_RATE_HZ` to `AUDIO_SAMPLE_RATE` on every call to `voice_next_sample()`.
The two rates are decoupled by design.

### Three-way comparison: 32 kHz vs 44.1 kHz vs 48 kHz

#### 32 kHz

| Dimension | Detail |
|---|---|
| **Packets / frame at 60 Hz** | 32000 / 4 / 60 = **133.3** — not an integer |
| **Resampling cost** | Zero: source rate equals transport rate; the `#if SOUND_SAMPLE_RATE_HZ == AUDIO_SAMPLE_RATE` fast path in `audio_i2s.c` is taken |
| **HDMI compliance** | 32 kHz is a valid CEA-861 audio sample rate. Most modern TVs and monitors accept it, but it is the least commonly tested consumer rate and a small number of displays misbehave |
| **Queue target** | `(32000 / 4) / 60 = 133.33` — fractional; the queue target constant must be rounded to 133 or 134, introducing a cumulative ±1-packet/frame drift that must be absorbed by the H-blank insertion logic |
| **Memory (test-tone LUT)** | `AUDIO_SAMPLE_RATE / 1000 = 32` entries for the 1 kHz debug tone — smallest of the three options |
| **Audio bandwidth** | Nyquist at 16 kHz — more than sufficient for 1970s arcade sound effects, which have no content above ~6 kHz |
| **Verdict** | Eliminates resampling entirely; fractional packets-per-frame and slightly reduced HDMI sink compatibility are the trade-offs |

#### 44.1 kHz

| Dimension | Detail |
|---|---|
| **Packets / frame at 60 Hz** | 44100 / 4 / 60 = **183.75** — not an integer |
| **Resampling cost** | The existing linear-interpolation resampler runs with a 32000/44100 step ratio. The ratio `32000 / 44100 = 320/441` has no small integer simplification, meaning the fractional accumulator in `voice_next_sample()` carries a non-power-of-two modulus. Every output sample requires one multiply and one compare regardless; cost is essentially the same as the 48 kHz path |
| **HDMI compliance** | 44.1 kHz is a valid CEA-861 rate and is well-supported by virtually all HDMI sinks (it is the CD standard), but it is slightly less universal in the HDMI/AV receiver world than 48 kHz |
| **Queue target** | `(44100 / 4) / 60 = 183.75` — fractional; same rounding/drift problem as 32 kHz, but worse because the fractional part (`0.75`) accumulates faster. Over 4 frames it drifts by 3 full packets. The HDMI standard handles this via the audio clock regeneration (ACR) mechanism, but the queue-level accounting in `audio_i2s_feed_queue()` needs explicit fractional compensation or will drift visibly |
| **Memory (test-tone LUT)** | `44100 / 1000 = 44.1` — not an integer; the 1 kHz debug tone LUT length does not come out clean, so the tone will have a small pitch artefact or the LUT needs rounding |
| **Audio bandwidth** | Nyquist at 22.05 kHz — overkill for this application |
| **Verdict** | Brings no meaningful quality benefit over 48 kHz for this source material, while introducing fractional-packet problems that 48 kHz avoids. It is the worst of the three options for this project |

#### 48 kHz (current)

| Dimension | Detail |
|---|---|
| **Packets / frame at 60 Hz** | 48000 / 4 / 60 = **200** — exact integer |
| **Resampling cost** | Linear interpolation from 32 kHz to 48 kHz with a 32000/48000 = **2/3** step ratio. This is the simplest non-trivial ratio: every 3 output samples consume exactly 2 input samples on average, so the fractional accumulator stays small and bounded |
| **HDMI compliance** | 48 kHz is the **primary** CEA-861 audio sample rate and is supported by every HDMI sink without exception |
| **Queue target** | `(48000 / 4) / 60 = 200` — exact, no drift, no rounding needed. `AUDIO_QUEUE_TARGET_LEVEL` is derived from this identity in `audio_i2s.h` |
| **Memory (test-tone LUT)** | `48000 / 1000 = 48` entries — an exact integer; the 1 kHz debug-tone LUT is clean |
| **Audio bandwidth** | Nyquist at 24 kHz — more than sufficient; same overkill factor as 44.1 kHz for this content |
| **Verdict** | Clean integer packets-per-frame, simplest resampling ratio, universal HDMI compatibility, and no queue-drift bookkeeping. Every structural property favours it |

### Summary comparison table

| Property | 32 kHz | 44.1 kHz | 48 kHz (current) |
|---|---|---|---|
| Packets/frame at 60 Hz | 133.3 (fractional) | 183.75 (fractional) | **200 (exact)** |
| Resampling from 32 kHz source | **None (zero cost)** | Yes, 320:441 ratio | Yes, 2:3 ratio |
| HDMI sink compatibility | Good (most sinks) | Very good | **Universal** |
| Queue drift | Yes – needs compensation | Yes – worse than 32 kHz | **None** |
| Debug tone LUT integer length | Yes (32 entries) | No (44.1 entries) | **Yes (48 entries)** |
| Audio bandwidth headroom | Sufficient | Excess | Excess |
| Net recommendation | Viable if CPU is critically tight | **Avoid** | **Preferred** |

### Why 32 kHz is the only credible alternative

If a future port of this code to a much slower MCU needed to eliminate the resampler overhead entirely,
switching `AUDIO_SAMPLE_RATE` from 48000 to 32000 would allow the `#if SOUND_SAMPLE_RATE_HZ == AUDIO_SAMPLE_RATE`
fast path in `audio_i2s.c` to be taken for all voices, saving roughly one multiply + one compare per
output sample per active voice.
The cost is fractional packet accounting (a 133/134 alternating queue target driven by a running
fractional remainder counter) and marginally reduced HDMI sink compatibility.
On the RP2350 at 126 MHz, the resampler cost is negligible compared to the CPU emulation budget,
so this trade is not worth making today.

44.1 kHz offers neither the zero-resampling benefit of 32 kHz nor the clean integer-packets benefit
of 48 kHz, and should not be considered for this project.

### Audio-related suggestions

- Keep **48 kHz** as the transport rate.
- Sound assets may remain at 32 kHz — the 2:3 resampler is correct and cheap.
- If memory or CPU becomes tight, consider reducing the **content** complexity of the mixer (fewer simultaneous voices, shorter sample tables) before changing the HDMI sample rate.
- If experimenting with 50 Hz, update the queue target to match **48000 / 4 / 50 = 240 packets**.
- If ever switching to 32 kHz transport, add explicit fractional-packet compensation to `audio_i2s_feed_queue()` to prevent cumulative queue-level drift.

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
