/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include "nvs_flash.h"
#include "nvs.h"

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_read_command.h>
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

#include "cJSON.h"

static const char *TAG = "app_main";

#define NVS_NAMESPACE "matter"
#define NVS_NODE_LIST_KEY "node_list"

using chip::NodeId;
using chip::ScopedNodeId;
using chip::SessionHandle;
using chip::Controller::CommissioningParameters;
using chip::Messaging::ExchangeManager;

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip;
using namespace chip::app::Clusters;

#pragma region Command Callbacks

static void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data)
{
    ChipLogProgress(chipTool, "Nodeid: %016llx  Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI " DataVersion: %" PRIu32,
                    remote_node_id, path.mEndpointId, ChipLogValueMEI(path.mClusterId), ChipLogValueMEI(path.mAttributeId),
                    path.mDataVersion.ValueOr(0));

    if (path.mEndpointId == 0x0 && path.mClusterId == Descriptor::Id && path.mAttributeId == Descriptor::Attributes::PartsList::Id)
    {
        // cb_data *_data = new callback_data(remote_node_id, path, data);
        // parse_cb_response(_data);
    }

    else if (path.mEndpointId != 0x0)
    {
        // callback_data *_data = new callback_data(remote_node_id, path, data);
        // parse_cb_response(_data);
    }
}

static void attribute_data_read_done(uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_path, const ScopedMemoryBufferWithSize<EventPathParams> &event_path)
{
    ESP_LOGI(TAG, "\nRead Info done for Nodeid: %016llx  Endpoint: %u Cluster: " ChipLogFormatMEI " Attribute " ChipLogFormatMEI "\n",
             remote_node_id, attr_path[0].mEndpointId, ChipLogValueMEI(attr_path[0].mClusterId), ChipLogValueMEI(attr_path[0].mAttributeId));

    // node_id_list_index++;

    // if (node_id_list_index < node_id_list.size())
    //     //_read_node_wild_info(node_id_list[node_id_list_index]);
    // else
    //     //print_data_model();
}

static void on_commissioning_success_callback(ScopedNodeId peer_id)
{
    ESP_LOGI(TAG, "commissioning_success_callback invoked!");

    uint64_t nodeId = peer_id.GetNodeId();
    char nodeIdStr[32];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "%" PRIu64, nodeId);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS node!");
        return;
    }

    err = nvs_set_i32(nvs_handle, nodeIdStr, 1);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write node!");
        return;
    }

    uint16_t endpointId = 0x0000;
    uint32_t clusterId = Descriptor::Id;
    uint32_t attributeId = Descriptor::Attributes::PartsList::Id;

    esp_matter::controller::read_command *read_descriptor_command = chip::Platform::New<read_command>(nodeId, endpointId, clusterId, attributeId,
                                                                                                      esp_matter::controller::READ_ATTRIBUTE,
                                                                                                      attribute_data_cb,
                                                                                                      attribute_data_read_done,
                                                                                                      nullptr);
    read_descriptor_command->send_command();
}

#pragma endregion

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

    // TODO: This should be random!
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

    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .commissioning_success_callback = on_commissioning_success_callback};

    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);

    // TODO Use returned status
    esp_matter::controller::pairing_ble_thread(node_id, pincode, disc, dataset_tlvs_buf, dataset_tlvs_len);

    // This process is asynchronous, so we return 202 Accepted
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, "Commissioning Started", 21);

    return ESP_OK;
}

static esp_err_t nodes_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all nodes...");

    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);

    cJSON *root = cJSON_CreateArray();

    while (res == ESP_OK)
    {
        cJSON *jNode = cJSON_CreateObject();

        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        ESP_LOGI(TAG, "Key: '%s'", info.key);
        res = nvs_entry_next(&it);

        cJSON_AddStringToObject(jNode, "nodeId", info.key);
        cJSON_AddItemToArray(root, jNode);
    }
    nvs_release_iterator(it);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 Accepted");

    const char *json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void *)json);
    cJSON_Delete(root);

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

    if (strcmp(filename, "/app.js") == 0)
    {
        return write_app_js(req);
    }
    else if (strcmp(filename, "/app.css") == 0)
    {
        return write_app_css(req);
    }
    else 
    {
        return write_index_html(req);
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

    const httpd_uri_t nodes_get_uri = {
        .uri = "/nodes",
        .method = HTTP_GET,
        .handler = nodes_get_handler,
        .user_ctx = NULL};

    const httpd_uri_t wildcard_get_uri = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = wildcard_get_handler,
        .user_ctx = NULL};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_register_uri_handler(server, &nodes_post_uri);
        httpd_register_uri_handler(server, &nodes_get_uri);
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
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "nvs_flash_init error");
    }
    ESP_ERROR_CHECK(err);

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

    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS storage [%s]!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(nvs_handle, "0x1234", 1);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write node!");
        return;
    }

#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
}
