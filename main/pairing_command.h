#pragma once

#include <controller/CHIPDeviceController.h>
#include <controller/CommissioningDelegate.h>

using chip::NodeId;
using chip::Controller::CommissioningParameters;

namespace heating_monitor
{
    namespace controller
    {
        class pairing_command : public chip::Controller::DevicePairingDelegate
        {
        public:
            void OnPairingComplete(CHIP_ERROR error) override;
            void OnCommissioningSuccess(chip::PeerId peerId) override;
            void OnCommissioningFailure(chip::PeerId peerId, CHIP_ERROR error, chip::Controller::CommissioningStage stageFailed, chip::Optional<chip::Credentials::AttestationVerificationResult> additionalErrorInfo) override;
            void OnICDRegistrationComplete(chip::ScopedNodeId deviceId, uint32_t icdCounter) override;
            void OnICDStayActiveComplete(chip::ScopedNodeId deviceId, uint32_t promisedActiveDuration) override;

            static esp_err_t pairing_ble_thread(NodeId node_id, uint32_t pincode, uint16_t disc, uint8_t *dataset_tlvs, uint8_t dataset_len);
            
            static pairing_command &get_instance()
            {
                static pairing_command s_instance;
                return s_instance;
            }

        private:
            NodeId m_remote_node_id;
            uint32_t m_setup_pincode;
            uint16_t m_discriminator;
        };
    }
}