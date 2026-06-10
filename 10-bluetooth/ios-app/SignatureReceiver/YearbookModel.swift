import SwiftUI
import UIKit
import Photos

// Pastel ink presets — recolor stroke pixels only (background is cropped away).
enum SignatureInk: String, CaseIterable, Identifiable {
    case charcoal, sky, rose, mint, lavender, peach

    var id: String { rawValue }

    var label: String {
        switch self {
        case .charcoal: return "Charcoal"
        case .sky: return "Sky"
        case .rose: return "Rose"
        case .mint: return "Mint"
        case .lavender: return "Lavender"
        case .peach: return "Peach"
        }
    }

    var color: Color {
        Color(uiColor: uiColor)
    }

    var uiColor: UIColor {
        switch self {
        case .charcoal: return UIColor(red: 0.28, green: 0.28, blue: 0.32, alpha: 1)
        case .sky: return UIColor(red: 0.55, green: 0.76, blue: 0.94, alpha: 1)
        case .rose: return UIColor(red: 0.94, green: 0.65, blue: 0.72, alpha: 1)
        case .mint: return UIColor(red: 0.62, green: 0.90, blue: 0.78, alpha: 1)
        case .lavender: return UIColor(red: 0.78, green: 0.70, blue: 0.94, alpha: 1)
        case .peach: return UIColor(red: 0.96, green: 0.78, blue: 0.66, alpha: 1)
        }
    }

    var strokeRGBA: (r: UInt8, g: UInt8, b: UInt8) {
        let c = uiColor
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        c.getRed(&r, green: &g, blue: &b, alpha: &a)
        return (UInt8(r * 255), UInt8(g * 255), UInt8(b * 255))
    }
}

struct YearbookPlacement: Identifiable, Equatable {
    let id: UUID
    var signatureIndex: Int
    var center: CGPoint
    var width: CGFloat
    var contentAspectRatio: CGFloat
    var rotation: Double
    var ink: SignatureInk

    var height: CGFloat {
        guard contentAspectRatio > 0 else { return width * 0.5 }
        return width / contentAspectRatio
    }
}

@MainActor
final class YearbookStore: ObservableObject {
    @Published var placements: [YearbookPlacement] = []
    @Published var removed: [YearbookPlacement] = []
    @Published var selectedId: UUID?
    @Published var canvasSize = CGSize(width: 680, height: 400)

    private var aspectCache: [Int: CGFloat] = [:]
    // renderForYearbook scans ~500k pixels; cache so SwiftUI redraws stay cheap.
    private var renderCache: [String: UIImage] = [:]

    var selectedPlacement: YearbookPlacement? {
        guard let id = selectedId else { return nil }
        return placements.first { $0.id == id }
    }

    func resetAll() {
        placements.removeAll()
        removed.removeAll()
        selectedId = nil
        aspectCache.removeAll()
        renderCache.removeAll()
        canvasSize = CGSize(width: 680, height: 400)
    }

    func sync(signatures: [UIImage]) {
        for i in signatures.indices {
            cacheAspectRatio(index: i, signatures: signatures)
        }
        let used = Set(placements.map(\.signatureIndex) + removed.map(\.signatureIndex))
        for i in signatures.indices where !used.contains(i) {
            placements.append(makeDefaultPlacement(signatureIndex: i, signatures: signatures))
        }
        refreshCanvasSize()
    }

    func displayImage(for index: Int, signatures: [UIImage], ink: SignatureInk) -> UIImage? {
        guard index < signatures.count else { return nil }
        let key = renderCacheKey(index: index, ink: ink)
        if let cached = renderCache[key] {
            return cached
        }
        let rendered = signatures[index].renderForYearbook(ink: ink)
        renderCache[key] = rendered
        return rendered
    }

    func aspectRatio(for index: Int, signatures: [UIImage]) -> CGFloat {
        guard index < signatures.count else { return 2 }
        if let cached = aspectCache[index] { return cached }
        cacheAspectRatio(index: index, signatures: signatures)
        return aspectCache[index] ?? 2
    }

    private func cacheAspectRatio(index: Int, signatures: [UIImage]) {
        guard index < signatures.count else { return }
        if let img = displayImage(for: index, signatures: signatures, ink: .charcoal),
           img.size.height > 0 {
            aspectCache[index] = img.size.width / img.size.height
        }
    }

    private func renderCacheKey(index: Int, ink: SignatureInk) -> String {
        "\(index)-\(ink.rawValue)"
    }

    private func invalidateRenderCache(forSignatureIndex index: Int) {
        let prefix = "\(index)-"
        for key in renderCache.keys where key.hasPrefix(prefix) {
            renderCache.removeValue(forKey: key)
        }
        aspectCache.removeValue(forKey: index)
    }

    func select(_ id: UUID?) {
        selectedId = id
    }

    func updatePlacement(_ id: UUID, _ edit: (inout YearbookPlacement) -> Void) {
        guard let idx = placements.firstIndex(where: { $0.id == id }) else { return }
        edit(&placements[idx])
        refreshCanvasSize()
    }

    func rotateSelected(by degrees: Double) {
        guard let id = selectedId else { return }
        updatePlacement(id) { $0.rotation += degrees }
    }

    func setInk(_ ink: SignatureInk) {
        guard let id = selectedId else { return }
        updatePlacement(id) { $0.ink = ink }
    }

    func deleteSelected() {
        guard let id = selectedId, let idx = placements.firstIndex(where: { $0.id == id }) else { return }
        let item = placements.remove(at: idx)
        removed.append(item)
        selectedId = nil
        refreshCanvasSize()
    }

    func restore(_ item: YearbookPlacement) {
        removed.removeAll { $0.id == item.id }
        var restored = item
        restored.center = CGPoint(x: canvasSize.width / 2, y: canvasSize.height / 2)
        placements.append(restored)
        selectedId = restored.id
        refreshCanvasSize()
    }

    private func makeDefaultPlacement(signatureIndex: Int, signatures: [UIImage]) -> YearbookPlacement {
        let margin: CGFloat = 16
        let cols = 3
        let hGap: CGFloat = 8
        let vGap: CGFloat = 4
        let pageW = canvasSize.width
        let colW = (pageW - margin * 2 - hGap * CGFloat(cols - 1)) / CGFloat(cols)
        let aspect = aspectRatio(for: signatureIndex, signatures: signatures)
        let slotH = colW / aspect

        var colHeights = Array(repeating: margin, count: cols)
        let colLeft: [CGFloat] = (0..<cols).map { i in
            margin + CGFloat(i) * (colW + hGap)
        }

        for p in placements {
            let col = nearestColumn(x: p.center.x, colLeft: colLeft, colW: colW, hGap: hGap)
            colHeights[col] = max(colHeights[col], p.center.y + p.height / 2 + vGap - margin)
        }

        let col = colHeights.enumerated().min(by: { $0.element < $1.element })!.offset
        let wobble = CGFloat((signatureIndex * 5) % 5) - 2
        let y = colHeights[col] + slotH / 2
        let x = colLeft[col] + colW / 2 + wobble
        let rot = Double((signatureIndex * 11) % 13) - 6

        return YearbookPlacement(
            id: UUID(),
            signatureIndex: signatureIndex,
            center: CGPoint(x: x, y: y),
            width: colW,
            contentAspectRatio: aspect,
            rotation: rot,
            ink: .charcoal
        )
    }

    private func nearestColumn(x: CGFloat, colLeft: [CGFloat], colW: CGFloat, hGap: CGFloat) -> Int {
        let centers = colLeft.map { $0 + colW / 2 }
        return centers.enumerated().min(by: { abs($0.element - x) < abs($1.element - x) })?.offset ?? 0
    }

    private func refreshCanvasSize() {
        let margin: CGFloat = 16
        guard !placements.isEmpty else {
            canvasSize = CGSize(width: 680, height: 400)
            return
        }
        var maxY: CGFloat = margin
        var maxX: CGFloat = margin
        for p in placements {
            maxY = max(maxY, p.center.y + p.height / 2)
            maxX = max(maxX, p.center.x + p.width / 2)
        }
        canvasSize = CGSize(
            width: max(680, maxX + margin),
            height: max(400, maxY + margin)
        )
    }
}

// Crop white margins, make paper transparent, recolor only dark stroke pixels.
extension UIImage {
    private static let inkThreshold: UInt8 = 248

    func renderForYearbook(ink: SignatureInk, padding: Int = 8) -> UIImage {
        guard let pixels = rgbaPixels() else { return self }
        let width = pixels.width
        let height = pixels.height
        let data = pixels.data
        let stride = pixels.bytesPerRow
        let stroke = ink.strokeRGBA
        let threshold = Self.inkThreshold

        var minX = width, minY = height, maxX = -1, maxY = -1
        for y in 0..<height {
            for x in 0..<width {
                let i = y * stride + x * 4
                if Self.isInk(data[i], data[i + 1], data[i + 2], threshold: threshold) {
                    minX = min(minX, x)
                    maxX = max(maxX, x)
                    minY = min(minY, y)
                    maxY = max(maxY, y)
                }
            }
        }

        guard maxX >= minX, maxY >= minY else { return self }

        minX = max(0, minX - padding)
        minY = max(0, minY - padding)
        maxX = min(width - 1, maxX + padding)
        maxY = min(height - 1, maxY + padding)

        let cropW = maxX - minX + 1
        let cropH = maxY - minY + 1
        var out = [UInt8](repeating: 0, count: cropW * cropH * 4)

        for y in 0..<cropH {
            for x in 0..<cropW {
                let si = (minY + y) * stride + (minX + x) * 4
                let di = (y * cropW + x) * 4
                if Self.isInk(data[si], data[si + 1], data[si + 2], threshold: threshold) {
                    out[di] = stroke.r
                    out[di + 1] = stroke.g
                    out[di + 2] = stroke.b
                    out[di + 3] = 255
                }
            }
        }

        return Self.imageFromRGBA(out, width: cropW, height: cropH, scale: scale) ?? self
    }

    private static func isInk(_ r: UInt8, _ g: UInt8, _ b: UInt8, threshold: UInt8) -> Bool {
        UInt16(r) + UInt16(g) + UInt16(b) < UInt16(threshold) * 3
    }

    private func rgbaPixels() -> (data: [UInt8], width: Int, height: Int, bytesPerRow: Int)? {
        guard let cg = cgImage else { return nil }
        let width = cg.width
        let height = cg.height
        let bytesPerRow = width * 4
        var data = [UInt8](repeating: 0, count: height * bytesPerRow)
        guard let ctx = CGContext(
            data: &data,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { return nil }

        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: width, height: height))
        return (data, width, height, bytesPerRow)
    }

    private static func imageFromRGBA(
        _ rgba: [UInt8],
        width: Int,
        height: Int,
        scale: CGFloat
    ) -> UIImage? {
        let bytesPerRow = width * 4
        guard rgba.count >= height * bytesPerRow else { return nil }
        guard let provider = CGDataProvider(data: Data(rgba) as CFData) else { return nil }
        guard let cg = CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        ) else { return nil }
        return UIImage(cgImage: cg, scale: scale, orientation: .up)
    }
}

enum CollageExporter {
    @MainActor
    static func render(
        placements: [YearbookPlacement],
        store: YearbookStore,
        signatures: [UIImage],
        canvasSize: CGSize
    ) -> UIImage? {
        let view = CollageSnapshotView(
            placements: placements,
            store: store,
            signatures: signatures,
            canvasSize: canvasSize
        )
        let renderer = ImageRenderer(content: view)
        renderer.scale = 2
        return renderer.uiImage
    }

    static func saveToPhotos(_ image: UIImage) async -> Result<Void, Error> {
        await withCheckedContinuation { continuation in
            PHPhotoLibrary.requestAuthorization(for: .addOnly) { status in
                guard status == .authorized || status == .limited else {
                    continuation.resume(returning: .failure(ExportError.photoAccessDenied))
                    return
                }
                PHPhotoLibrary.shared().performChanges({
                    PHAssetChangeRequest.creationRequestForAsset(from: image)
                }, completionHandler: { ok, error in
                    if let error {
                        continuation.resume(returning: .failure(error))
                    } else if ok {
                        continuation.resume(returning: .success(()))
                    } else {
                        continuation.resume(returning: .failure(ExportError.saveFailed))
                    }
                })
            }
        }
    }

    enum ExportError: LocalizedError {
        case photoAccessDenied
        case saveFailed

        var errorDescription: String? {
            switch self {
            case .photoAccessDenied:
                return "Photo library access denied. Enable in Settings."
            case .saveFailed:
                return "Could not save the collage."
            }
        }
    }
}

struct CollageSnapshotView: View {
    let placements: [YearbookPlacement]
    let store: YearbookStore
    let signatures: [UIImage]
    let canvasSize: CGSize

    var body: some View {
        ZStack {
            Color.white
            ForEach(placements) { p in
                if let img = store.displayImage(for: p.signatureIndex, signatures: signatures, ink: p.ink) {
                    Image(uiImage: img)
                        .resizable()
                        .interpolation(.high)
                        .aspectRatio(contentMode: .fit)
                        .frame(width: p.width, height: p.height)
                        .rotationEffect(.degrees(p.rotation))
                        .position(x: p.center.x, y: p.center.y)
                }
            }
        }
        .frame(width: canvasSize.width, height: canvasSize.height)
    }
}
