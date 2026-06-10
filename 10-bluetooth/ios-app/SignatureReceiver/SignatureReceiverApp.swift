import SwiftUI

@main
struct SignatureReceiverApp: App {
    @StateObject private var ble = BLEManager()
    @StateObject private var yearbook = YearbookStore()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(ble)
                .environmentObject(yearbook)
        }
    }
}

struct RootView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        TabView {
            ContentView()
                .tabItem { Label("Receive", systemImage: "antenna.radiowaves.left.and.right") }

            YearbookView()
                .tabItem { Label("Yearbook", systemImage: "book") }
                .badge(ble.signatures.count)
        }
    }
}
