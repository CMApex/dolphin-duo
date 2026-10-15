"""sheet.py <out.png> <image> [image...]: a labelled contact sheet of screenshots."""
import sys
from PIL import Image, ImageDraw
paths = sys.argv[2:]
ims = [Image.open(p).convert('RGB') for p in paths]
w, h = 400, int(400 * ims[0].height / ims[0].width)
cols = min(3, len(ims))
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * (h + 18)), 'white')
d = ImageDraw.Draw(sheet)
for i, (p, im) in enumerate(zip(paths, ims)):
    x, y = (i % cols) * w, (i // cols) * (h + 18)
    sheet.paste(im.resize((w, h)), (x, y + 18))
    d.text((x + 4, y + 2), p.split('/')[-1], fill='black')
sheet.save(sys.argv[1])
