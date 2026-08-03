import SwiftUI

struct SettingsView: View {

    @Binding var hub: PairedHub?

    @State private var isShowingPairing = false
    @State private var isConfirmingUnpair = false
    @State private var reachability: Reachability = .unknown

    private enum Reachability {
        case unknown
        case checking
        case reachable
        case unreachable(String)
    }

    var body: some View {
        NavigationStack {
            Form {
                if let hub {
                    Section("Heating Monitor") {
                        LabeledContent("Name", value: hub.name)
                        LabeledContent("Hostname", value: hub.host)
                        LabeledContent("IP Address", value: hub.ip ?? "Unknown")

                        if !hub.id.isEmpty {
                            LabeledContent("Device ID", value: hub.id)
                        }

                        LabeledContent("Pairing Token",
                                       value: (hub.token?.isEmpty == false) ? "Stored" : "None")
                    }

                    Section {
                        Button("Test Connection") {
                            Task { await checkReachability(hub: hub) }
                        }

                        switch reachability {
                        case .unknown:
                            EmptyView()
                        case .checking:
                            HStack(spacing: 10) {
                                ProgressView()
                                Text("Checking…").foregroundStyle(.secondary)
                            }
                        case .reachable:
                            Label("Reachable", systemImage: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                        case .unreachable(let reason):
                            Label(reason, systemImage: "exclamationmark.triangle.fill")
                                .foregroundStyle(.orange)
                        }
                    }

                    Section {
                        Button("Unpair", role: .destructive) {
                            isConfirmingUnpair = true
                        }
                    } footer: {
                        Text("Removes this Heating Monitor from the app. Devices already commissioned onto it are left alone.")
                    }
                } else {
                    Section {
                        Button("Scan Pairing Code") { isShowingPairing = true }
                    } footer: {
                        Text("Open http://heating-monitor.local/settings in a browser to display the code.")
                    }
                }
            }
            .navigationTitle("Settings")
            .sheet(isPresented: $isShowingPairing) {
                PairHubView(hub: $hub)
            }
            .confirmationDialog("Unpair this Heating Monitor?",
                                isPresented: $isConfirmingUnpair,
                                titleVisibility: .visible) {
                Button("Unpair", role: .destructive) {
                    HubStore.clear()
                    hub = nil
                    reachability = .unknown
                }
            }
        }
    }

    private func checkReachability(hub: PairedHub) async {
        reachability = .checking

        do {
            _ = try await HeatingMonitorClient(hub: hub).info()
            reachability = .reachable
        } catch {
            reachability = .unreachable(error.localizedDescription)
        }
    }
}
