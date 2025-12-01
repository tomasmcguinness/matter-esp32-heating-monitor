/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_ota.h>
#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_ot_config.h>
#include <esp_spiffs.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
#include <common_macros.h>

#include <app/server/Server.h>
#include <credentials/FabricTable.h>

#include <esp_http_server.h>

#include <string>

#include <esp_matter.h>
#include <lib/dnssd/Types.h>

#include "esp_spiffs.h"

#include <pairing_command.h>

static const char *TAG = "app_main";
uint16_t switch_endpoint_id = 0;

using chip::NodeId;
using chip::ScopedNodeId;
using chip::SessionHandle;
using chip::Controller::CommissioningParameters;
using chip::Messaging::ExchangeManager;

#pragma region WebServer

#define STACK_SIZE 200

static int char_to_int(char ch)
{
    if ('A' <= ch && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    else if ('a' <= ch && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    else if ('0' <= ch && ch <= '9')
    {
        return ch - '0';
    }
    return -1;
}

static bool convert_hex_str_to_bytes(const char *hex_str, uint8_t *bytes, uint8_t &bytes_len)
{
    if (!hex_str)
    {
        return false;
    }
    size_t hex_str_len = strlen(hex_str);
    if (hex_str_len == 0 || hex_str_len % 2 != 0 || hex_str_len / 2 > bytes_len)
    {
        return false;
    }
    bytes_len = hex_str_len / 2;
    for (size_t i = 0; i < bytes_len; ++i)
    {
        int byte_h = char_to_int(hex_str[2 * i]);
        int byte_l = char_to_int(hex_str[2 * i + 1]);
        if (byte_h < 0 || byte_l < 0)
        {
            return false;
        }
        bytes[i] = (byte_h << 4) + byte_l;
    }
    return true;
}

static esp_err_t nodes_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Commissioning a node");

    /*
    // This needs to be random
    NodeId node_id = 0x1234;

    // Get from the request
    uint32_t pincode = 20202021;
    uint16_t disc = 3840;

    chip::RendezvousParameters params = chip::RendezvousParameters().SetSetupPINCode(pincode).SetDiscriminator(disc).SetPeerAddress(chip::Transport::PeerAddress::BLE());
    auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();

    ESP_RETURN_ON_FALSE(controller_instance.get_commissioner()->GetPairingDelegate() == nullptr, ESP_ERR_INVALID_STATE, TAG, "There is already a pairing process");

    //controller_instance.get_commissioner()->RegisterPairingDelegate(&pairing_command::get_instance());

    uint8_t dataset_tlvs_buf[254];
    uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);

    // TODO Get this from configuration.
    char *dataset = "0e080000000000000000000300001935060004001fffc002089f651677026f48070708fd9f6516770200000510d3ad39f0967b08debd26d32640a5dc8f03084d79486f6d6534300102ebf8041057aee90914b5d1097de9bb0818dc94690c0402a0f7f8";

    if (!convert_hex_str_to_bytes(dataset, dataset_tlvs_buf, dataset_tlvs_len))
    {
        return ESP_ERR_INVALID_ARG;
    }

    chip::ByteSpan dataset_span(dataset_tlvs_buf, dataset_tlvs_len);
    chip::Controller::CommissioningParameters commissioning_params = CommissioningParameters().SetThreadOperationalDataset(dataset_span);
    chip::NodeId commissioner_node_id = controller_instance.get_commissioner()->GetNodeId();

    // if (pairing_command::get_instance().m_icd_registration) {
    //     pairing_command::get_instance().m_device_is_icd = false;
    //     commissioning_params.SetICDRegistrationStrategy(pairing_command::get_instance().m_icd_registration_strategy)
    //         .SetICDClientType(app::Clusters::IcdManagement::ClientTypeEnum::kPermanent)
    //         .SetICDCheckInNodeId(commissioner_node_id)
    //         .SetICDMonitoredSubject(commissioner_node_id)
    //         .SetICDSymmetricKey(pairing_command::get_instance().m_icd_symmetric_key);
    // }

    // controller_instance.get_commissioner()->PairDevice(node_id, params, commissioning_params);
    */

    NodeId node_id = 0x1234;

    // Get from the request
    uint32_t pincode = 20202021;
    uint16_t disc = 3840;

    uint8_t dataset_tlvs_buf[254];
    uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);

    // TODO Get this from configuration.
    char *dataset = "0e080000000000000000000300001935060004001fffc002089f651677026f48070708fd9f6516770200000510d3ad39f0967b08debd26d32640a5dc8f03084d79486f6d6534300102ebf8041057aee90914b5d1097de9bb0818dc94690c0402a0f7f8";

    if (!convert_hex_str_to_bytes(dataset, dataset_tlvs_buf, dataset_tlvs_len))
    {
        return ESP_ERR_INVALID_ARG;
    }

    heating_monitor::controller::pairing_command cmd = heating_monitor::controller::pairing_command::get_instance();

    esp_err_t result = cmd.pairing_ble_thread(node_id, pincode, disc, dataset_tlvs_buf, dataset_tlvs_len);

    // Old way
    /*
    esp_matter::controller::pairing_command cmd = esp_matter::controller::pairing_command::get_instance();

    // TODO Random
    NodeId node_id = 0x1234;

    // Get from the request
    uint32_t pincode = 20202021;
    uint16_t disc = 3840;

    uint8_t dataset_tlvs_buf[254];
    uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);

    // TODO Get this from configuration.
    char *dataset = "0e080000000000000000000300001935060004001fffc002089f651677026f48070708fd9f6516770200000510d3ad39f0967b08debd26d32640a5dc8f03084d79486f6d6534300102ebf8041057aee90914b5d1097de9bb0818dc94690c0402a0f7f8";

    if (!convert_hex_str_to_bytes(dataset, dataset_tlvs_buf, dataset_tlvs_len))
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = cmd.pairing_ble_thread(node_id, pincode, disc, dataset_tlvs_buf, dataset_tlvs_len);

    */

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "Commissioning Started", 21);

    return ESP_OK;
}

static esp_err_t write_index_html(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve root");

    httpd_resp_set_type(req, "text/html");

    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t bootstrap_css_start[] asm("_binary_index_html_start");
    extern const uint8_t bootstrap_css_end[] asm("_binary_index_html_end");
    const size_t bootcss_size = ((bootstrap_css_end - 1) - bootstrap_css_start);
    httpd_resp_send(req, (const char *)bootstrap_css_start, bootcss_size);

    return ESP_OK;
}

static esp_err_t write_app_js(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve js");

    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t js_file_start[] asm("_binary_app_js_start");
    extern const uint8_t js_file_end[] asm("_binary_app_js_end");
    const size_t js_file_size = ((js_file_end - 1) - js_file_start);
    httpd_resp_send(req, (const char *)js_file_start, js_file_size);

    return ESP_OK;
}

static esp_err_t write_app_css(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve css");

    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");

    extern const uint8_t css_file_start[] asm("_binary_app_css_start");
    extern const uint8_t css_file_end[] asm("_binary_app_css_end");
    const size_t css_file_size = ((css_file_end - 1) - css_file_start);
    httpd_resp_send(req, (const char *)css_file_start, css_file_size);

    return ESP_OK;
}

static esp_err_t wildcard_get_handler(httpd_req_t *req)
{
    const char *filename = req->uri;

    if (!filename)
    {
        ESP_LOGE(TAG, "Filename is too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_OK;
    }

    if (strcmp(filename, "/") == 0)
    {
        return write_index_html(req);
    }
    else if (strcmp(filename, "/app.js") == 0)
    {
        return write_app_js(req);
    }
    else if (strcmp(filename, "/app.css") == 0)
    {
        return write_app_css(req);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    ESP_LOGI(TAG, "Configuring webserver...");

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.max_uri_handlers = 11;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 20480;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    const httpd_uri_t nodes_post_uri = {
        .uri = "/nodes",
        .method = HTTP_POST,
        .handler = nodes_post_handler,
        .user_ctx = NULL};

    // const httpd_uri_t nodes_get_uri = {
    //     .uri = "/nodes",
    //     .method = HTTP_GET,
    //     .handler = nodes_get_handler,
    //     .user_ctx = NULL};

    const httpd_uri_t wildcard_get_uri = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = wildcard_get_handler,
        .user_ctx = NULL};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_register_uri_handler(server, &nodes_post_uri);
        // httpd_register_uri_handler(server, &nodes_get_uri);
        httpd_register_uri_handler(server, &wildcard_get_uri);

        ESP_LOGI(TAG, "WebService is up and running!");

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");

    return NULL;
}

#pragma endregion

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "kInterfaceIpAddressChanged");
        break;
    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        ESP_LOGI(TAG, "kESPSystemEvent");

        if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
            event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP)
        {

            start_webserver();

#if CONFIG_OPENTHREAD_BORDER_ROUTER
            static bool sThreadBRInitialized = false;
            if (!sThreadBRInitialized)
            {
                esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
                esp_openthread_lock_acquire(portMAX_DELAY);
                esp_openthread_border_router_init();
                esp_openthread_lock_release();
                sThreadBRInitialized = true;
            }
#endif
        }
        break;
    default:
        break;
    }
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize SPIFFS */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true};

    err = esp_vfs_spiffs_register(&conf);

    if (err != ESP_OK)
    {
        if (err == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (err == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(err));
        }
        return;
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif // CONFIG_ESP_MATTER_CONTROLLER_ENABLE
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    esp_matter::console::otcli_register_commands();
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
#endif // CONFIG_ENABLE_CHIP_SHELL
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
#ifdef CONFIG_AUTO_UPDATE_RCP
    esp_vfs_spiffs_conf_t rcp_fw_conf = {
        .base_path = "/rcp_fw", .partition_label = "rcp_fw", .max_files = 10, .format_if_mount_failed = false};
    if (ESP_OK != esp_vfs_spiffs_register(&rcp_fw_conf))
    {
        ESP_LOGE(TAG, "Failed to mount rcp firmware storage");
        return;
    }
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    openthread_init_br_rcp(&rcp_update_config);
#endif
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE

    ESP_LOGI(TAG, "Setup controller client and commissioner...");

    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    esp_matter::lock::chip_stack_unlock();

    ESP_LOGI(TAG, "Getting fabric table...");

    auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();

    auto fabricTable = controller_instance.get_commissioner()->GetFabricTable();

    uint8_t fabricCount = fabricTable->FabricCount();

    ESP_LOGI(TAG, "There are %u fabrics", fabricCount);

#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
}
