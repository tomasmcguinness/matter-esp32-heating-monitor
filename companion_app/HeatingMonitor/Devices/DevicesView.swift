import SwiftUI
import MatterSupport

struct DevicesView: View {

    @Binding var hub: PairedHub?

    @State private var model = DevicesModel()
    @State private var isShowingPairing = false

    var body: some View {
        NavigationStack {
            Group {
                if hub == nil {
                    notPaired
                } else {
                    deviceList
                }
            }
            .navigationTitle("Devices")
            .toolbar {
                if hub != nil {
                    ToolbarItem(placement: .primaryAction) {
                        Button {
                            Task { await addDevice() }
                        } label: {
                            Label("Add Device", systemImage: "plus")
                        }
                        .disabled(model.isWaitingForCommissioning)
                    }
                }
            }
            .sheet(isPresented: $isShowingPairing) {
                PairHubView(hub: $hub)
            }
            .alert("Something Went Wrong",
                   isPresented: Binding(get: { model.errorMessage != nil },
                                        set: { if !$0 { model.errorMessage = nil } })) {
                Button("OK") { model.errorMessage = nil }
            } message: {
                Text(model.errorMessage ?? "")
            }
        }
        .task(id: hub) {
            await model.load(hub: hub)
        }
    }

    private var notPaired: some View {
        ContentUnavailableView {
            Label("No Heating Monitor", systemImage: "qrcode.viewfinder")
        } description: {
            Text("Scan the pairing code on your Heating Monitor's Settings page to get started.")
        } actions: {
            Button("Scan Pairing Code") { isShowingPairing = true }
                .buttonStyle(.borderedProminent)
        }
    }

    private var deviceList: some View {
        List {
            if model.isWaitingForCommissioning {
                Section {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Commissioning a new device…")
                            .foregroundStyle(.secondary)
                    }
                }
            }

            Section {
                ForEach(model.nodes) { node in
                    NavigationLink(value: node) {
                        DeviceRow(node: node)
                    }
                }
                .onDelete(perform: unpair)
            } footer: {
                if !model.nodes.isEmpty {
                    Text("\(model.nodes.count) device\(model.nodes.count == 1 ? "" : "s") on \(hub?.name ?? "the hub").")
                }
            }
        }
        .navigationDestination(for: Node.self) { node in
            DeviceDetailView(node: node, hub: hub, model: model)
        }
        .refreshable {
            await model.load(hub: hub)
        }
        .overlay {
            if model.nodes.isEmpty && !model.isLoading && !model.isWaitingForCommissioning {
                ContentUnavailableView {
                    Label("No Devices", systemImage: "sensor.fill")
                } description: {
                    Text("Tap + to commission a Matter device onto your Heating Monitor.")
                }
            }
        }
    }

    private func unpair(at offsets: IndexSet) {
        let doomed = offsets.map { model.nodes[$0] }

        Task {
            for node in doomed {
                await model.unpair(node, hub: hub)
            }
        }
    }

    /// Hands off to the system Matter setup sheet.
    ///
    /// Everything from here -- camera, QR scan, manual pairing code entry, Thread credentials
    /// -- is Apple's UI. When the user picks a device, the system launches our app extension
    /// out of process and calls its `commissionDevice` method with the onboarding payload;
    /// that's where the call to the Heating Monitor happens.
    private func addDevice() async {
        guard let hub else { return }

        HubStore.lastCommissionedNodeId = nil

        let topology = MatterAddDeviceRequest.Topology(
            ecosystemName: "Heating Monitor",
            homes: [MatterAddDeviceRequest.Home(displayName: hub.name)])

        do {
            try await MatterAddDeviceRequest(topology: topology).perform()
        } catch {
            // The user cancelling the sheet lands here too, so only complain if the hub was
            // actually asked to do something.
            if HubStore.lastCommissionedNodeId != nil {
                model.errorMessage = error.localizedDescription
            }

            HubStore.lastCommissionedNodeId = nil

            return
        }

        await model.awaitCommissionedNode(hub: hub)
    }
}

struct DeviceRow: View {

    let node: Node

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(node.displayName)
                .font(.body)

            if let subtitle = node.subtitle {
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 6) {
                if node.isIcd {
                    StatusBadge(text: "Sleepy", tint: .orange)
                }

                if node.hasSubscription {
                    StatusBadge(text: "Subscribed", tint: .green)
                } else {
                    StatusBadge(text: "No subscription", tint: .secondary)
                }
            }
        }
        .padding(.vertical, 2)
    }
}

struct StatusBadge: View {

    let text: String
    let tint: Color

    var body: some View {
        Text(text)
            .font(.caption2.weight(.medium))
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(tint.opacity(0.15), in: Capsule())
            .foregroundStyle(tint)
    }
}
