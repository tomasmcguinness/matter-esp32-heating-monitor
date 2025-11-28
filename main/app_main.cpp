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

static const char *TAG = "app_main";
uint16_t switch_endpoint_id = 0;

using chip::NodeId;
using chip::ScopedNodeId;
using chip::SessionHandle;
using chip::Controller::CommissioningParameters;
using chip::Messaging::ExchangeManager;

void vTaskCode(void *pvParameters)
{
    esp_matter::controller::pairing_command cmd = esp_matter::controller::pairing_command::get_instance();

    NodeId node_id = 0x1234;
    uint32_t pincode = 20202021;
    uint16_t disc = 3840;

    char *dataset = "0e080000000000000000000300001935060004001fffc002089f651677026f48070708fd9f6516770200000510d3ad39f0967b08debd26d32640a5dc8f03084d79486f6d6534300102ebf8041057aee90914b5d1097de9bb0818dc94690c0402a0f7f8";
    uint8_t *dataset_tlvs = (uint8_t *)dataset;
    uint8_t dataset_len = 198;

    esp_err_t result = cmd.pairing_ble_thread(node_id, pincode, disc, dataset_tlvs, dataset_len);
}

#pragma region WebServer

#define STACK_SIZE 200

static esp_err_t commissioning_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling commissioning post root");

    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    // Create the task, storing the handle.  Note that the passed parameter ucParameterToPass
    // must exist for the lifetime of the task, so in this case is declared static.  If it was just an
    // an automatic stack variable it might no longer exist, or at least have been corrupted, by the time
    // the new task attempts to access it.
    xTaskCreate(vTaskCode, "COMMISSION", STACK_SIZE, &ucParameterToPass, tskIDLE_PRIORITY, &xHandle);
    configASSERT(xHandle);

    // RendezvousParameters params = RendezvousParameters().SetSetupPINCode(pincode).SetDiscriminator(disc).SetPeerAddress(Transport::PeerAddress::BLE());
    // auto &controller_instance = esp_matter::controller::matter_controller_client::get_instance();

    // ESP_RETURN_ON_FALSE(controller_instance.get_commissioner()->GetPairingDelegate() == nullptr, ESP_ERR_INVALID_STATE, TAG, "There is already a pairing process");

    // // THIS IS KEY!
    // //controller_instance.get_commissioner()->RegisterPairingDelegate(&pairing_command::get_instance());

    // ByteSpan dataset_span(dataset_tlvs, dataset_len);
    // CommissioningParameters commissioning_params = CommissioningParameters().SetThreadOperationalDataset(dataset_span);
    // NodeId commissioner_node_id = controller_instance.get_commissioner()->GetNodeId();

    // // if (pairing_command::get_instance().m_icd_registration) {
    // //     pairing_command::get_instance().m_device_is_icd = false;
    // //     commissioning_params.SetICDRegistrationStrategy(pairing_command::get_instance().m_icd_registration_strategy)
    // //         .SetICDClientType(app::Clusters::IcdManagement::ClientTypeEnum::kPermanent)
    // //         .SetICDCheckInNodeId(commissioner_node_id)
    // //         .SetICDMonitoredSubject(commissioner_node_id)
    // //         .SetICDSymmetricKey(pairing_command::get_instance().m_icd_symmetric_key);
    // //}
    // controller_instance.get_commissioner()->PairDevice(node_id, params, commissioning_params);

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");

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

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    const httpd_uri_t commissioning_post_uri = {
        .uri = "/commissioning",
        .method = HTTP_POST,
        .handler = commissioning_post_handler,
        .user_ctx = NULL};

    const httpd_uri_t wildcard_get_uri = {
        .uri = "/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = wildcard_get_handler,
        .user_ctx = NULL};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_register_uri_handler(server, &commissioning_post_uri);
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
