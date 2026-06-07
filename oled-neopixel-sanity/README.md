# Summary
Grad cap using
- WS2812B lab
- SSD1306 OLED lab
- TSC2007 resistive touch

## Build and run with parthiv board

```bash
cd labs/final-project/oled-neopixel-sanity
make
```

## Build and run on powerup (flash to SD card)
```bash
cd labs/final-project/oled-neopixel-sanity
make install-sd SD=/Volumes/DISK_IMG
diskutil eject /Volumes/DISK_IMG
```

## Behavior
oled-neopixel-sanity just does bare minimum rn
1. One-time boot init (OLED, touch probe, NeoPixels)
2. inf loop of LED red scan to blue scan, then tests the resistive touch

