import Foundation
import Security

/// Where the paired hub lives.
///
/// The app and the Matter extension are separate processes, so this has to be shared storage
/// rather than in-memory state: the extension is launched by the system to handle a
/// commissioning request and has no way to ask the app which device to talk to.
///
/// Hub metadata goes in the App Group's `UserDefaults`; the pairing token goes in a shared
/// Keychain access group, so it isn't sitting in a plist inside the container.
enum HubStore {

    static let appGroup = "group.com.tomasmcguinness.heating-monitor-companion"
    static let keychainAccessGroup = "com.tomasmcguinness.heating-monitor-companion"

    private static let hubKey = "pairedHub"
    private static let lastCommissionedNodeIdKey = "lastCommissionedNodeId"
    private static let keychainService = "com.tomasmcguinness.heating-monitor-companion.token"

    private static var defaults: UserDefaults? {
        UserDefaults(suiteName: appGroup)
    }

    // MARK: - Paired hub

    static var current: PairedHub? {
        guard let data = defaults?.data(forKey: hubKey),
              var hub = try? JSONDecoder().decode(PairedHub.self, from: data) else {
            return nil
        }

        hub.token = readToken()

        return hub
    }

    static var isPaired: Bool {
        current != nil
    }

    static func save(_ hub: PairedHub) throws {
        guard let defaults else {
            throw HubStoreError.appGroupUnavailable
        }

        // Strip the token before it reaches UserDefaults -- it belongs in the Keychain.
        var metadata = hub
        metadata.token = nil

        defaults.set(try JSONEncoder().encode(metadata), forKey: hubKey)

        if let token = hub.token, !token.isEmpty {
            try writeToken(token)
        } else {
            deleteToken()
        }
    }

    static func clear() {
        defaults?.removeObject(forKey: hubKey)
        defaults?.removeObject(forKey: lastCommissionedNodeIdKey)
        deleteToken()
    }

    // MARK: - Commissioning handoff

    /// Written by the Matter extension when the hub accepts a commissioning request, read by
    /// the app so it knows which node to wait for. Without this the app has no way to learn
    /// the node id -- `MatterAddDeviceRequest.perform()` returns nothing.
    static var lastCommissionedNodeId: UInt64? {
        get {
            guard let defaults, defaults.object(forKey: lastCommissionedNodeIdKey) != nil else {
                return nil
            }

            let value = defaults.integer(forKey: lastCommissionedNodeIdKey)

            return value > 0 ? UInt64(value) : nil
        }
        set {
            guard let defaults else { return }

            if let newValue {
                defaults.set(Int(newValue), forKey: lastCommissionedNodeIdKey)
            } else {
                defaults.removeObject(forKey: lastCommissionedNodeIdKey)
            }
        }
    }

    // MARK: - Keychain

    private static func keychainQuery() -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: keychainService,
            kSecAttrAccount as String: "pairingToken",
            kSecAttrAccessGroup as String: keychainAccessGroup
        ]
    }

    private static func readToken() -> String? {
        var query = keychainQuery()
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne

        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)

        guard status == errSecSuccess, let data = item as? Data else {
            return nil
        }

        return String(data: data, encoding: .utf8)
    }

    private static func writeToken(_ token: String) throws {
        deleteToken()

        var query = keychainQuery()
        query[kSecValueData as String] = Data(token.utf8)
        query[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock

        let status = SecItemAdd(query as CFDictionary, nil)

        guard status == errSecSuccess else {
            throw HubStoreError.keychainFailure(status)
        }
    }

    private static func deleteToken() {
        SecItemDelete(keychainQuery() as CFDictionary)
    }
}

enum HubStoreError: LocalizedError {
    case appGroupUnavailable
    case keychainFailure(OSStatus)

    var errorDescription: String? {
        switch self {
        case .appGroupUnavailable:
            return "The app group isn't configured, so the pairing can't be saved."
        case .keychainFailure(let status):
            return "Couldn't save the pairing token to the keychain (status \(status))."
        }
    }
}
