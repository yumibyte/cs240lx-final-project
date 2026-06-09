import Foundation

// Mirrors ble_xfer.h on the Pi. Frame = 15-byte header + optional payload,
// all multi-byte fields little-endian.
//
//   magic[4] = 'C','S','2','4'
//   cmd[1]
//   seq[2]
//   total[4]
//   offset[4]     (for GET this is the image id)
//   payload[]
enum CS24 {
    static let magic: [UInt8] = [0x43, 0x53, 0x32, 0x34] // "CS24"
    static let headerLen = 15

    // Nordic UART Service UUIDs.
    static let serviceUUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    static let rxUUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // phone writes here
    static let txUUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // phone notifies here

    // client -> Pi
    static let cmdChunk: UInt8 = 0x01
    static let cmdCommit: UInt8 = 0x02
    static let cmdReset: UInt8 = 0x03
    static let cmdGet: UInt8 = 0x04
    static let cmdList: UInt8 = 0x05

    // Pi -> client
    static let rspAck: UInt8 = 0x80
    static let rspNack: UInt8 = 0x81
    static let rspDone: UInt8 = 0x82
    static let rspPushChunk: UInt8 = 0x83
    static let rspPushDone: UInt8 = 0x84
    static let rspList: UInt8 = 0x85

    static func errorName(_ code: UInt32) -> String {
        switch code {
        case 1: return "BAD_MAGIC"
        case 2: return "BAD_CMD"
        case 3: return "TOO_LARGE"
        case 4: return "NO_TRANSFER"
        case 5: return "INCOMPLETE"
        case 6: return "STORAGE_FULL"
        case 7: return "SHORT"
        case 8: return "NO_IMAGE"
        case 9: return "NOTIFY_OFF"
        default: return "ERR(\(code))"
        }
    }

    // Build a header-only frame (used for GET / LIST / COMMIT / RESET).
    static func makeFrame(cmd: UInt8, seq: UInt16, total: UInt32, offset: UInt32) -> Data {
        var d = Data(magic)
        d.append(cmd)
        d.append(UInt8(seq & 0xff))
        d.append(UInt8(seq >> 8))
        appendLE32(&d, total)
        appendLE32(&d, offset)
        return d
    }

    struct Frame {
        let cmd: UInt8
        let seq: UInt16
        let total: UInt32
        let offset: UInt32
        let payload: Data
    }

    static func parse(_ data: Data) -> Frame? {
        guard data.count >= headerLen else { return nil }
        let b = [UInt8](data)
        guard Array(b[0..<4]) == magic else { return nil }
        let cmd = b[4]
        let seq = UInt16(b[5]) | (UInt16(b[6]) << 8)
        let total = le32(b, 7)
        let offset = le32(b, 11)
        let payload = data.subdata(in: headerLen..<data.count)
        return Frame(cmd: cmd, seq: seq, total: total, offset: offset, payload: payload)
    }

    private static func appendLE32(_ d: inout Data, _ v: UInt32) {
        d.append(UInt8(v & 0xff))
        d.append(UInt8((v >> 8) & 0xff))
        d.append(UInt8((v >> 16) & 0xff))
        d.append(UInt8((v >> 24) & 0xff))
    }

    private static func le32(_ b: [UInt8], _ i: Int) -> UInt32 {
        return UInt32(b[i]) | (UInt32(b[i + 1]) << 8)
            | (UInt32(b[i + 2]) << 16) | (UInt32(b[i + 3]) << 24)
    }
}
