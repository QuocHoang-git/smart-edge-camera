#!/usr/bin/env python3
import struct
import math

def lzw_compress(data, min_code_size):
    clear_code = 1 << min_code_size
    end_of_info = clear_code + 1
    next_code = end_of_info + 1
    code_size = min_code_size + 1
    
    dictionary = {bytes([i]): i for i in range(clear_code)}
    
    codes = [clear_code]
    cur = b''
    
    for byte in data:
        char = bytes([byte])
        cur_char = cur + char
        if cur_char in dictionary:
            cur = cur_char
        else:
            codes.append(dictionary[cur])
            if next_code < 4096:
                dictionary[cur_char] = next_code
                next_code += 1
                if next_code > (1 << code_size) and code_size < 12:
                    code_size += 1
            cur = char
            
    if cur:
        codes.append(dictionary[cur])
    codes.append(end_of_info)
    
    packed = bytearray()
    bit_buf = 0
    bit_cnt = 0
    
    code_size = min_code_size + 1
    next_code = end_of_info + 1
    for code in codes:
        bit_buf |= (code << bit_cnt)
        bit_cnt += code_size
        while bit_cnt >= 8:
            packed.append(bit_buf & 0xFF)
            bit_buf >>= 8
            bit_cnt -= 8
        if code == clear_code:
            code_size = min_code_size + 1
            next_code = end_of_info + 1
        else:
            next_code += 1
            if next_code >= (1 << code_size) and code_size < 12:
                code_size += 1
                
    if bit_cnt > 0:
        packed.append(bit_buf & 0xFF)
        
    output = bytearray([min_code_size])
    idx = 0
    while idx < len(packed):
        chunk = packed[idx:idx+254]
        output.append(len(chunk))
        output.extend(chunk)
        idx += len(chunk)
    output.append(0)
    return bytes(output)

# 8-color palette (3-bit color index)
# 0: Dark background (#111827)
# 1: Window frame / border (#374151)
# 2: Window title bar (#1f2937)
# 3: Video area (#0f172a)
# 4: Subject / Human silhouette (#64748b)
# 5: Bounding box green (#10b981)
# 6: Text white (#f9fafb)
# 7: Accent cyan (#06b6d4)
PALETTE = bytes([
    0x11, 0x18, 0x27,
    0x37, 0x41, 0x51,
    0x1f, 0x29, 0x37,
    0x0a, 0x0f, 0x1d,
    0x64, 0x74, 0x8b,
    0x10, 0xb9, 0x81,
    0xf9, 0xfa, 0xfb,
    0x06, 0xb6, 0xd4,
])

W, H = 640, 320

def draw_rect(buf, x, y, w, h, color):
    for r in range(max(0, y), min(H, y + h)):
        row_offset = r * W
        for c in range(max(0, x), min(W, x + w)):
            buf[row_offset + c] = color

def draw_hollow_rect(buf, x, y, w, h, thickness, color):
    draw_rect(buf, x, y, w, thickness, color)
    draw_rect(buf, x, y + h - thickness, w, thickness, color)
    draw_rect(buf, x, y, thickness, h, color)
    draw_rect(buf, x + w - thickness, y, thickness, h, color)

def draw_circle(buf, cx, cy, radius, color):
    r2 = radius * radius
    for dy in range(-radius, radius + 1):
        y = cy + dy
        if 0 <= y < H:
            row_offset = y * W
            for dx in range(-radius, radius + 1):
                x = cx + dx
                if 0 <= x < W and (dx*dx + dy*dy <= r2):
                    buf[row_offset + x] = color

def render_frame(face_x, face_y):
    buf = bytearray(W * H) # default 0 (Dark BG)
    
    # Outer header banner
    draw_rect(buf, 0, 0, W, 28, 2)
    # Header underline cyan
    draw_rect(buf, 0, 27, W, 1, 7)
    
    # Window 1 (Left: Main 1080p)
    # Title bar
    draw_rect(buf, 10, 36, 305, 20, 2)
    draw_rect(buf, 10, 56, 305, 250, 3)
    draw_hollow_rect(buf, 10, 36, 305, 270, 1, 1)
    
    # Window 2 (Right: Sub 360p + AI)
    draw_rect(buf, 325, 36, 305, 20, 2)
    draw_rect(buf, 325, 56, 305, 250, 3)
    draw_hollow_rect(buf, 325, 36, 305, 270, 1, 1)
    
    # Person silhouette in Left Window (Clean Main Stream - NO Box)
    left_person_x = 10 + 152 + (face_x - 150)
    left_person_y = 56 + 100 + (face_y - 120)
    # Head
    draw_circle(buf, left_person_x, left_person_y, 32, 4)
    # Shoulders
    draw_rect(buf, left_person_x - 50, left_person_y + 40, 100, 100, 4)
    
    # Person silhouette in Right Window (Sub Stream)
    right_person_x = 325 + 152 + (face_x - 150)
    right_person_y = 56 + 100 + (face_y - 120)
    # Head
    draw_circle(buf, right_person_x, right_person_y, 32, 4)
    # Shoulders
    draw_rect(buf, right_person_x - 50, right_person_y + 40, 100, 100, 4)
    
    # Live RetinaFace Green Bounding Box on Window 2 ONLY!
    box_w, box_h = 76, 84
    box_x = right_person_x - box_w // 2
    box_y = right_person_y - box_h // 2
    draw_hollow_rect(buf, box_x, box_y, box_w, box_h, 2, 5) # Green box
    # Small label tag above box
    draw_rect(buf, box_x, box_y - 10, 44, 10, 5)
    
    return bytes(buf)

def main():
    frames = []
    # 12-frame loop smoothly moving face left and right
    num_frames = 12
    for i in range(num_frames):
        angle = (2 * math.pi * i) / num_frames
        fx = int(150 + 35 * math.sin(angle))
        fy = int(120 + 10 * math.cos(angle * 2))
        frames.append(render_frame(fx, fy))
        
    # Write GIF89a
    with open('docs/assets/demo_dual_stream.gif', 'wb') as f:
        f.write(b'GIF89a')
        # Logical Screen Descriptor
        f.write(struct.pack('<HHBBB', W, H, 0x80 | 0x70 | 0x02, 0, 0)) # 8 colors (table size 3 bits)
        f.write(PALETTE)
        
        # Netscape looping extension (infinite loop)
        f.write(b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00')
        
        for frame in frames:
            # Graphic Control Extension (150ms per frame = 15 centiseconds)
            f.write(b'\x21\xf9\x04\x00' + struct.pack('<H', 15) + b'\x00\x00')
            # Image Descriptor
            f.write(b'\x2c' + struct.pack('<HHHHB', 0, 0, W, H, 0))
            # Image Data
            f.write(lzw_compress(frame, 3))
            
        f.write(b';') # Trailer
    print('Generated docs/assets/demo_dual_stream.gif successfully!')

if __name__ == '__main__':
    main()
