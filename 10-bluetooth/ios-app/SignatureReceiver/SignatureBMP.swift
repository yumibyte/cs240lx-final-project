import UIKit

// Decodes the BMPs the Pi serves into UIImages.
//
// The capture board writes 1024x512, 24-bit, top-down BMPs (white background,
// black ink). Most decode fine with UIImage(data:), but some files on the SD
// card have a slightly malformed 14-byte file header (they don't start with
// "BM") even though the pixel payload is intact. So we try the native decoder
// first and fall back to a hand-rolled 24-bit BGR reader that assumes the known
// signature geometry and reads pixels from the end of the buffer.
enum SignatureBMP {
    // Known geometry written by sig_save() on the Pi.
    static let defaultWidth = 1024
    static let defaultHeight = 512

    static func image(from data: Data) -> UIImage? {
        if let img = UIImage(data: data) {
            return img
        }
        return decodeRaw(data)
    }

    private static func le32(_ b: [UInt8], _ i: Int) -> UInt32 {
        guard i + 3 < b.count else { return 0 }
        return UInt32(b[i]) | (UInt32(b[i + 1]) << 8)
            | (UInt32(b[i + 2]) << 16) | (UInt32(b[i + 3]) << 24)
    }

    // Fallback decoder for 24-bit BGR pixel data with a possibly broken header.
    static func decodeRaw(_ data: Data) -> UIImage? {
        let bytes = [UInt8](data)
        guard bytes.count > 54 else { return nil }

        // Trust width/height from the DIB header only if they look sane;
        // otherwise assume the standard signature geometry.
        var width = defaultWidth
        var height = defaultHeight
        var topDown = true

        let w = Int(le32(bytes, 18))
        let hSigned = Int(Int32(bitPattern: le32(bytes, 22)))
        if w > 0 && w <= 4096 { width = w }
        if abs(hSigned) > 0 && abs(hSigned) <= 4096 {
            height = abs(hSigned)
            topDown = hSigned < 0
        }

        let rowBytes = width * 3
        let pad = (4 - (rowBytes % 4)) % 4   // BMP rows are 4-byte aligned
        let stride = rowBytes + pad
        let needed = stride * height
        guard bytes.count >= needed else { return nil }

        // Pixel data sits at the end of the file. This is correct for clean
        // files (offset 54) and for the malformed ones whose header byte counts
        // are wrong but whose payload is still the trailing W*H*3 bytes.
        let off = bytes.count - needed

        var rgba = [UInt8](repeating: 255, count: width * height * 4)
        for y in 0..<height {
            let srcRow = topDown ? y : (height - 1 - y)
            let rowBase = off + srcRow * stride
            for x in 0..<width {
                let si = rowBase + x * 3
                guard si + 2 < bytes.count else { continue }
                let di = (y * width + x) * 4
                rgba[di] = bytes[si + 2]     // R (BMP stores BGR)
                rgba[di + 1] = bytes[si + 1] // G
                rgba[di + 2] = bytes[si]     // B
                rgba[di + 3] = 255
            }
        }

        return imageFromRGBA(&rgba, width: width, height: height)
    }

    private static func imageFromRGBA(_ rgba: inout [UInt8], width: Int, height: Int) -> UIImage? {
        let cs = CGColorSpaceCreateDeviceRGB()
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)
        guard let ctx = CGContext(
            data: &rgba,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: width * 4,
            space: cs,
            bitmapInfo: info.rawValue
        ), let cg = ctx.makeImage() else {
            return nil
        }
        return UIImage(cgImage: cg)
    }
}
