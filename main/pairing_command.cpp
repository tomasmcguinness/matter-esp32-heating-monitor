#include <esp_check.h>
#include <esp_log.h>
#include <pairing_command.h>
#include <optional>

#include <esp_matter_controller_client.h>
#include <app-common/zap-generated/cluster-enums.h>
#include <credentials/FabricTable.h>

static const char *TAG = "pairing_command";

using namespace chip;
using namespace chip::Controller;

namespace heating_monitor
{
    namespace controller
    {
        void pairing_command::OnPairingComplete(CHIP_ERROR err)
        {
            if (err == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "PASE session establishment success");
            }
            else
            {
                ESP_LOGI(TAG, "PASE session establishment failure: Matter-%s", ErrorStr(err));
            }
        }

        void pairing_command::OnCommissioningSuccess(chip::PeerId peerId)
        {
            ESP_LOGI(TAG, "Commissioning success with node %" PRIX64 "-%" PRIX64, peerId.GetCompressedFabricId(), peerId.GetNodeId());
        }

        void pairing_command::OnCommissioningFailure(chip::PeerId peerId, CHIP_ERROR error, chip::Controller::CommissioningStage stageFailed, chip::Optional<chip::Credentials::AttestationVerificationResult> additionalErrorInfo)
        {
            ESP_LOGI(TAG, "Commissioning failure with node %" PRIX64 "-%" PRIX64, peerId.GetCompressedFabricId(), peerId.GetNodeId());
        }

        void pairing_command::OnICDRegistrationComplete(ScopedNodeId nodeId, uint32_t icdCounter)
        {
        }

        void pairing_command::OnICDStayActiveComplete(ScopedNodeId deviceId, uint32_t promisedActiveDuration)
        {
        }

        esp_err_t pairing_command::pairing_ble_thread(NodeId node_id, uint32_t pincode, uint16_t disc, uint8_t *dataset_tlvs, uint8_t dataset_len)
        {
            RendezvousParameters params = RendezvousParameters().SetSetupPINCode(pincode).SetDiscriminator(disc).SetPeerAddress(Transport::PeerAddress::BLE());
            auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();
            ESP_RETURN_ON_FALSE(controller_instance.get_commissioner()->GetPairingDelegate() == nullptr, ESP_ERR_INVALID_STATE, TAG, "There is already a pairing process");
            controller_instance.get_commissioner()->RegisterPairingDelegate(&pairing_command::get_instance());
            ByteSpan dataset_span(dataset_tlvs, dataset_len);
            CommissioningParameters commissioning_params = CommissioningParameters().SetThreadOperationalDataset(dataset_span);
            
            //NodeId commissioner_node_id = controller_instance.get_commissioner()->GetNodeId();
            
            // if (pairing_command::get_instance().m_icd_registration)
            // {
            //     pairing_command::get_instance().m_device_is_icd = false;
            //     commissioning_params.SetICDRegistrationStrategy(pairing_command::get_instance().m_icd_registration_strategy)
            //         .SetICDClientType(app::Clusters::IcdManagement::ClientTypeEnum::kPermanent)
            //         .SetICDCheckInNodeId(commissioner_node_id)
            //         .SetICDMonitoredSubject(commissioner_node_id)
            //         .SetICDSymmetricKey(pairing_command::get_instance().m_icd_symmetric_key);
            // }
            
            controller_instance.get_commissioner()->PairDevice(node_id, params, commissioning_params);
            
            return ESP_OK;
        }
    }
}