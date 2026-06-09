import SwiftUI

// Pannable, zoomable white canvas with every received signature laid out on it.
struct YearbookView: View {
    @EnvironmentObject var ble: BLEManager

    @State private var scale: CGFloat = 1
    @State private var lastScale: CGFloat = 1
    @State private var offset: CGSize = .zero
    @State private var lastOffset: CGSize = .zero

    private var entries: [SignatureEntry] {
        ble.signatures.enumerated().map { SignatureEntry(id: $0.offset, image: $0.element) }
    }

    var body: some View {
        NavigationStack {
            GeometryReader { geo in
                if entries.isEmpty {
                    emptyState
                } else {
                    canvas(in: geo.size)
                }
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("Yearbook")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                if !entries.isEmpty {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button("Reset view") { resetView() }
                    }
                }
            }
        }
    }

    private func canvas(in viewport: CGSize) -> some View {
        let layout = YearbookLayout(entries: entries)
        let canvasSize = layout.canvasSize

        return ZStack {
            Color(.systemGroupedBackground)

            ZStack(alignment: .topLeading) {
                Rectangle()
                    .fill(Color.white)
                    .frame(width: canvasSize.width, height: canvasSize.height)
                    .shadow(color: .black.opacity(0.12), radius: 8, x: 0, y: 4)

                ForEach(entries) { entry in
                    let frame = layout.frame(for: entry.id)
                    let rot = layout.rotation(for: entry.id)
                    Image(uiImage: entry.image)
                        .resizable()
                        .interpolation(.high)
                        .aspectRatio(contentMode: .fit)
                        .frame(width: frame.width, height: frame.height)
                        .rotationEffect(.degrees(rot))
                        .position(x: frame.midX, y: frame.midY)
                }
            }
            .frame(width: canvasSize.width, height: canvasSize.height)
            .scaleEffect(scale)
            .offset(offset)
            .gesture(canvasGestures)
            .onAppear {
                fitCanvasToScreen(viewport: viewport, canvas: canvasSize)
            }
            .onChange(of: entries.count) { _ in
                fitCanvasToScreen(viewport: viewport, canvas: layout.canvasSize)
            }
        }
    }

    private var canvasGestures: some Gesture {
        SimultaneousGesture(
            MagnificationGesture()
                .onChanged { value in
                    scale = max(0.25, min(lastScale * value, 4))
                }
                .onEnded { _ in lastScale = scale },
            DragGesture()
                .onChanged { value in
                    offset = CGSize(
                        width: lastOffset.width + value.translation.width,
                        height: lastOffset.height + value.translation.height
                    )
                }
                .onEnded { _ in lastOffset = offset }
        )
    }

    private func fitCanvasToScreen(viewport: CGSize, canvas: CGSize) {
        let fit = min(viewport.width / canvas.width, viewport.height / canvas.height) * 0.92
        scale = fit
        lastScale = fit
        offset = .zero
        lastOffset = .zero
    }

    private func resetView() {
        withAnimation(.spring(response: 0.35)) {
            scale = 1
            lastScale = 1
            offset = .zero
            lastOffset = .zero
        }
    }

    private var emptyState: some View {
        VStack(spacing: 10) {
            Image(systemName: "book.closed")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No signatures yet")
                .font(.system(.headline, design: .serif))
            Text("Fetch from the Pi on the Receive tab.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// Tight scrapbook-style collage: 3-column masonry, slight tilt per signature.
private struct YearbookLayout {
    let entries: [SignatureEntry]
    let canvasSize: CGSize
    private let frames: [Int: CGRect]
    private let rotations: [Int: Double]

    init(entries: [SignatureEntry]) {
        self.entries = entries
        let margin: CGFloat = 16
        let cols = 3
        let hGap: CGFloat = 10
        let vGap: CGFloat = 6
        let pageW: CGFloat = 680
        let colW = (pageW - margin * 2 - hGap * CGFloat(cols - 1)) / CGFloat(cols)
        let sigAspect: CGFloat = 1024.0 / 512.0
        let slotH = colW / sigAspect

        var computed = [Int: CGRect]()
        var rots = [Int: Double]()
        var colHeights = Array(repeating: margin, count: cols)
        let colLeft: [CGFloat] = (0..<cols).map { i in
            margin + CGFloat(i) * (colW + hGap)
        }

        for entry in entries {
            let col = colHeights.enumerated().min(by: { $0.element < $1.element })!.offset
            let wobble = CGFloat((entry.id * 5) % 7) - 3
            let y = colHeights[col] + slotH / 2
            let x = colLeft[col] + colW / 2 + wobble
            computed[entry.id] = CGRect(
                x: x - colW / 2,
                y: y - slotH / 2,
                width: colW,
                height: slotH
            )
            rots[entry.id] = Double((entry.id * 11) % 15) - 7
            colHeights[col] += slotH + vGap
        }

        let contentH = (colHeights.max() ?? margin) + margin
        self.frames = computed
        self.rotations = rots
        self.canvasSize = CGSize(width: pageW, height: max(contentH, 400))
    }

    func frame(for id: Int) -> CGRect {
        frames[id] ?? .zero
    }

    func rotation(for id: Int) -> Double {
        rotations[id] ?? 0
    }
}

#Preview {
    YearbookView()
        .environmentObject(BLEManager())
}
