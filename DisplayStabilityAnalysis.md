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

### Why 48 kHz is the right baseline

- It is already integrated with `pico_hdmi`.
- It aligns cleanly with 60 Hz (**800 samples/frame**) and 50 Hz (**960 samples/frame**).
- HDMI sinks commonly expect 48 kHz and handle it well.

### Audio-related suggestions

- Keep **48 kHz** as the transport rate.
- If memory or CPU becomes tight, consider reducing the **content** complexity of the mixer before changing the HDMI sample rate.
- If experimenting with 50 Hz, update the queue target to match **960 samples/frame / 4 = 240 packets**.

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
