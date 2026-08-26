import SwiftUI

struct SettingsView: View {

    @Binding var hub: PairedHub?

    private enum PairingIntent: Identifiable {
        case scan
        case manual

        var id: Self { self }
    }

    @State private var pairingIntent: PairingIntent?
    @State private var isConfirmingUnpair = false
    @State private var reachability: Reachability = .unknown

    @State private var addressText: String = ""
    @State private var isSavingAddress = false
    @State private var addressError: String?

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

                        if !hub.id.isEmpty {
                            LabeledContent("Device ID", value: hub.id)
                        }

                        LabeledContent("Pairing Token",
                                       value: (hub.token?.isEmpty == false) ? "Stored" : "None")
                    }

                    Section {
                        TextField("http://heating-monitor.local", text: $addressText)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .keyboardType(.URL)

                        if let addressError {
                            Label(addressError, systemImage: "exclamationmark.triangle.fill")
                                .foregroundStyle(.orange)
                        }

                        Button {
                            Task { await saveAddress(hub: hub) }
                        } label: {
                            if isSavingAddress {
                                HStack(spacing: 10) {
                                    ProgressView()
                                    Text("Saving…")
                                }
                            } else {
                                Text("Save")
                            }
                        }
                        .disabled(isSavingAddress || PairedHub.connectionURL(from: addressText) == nil)
                    } header: {
                        Text("Address")
                    } footer: {
                        Text("The full http:// or https:// address used to reach the Heating Monitor.")
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
                        Button("Scan Pairing Code") {
                            pairingIntent = .scan
                        }

                        Button("Enter Address Manually") {
                            pairingIntent = .manual
                        }
                    } footer: {
                        Text("Open http://heating-monitor.local/settings in a browser to display the code.")
                    }
                }
            }
            .navigationTitle("Settings")
            .onAppear { addressText = hub?.url.absoluteString ?? "" }
            .onChange(of: hub) { addressText = hub?.url.absoluteString ?? "" }
            .sheet(item: $pairingIntent) { intent in
                PairHubView(hub: $hub, startInManualEntry: intent == .manual)
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

    private func saveAddress(hub: PairedHub) async {
        guard let newURL = PairedHub.connectionURL(from: addressText) else {
            addressError = "Enter a valid http:// or https:// address."
            return
        }

        guard newURL != hub.url else {
            addressError = nil
            return
        }

        isSavingAddress = true
        addressError = nil
        defer { isSavingAddress = false }

        var candidate = hub
        candidate.url = newURL

        do {
            _ = try await HeatingMonitorClient(hub: candidate).info()
            try HubStore.save(candidate)
            self.hub = HubStore.current
            reachability = .unknown
        } catch {
            addressError = error.localizedDescription
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
