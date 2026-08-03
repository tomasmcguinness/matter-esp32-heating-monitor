import SwiftUI
import VisionKit

/// Camera view that reads the pairing QR code from the Heating Monitor's Settings page.
///
/// This is only for the hub's own code. Matter device setup codes are scanned by the system
/// sheet that `MatterAddDeviceRequest` presents -- we never see the camera for those.
struct QRScannerView: UIViewControllerRepresentable {

    /// Called with the raw string of the first code recognised. The view stops scanning after
    /// the first hit so a single code can't fire repeatedly.
    let onScan: (String) -> Void

    static var isSupported: Bool {
        DataScannerViewController.isSupported && DataScannerViewController.isAvailable
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(onScan: onScan)
    }

    func makeUIViewController(context: Context) -> DataScannerViewController {
        let controller = DataScannerViewController(
            recognizedDataTypes: [.barcode(symbologies: [.qr])],
            qualityLevel: .balanced,
            recognizesMultipleItems: false,
            isHighFrameRateTrackingEnabled: false,
            isHighlightingEnabled: true)

        controller.delegate = context.coordinator

        return controller
    }

    func updateUIViewController(_ controller: DataScannerViewController, context: Context) {
        guard !context.coordinator.hasScanned else {
            controller.stopScanning()
            return
        }

        guard !controller.isScanning else { return }

        try? controller.startScanning()
    }

    static func dismantleUIViewController(_ controller: DataScannerViewController, coordinator: Coordinator) {
        controller.stopScanning()
    }

    final class Coordinator: NSObject, DataScannerViewControllerDelegate {

        private let onScan: (String) -> Void

        private(set) var hasScanned = false

        init(onScan: @escaping (String) -> Void) {
            self.onScan = onScan
        }

        func dataScanner(_ scanner: DataScannerViewController, didAdd addedItems: [RecognizedItem],
                         allItems: [RecognizedItem]) {
            handle(addedItems, in: scanner)
        }

        func dataScanner(_ scanner: DataScannerViewController, didTapOn item: RecognizedItem) {
            handle([item], in: scanner)
        }

        private func handle(_ items: [RecognizedItem], in scanner: DataScannerViewController) {
            guard !hasScanned else { return }

            for item in items {
                guard case .barcode(let barcode) = item, let payload = barcode.payloadStringValue else {
                    continue
                }

                hasScanned = true
                scanner.stopScanning()
                onScan(payload)

                return
            }
        }
    }
}
