import Foundation
import CoreBluetooth
import UIKit

// Connects to the Pi ("cs240lx-pi"), enables TX notifications, and pulls
// images using the CS24 GET protocol. Each completed image is decoded into a
// UIImage and appended to `signatures` for the collage.
@MainActor
final class BLEManager: NSObject, ObservableObject {
    enum Status: Equatable {
        case poweredOff
        case scanning
        case connecting
        case connected
        case receiving(Int, Int) // received, total
        case disconnected
        case error(String)
    }

    @Published var status: Status = .disconnected
    @Published var statusText: String = "Idle"
    @Published var imageCountOnPi: Int = 0
    @Published var signatures: [UIImage] = []

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rxChar: CBCharacteristic? // write to Pi
    private var txChar: CBCharacteristic? // notifications from Pi

    private let deviceName = "cs240lx-pi"

    var isSimulator: Bool {
        #if targetEnvironment(simulator)
        return true
        #else
        return false
        #endif
    }

    // Reassembly state for the in-progress incoming image.
    private var rxBuf = Data()
    private var expectedTotal = 0
    private var nextImageToFetch = 0
    private var lastProgressUpdate = 0

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func startScan() {
        if isSimulator {
            status = .error("Simulator cannot use BLE")
            statusText = "Use a physical iPhone — Simulator cannot reach the Pi"
            return
        }
        guard central.state == .poweredOn else {
            statusText = bluetoothStateMessage(central.state)
            return
        }
        status = .scanning
        statusText = "Scanning for \(deviceName)..."
        let nus = CBUUID(string: CS24.serviceUUID)
        central.scanForPeripherals(withServices: [nus], options: nil)
    }

    private func bluetoothStateMessage(_ state: CBManagerState) -> String {
        switch state {
        case .unknown:
            return "Bluetooth starting… try again in a second"
        case .resetting:
            return "Bluetooth resetting…"
        case .unsupported:
            return "Bluetooth not supported on this device"
        case .unauthorized:
            return "Bluetooth permission denied — check Settings"
        case .poweredOff:
            return "Turn on Bluetooth in Settings"
        case .poweredOn:
            return "Bluetooth ready"
        @unknown default:
            return "Bluetooth not ready"
        }
    }

    func disconnect() {
        if let p = peripheral {
            central.cancelPeripheralConnection(p)
        }
    }

    // Ask the Pi how many images it has stored.
    func requestList() {
        guard let rx = rxChar, let p = peripheral else { return }
        let frame = CS24.makeFrame(cmd: CS24.cmdList, seq: 0, total: 0, offset: 0)
        p.writeValue(frame, for: rx, type: .withoutResponse)
        statusText = "Requested image list"
    }

    // Request a single image by id (Pi streams it back over notifications).
    func requestImage(_ id: Int) {
        guard let rx = rxChar, let p = peripheral else { return }
        rxBuf.removeAll()
        expectedTotal = 0
        nextImageToFetch = id
        let frame = CS24.makeFrame(cmd: CS24.cmdGet, seq: 1, total: 0, offset: UInt32(id))
        p.writeValue(frame, for: rx, type: .withoutResponse)
        statusText = "Requesting image \(id)..."
    }

    // Pull every image the Pi reports (simple sequential fetch).
    func fetchAll() {
        guard imageCountOnPi > 0 else {
            requestList()
            return
        }
        signatures.removeAll()
        requestImage(0)
    }

    private func handleFrame(_ f: CS24.Frame) {
        switch f.cmd {
        case CS24.rspList:
            imageCountOnPi = Int(f.total)
            statusText = "Pi has \(imageCountOnPi) image(s)"

        case CS24.rspPushChunk:
            if f.offset == 0 {
                expectedTotal = Int(f.total)
                // Append-only reassembly avoids zeroing a multi-MB buffer on the
                // main thread (that was freezing the phone on large SIG BMPs).
                rxBuf = Data()
                rxBuf.reserveCapacity(expectedTotal)
                lastProgressUpdate = 0
                status = .receiving(0, expectedTotal)
            }
            if Int(f.offset) == rxBuf.count {
                rxBuf.append(f.payload)
            }
            let got = rxBuf.count
            status = .receiving(got, expectedTotal)
            if got - lastProgressUpdate >= 50_000 || got == expectedTotal {
                lastProgressUpdate = got
                statusText = "Receiving \(got)/\(expectedTotal) bytes"
            }

        case CS24.rspPushDone:
            finishImage()

        case CS24.rspNack:
            statusText = "Pi error: \(CS24.errorName(f.offset))"
            status = .error(statusText)

        default:
            break
        }
    }

    private func finishImage() {
        let data = rxBuf.prefix(expectedTotal)

        // Detect zip by PK magic (0x50 0x4B). If it's a zip, extract the
        // inner BMP first; otherwise try to decode the bytes directly as BMP.
        let bmpData: Data
        if data.count > 2 && data[data.startIndex] == 0x50 && data[data.index(after: data.startIndex)] == 0x4B {
            if let extracted = ZipExtract.extractFirst(from: Data(data)) {
                bmpData = extracted
            } else {
                statusText = "Received zip (\(data.count) bytes) but could not extract BMP"
                status = .connected
                return
            }
        } else {
            bmpData = Data(data)
        }

        if let img = SignatureBMP.image(from: bmpData) {
            signatures.append(img)
            statusText = "Received signature #\(signatures.count)"
        } else {
            statusText = "Got \(bmpData.count) bytes but could not decode BMP"
        }
        status = .connected

        // Fetch the next image if there are more on the Pi.
        let next = nextImageToFetch + 1
        if next < imageCountOnPi {
            requestImage(next)
        }
    }
}

extension BLEManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            if isSimulator {
                statusText = "Simulator — plug in a real iPhone to test BLE"
                return
            }
            switch central.state {
            case .poweredOn:
                statusText = "Bluetooth ready — tap Connect"
            case .poweredOff:
                status = .poweredOff
                statusText = bluetoothStateMessage(central.state)
            case .unauthorized:
                status = .error("Bluetooth permission denied")
                statusText = bluetoothStateMessage(central.state)
            default:
                statusText = bluetoothStateMessage(central.state)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any],
                                    rssi RSSI: NSNumber) {
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let piName = advName ?? peripheral.name ?? "cs240lx"
        Task { @MainActor in
            central.stopScan()
            self.peripheral = peripheral
            peripheral.delegate = self
            status = .connecting
            statusText = "Connecting to \(piName)..."
            central.connect(peripheral, options: nil)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            status = .connected
            statusText = "Connected, discovering services..."
            peripheral.discoverServices([CBUUID(string: CS24.serviceUUID)])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                    didDisconnectPeripheral peripheral: CBPeripheral,
                                    error: Error?) {
        Task { @MainActor in
            if expectedTotal > 0 && rxBuf.count < expectedTotal {
                status = .error("Transfer interrupted")
                statusText = "Disconnected after \(rxBuf.count)/\(expectedTotal) bytes — stay on Receive tab and keep the app open while fetching"
            } else {
                status = .disconnected
                statusText = "Disconnected"
            }
            rxChar = nil
            txChar = nil
        }
    }
}

extension BLEManager: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for s in services where s.uuid == CBUUID(string: CS24.serviceUUID) {
            peripheral.discoverCharacteristics(
                [CBUUID(string: CS24.rxUUID), CBUUID(string: CS24.txUUID)], for: s)
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didDiscoverCharacteristicsFor service: CBService,
                                error: Error?) {
        guard let chars = service.characteristics else { return }
        Task { @MainActor in
            for c in chars {
                if c.uuid == CBUUID(string: CS24.rxUUID) {
                    rxChar = c
                } else if c.uuid == CBUUID(string: CS24.txUUID) {
                    txChar = c
                    peripheral.setNotifyValue(true, for: c)
                }
            }
            statusText = "Ready. Tap 'Fetch signatures'."
            // Auto-query the image list once we're set up.
            requestList()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                didUpdateValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        guard let data = characteristic.value else { return }
        Task { @MainActor in
            if let f = CS24.parse(data) {
                handleFrame(f)
            }
        }
    }
}
