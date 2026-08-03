import Foundation

/// A Matter node commissioned onto the Heating Monitor's fabric.
///
/// Mirrors the JSON built by `nodes_get_handler` in the firmware's `app_main.cpp`.
struct Node: Codable, Identifiable, Hashable, Sendable {

    let nodeId: UInt64
    let isIcd: Bool
    let vendorName: String?
    let productName: String?
    let nodeName: String?
    let powerSource: Int
    let extAddress: UInt64
    let hasSubscription: Bool
    let endpoints: [Endpoint]

    var id: UInt64 { nodeId }

    /// What to show in a list. The firmware leaves `nodeName` null until the user sets one,
    /// and `productName` null until the node has answered a Basic Information read.
    var displayName: String {
        if let nodeName, !nodeName.isEmpty { return nodeName }
        if let productName, !productName.isEmpty { return productName }
        return "Node \(nodeIdHex)"
    }

    var nodeIdHex: String {
        "0x" + String(nodeId, radix: 16, uppercase: true)
    }

    var subtitle: String? {
        let parts = [vendorName, productName].compactMap { $0 }.filter { !$0.isEmpty }
        return parts.isEmpty ? nil : parts.joined(separator: " · ")
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        // cJSON prints anything that doesn't fit an int in scientific notation, which a plain
        // UInt64 decode chokes on -- Thread extended addresses always land in that range.
        nodeId = try container.decodeLenientUInt64(forKey: .nodeId)
        extAddress = try container.decodeLenientUInt64(forKey: .extAddress)

        isIcd = try container.decodeIfPresent(Bool.self, forKey: .isIcd) ?? false
        vendorName = try container.decodeIfPresent(String.self, forKey: .vendorName)
        productName = try container.decodeIfPresent(String.self, forKey: .productName)
        nodeName = try container.decodeIfPresent(String.self, forKey: .nodeName)
        powerSource = try container.decodeIfPresent(Int.self, forKey: .powerSource) ?? 0
        hasSubscription = try container.decodeIfPresent(Bool.self, forKey: .hasSubscription) ?? false
        endpoints = try container.decodeIfPresent([Endpoint].self, forKey: .endpoints) ?? []
    }
}

struct Endpoint: Codable, Identifiable, Hashable, Sendable {

    let endpointId: Int
    let endpointName: String?
    let powerSource: Int
    let measuredValue: Double?
    let deviceTypes: [UInt32]

    var id: Int { endpointId }

    /// The firmware reports temperatures and flow rates in hundredths, the way the Matter
    /// clusters carry them.
    var scaledValue: Double? {
        measuredValue.map { $0 / 100 }
    }

    var deviceTypeNames: [String] {
        deviceTypes.map { MatterDeviceType.name(for: $0) }
    }

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        endpointId = try container.decodeIfPresent(Int.self, forKey: .endpointId) ?? 0
        endpointName = try container.decodeIfPresent(String.self, forKey: .endpointName)
        powerSource = try container.decodeIfPresent(Int.self, forKey: .powerSource) ?? 0
        measuredValue = try container.decodeIfPresent(Double.self, forKey: .measuredValue)
        deviceTypes = try container.decodeIfPresent([UInt32].self, forKey: .deviceTypes) ?? []
    }
}

/// The subset of Matter device type ids this system runs into, matching the switch in the
/// web UI's `Devices.tsx` so both front ends label things the same way.
enum MatterDeviceType {

    static func name(for id: UInt32) -> String {
        switch id {
        case 14: return "Aggregator"
        case 15: return "Generic Switch"
        case 17: return "Power Source"
        case 19: return "Bridged Device"
        case 117: return "Dishwasher"
        case 266: return "On/Off Plug-in Unit"
        case 269: return "Extended Color Light"
        case 769: return "Thermostat"
        case 770: return "Temperature Sensor"
        case 773: return "Pressure Sensor"
        case 774: return "Flow Sensor"
        case 775: return "Humidity Sensor"
        case 777: return "Heat Pump"
        case 1293: return "Device Energy Manager"
        case 1296: return "Electrical Sensor"
        default: return "Type \(id)"
        }
    }
}

// MARK: - Request and response bodies

struct CommissionRequest: Encodable, Sendable {
    /// Always false from this app: MatterSupport only ever hands us devices that aren't yet
    /// on a fabric, which is the firmware's BLE + Thread path.
    let inUse: Bool
    let setupCode: String
}

struct CommissionResponse: Decodable, Sendable {
    let nodeId: UInt64

    init(from decoder: any Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        nodeId = try container.decodeLenientUInt64(forKey: .nodeId)
    }

    enum CodingKeys: String, CodingKey {
        case nodeId
    }
}

struct RenameRequest: Encodable, Sendable {
    let name: String
}

// MARK: - Lenient number decoding

private extension KeyedDecodingContainer {

    /// Decodes a 64-bit id that the firmware may have emitted as an integer, as a float in
    /// scientific notation, or as a string. Missing or unparseable values decode as 0 rather
    /// than failing the whole response -- these are display fields, not something worth
    /// dropping a device list over.
    func decodeLenientUInt64(forKey key: Key) throws -> UInt64 {
        if let value = try? decodeIfPresent(UInt64.self, forKey: key) {
            return value
        }

        if let value = try? decodeIfPresent(Double.self, forKey: key), value >= 0,
           value < Double(UInt64.max) {
            return UInt64(value)
        }

        if let string = try? decodeIfPresent(String.self, forKey: key), let value = UInt64(string) {
            return value
        }

        return 0
    }
}
