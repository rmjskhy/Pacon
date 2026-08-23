# PACON USB media-disk handover

Date: 2026-08-01

The **Disk** icon in the pull-down app launcher hands U2 (`MKDV4GCL-AB` SD
NAND) to the PC as a USB Mass Storage Class device.  The implementation is in
`main/fluid_pendant.c` and uses Espressif's `esp_tinyusb` component.

## Required ownership sequence

1. Normal boot mounts U2 at `/sdnand`; the board can use its files normally.
2. Tapping **Disk** only opens the status page. Serial/JTAG remains connected,
   NAND stays mounted, and no USB mode change happens yet.
3. Turning the on-screen switch **ON** unmounts the firmware FAT VFS and
   initializes the NAND as an MSC block device.
4. The native USB connection re-enumerates as **PACON Media Disk**, so the
   serial port disconnects only after this explicit switch action.
5. Windows/macOS/Linux owns the filesystem while the disk screen says `USB
   DISK ON`.
   Firmware must not read or write `/sdnand` in this state.
6. Safely eject the disk on the computer, then turn the on-screen switch
   **OFF**. It performs a soft reboot and returns to normal Serial/JTAG and
   application mode with U2 mounted at `/sdnand`.

This one-way hand-off deliberately avoids corrupting the FAT filesystem with
simultaneous PC and firmware access.

## Home media files

After entering Disk mode, put media files in the NAND folder `media`.  On the
next normal boot the home carousel scans up to 12 lowercase `.rgb565` files:

- Every frame must be exactly 475 x 466 RGB565 little-endian (442700 bytes).
- A file with exactly one frame is a still image.
- A file with multiple complete frames is a looping animation.
- The frame rate is read from `Nfps` in the filename, for example
  `misaka_475x466_8fps.rgb565`; files without it default to 8 fps.
- The U2 FAT filesystem may expose a copied long filename as an 8.3 alias
  such as `MISAKA~1.RGB`; firmware accepts that alias too, using the default
  8 fps for animations when the original `Nfps` portion is unavailable.

Files are read only while normal firmware owns U2.  Safely eject and tap
**RETURN** before expecting newly copied media to appear.

If `media` is missing, empty, or contains no valid files, the home page uses a
quiet initial-Miku teal background instead of a bundled photograph.

## Type-C orientation limit

The ESP32-S3 native USB data pair has no software-visible plug-orientation
state.  Firmware therefore cannot reliably assign `normal plug = serial` and
`flipped plug = disk`.  Normal boot uses the existing Serial/JTAG path; the
Disk app exposes an explicit switch which changes the same working native-USB
path to MSC. Merely opening the page does not disconnect serial.

## Project settings

- ESP-IDF: v5.4.3
- `main/idf_component.yml`: `espressif/esp_tinyusb` dependency
- `sdkconfig.defaults`: MSC enabled with an 8192-byte FIFO
- Build verified: `build/pacon_fluid_pendant.bin`, 1,360,272 bytes
