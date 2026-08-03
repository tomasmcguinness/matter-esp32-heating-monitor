import SwiftUI

/// Pairs the app with a Heating Monitor by scanning the QR code on its Settings page.
struct PairHubView: View {

    @Binding var hub: PairedHub?

    @Environment(\.dismiss) private var dismiss

    @State private var status: Status = .scanning
    @State private var errorMessage: String?
    @State private var manualHost = ""

    private enum Status {
        case scanning
        case verifying
        case manualEntry
    }

    var body: some View {
        NavigationStack {
            Group {
                switch status {
                case .scanning:
                    scanner
                case .verifying:
                    ProgressView("Connecting…")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                case .manualEntry:
                    manualEntryForm
                }
            }
            .navigationTitle("Add Heating Monitor")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }

                ToolbarItem(placement: .primaryAction) {
                    if status == .scanning {
                        Button("Enter Address") { status = .manualEntry }
                    }
                }
            }
            .alert("Couldn't Pair",
                   isPresented: Binding(get: { errorMessage != nil },
                                        set: { if !$0 { errorMessage = nil } })) {
                Button("OK") {
                    errorMessage = nil
                    if status == .verifying { status = .scanning }
                }
            } message: {
                Text(errorMessage ?? "")
            }
        }
    }

    private var scanner: some View {
        ZStack(alignment: .bottom) {
            if QRScannerView.isSupported {
                QRScannerView { payload in
                    Task { await pair(qrPayload: payload) }
                }
                .ignoresSafeArea(edges: .bottom)
            } else {
                ContentUnavailableView("Camera Unavailable",
                                       systemImage: "camera.fill",
                                       description: Text("This device can't scan codes. Enter the Heating Monitor's address instead."))
            }

            Text("Open **http://heating-monitor.local/settings** in a browser and scan the code shown there.")
                .font(.callout)
                .multilineTextAlignment(.center)
                .padding()
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                .padding()
        }
    }

    private var manualEntryForm: some View {
        Form {
            Section {
                TextField("heating-monitor.local", text: $manualHost)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.URL)
            } header: {
                Text("Hostname or IP Address")
            } footer: {
                Text("Pairing this way doesn't pick up the device's token, so scanning the QR code is preferred.")
            }

            Section {
                Button("Connect") {
                    Task { await pair(hub: PairedHub(manualHost: manualHost.trimmingCharacters(in: .whitespaces))) }
                }
                .disabled(manualHost.trimmingCharacters(in: .whitespaces).isEmpty)

                Button("Scan a Code Instead") { status = .scanning }
            }
        }
    }

    private func pair(qrPayload: String) async {
        do {
            await pair(hub: try PairedHub.decode(qrPayload: qrPayload))
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func pair(hub scanned: PairedHub) async {
        status = .verifying

        do {
            // Check the device actually answers before saving, so a stale QR code or a typo
            // surfaces here rather than as an empty Devices tab.
            let confirmed = try await HeatingMonitorClient(hub: scanned).info()

            // Trust the live response over the QR code for everything but the token, which
            // the manual-entry path never has.
            var resolved = confirmed
            resolved.token = confirmed.token ?? scanned.token

            try HubStore.save(resolved)

            hub = HubStore.current

            dismiss()
        } catch {
            errorMessage = error.localizedDescription
            status = .scanning
        }
    }
}
