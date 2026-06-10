# Phone-only signature workflow (SD card -> Pi -> iPhone)

The Pi now reads signature BMPs straight off its SD card and serves them over
BLE. No more Mac-then-phone upload: only the phone connects.

## How it works

- `7-ble-xfer.c` calls `ble_xfer_load_sd_images()` at boot.
- That mounts partition 0 of the SD card (FAT32), reads every `*.BMP` in the
  root directory, and registers each as a BLE image.
- The phone connects, sends `LIST`, then `GET`s each image into the collage.
- If the card has no BMPs, the Pi falls back to the built-in red/blue test
  square so there is always something to fetch.

The SD/FAT32 driver stack lives in `libs/sd` and `libs/fat32`, copied from the
`cs240lx-final-project` submodule (`oled-neopixel-sanity/libs`).

## Putting signatures on the SD card

The capture board writes signatures itself via `sig_save()` as
`SIG000.BMP`, `SIG001.BMP`, ... To test by hand from the Mac:

1. Power down the Pi and move its SD card to the Mac (it mounts as a normal
   FAT32 volume, e.g. `/Volumes/DISK_IMG`).
2. Copy one or more 24-bit BMP files to the **root** of the card. Prefer 8.3
   names so they survive as short names:

   ```bash
   cp tools/signature.bmp /Volumes/DISK_IMG/SIG000.BMP
   sync
   diskutil eject /Volumes/DISK_IMG
   ```

3. Put the card back in the Pi.

Only **`SIG000.BMP` .. `SIG999.BMP`** are loaded (8.3 names, no `~` aliases).
Rename `signature_test.bmp` to e.g. `SIG003.BMP` before copying to the card.
macOS creates duplicate short names like `SIGNA~38.BMP` that are skipped.

## Run it

```bash
cd labs/10-bluetooth/code
make            # flashes 7-ble-xfer over UART and runs it
```

On boot you should see, e.g.:

```
sd: initializing SD card...
sd: root has N entries
sd: loaded SIG000.BMP -> image id=0 (NNNN bytes)
sd: loaded 1 image(s) from SD card
BLE xfer ready. Advertising as 'cs240lx-pi'...
```

Then on the **physical iPhone** (not Simulator):

1. Open the Pi Signatures app.
2. **Connect** -> status shows `Pi has N image(s)`.
3. **Fetch** -> the signatures fill the collage.

## Notes

- Large signatures take a while: a 1024x512 24-bit BMP is ~1.5 MB and streams
  at ~167 bytes per 5 ms notification (~45 s). Smaller BMPs are much faster.
- Up to `XFER_MAX_IMAGES` (8) images are loaded.
- The SD card must be MBR-partitioned with partition 0 = FAT32 (the standard
  Pi boot card already is).
