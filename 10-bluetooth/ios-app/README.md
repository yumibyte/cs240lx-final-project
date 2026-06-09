# Pi Signatures — iOS app

A small SwiftUI + CoreBluetooth app that connects to the CS240LX Pi
(`cs240lx-pi`), pulls BMP signatures over the CS24 BLE protocol, and renders
them in a collage grid. No TestFlight — sideload directly with a free Apple ID.

## What it does

1. Scans for and connects to the Pi.
2. Enables TX notifications on the Nordic UART Service.
3. Queries how many images the Pi has (`LIST`), then `GET`s each one.
4. Reassembles the `PUSH_CHUNK` stream and decodes each BMP into a `UIImage`.
5. Shows every received signature in a collage.

The protocol in `CS24Protocol.swift` matches `code/ble_xfer.h` byte-for-byte.

## Tabs

- **Receive** — connect to the Pi, fetch images, watch progress.
- **Yearbook** — every signature framed on a white page, yearbook style.
  Switch between 1/2/3 columns, and tap any signature to view it large with
  pinch-to-zoom and drag (double-tap to toggle zoom). When no Pi is connected
  (e.g. in the Simulator) it previews the bundled sample signatures.

## BMP decoding

`SignatureBMP.swift` decodes the Pi's 1024×512, 24-bit, top-down BMPs. It tries
`UIImage(data:)` first, then falls back to a hand-rolled 24-bit BGR reader that
assumes the known signature geometry and reads pixels from the end of the
buffer — so it still renders files whose 14-byte header is slightly malformed
(some SD-card examples don't start with `BM`).

## Files

```
ios-app/
  project.yml                     # XcodeGen spec (turnkey project generation)
  SignatureReceiver/
    SignatureReceiverApp.swift    # @main entry + RootView TabView
    ContentView.swift             # Receive tab: connect, fetch, collage
    YearbookView.swift            # Yearbook tab: framed grid + zoomable detail
    BLEManager.swift              # CoreBluetooth + reassembly
    CS24Protocol.swift            # frame format + UUIDs (mirrors the Pi)
    SignatureBMP.swift            # robust BMP -> UIImage decoder
    SampleSignatures.swift        # bundled sample loader (preview)
    SampleSignatures/SIG00x.bmp   # example signatures from the final-project repo
    Info.plist                    # Bluetooth usage string
```

## Build it — easiest path (XcodeGen)

```bash
brew install xcodegen
cd labs/10-bluetooth/ios-app
xcodegen generate
open SignatureReceiver.xcodeproj
```

## Build it — manual path (no XcodeGen)

1. Xcode → **File ▸ New ▸ Project ▸ iOS App**.
   - Product Name: `SignatureReceiver`
   - Interface: **SwiftUI**, Language: **Swift**
2. Delete the auto-created `ContentView.swift` and the `...App.swift`.
3. Drag the four `.swift` files from `SignatureReceiver/` into the project
   (check **Copy items if needed**).
4. Select the target ▸ **Info** tab ▸ add a row:
   - Key: `Privacy - Bluetooth Always Usage Description`
   - Value: `Connects to the CS240LX Raspberry Pi over Bluetooth...`

## Run on your iPhone (free Apple ID, no TestFlight)

### Fix: "A build only device cannot be used to run this target"

This means Xcode can **compile** but not **install** on the selected destination.
Do all of these:

1. **Pick a real destination** (top toolbar, next to the Play button):
   - Good: your iPhone's name, e.g. `Aditri's iPhone`
   - **iOS Simulator** (e.g. iPhone 17): builds and runs the UI, but **cannot scan or connect to the Pi** — CoreBluetooth is not available for real peripherals in the simulator. You will see "Bluetooth not ready" / the orange banner.
   - Bad: `Any iOS Device (arm64)` — always build-only
   - Bad: `My Mac` — won't run a phone-only target

2. **Set up signing** (required for a physical iPhone):
   - Click the blue **SignatureReceiver** project in the left sidebar
   - Select the **SignatureReceiver** target ▸ **Signing & Capabilities**
   - Check **Automatically manage signing**
   - **Team**: choose your Apple ID (shows as "Personal Team" or your name)
   - If you see a signing error, change **Bundle Identifier** to something unique:
     `edu.stanford.cs240lx.SignatureReceiver.yourname`

3. **Prepare the iPhone**:
   - Unlock the phone, plug in USB, tap **Trust** on the phone
   - iOS 16+: **Settings ▸ Privacy & Security ▸ Developer Mode** → ON (reboot)

4. Press **Run** (Play). First launch: on the phone,
   **Settings ▸ General ▸ VPN & Device Management** → trust your developer cert.

5. Free certs expire after 7 days — re-run from Xcode to refresh.

### Normal run steps

1. Plug in your iPhone, select it as the run destination (not "Any iOS Device").
2. Complete signing steps above.
3. Press **Run**.

## Using it

1. Flash the Pi with `7-ble-xfer.c` (it stores a red test BMP as image 0):
   ```bash
   cd ../code && make
   ```
2. Make sure **no other device** (your Mac script, nRF Connect) is connected to
   the Pi — BLE allows one central at a time.
3. In the app: tap **Connect** → it auto-queries the list → tap **Fetch**.
4. The red test square should appear in the collage.

## Notes / troubleshooting

- **Stuck on "Scanning"**: the Pi must be advertising (`make` shows
  `BLE xfer ready...`). Power-cycle the Pi if needed.
- **Connects but no images**: confirm the Pi printed
  `xfer: test BMP stored as image id=0`.
- **"NOTIFY_OFF"**: the app enables notifications automatically; if you see this,
  disconnect and reconnect.
- **BMP won't render**: iOS decodes standard BMP via `UIImage(data:)`. The Pi's
  test image is a 24-bit bottom-up BMP, which is supported.
- When the Pi later captures real signatures (touch/OLED), they become images
  1, 2, ... and the app fetches them all in one pass.
