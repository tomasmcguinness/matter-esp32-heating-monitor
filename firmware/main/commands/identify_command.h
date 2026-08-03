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

#pragma once

#include <esp_matter.h>
#include <esp_matter_client.h>
#include <esp_matter_mem.h>
#include <lib/core/ScopedNodeId.h>
#include <messaging/ExchangeMgr.h>
#include <transport/Session.h>

using chip::ScopedNodeId;
using chip::SessionHandle;
using chip::Messaging::ExchangeManager;

namespace heating_monitor
{
    namespace controller
    {
        class identify_command
        {
        public:
            static identify_command &get_instance()
            {
                static identify_command instance;
                return instance;
            }

            esp_err_t send_identify_command(uint64_t node_id);

        private:
            identify_command()
                : on_device_connected_cb(on_device_connected_fcn, this), on_device_connection_failure_cb(on_device_connection_failure_fcn, this)
            {
            }
            ~identify_command() {}

            static void on_device_connected_fcn(void *context, ExchangeManager &exchangeMgr, const SessionHandle &sessionHandle);
            static void on_device_connection_failure_fcn(void *context, const ScopedNodeId &peerId, CHIP_ERROR error);

            static void send_command_success_callback(void *context, const chip::app::DataModel::NullObjectType &data);
            static void send_command_failure_callback(void *context, CHIP_ERROR error);

            chip::Callback::Callback<chip::OnDeviceConnected> on_device_connected_cb;
            chip::Callback::Callback<chip::OnDeviceConnectionFailure> on_device_connection_failure_cb;
        };

    } // namespace controller
} // namespace heating_monitor
