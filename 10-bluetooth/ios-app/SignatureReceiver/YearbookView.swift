import SwiftUI

struct YearbookView: View {
    @EnvironmentObject var ble: BLEManager
    @EnvironmentObject var yearbook: YearbookStore

    @State private var canvasScale: CGFloat = 1
    @State private var lastCanvasScale: CGFloat = 1
    @State private var canvasOffset: CGSize = .zero
    @State private var lastCanvasOffset: CGSize = .zero

    @State private var showRemovedSheet = false
    @State private var saveMessage: String?
    @State private var showSaveAlert = false
    @State private var isSaving = false

    var body: some View {
        NavigationStack {
            GeometryReader { geo in
                if yearbook.placements.isEmpty && yearbook.removed.isEmpty && ble.signatures.isEmpty {
                    emptyState
                } else {
                    editorCanvas(in: geo.size)
                }
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("Yearbook")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { toolbarContent }
            .safeAreaInset(edge: .bottom) {
                if yearbook.selectedId != nil {
                    editBar
                }
            }
            .sheet(isPresented: $showRemovedSheet) {
                removedSignaturesSheet
            }
            .alert("Yearbook", isPresented: $showSaveAlert) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(saveMessage ?? "")
            }
            .onChange(of: ble.signatures.count) { _, new in
                if new == 0 {
                    yearbook.resetAll()
                } else {
                    yearbook.sync(signatures: ble.signatures)
                }
            }
            .onAppear {
                yearbook.sync(signatures: ble.signatures)
            }
        }
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItem(placement: .topBarLeading) {
            if !yearbook.removed.isEmpty {
                Button {
                    showRemovedSheet = true
                } label: {
                    Label("Add back", systemImage: "plus.circle")
                }
            }
        }
        ToolbarItemGroup(placement: .topBarTrailing) {
            if !yearbook.placements.isEmpty {
                Button {
                    Task { await saveCollage() }
                } label: {
                    if isSaving {
                        ProgressView()
                    } else {
                        Label("Save", systemImage: "square.and.arrow.down")
                    }
                }
                .disabled(isSaving)
            }
            if !yearbook.placements.isEmpty {
                Button("Reset view") { resetCanvasView() }
            }
        }
    }

    private func editorCanvas(in viewport: CGSize) -> some View {
        let size = yearbook.canvasSize

        return ZStack {
            Color(.systemGroupedBackground)

            ZStack(alignment: .topLeading) {
                Rectangle()
                    .fill(Color.white)
                    .frame(width: size.width, height: size.height)
                    .shadow(color: .black.opacity(0.12), radius: 8, x: 0, y: 4)
                    .onTapGesture { yearbook.select(nil) }

                ForEach(yearbook.placements) { placement in
                    if placement.signatureIndex < ble.signatures.count {
                        placementView(placement)
                    }
                }
            }
            .frame(width: size.width, height: size.height)
            .scaleEffect(canvasScale)
            .offset(canvasOffset)
            .gesture(canvasPanGesture)
            .simultaneousGesture(canvasPinchGesture)
            .onAppear {
                fitCanvasToScreen(viewport: viewport, canvas: size)
            }
            .onChange(of: yearbook.placements.count) { _, _ in
                fitCanvasToScreen(viewport: viewport, canvas: yearbook.canvasSize)
            }
        }
    }

    private func placementView(_ placement: YearbookPlacement) -> some View {
        let isSelected = yearbook.selectedId == placement.id
        let image = ble.signatures[placement.signatureIndex].applyingInk(placement.ink)

        return YearbookPlacementView(
            image: image,
            placement: placement,
            isSelected: isSelected,
            canvasScale: canvasScale,
            onSelect: { yearbook.select(placement.id) },
            onMove: { newCenter in
                yearbook.updatePlacement(placement.id) { $0.center = newCenter }
            },
            onResize: { newWidth in
                yearbook.updatePlacement(placement.id) {
                    $0.width = min(480, max(48, newWidth))
                }
            },
            onSetRotation: { degrees in
                yearbook.updatePlacement(placement.id) { $0.rotation = degrees }
            }
        )
    }

    private var canvasPanGesture: some Gesture {
        DragGesture()
            .onChanged { value in
                guard yearbook.selectedId == nil else { return }
                canvasOffset = CGSize(
                    width: lastCanvasOffset.width + value.translation.width,
                    height: lastCanvasOffset.height + value.translation.height
                )
            }
            .onEnded { _ in
                lastCanvasOffset = canvasOffset
            }
    }

    private var canvasPinchGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                guard yearbook.selectedId == nil else { return }
                canvasScale = max(0.25, min(lastCanvasScale * value, 4))
            }
            .onEnded { _ in
                lastCanvasScale = canvasScale
            }
    }

    private var editBar: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                ForEach(SignatureInk.allCases) { ink in
                    let selected = yearbook.selectedPlacement?.ink == ink
                    Button {
                        yearbook.setInk(ink)
                    } label: {
                        Circle()
                            .fill(ink.color)
                            .frame(width: 28, height: 28)
                            .overlay(
                                Circle()
                                    .strokeBorder(selected ? Color.accentColor : Color.clear, lineWidth: 3)
                            )
                    }
                    .accessibilityLabel(ink.label)
                }
            }

            HStack(spacing: 20) {
                Button {
                    yearbook.rotateSelected(by: -15)
                } label: {
                    Label("Rotate left", systemImage: "rotate.left")
                }

                Button {
                    yearbook.rotateSelected(by: 15)
                } label: {
                    Label("Rotate right", systemImage: "rotate.right")
                }

                Spacer()

                Button(role: .destructive) {
                    yearbook.deleteSelected()
                } label: {
                    Label("Remove", systemImage: "trash")
                }
            }
            .font(.subheadline)
        }
        .padding(.horizontal)
        .padding(.vertical, 10)
        .background(.bar)
    }

    private var removedSignaturesSheet: some View {
        NavigationStack {
            List(yearbook.removed) { item in
                Button {
                    yearbook.restore(item)
                    showRemovedSheet = false
                } label: {
                    HStack {
                        if item.signatureIndex < ble.signatures.count {
                            Image(uiImage: ble.signatures[item.signatureIndex].applyingInk(item.ink))
                                .resizable()
                                .aspectRatio(contentMode: .fit)
                                .frame(height: 44)
                        }
                        Text("Signature \(item.signatureIndex + 1)")
                        Spacer()
                        Image(systemName: "plus.circle.fill")
                            .foregroundStyle(.accent)
                    }
                }
            }
            .navigationTitle("Add signature back")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { showRemovedSheet = false }
                }
            }
        }
    }

    private func fitCanvasToScreen(viewport: CGSize, canvas: CGSize) {
        let fit = min(viewport.width / canvas.width, viewport.height / canvas.height) * 0.88
        canvasScale = fit
        lastCanvasScale = fit
        canvasOffset = .zero
        lastCanvasOffset = .zero
    }

    private func resetCanvasView() {
        withAnimation(.spring(response: 0.35)) {
            canvasScale = 1
            lastCanvasScale = 1
            canvasOffset = .zero
            lastCanvasOffset = .zero
            yearbook.select(nil)
        }
    }

    private func saveCollage() async {
        isSaving = true
        defer { isSaving = false }
        guard let image = CollageExporter.render(
            placements: yearbook.placements,
            signatures: ble.signatures,
            canvasSize: yearbook.canvasSize
        ) else {
            saveMessage = "Could not render the collage."
            showSaveAlert = true
            return
        }
        let result = await CollageExporter.saveToPhotos(image)
        switch result {
        case .success:
            saveMessage = "Saved to your Photos library."
        case .failure(let error):
            saveMessage = error.localizedDescription
        }
        showSaveAlert = true
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

// One signature on the canvas — tap to select, drag to move, pinch to resize/rotate.
private struct YearbookPlacementView: View {
    let image: UIImage
    let placement: YearbookPlacement
    let isSelected: Bool
    let canvasScale: CGFloat
    let onSelect: () -> Void
    let onMove: (CGPoint) -> Void
    let onResize: (CGFloat) -> Void
    let onSetRotation: (Double) -> Void

    @State private var dragOrigin: CGPoint?
    @State private var resizeOrigin: CGFloat?
    @State private var rotateStart: Double?

    var body: some View {
        Image(uiImage: image)
            .resizable()
            .interpolation(.high)
            .aspectRatio(contentMode: .fit)
            .frame(width: placement.width, height: placement.height)
            .rotationEffect(.degrees(placement.rotation))
            .overlay {
                if isSelected {
                    RoundedRectangle(cornerRadius: 4)
                        .strokeBorder(Color.accentColor, lineWidth: 2)
                        .padding(-4)
                }
            }
            .position(x: placement.center.x, y: placement.center.y)
            .onTapGesture { onSelect() }
            .gesture(moveGesture)
            .simultaneousGesture(resizeGesture)
            .simultaneousGesture(rotateGesture)
    }

    private var moveGesture: some Gesture {
        DragGesture()
            .onChanged { value in
                guard isSelected else { return }
                if dragOrigin == nil { dragOrigin = placement.center }
                guard let start = dragOrigin else { return }
                let dx = value.translation.width / canvasScale
                let dy = value.translation.height / canvasScale
                onMove(CGPoint(x: start.x + dx, y: start.y + dy))
            }
            .onEnded { _ in dragOrigin = nil }
    }

    private var resizeGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                guard isSelected else { return }
                if resizeOrigin == nil { resizeOrigin = placement.width }
                guard let start = resizeOrigin else { return }
                onResize(start * value)
            }
            .onEnded { _ in resizeOrigin = nil }
    }

    private var rotateGesture: some Gesture {
        RotationGesture()
            .onChanged { angle in
                guard isSelected else { return }
                if rotateStart == nil { rotateStart = placement.rotation }
                guard let start = rotateStart else { return }
                onSetRotation(start + angle.degrees)
            }
            .onEnded { _ in rotateStart = nil }
    }
}

#Preview {
    YearbookView()
        .environmentObject(BLEManager())
        .environmentObject(YearbookStore())
}
