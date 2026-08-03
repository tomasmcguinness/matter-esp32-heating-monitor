import Foundation

/// The Heating Monitor this app is paired with.
///
/// This is decoded straight from the JSON in the QR code the device renders on its Settings
/// page, so the coding keys have to match `info_get_handler` in the firmware's `app_main.cpp`.
struct PairedHub: Codable, Equatable, Sendable {

    /// Payload schema version. The firmware currently emits 1.
    let version: Int

    let name: String

    /// mDNS hostname, e.g. `heating-monitor.local`.
    let host: String

    /// The device's DHCP address, used as a fallback when mDNS doesn't resolve. The firmware
    /// sends null if Ethernet has no address yet.
    let ip: String?

    /// Stable identifier derived from the device's MAC.
    let id: String

    /// Kept out of `UserDefaults` by `HubStore`, which holds it in the Keychain instead.
    var token: String?

    enum CodingKeys: String, CodingKey {
        case version = "v"
        case name
        case host
        case ip
        case id
        case token
    }

    /// The addresses to try, in order. mDNS is preferred because the DHCP lease can change,
    /// but this device publishes an IPv4 delegated hostname that phones don't always resolve
    /// promptly, so the raw address is worth keeping as a backstop.
    var baseURLs: [URL] {
        var urls: [URL] = []

        if let hostURL = URL(string: "http://\(host)") {
            urls.append(hostURL)
        }

        if let ip, let ipURL = URL(string: "http://\(ip)") {
            urls.append(ipURL)
        }

        return urls
    }
}

extension PairedHub {

    /// Builds a hub from a hostname typed in by hand, for when the QR code isn't available.
    init(manualHost: String) {
        self.version = 1
        self.name = "Heating Monitor"
        self.host = manualHost
        self.ip = nil
        self.id = ""
        self.token = nil
    }

    /// Decodes the JSON payload carried in the device's pairing QR code.
    static func decode(qrPayload: String) throws -> PairedHub {
        guard let data = qrPayload.data(using: .utf8) else {
            throw HubPairingError.unrecognisedQRCode
        }

        do {
            return try JSONDecoder().decode(PairedHub.self, from: data)
        } catch {
            throw HubPairingError.unrecognisedQRCode
        }
    }
}

enum HubPairingError: LocalizedError {
    case unrecognisedQRCode
    case notPaired

    var errorDescription: String? {
        switch self {
        case .unrecognisedQRCode:
            return "That doesn't look like a Heating Monitor pairing code. Open the device's Settings page and scan the code shown there."
        case .notPaired:
            return "This app isn't paired with a Heating Monitor yet."
        }
    }
}
