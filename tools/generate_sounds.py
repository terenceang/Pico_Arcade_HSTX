import math
import os
import random
import struct

SAMPLE_RATE = 32000
SOUNDS_DIR = os.path.join(os.path.dirname(__file__), "..", "sounds", "space_invaders")

def clamp16(val):
    if isinstance(val, complex):
        val = val.real
    ival = int(round(float(val)))
    if ival > 32767:
        return 32767
    elif ival < -32768:
        return -32768
    return ival

def write_pcm(filename, samples):
    os.makedirs(SOUNDS_DIR, exist_ok=True)
    filepath = os.path.join(SOUNDS_DIR, filename)
    with open(filepath, "wb") as f:
        for s in samples:
            f.write(struct.pack("<h", clamp16(s)))
    print(f"Generated {filename}: {len(samples)} samples ({len(samples)/SAMPLE_RATE:.3f}s, {len(samples)*2} bytes)")

def gen_ufo():
    # UFO hum: level-triggered, loopable
    # LFO = 8 Hz, center = 560 Hz, depth = 160 Hz
    # Duration = 0.25 s (2 LFO cycles, 140 center carrier cycles -> seamless phase loop)
    lfo_freq = 8.0
    center_freq = 560.0
    depth = 160.0
    duration = 0.25
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    
    for i in range(num_samples):
        t = i / SAMPLE_RATE
        phase = 2.0 * math.pi * center_freq * t - (depth / lfo_freq) * math.cos(2.0 * math.pi * lfo_freq * t)
        # Sum harmonics for a retro analog VCO sound
        wave = math.sin(phase) + 0.3 * math.sin(3.0 * phase) + 0.1 * math.sin(5.0 * phase)
        samples.append(wave * 18000.0)
    
    write_pcm("ufo.pcm", samples)

def gen_shot():
    # Player laser shot: sweep pitch 2200 Hz -> 300 Hz
    duration = 0.18
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0.0
    
    for i in range(num_samples):
        t = i / (num_samples - 1) if num_samples > 1 else 0.0
        freq = 2200.0 * math.pow(0.12, t)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        
        # Pulse wave + slight noise
        wave = 1.0 if math.sin(phase) > 0 else -1.0
        wave += 0.2 * random.uniform(-1.0, 1.0)
        
        env = math.pow(max(0.0, 1.0 - t), 0.8)
        samples.append(wave * env * 22000.0)
        
    write_pcm("shot.pcm", samples)

def gen_player_die():
    # Player explosion: noise + low frequency rumble oscillation
    duration = 0.50
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0.0
    
    for i in range(num_samples):
        t = i / (num_samples - 1) if num_samples > 1 else 0.0
        t_sec = i / SAMPLE_RATE
        
        phase += 2.0 * math.pi * 35.0 / SAMPLE_RATE
        rumble = math.sin(phase)
        noise = random.uniform(-1.0, 1.0)
        
        wave = 0.7 * noise + 0.5 * rumble
        env = math.exp(-4.5 * t)
        if t < 0.02:
            env *= (t / 0.02)
            
        samples.append(wave * env * 24000.0)
        
    write_pcm("player_die.pcm", samples)

def gen_invader_die():
    # Invader explosion: short sharp noise pop
    duration = 0.14
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0.0
    
    for i in range(num_samples):
        t = i / (num_samples - 1) if num_samples > 1 else 0.0
        t_sec = i / SAMPLE_RATE
        
        freq = 600.0 * (1.0 - t) + 80.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        
        tone = 1.0 if math.sin(phase) > 0 else -1.0
        noise = random.uniform(-1.0, 1.0)
        
        wave = 0.6 * noise + 0.4 * tone
        env = math.pow(max(0.0, 1.0 - t), 1.5)
        samples.append(wave * env * 23000.0)
        
    write_pcm("invader_die.pcm", samples)

def gen_extra_life():
    # Extended play chime: 6 rapid ascending tones
    notes = [523.25, 659.25, 783.99, 1046.50, 1318.51, 1567.98]
    note_dur = 0.09
    num_samples = int(SAMPLE_RATE * note_dur * len(notes))
    samples = []
    
    for note_idx, freq in enumerate(notes):
        note_samples = int(SAMPLE_RATE * note_dur)
        phase = 0.0
        for i in range(note_samples):
            t = i / (note_samples - 1) if note_samples > 1 else 0.0
            phase += 2.0 * math.pi * freq / SAMPLE_RATE
            
            wave = math.sin(phase) + 0.25 * math.sin(2.0 * phase)
            env = math.exp(-3.0 * t)
            samples.append(wave * env * 19000.0)
            
    write_pcm("extra_life.pcm", samples)

def gen_fleet_thump(filename, freq):
    duration = 0.075
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0.0
    
    for i in range(num_samples):
        t = i / (num_samples - 1) if num_samples > 1 else 0.0
        t_sec = i / SAMPLE_RATE
        
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        # Low square wave + sine sub fundamental
        sq = 1.0 if math.sin(phase) > 0 else -1.0
        sin_wave = math.sin(phase)
        wave = 0.6 * sq + 0.4 * sin_wave
        
        env = math.exp(-25.0 * t_sec)
        if t_sec < 0.003:
            env *= (t_sec / 0.003)
            
        samples.append(wave * env * 25000.0)
        
    write_pcm(filename, samples)

def gen_ufo_hit():
    # UFO hit / destroyed: high pitch sweep + explosion burst
    duration = 0.35
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0.0
    
    for i in range(num_samples):
        t = i / (num_samples - 1) if num_samples > 1 else 0.0
        freq = 1600.0 * math.pow(max(0.0, 1.0 - t), 2.0) + 150.0
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        
        tone = math.sin(phase)
        noise = random.uniform(-1.0, 1.0)
        wave = 0.5 * tone + 0.5 * noise
        
        env = math.pow(max(0.0, 1.0 - t), 1.2)
        samples.append(wave * env * 23000.0)
        
    write_pcm("ufo_hit.pcm", samples)

def main():
    print("Generating Space Invaders 32000Hz 16-bit PCM sound files...")
    gen_ufo()
    gen_shot()
    gen_player_die()
    gen_invader_die()
    gen_extra_life()
    gen_fleet_thump("fleet1.pcm", 165.0)
    gen_fleet_thump("fleet2.pcm", 146.8)
    gen_fleet_thump("fleet3.pcm", 130.8)
    gen_fleet_thump("fleet4.pcm", 116.5)
    gen_ufo_hit()
    print("All sound files generated successfully.")

if __name__ == "__main__":
    main()
