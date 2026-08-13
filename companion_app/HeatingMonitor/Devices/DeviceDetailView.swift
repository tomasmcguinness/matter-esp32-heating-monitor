import SwiftUI

struct DeviceDetailView: View {

    let node: Node
    let hub: PairedHub?
    let model: DevicesModel

    @Environment(\.dismiss) private var dismiss

    @State private var name: String = ""

    var body: some View {
        Form {
            Section("Name") {
                TextField("Device name", text: $name)

                Button("Save Name") {
                    Task {
                        await model.rename(node, to: name.trimmingCharacters(in: .whitespaces), hub: hub)
                    }
                }
                .disabled(!canSaveName)
            }

            Section("Node") {
                LabeledContent("Node ID", value: node.nodeIdHex)
                LabeledContent("Vendor", value: node.vendorName ?? "Unknown")
                LabeledContent("Product", value: node.productName ?? "Unknown")

                if node.extAddress != 0 {
                    LabeledContent("Thread Address",
                                   value: "0x" + String(node.extAddress, radix: 16, uppercase: true))
                }

                LabeledContent("Sleepy Device", value: node.isIcd ? "Yes" : "No")
                LabeledContent("Subscribed", value: node.hasSubscription ? "Yes" : "No")

                if let battery = node.battery {
                    LabeledContent("Battery") {
                        BatteryLabel(battery: battery)
                    }
                }
            }

            ForEach(node.endpoints) { endpoint in
                Section("Endpoint \(endpoint.endpointId)") {
                    if let endpointName = endpoint.endpointName, !endpointName.isEmpty {
                        LabeledContent("Name", value: endpointName)
                    }

                    if !endpoint.deviceTypeNames.isEmpty {
                        LabeledContent("Device Types", value: endpoint.deviceTypeNames.joined(separator: ", "))
                    }

                    if let value = endpoint.scaledValue {
                        LabeledContent("Measured Value", value: String(format: "%.2f", value))
                    }

                    if let battery = endpoint.battery {
                        LabeledContent("Battery") {
                            BatteryLabel(battery: battery)
                        }
                    }
                }
            }

            Section {
                Button("Remove From Heating Monitor", role: .destructive) {
                    Task {
                        await model.unpair(node, hub: hub)
                        dismiss()
                    }
                }
            } footer: {
                Text("This unpairs the device from the Heating Monitor's Matter fabric.")
            }
        }
        .navigationTitle(node.displayName)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            name = node.nodeName ?? ""
        }
    }

    private var canSaveName: Bool {
        let trimmed = name.trimmingCharacters(in: .whitespaces)

        return !trimmed.isEmpty && trimmed != (node.nodeName ?? "")
    }
}
