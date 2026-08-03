// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_utils.h>
#include "identify_command.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/server/Server.h>
#include <controller/CHIPCluster.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPError.h>
#include <lib/core/NodeId.h>
#include <lib/core/Optional.h>

using namespace chip::app::Clusters;

#define TAG "identify_command"

namespace heating_monitor
{
    namespace controller
    {
        esp_err_t identify_command::send_identify_command(uint64_t node_id)
        {
            if (!chip::IsOperationalNodeId(node_id))
            {
                return ESP_ERR_INVALID_ARG;
            }

            auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();

            if (CHIP_NO_ERROR == controller_instance.get_commissioner()->GetConnectedDevice(node_id, &on_device_connected_cb, &on_device_connection_failure_cb))
            {
                return ESP_OK;
            }
            return ESP_OK;
        }

        void identify_command::on_device_connected_fcn(void *context, ExchangeManager &exchangeMgr, const SessionHandle &sessionHandle)
        {
            ESP_LOGI(TAG, "Established CASE session for identify command");

            Identify::Commands::Identify::Type command_data;
            command_data.identifyTime = static_cast<uint16_t>(5);
            chip::Controller::ClusterBase cluster(exchangeMgr, sessionHandle, 0);
            cluster.InvokeCommand(command_data, context, send_command_success_callback, send_command_failure_callback, chip::MakeOptional(5000));
        }

        void identify_command::on_device_connection_failure_fcn(void *context, const ScopedNodeId &peerId, CHIP_ERROR error)
        {
            ESP_LOGE(TAG, "Failed to establish CASE session for identify command");
        }

        void identify_command::send_command_success_callback(void *context, const chip::app::DataModel::NullObjectType &data)
        {
            ESP_LOGI(TAG, "Sent identify command");
        }

        void identify_command::send_command_failure_callback(void *context, CHIP_ERROR error)
        {
            ESP_LOGE(TAG, "Failed to send Identify command");
        }

    } // namespace controller
} // namespace heating_monitor
