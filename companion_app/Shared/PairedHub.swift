import Foundation

/// The Heating Monitor this app is paired with.
///
/// This is decoded straight from the JSON in the QR code the device renders on its Settings
/// page, so most coding keys have to match `info_get_handler` in the firmware's `app_main.cpp`.
struct PairedHub: Codable, Equatable, Sendable {

    /// Payload schema version. The firmware currently emits 1.
    let version: Int

    let name: String

    /// The full http(s) address used to reach the hub. Editable from Settings, unlike the
    /// rest of this struct, so a changed IP or hostname doesn't require re-pairing.
    var url: URL

    /// Stable identifier derived from the device's MAC.
    let id: String

    /// Kept out of `UserDefaults` by `HubStore`, which holds it in the Keychain instead.
    var token: String?

    enum CodingKeys: String, CodingKey {
        case version = "v"
        case name
        case url
        case id
        case token
    }
}

extension PairedHub {

    /// Builds a hub from a URL typed in by hand, for when the QR code isn't available.
    init(url: URL) {
        self.version = 1
        self.name = "Heating Monitor"
        self.url = url
        self.id = ""
        self.token = nil
    }

    /// Parses free-form text into a connection URL, requiring an http/https scheme and a
    /// non-empty host. Returns nil if the text isn't a usable address.
    static func connectionURL(from text: String) -> URL? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)

        guard !trimmed.isEmpty,
              let url = URL(string: trimmed),
              let scheme = url.scheme?.lowercased(),
              scheme == "http" || scheme == "https",
              let host = url.host, !host.isEmpty else {
            return nil
        }

        return url
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
