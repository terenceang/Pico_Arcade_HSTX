import os
import struct

def analyze_si_vram(filepath):
    print(f"\n================ SI VRAM ANALYSIS: {filepath} ================")
    with open(filepath, "rb") as f:
        data = f.read()
    
    print(f"File size: {len(data)} bytes (expected 7168)")
    non_zero = sum(1 for b in data if b != 0)
    print(f"Non-zero bytes: {non_zero} / {len(data)} ({non_zero*100/len(data):.1f}%)")

    # Space Invaders VRAM is 224 columns x 32 bytes (256 pixels high)
    # Let's decode the screen to ASCII text (256 wide x 224 high, or 224 wide x 256 high)
    # In native portrait: X is 0..223, Y is 0..255 (top to bottom)
    # VRAM address = X * 32 + (Y // 8)
    # bit = Y % 8
    # Let's render an ASCII art preview downsampled (e.g. 64x64 or 32x32)
    grid = []
    for y in range(0, 256, 4):
        row = ""
        for x in range(0, 224, 4):
            # Sample 4x4 block
            count = 0
            for dy in range(4):
                for dx in range(4):
                    px = x + dx
                    py = y + dy
                    byte_idx = px * 32 + (py // 8)
                    bit_idx = py % 8
                    if byte_idx < len(data) and (data[byte_idx] & (1 << bit_idx)):
                        count += 1
            row += " " if count == 0 else ("." if count < 6 else ("#" if count < 12 else "@"))
        grid.append(row)
    
    # Print preview
    print("\nASCII Preview (224x256 sampled 4x):")
    for r in grid:
        if any(c != ' ' for c in r):
            print(r)

def analyze_pacman_vram(vram_path, colorram_path, spriteram_path, spritecoords_path):
    print(f"\n================ PAC-MAN VRAM ANALYSIS: {vram_path} ================")
    with open(vram_path, "rb") as f:
        vram = f.read()
    with open(colorram_path, "rb") as f:
        cram = f.read()
    with open(spriteram_path, "rb") as f:
        sram = f.read()
    with open(spritecoords_path, "rb") as f:
        coords = f.read()
    
    # Tilemap is 28 columns x 36 rows
    # Tile encoding: Pac-Man uses standard Namco ASCII-ish character set in tile ROM
    # 0x00-0x1F: digits/special, 0x40-0x5F: uppercase letters etc.
    print("Tilemap text preview (36 rows x 28 columns):")
    for r in range(36):
        row_str = ""
        for c in range(28):
            if r < 2:
                addr = 0x3C0 + r * 32 + (c + 2)
            elif r >= 34:
                addr = 0x000 + (r - 34) * 32 + (c + 2)
            else:
                addr = 0x040 + c * 32 + (r - 2)
            
            tile = vram[addr] if addr < len(vram) else 0
            pal = cram[addr] & 0x1F if addr < len(cram) else 0
            
            # Map tile index to char if printable
            # In Pacman ROM:
            # 0x00: space
            # 0x01-0x0A: numbers 0-9? Or letters?
            # Let's see raw tile hex or simple translation
            if tile == 0x40:
                ch = ' '
            elif 0x00 <= tile <= 0x09:
                ch = str(tile)
            elif 0x01 <= tile <= 0x1A: # or letters
                ch = chr(ord('A') + tile - 1)
            elif 0x20 <= tile <= 0x7E:
                ch = chr(tile)
            else:
                ch = f"[{tile:02X}]" if tile != 0 else " "
            row_str += ch if len(ch) == 1 else "?"
        if row_str.strip():
            print(f"Row {r:02d}: {row_str}")

    print("\nSprites:")
    for s in range(8):
        y = coords[s * 2] if s * 2 < len(coords) else 0
        x = coords[s * 2 + 1] if s * 2 + 1 < len(coords) else 0
        a0 = sram[s * 2] if s * 2 < len(sram) else 0
        a1 = sram[s * 2 + 1] if s * 2 + 1 < len(sram) else 0
        sprite_idx = a0 >> 2
        flip_x = bool(a0 & 1)
        flip_y = bool(a0 & 2)
        pal = a1 & 0x1F
        print(f"  Sprite {s}: X={x}, Y={y}, Tile={sprite_idx} (0x{sprite_idx:02X}), Pal={pal}, FlipX={flip_x}, FlipY={flip_y}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        analyze_si_vram(sys.argv[1])
    else:
        print("Usage: python analyze_vram.py <vram_file.bin>")

