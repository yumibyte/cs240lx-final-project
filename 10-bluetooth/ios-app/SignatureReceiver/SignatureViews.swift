import SwiftUI

struct SignatureEntry: Identifiable {
    let id: Int
    let image: UIImage

    var label: String { String(format: "No. %03d", id) }
}

// Fullscreen view of one signature: pinch to zoom, drag to pan, double-tap toggle.
struct SignatureDetailView: View {
    let entry: SignatureEntry
    @Environment(\.dismiss) private var dismiss

    @State private var scale: CGFloat = 1
    @State private var lastScale: CGFloat = 1
    @State private var offset: CGSize = .zero
    @State private var lastOffset: CGSize = .zero

    var body: some View {
        NavigationStack {
            ZStack {
                Color.white.ignoresSafeArea()

                Image(uiImage: entry.image)
                    .resizable()
                    .interpolation(.high)
                    .aspectRatio(contentMode: .fit)
                    .scaleEffect(scale)
                    .offset(offset)
                    .gesture(
                        MagnificationGesture()
                            .onChanged { value in
                                scale = max(1, min(lastScale * value, 8))
                            }
                            .onEnded { _ in
                                lastScale = scale
                                if scale <= 1 { resetPan() }
                            }
                    )
                    .simultaneousGesture(
                        DragGesture()
                            .onChanged { value in
                                guard scale > 1 else { return }
                                offset = CGSize(
                                    width: lastOffset.width + value.translation.width,
                                    height: lastOffset.height + value.translation.height
                                )
                            }
                            .onEnded { _ in lastOffset = offset }
                    )
                    .onTapGesture(count: 2) { toggleZoom() }
                    .padding()
            }
            .navigationTitle(entry.label)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }

    private func resetPan() {
        offset = .zero
        lastOffset = .zero
    }

    private func toggleZoom() {
        withAnimation(.spring(response: 0.3)) {
            if scale > 1 {
                scale = 1
                lastScale = 1
                resetPan()
            } else {
                scale = 3
                lastScale = 3
            }
        }
    }
}
