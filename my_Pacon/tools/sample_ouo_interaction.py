"""Timestamped, read-only video sampling for touch/response review."""
import argparse
from pathlib import Path
import av
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument('video', type=Path)
parser.add_argument('out', type=Path)
parser.add_argument('--start', type=float, required=True)
parser.add_argument('--end', type=float, required=True)
parser.add_argument('--step', type=float, default=.2)
parser.add_argument('--save-frames', action='store_true')
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 20)
frames = []
with av.open(str(args.video)) as video:
    stream = video.streams.video[0]
    print(f'{stream.width}x{stream.height}, {float(stream.duration*stream.time_base):.3f}s')
    video.seek(max(0, int((args.start - 1) / stream.time_base)), stream=stream)
    next_time = args.start
    for frame in video.decode(video=0):
        time = float(frame.pts * frame.time_base)
        if time < next_time:
            continue
        if time > args.end:
            break
        picture = frame.to_image()
        if args.save_frames:
            picture.save(args.out / f'frame-{time:.3f}.png')
        picture.thumbnail((720, 324))
        frames.append((time, picture))
        next_time += args.step
for start in range(0, len(frames), 16):
    group = frames[start:start+16]
    sheet = Image.new('RGB', (1440, ((len(group)+1)//2)*355), '#222630')
    draw = ImageDraw.Draw(sheet)
    for i, (time, picture) in enumerate(group):
        x, y = i%2*720, i//2*355
        sheet.paste(picture, (x,y))
        draw.text((x+10,y+327), f'{time:.3f}s', font=font, fill='white')
    sheet.save(args.out / f'{args.start:g}-{start//16}.jpg')
print(f'{len(frames)} frames')
