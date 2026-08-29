"""Visual QA: normalize source feature anchors, compare with real gesture renders.

This is analysis output only, not an animation asset or implementation source.
"""
from pathlib import Path
import sys
import av
from PIL import Image, ImageDraw, ImageFont, ImageChops

root = Path(__file__).resolve().parents[1]
out = root / 'tools/ouo-preview/experiments/touch-recording'
video = Path(sys.argv[1])
samples = [(65.01, 0, '额头按住'), (68.017, 1, '额头松开后的开心'),
           (97.818, 2, '按左眼'), (91.524, 3, '按右眼'),
           (210.510, 4, '三角嘴竖拉'), (209.526, 5, '三角嘴向侧边拉宽'),
           (211.537, 6, '拉嘴松手')]
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 18)
sheet = Image.new('RGB', (760, 70 + len(samples)*330), '#20232a')
draw = ImageDraw.Draw(sheet)
draw.text((40, 20), 'Android 录像（锚点归一化）', font=font, fill='white')
draw.text((430, 20), '实际预览器手势绘图', font=font, fill='white')
for row, (time, index, label) in enumerate(samples):
    with av.open(str(video)) as recording:
        stream = recording.streams.video[0]
        recording.seek(int(max(0, time-1)/stream.time_base), stream=stream)
        for frame in recording.decode(video=0):
            actual = float(frame.pts*frame.time_base)
            if actual >= time:
                source = frame.to_image().convert('RGB')
                break
    normalized = Image.new('RGB', (466,466), 'black')
    for crop, size, destination in [
        ((378,241,858,661),(96,84),(72,174)),
        ((1543,241,2023,661),(96,84),(298,174)),
        ((780,430,1620,1050),(168,124),(149,210))]:
        tile = source.crop(crop).resize(size, Image.Resampling.LANCZOS)
        layer = Image.new('RGB', (466,466), 'black')
        layer.paste(tile, destination)
        normalized = ImageChops.lighter(normalized, layer)
    rendered = Image.open(out/f'gesture-{index}.png').convert('RGB')
    y = 60 + row*330
    for x, picture in [(40,normalized),(420,rendered)]:
        sheet.paste(picture.resize((290,290),Image.Resampling.LANCZOS),(x,y))
    draw.text((40,y+293),f'{actual:.3f}s · {label}',font=font,fill='#ddd')
    draw.text((420,y+293),label,font=font,fill='#ddd')
sheet.save(out/'android-comparison.png')
print(out/'android-comparison.png')
