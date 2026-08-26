import Foundation

/// Talks to the Heating Monitor's REST API.
///
/// Used from both the app and the Matter extension, so it holds no state beyond the hub it
/// was built for.
struct HeatingMonitorClient: Sendable {

    let hub: PairedHub

    private let session: URLSession

    init(hub: PairedHub, session: URLSession = .shared) {
        self.hub = hub
        self.session = session
    }

    /// Builds a client for whatever hub is currently paired, or throws if there isn't one.
    static func forPairedHub() throws -> HeatingMonitorClient {
        guard let hub = HubStore.current else {
            throw HubPairingError.notPaired
        }

        return HeatingMonitorClient(hub: hub)
    }

    // MARK: - Endpoints

    /// Confirms the hub is reachable and returns what it says about itself. Used right after
    /// scanning, so a bad pairing fails immediately rather than on the Devices tab.
    func info() async throws -> PairedHub {
        try await get("/api/info")
    }

    func nodes() async throws -> [Node] {
        try await get("/api/nodes")
    }

    /// Hands a Matter onboarding payload to the hub, which does the actual commissioning.
    ///
    /// Returns the node id the hub reserved. Commissioning itself is asynchronous -- a 202
    /// means the hub started, not that the device joined.
    @discardableResult
    func commission(setupCode: String) async throws -> UInt64 {
        let body = try JSONEncoder().encode(CommissionRequest(inUse: false, setupCode: setupCode))
        let data = try await perform(path: "/api/nodes", method: "POST", bodyData: body, timeout: 90)

        return try decode(CommissionResponse.self, from: data).nodeId
    }

    func rename(nodeId: UInt64, to name: String) async throws {
        let body = try JSONEncoder().encode(RenameRequest(name: name))
        _ = try await perform(path: "/api/nodes/\(nodeId)/update", method: "PUT", bodyData: body, timeout: 20)
    }

    func unpair(nodeId: UInt64) async throws {
        _ = try await perform(path: "/api/nodes/\(nodeId)", method: "DELETE", bodyData: nil, timeout: 20)
    }

    // MARK: - Transport

    /// Built by string rather than `appendingPathComponent`, which escapes and re-normalises
    /// in ways that don't survive a leading slash cleanly.
    private func makeRequest(baseURL: URL, path: String, method: String, timeout: TimeInterval) -> URLRequest? {
        guard let url = URL(string: baseURL.absoluteString + path) else {
            return nil
        }

        var request = URLRequest(url: url)

        request.httpMethod = method
        request.timeoutInterval = timeout
        request.setValue("application/json", forHTTPHeaderField: "Accept")

        if let token = hub.token, !token.isEmpty {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }

        return request
    }

    /// Sends a single request to the hub's configured URL.
    private func perform(path: String, method: String, bodyData: Data?, timeout: TimeInterval) async throws -> Data {
        guard var request = makeRequest(baseURL: hub.url, path: path, method: method, timeout: timeout) else {
            throw ClientError.unreachable(hub.url.absoluteString, underlying: nil)
        }

        if let bodyData {
            request.httpBody = bodyData
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }

        let data: Data
        let response: URLResponse

        do {
            (data, response) = try await session.data(for: request)
        } catch {
            throw ClientError.unreachable(hub.url.absoluteString, underlying: error)
        }

        guard let http = response as? HTTPURLResponse else {
            throw ClientError.badResponse
        }

        guard (200..<300).contains(http.statusCode) else {
            throw ClientError.httpError(status: http.statusCode,
                                        message: String(data: data, encoding: .utf8))
        }

        return data
    }

    private func get<Response: Decodable>(_ path: String) async throws -> Response {
        let data = try await perform(path: path, method: "GET", bodyData: nil, timeout: 20)

        return try decode(Response.self, from: data)
    }

    private func decode<Response: Decodable>(_ type: Response.Type, from data: Data) throws -> Response {
        do {
            return try JSONDecoder().decode(type, from: data)
        } catch {
            throw ClientError.decodingFailed(error)
        }
    }
}

enum ClientError: LocalizedError {
    case unreachable(String, underlying: (any Error)?)
    case badResponse
    case httpError(status: Int, message: String?)
    case decodingFailed(any Error)

    var errorDescription: String? {
        switch self {
        case .unreachable(let address, let underlying):
            let reason = underlying.map { " (\($0.localizedDescription))" } ?? ""
            return "Couldn't reach \(address)\(reason). Check that it's powered on and on the same network."
        case .badResponse:
            return "The Heating Monitor sent back something unexpected."
        case .httpError(let status, let message):
            if let message, !message.isEmpty {
                return "The Heating Monitor returned \(status): \(message)"
            }
            return "The Heating Monitor returned \(status)."
        case .decodingFailed:
            return "Couldn't read the Heating Monitor's response."
        }
    }
}
