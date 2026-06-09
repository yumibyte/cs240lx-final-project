import SwiftUI
import UIKit
import Photos

// Preset ink colors for signature strokes (black BMP pixels are recolored).
enum SignatureInk: String, CaseIterable, Identifiable {
    case black, navy, crimson, forest, purple, gold

    var id: String { rawValue }

    var label: String {
        switch self {
        case .black: return "Black"
        case .navy: return "Navy"
        case .crimson: return "Red"
        case .forest: return "Green"
        case .purple: return "Purple"
        case .gold: return "Gold"
        }
    }

    var color: Color {
        switch self {
        case .black: return .black
        case .navy: return Color(red: 0.05, green: 0.15, blue: 0.55)
        case .crimson: return Color(red: 0.75, green: 0.08, blue: 0.12)
        case .forest: return Color(red: 0.05, green: 0.42, blue: 0.18)
        case .purple: return Color(red: 0.42, green: 0.12, blue: 0.55)
        case .gold: return Color(red: 0.72, green: 0.52, blue: 0.08)
        }
    }

    var uiColor: UIColor {
        switch self {
        case .black: return .black
        case .navy: return UIColor(red: 0.05, green: 0.15, blue: 0.55, alpha: 1)
        case .crimson: return UIColor(red: 0.75, green: 0.08, blue: 0.12, alpha: 1)
        case .forest: return UIColor(red: 0.05, green: 0.42, blue: 0.18, alpha: 1)
        case .purple: return UIColor(red: 0.42, green: 0.12, blue: 0.55, alpha: 1)
        case .gold: return UIColor(red: 0.72, green: 0.52, blue: 0.08, alpha: 1)
        }
    }

    static let aspectRatio: CGFloat = 1024.0 / 512.0
}

struct YearbookPlacement: Identifiable, Equatable {
    let id: UUID
    var signatureIndex: Int
    var center: CGPoint
    var width: CGFloat
    var rotation: Double
    var ink: SignatureInk

    var height: CGFloat { width / SignatureInk.aspectRatio }
}

@MainActor
final class YearbookStore: ObservableObject {
    @Published var placements: [YearbookPlacement] = []
    @Published var removed: [YearbookPlacement] = []
    @Published var selectedId: UUID?
    @Published var canvasSize = CGSize(width: 680, height: 400)

    var selectedPlacement: YearbookPlacement? {
        guard let id = selectedId else { return nil }
        return placements.first { $0.id == id }
    }

    func resetAll() {
        placements.removeAll()
        removed.removeAll()
        selectedId = nil
        canvasSize = CGSize(width: 680, height: 400)
    }

    func sync(signatures: [UIImage]) {
        let used = Set(placements.map(\.signatureIndex) + removed.map(\.signatureIndex))
        for i in signatures.indices where !used.contains(i) {
            placements.append(makeDefaultPlacement(signatureIndex: i))
        }
        refreshCanvasSize()
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

    private func makeDefaultPlacement(signatureIndex: Int) -> YearbookPlacement {
        let margin: CGFloat = 16
        let cols = 3
        let hGap: CGFloat = 10
        let vGap: CGFloat = 6
        let pageW = canvasSize.width
        let colW = (pageW - margin * 2 - hGap * CGFloat(cols - 1)) / CGFloat(cols)
        let slotH = colW / SignatureInk.aspectRatio

        var colHeights = Array(repeating: margin, count: cols)
        let colLeft: [CGFloat] = (0..<cols).map { i in
            margin + CGFloat(i) * (colW + hGap)
        }

        for p in placements {
            let col = nearestColumn(x: p.center.x, colLeft: colLeft, colW: colW, hGap: hGap)
            colHeights[col] = max(colHeights[col], p.center.y + p.height / 2 + vGap - margin)
        }

        let col = colHeights.enumerated().min(by: { $0.element < $1.element })!.offset
        let wobble = CGFloat((signatureIndex * 5) % 7) - 3
        let y = colHeights[col] + slotH / 2
        let x = colLeft[col] + colW / 2 + wobble
        let rot = Double((signatureIndex * 11) % 15) - 7

        return YearbookPlacement(
            id: UUID(),
            signatureIndex: signatureIndex,
            center: CGPoint(x: x, y: y),
            width: colW,
            rotation: rot,
            ink: .black
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
            let halfH = p.height / 2
            let halfW = p.width / 2
            maxY = max(maxY, p.center.y + halfH)
            maxX = max(maxX, p.center.x + halfW)
        }
        canvasSize = CGSize(
            width: max(680, maxX + margin),
            height: max(400, maxY + margin)
        )
    }
}

extension UIImage {
    func applyingInk(_ ink: SignatureInk) -> UIImage {
        guard ink != .black else { return self }
        let format = UIGraphicsImageRendererFormat()
        format.scale = scale
        let renderer = UIGraphicsImageRenderer(size: size, format: format)
        return renderer.image { ctx in
            let rect = CGRect(origin: .zero, size: size)
            ink.uiColor.setFill()
            ctx.fill(rect)
            draw(in: rect, blendMode: .destinationIn, alpha: 1)
        }
    }
}

enum CollageExporter {
    @MainActor
    static func render(
        placements: [YearbookPlacement],
        signatures: [UIImage],
        canvasSize: CGSize
    ) -> UIImage? {
        let view = CollageSnapshotView(
            placements: placements,
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
    let signatures: [UIImage]
    let canvasSize: CGSize

    var body: some View {
        ZStack {
            Color.white
            ForEach(placements) { p in
                if p.signatureIndex < signatures.count {
                    Image(uiImage: signatures[p.signatureIndex].applyingInk(p.ink))
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
