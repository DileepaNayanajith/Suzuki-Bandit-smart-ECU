from PIL import Image
import glob

# Get all PNG files in 'frames' folder
frames = sorted(glob.glob('frames/*.png'))
W, H = 128, 128  # display resolution

# Convert RGB888 to RGB565
def rgb_to_565(r,g,b):
    return ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)

# Open output file
with open('animation.h','w') as f:
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write(f'static const uint16_t anim_frames[{len(frames)}][{W*H}] PROGMEM = {{\n')

    for img_file in frames:
        im = Image.open(img_file).convert('RGB').resize((W,H))
        f.write('  {')
        count = 0
        for r,g,b in im.getdata():
            val = rgb_to_565(r,g,b)
            f.write(f'0x{val:04X},')
            count +=1
            if count % 16 == 0:
                f.write('\n   ')
        f.write('},\n')

    f.write('};\n')
print("animation.h generated successfully!")
