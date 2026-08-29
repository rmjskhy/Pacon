"""Offline reference extraction; never changes the supplied recording."""
import json
import sys
from pathlib import Path

import av
import numpy as np
from PIL import Image, ImageDraw, ImageFont

video = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
container = av.open(str(video))
stream = container.streams.video[0]
print(json.dumps({'size': [stream.width, stream.height], 'fps': str(stream.average_rate),
                  'duration': float(stream.duration * stream.time_base)}, indent=2))
frames = []
next_sample = 0.0
for frame in container.decode(video=0):
    time = float(frame.pts * frame.time_base)
    if time < next_sample:
        continue
    picture = frame.to_image()
    if not frames:
        picture.save(out / 'first.png')
    picture.thumbnail((480, 240))
    frames.append((time, picture.copy()))
    next_sample = time + .96
container.close()
font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 18)
for start in range(0, len(frames), 30):
    page = frames[start:start+30]
    sheet = Image.new('RGB', (1440, ((len(page)+2)//3)*244), '#252830')
    draw = ImageDraw.Draw(sheet)
    for i, (time, picture) in enumerate(page):
        x, y = i % 3 * 480, i // 3 * 244
        sheet.paste(picture, (x, y))
        draw.text((x+8,y+218), f'{time:.3f}s',font=font,fill='white')
    sheet.save(out / f'contact-{start//30}.jpg')
print(f'{len(frames)} samples written to {out}')

# Measure each decoded frame at 600x270, keeping real presentation timestamps
# (the recorder has a variable rate). Exclude recorder overlays at the edges.
measurements=[]
with av.open(str(video)) as recording:
    for frame in recording.decode(video=0):
        time=float(frame.pts*frame.time_base)
        gray=frame.reformat(width=600,height=270,format='gray').to_ndarray()
        row={'time':round(time,4)}
        for key,(x0,y0,x1,y1) in {'eye':(110,65,205,155),'mouth':(255,140,345,225)}.items():
            yy,xx=np.where(gray[y0:y1,x0:x1]>210)
            row[key]={'area':len(xx),'box':[int(xx.min()+x0),int(yy.min()+y0),int(xx.max()+x0),int(yy.max()+y0)] if len(xx) else None}
        measurements.append(row)
(out/'measurements.json').write_text(json.dumps(measurements),encoding='utf8')
print('Whole-second measurements:')
for second in range(0,81):
    row=min(measurements,key=lambda m:abs(m['time']-second))
    print(row)
