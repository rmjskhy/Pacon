# PACON round-screen media assets

All screen-ready assets in this folder are cropped to **475 x 466**, the SH8601 panel's native resolution. Source files in `D:\my_project\Pacon\图片` are untouched.

## Ready-to-preview images

| File | Subject-preserving crop |
| --- | --- |
| `misaka_lightning_landscape_475x466.png` | Keeps Mikoto's face and extended hand centered in the circular viewing area. |
| `misaka_lightning_portrait_475x466.png` | Keeps the head, torso and lightning inside the screen area; the lower legs are intentionally cropped. |
| `misaka_loop_preview_476x466_8fps.mp4` | 9.875-second loop preview. It is 476 pixels wide solely because H.264 requires an even width; the first 475 columns correspond to the panel. |

## Firmware playback data

`rgb565/` contains uncompressed **RGB565 little-endian** streams for direct panel playback after copying to the MKDV4GCL-AB NAND filesystem.

- One still image: `475 x 466 x 2 = 442700` bytes.
- `misaka_loop_475x466_8fps.rgb565`: 79 frames, 8 FPS, 9.875 seconds, 34973300 bytes.

The raw video is deliberately kept out of the ESP32 program flash. Firmware must read one 442700-byte frame at a time from NAND (preferably in DMA-sized stripes); panel byte order must be verified when the renderer is wired up.
