# Contact sheet of the captured frames: python3 render.py -> screens.png (needs Pillow)
from PIL import Image, ImageDraw
import glob, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
S = 1
files = sorted(glob.glob('frames/*.ppm'))
imgs = [(os.path.basename(f)[:-4], Image.open(f)) for f in files]
cols = 3
rows = (len(imgs) + cols - 1) // cols
W, H = 320 * S + 16, 240 * S + 30
sheet = Image.new('RGB', (cols * W, rows * H), (30, 30, 35))
d = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(imgs):
    x, y = (i % cols) * W + 8, (i // cols) * H + 22
    sheet.paste(im.resize((320 * S, 240 * S), Image.NEAREST), (x, y))
    d.text((x, y - 16), name, fill=(220, 220, 220))
sheet.save('screens.png')
print(len(imgs), 'frames ->', 'screens.png')
