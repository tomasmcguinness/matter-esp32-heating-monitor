import SwiftUI

struct ContentView: View {

    /// Held here rather than read from `HubStore` at each use site, so that pairing and
    /// unpairing redraw both tabs.
    @State private var hub: PairedHub? = HubStore.current

    var body: some View {
        TabView {
            DevicesView(hub: $hub)
                .tabItem {
                    Label("Devices", systemImage: "sensor")
                }

            SettingsView(hub: $hub)
                .tabItem {
                    Label("Settings", systemImage: "gear")
                }
        }
    }
}

#Preview {
    ContentView()
}
