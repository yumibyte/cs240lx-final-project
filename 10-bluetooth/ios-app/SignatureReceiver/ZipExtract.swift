import Foundation
import Compression

// Minimal ZIP reader that handles data-descriptor (streaming) zips.
//
// Standard tools (zip, macOS Finder, Python zipfile) often write the
// compressed/uncompressed sizes *after* the data in a "data descriptor"
// record, leaving those fields as zero in the local file header.  Reading
// sizes from the local header therefore gives 0, which is why naive parsers
// return 1 byte.
//
// This implementation reads sizes from the Central Directory at the *end* of
// the file, which is always correct, then goes back to the matching local
// header to locate the payload.  Handles STORE (method 0) and DEFLATE
// (method 8).
enum ZipExtract {

    // Extract the first non-directory file from a ZIP archive.
    static func extractFirst(from data: Data) -> Data? {
        let bytes = [UInt8](data)

        // 1. Find the End of Central Directory (EOCD): signature PK\x05\x06.
        //    Search backward from the end (comment can be up to 64KB).
        guard let eocdOff = findEOCD(bytes) else { return nil }

        // 2. Read EOCD to get Central Directory offset.
        //    EOCD layout (offsets relative to signature start):
        //      +0  signature (4)
        //      +4  disk number (2)
        //      +6  disk with CD start (2)
        //      +8  entries on this disk (2)
        //      +10 total entries (2)
        //      +12 CD size (4)
        //      +16 CD offset (4)
        //      +20 comment length (2)
        guard eocdOff + 22 <= bytes.count else { return nil }
        let cdOffset = Int(le32(bytes, eocdOff + 16))

        // 3. Read the first Central Directory entry: signature PK\x01\x02.
        //    CD entry layout (offsets from signature start):
        //      +0  signature (4)
        //      +10 compression method (2)
        //      +20 compressed size (4)
        //      +24 uncompressed size (4)
        //      +28 filename length (2)
        //      +30 extra length (2)
        //      +32 comment length (2)
        //      +42 local header offset (4)
        //      +46 filename …
        guard cdOffset + 46 <= bytes.count else { return nil }
        guard bytes[cdOffset]     == 0x50 && bytes[cdOffset + 1] == 0x4B &&
              bytes[cdOffset + 2] == 0x01 && bytes[cdOffset + 3] == 0x02 else { return nil }

        let method     = le16(bytes, cdOffset + 10)
        let compSize   = Int(le32(bytes, cdOffset + 20))
        let uncompSize = Int(le32(bytes, cdOffset + 24))
        let lhOffset   = Int(le32(bytes, cdOffset + 42))

        // 4. Jump to the Local File Header to find where the payload starts.
        //    LFH layout: signature(4) + 22 fixed bytes + fnLen(2) + extraLen(2) = 30 bytes
        guard lhOffset + 30 <= bytes.count else { return nil }
        guard bytes[lhOffset]     == 0x50 && bytes[lhOffset + 1] == 0x4B &&
              bytes[lhOffset + 2] == 0x03 && bytes[lhOffset + 3] == 0x04 else { return nil }

        let lfnLen   = Int(le16(bytes, lhOffset + 26))
        let lextraLen = Int(le16(bytes, lhOffset + 28))
        let payloadStart = lhOffset + 30 + lfnLen + lextraLen

        guard payloadStart + compSize <= bytes.count else { return nil }
        let payload = data.subdata(in: payloadStart ..< payloadStart + compSize)

        switch method {
        case 0: // STORE
            return payload

        case 8: // DEFLATE
            return inflate(payload, uncompressedSize: uncompSize)

        default:
            return nil
        }
    }

    // MARK: - Private helpers

    // Find the End of Central Directory signature PK\x05\x06 by scanning
    // backward from the end of the file.
    private static func findEOCD(_ bytes: [UInt8]) -> Int? {
        guard bytes.count >= 22 else { return nil }
        var i = bytes.count - 22
        while i >= 0 {
            if bytes[i]     == 0x50 && bytes[i + 1] == 0x4B &&
               bytes[i + 2] == 0x05 && bytes[i + 3] == 0x06 {
                return i
            }
            i -= 1
        }
        return nil
    }

    // Raw DEFLATE decompression via the system Compression framework.
    // ZIP stores compressed file data as raw DEFLATE (method 8) — no zlib
    // header or adler32 trailer.  Apple's COMPRESSION_ZLIB constant implements
    // the raw DEFLATE algorithm (despite the confusing name), so pass the
    // payload bytes directly with no wrapper.
    private static func inflate(_ data: Data, uncompressedSize: Int) -> Data? {
        guard uncompressedSize > 0 else { return nil }
        var dst = [UInt8](repeating: 0, count: uncompressedSize)
        let result = data.withUnsafeBytes { srcPtr -> Int in
            guard let src = srcPtr.bindMemory(to: UInt8.self).baseAddress else { return 0 }
            return compression_decode_buffer(&dst, uncompressedSize, src, data.count, nil, COMPRESSION_ZLIB)
        }
        guard result > 0 else { return nil }
        return Data(dst.prefix(result))
    }

    private static func le16(_ b: [UInt8], _ i: Int) -> UInt16 {
        guard i + 1 < b.count else { return 0 }
        return UInt16(b[i]) | (UInt16(b[i + 1]) << 8)
    }

    private static func le32(_ b: [UInt8], _ i: Int) -> UInt32 {
        guard i + 3 < b.count else { return 0 }
        return UInt32(b[i])       | (UInt32(b[i + 1]) << 8)
             | (UInt32(b[i + 2]) << 16) | (UInt32(b[i + 3]) << 24)
    }
}
