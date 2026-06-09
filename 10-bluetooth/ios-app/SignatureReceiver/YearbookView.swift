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
                // Large white page
                Rectangle()
                    .fill(Color.white)
                    .frame(width: canvasSize.width, height: canvasSize.height)
                    .shadow(color: .black.opacity(0.12), radius: 8, x: 0, y: 4)

                ForEach(entries) { entry in
                    let frame = layout.frame(for: entry.id)
                    Image(uiImage: entry.image)
                        .resizable()
                        .interpolation(.high)
                        .aspectRatio(contentMode: .fit)
                        .frame(width: frame.width, height: frame.height)
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

// Lays signatures out on a large white page in a loose yearbook collage.
private struct YearbookLayout {
    let entries: [SignatureEntry]
    let canvasSize: CGSize
    private let frames: [Int: CGRect]

    init(entries: [SignatureEntry]) {
        self.entries = entries
        let margin: CGFloat = 48
        let colWidth: CGFloat = 320
        let colGap: CGFloat = 40
        let rowGap: CGFloat = 36
        let thumbHeight: CGFloat = 140

        var computed = [Int: CGRect]()
        var colHeights = [CGFloat(0), CGFloat(0)]
        let colX = [margin + colWidth / 2, margin + colWidth + colGap + colWidth / 2]

        for entry in entries {
            let col = colHeights[0] <= colHeights[1] ? 0 : 1
            let y = margin + colHeights[col] + thumbHeight / 2
            let x = colX[col]
            computed[entry.id] = CGRect(
                x: x - colWidth / 2,
                y: y - thumbHeight / 2,
                width: colWidth,
                height: thumbHeight
            )
            colHeights[col] += thumbHeight + rowGap
        }

        let contentH = max(colHeights[0], colHeights[1]) + margin
        self.frames = computed
        self.canvasSize = CGSize(
            width: margin * 2 + colWidth * 2 + colGap,
            height: max(contentH, 600)
        )
    }

    func frame(for id: Int) -> CGRect {
        frames[id] ?? .zero
    }
}

#Preview {
    YearbookView()
        .environmentObject(BLEManager())
}
