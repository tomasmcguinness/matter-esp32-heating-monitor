import Foundation
import Observation

@Observable
@MainActor
final class DevicesModel {

    private(set) var nodes: [Node] = []
    private(set) var isLoading = false
    private(set) var isWaitingForCommissioning = false

    var errorMessage: String?

    func load(hub: PairedHub?) async {
        guard let hub else {
            nodes = []
            return
        }

        isLoading = true
        defer { isLoading = false }

        do {
            nodes = try await HeatingMonitorClient(hub: hub).nodes().sorted { $0.nodeId < $1.nodeId }
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func unpair(_ node: Node, hub: PairedHub?) async {
        guard let hub else { return }

        // Drop it from the list straight away; the reload below puts it back if the hub
        // disagreed.
        nodes.removeAll { $0.nodeId == node.nodeId }

        do {
            try await HeatingMonitorClient(hub: hub).unpair(nodeId: node.nodeId)
        } catch {
            errorMessage = error.localizedDescription
        }

        await load(hub: hub)
    }

    func rename(_ node: Node, to name: String, hub: PairedHub?) async {
        guard let hub else { return }

        do {
            try await HeatingMonitorClient(hub: hub).rename(nodeId: node.nodeId, to: name)
            await load(hub: hub)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    /// Waits for a freshly commissioned node to show up.
    ///
    /// The hub answers `POST /api/nodes` with 202 as soon as it starts pairing, and there is
    /// no completion callback on the API, so polling is the only way to know it worked. The
    /// extension leaves the node id it was given in the App Group for us to look for.
    func awaitCommissionedNode(hub: PairedHub?) async {
        guard let hub else { return }

        let expectedNodeId = HubStore.lastCommissionedNodeId

        isWaitingForCommissioning = true
        defer {
            isWaitingForCommissioning = false
            HubStore.lastCommissionedNodeId = nil
        }

        // BLE + Thread commissioning takes a while, and the device only lands in the node
        // list once the hub has read its Basic Information cluster.
        for attempt in 0..<20 {
            if attempt > 0 {
                try? await Task.sleep(for: .seconds(3))
            }

            await load(hub: hub)

            guard let expectedNodeId else {
                // No id to wait for -- the extension never got a response, so one refresh is
                // all we can usefully do.
                return
            }

            if nodes.contains(where: { $0.nodeId == expectedNodeId }) {
                return
            }
        }

        errorMessage = "The device didn't finish commissioning. Check the Heating Monitor's logs, then pull to refresh."
    }
}
