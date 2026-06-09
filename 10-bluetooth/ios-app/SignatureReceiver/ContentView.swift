import SwiftUI

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager

    @State private var selected: SignatureEntry?

    private let columns = [GridItem(.adaptive(minimum: 90), spacing: 12)]

    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                if ble.isSimulator {
                    simulatorBanner
                }

                statusCard

                controls

                if ble.signatures.isEmpty {
                    emptyState
                } else {
                    collage
                }

                Spacer(minLength: 0)
            }
            .padding()
            .navigationTitle("Pi Signatures")
            .sheet(item: $selected) { entry in
                SignatureDetailView(entry: entry)
            }
        }
    }

    private var simulatorBanner: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.orange)
            Text("iOS Simulator has no real Bluetooth. Use a physical iPhone to connect to the Pi.")
                .font(.caption)
        }
        .padding()
        .background(Color.orange.opacity(0.15))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private var statusCard: some View {
        VStack(spacing: 6) {
            HStack {
                Circle()
                    .fill(statusColor)
                    .frame(width: 12, height: 12)
                Text(ble.statusText)
                    .font(.subheadline)
                    .lineLimit(2)
                Spacer()
            }
            if case let .receiving(got, total) = ble.status, total > 0 {
                ProgressView(value: Double(got), total: Double(total))
            }
        }
        .padding()
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private var controls: some View {
        HStack(spacing: 12) {
            Button {
                ble.startScan()
            } label: {
                Label("Connect", systemImage: "antenna.radiowaves.left.and.right")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)

            Button {
                ble.fetchAll()
            } label: {
                Label("Fetch", systemImage: "square.and.arrow.down")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(!isConnected)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 8) {
            Image(systemName: "signature")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No signatures yet")
                .foregroundStyle(.secondary)
            Text("Connect to the Pi, then tap Fetch.")
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 40)
    }

    private var collage: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 12) {
                ForEach(Array(ble.signatures.enumerated()), id: \.offset) { idx, img in
                    let entry = SignatureEntry(id: idx, image: img)
                    Image(uiImage: img)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 90, height: 90)
                        .background(Color.white)
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(Color(.separator))
                        )
                        .onTapGesture { selected = entry }
                }
            }
            .padding(.top, 4)
        }
    }

    private var isConnected: Bool {
        switch ble.status {
        case .connected, .receiving:
            return true
        default:
            return false
        }
    }

    private var statusColor: Color {
        switch ble.status {
        case .connected, .receiving:
            return .green
        case .scanning, .connecting:
            return .orange
        case .error, .poweredOff:
            return .red
        default:
            return .gray
        }
    }
}

#Preview {
    ContentView()
        .environmentObject(BLEManager())
}
