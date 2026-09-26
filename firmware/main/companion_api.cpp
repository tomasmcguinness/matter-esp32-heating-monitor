#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "esp_log.h"
#include "esp_netif.h"

#include "cJSON.h"

#include "device_identity.h"
#include "companion_api.h"
#include "managers/node_manager.h"
#include "node_api.h"

#include "utilities/TokenIterator.h"
#include "utilities/UrlTokenBindings.h"

static const char *TAG = "companion_api";

// Injected by companion_api_register(), the way history_logger_init() takes home_manager.
static node_manager_t *s_node_manager;

// A string field the contract types as "string or null". An unset name reaches us as NULL or as
// "", and the app falls back to the product name on null but shows an empty row for "", so both
// are reported as null.
static void add_optional_string(cJSON *obj, const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0')
    {
        cJSON_AddNullToObject(obj, key);
    }
    else
    {
        cJSON_AddStringToObject(obj, key, value);
    }
}

// The node array GET /api/companion/nodes returns.
//
// This is deliberately **not** the array /api/nodes serves. That one feeds our web UI and carries
// whatever the UI needs; this one carries what the app needs and nothing else, which is the whole
// point of the separate namespace.
//
// The contract lists more fields than are emitted here, and every one of them is optional with a
// documented default. What the app actually uses is the node's name (`nodeName`, falling back to
// `productName` then the id), `vendorName` for the subtitle, `hasSubscription`, and the endpoint
// list with its device types -- which is exactly what matter-esp32-controller and
// matter-esp32-home-energy-manager send. Four fields are therefore left out on purpose:
//
//   isIcd, powerSource, batteryPercent, batteryVoltage, measuredValue
//     Real readings, but the app does not display them, and a device list refreshed on every pull
//     should not carry a per-endpoint sensor feed. They stay on /api/nodes, where the web UI shows
//     them.
//
//   extAddress
//     No working source: ThreadNetworkDiagnostics::ExtAddress is subscribed per node but reads 0
//     for every one of them. The contract's own advice is to leave a field out rather than invent
//     it, and a missing value and a 0 decode identically on the app's side.
//
// Adding a field here means the app has a use for it. If the web UI needs something, it goes in
// build_nodes_json() instead.
static cJSON *build_companion_nodes_json(void)
{
    cJSON *root = cJSON_CreateArray();

    if (s_node_manager == NULL)
    {
        return root;
    }

    for (matter_node_t *node = s_node_manager->node_list; node != NULL; node = node->next)
    {
        cJSON *jNode = cJSON_CreateObject();

        cJSON_AddNumberToObject(jNode, "nodeId", (double)node->node_id);

        add_optional_string(jNode, "vendorName", node->vendor_name);
        add_optional_string(jNode, "productName", node->product_name);
        add_optional_string(jNode, "nodeName", node->name);

        cJSON_AddBoolToObject(jNode, "hasSubscription", node->has_subscription);

        cJSON *endpoints = cJSON_AddArrayToObject(jNode, "endpoints");

        for (uint16_t i = 0; i < node->endpoints_count; i++)
        {
            const endpoint_entry_t *endpoint = &node->endpoints[i];

            cJSON *jEndpoint = cJSON_CreateObject();

            cJSON_AddNumberToObject(jEndpoint, "endpointId", endpoint->endpoint_id);
            add_optional_string(jEndpoint, "endpointName", endpoint->name);

            cJSON *device_types = cJSON_AddArrayToObject(jEndpoint, "deviceTypes");

            for (uint8_t j = 0; j < endpoint->device_type_count; j++)
            {
                cJSON_AddItemToArray(device_types, cJSON_CreateNumber((double)endpoint->device_type_ids[j]));
            }

            cJSON_AddItemToArray(endpoints, jEndpoint);
        }

        cJSON_AddItemToArray(root, jNode);
    }

    return root;
}

// Pulls :nodeId out of a /api/companion/nodes/... URI. `template_path` has to be writable, which
// is why every caller keeps its own char[] -- TokenIterator chops the string up in place.
// UrlTokenBindings::hasBinding() only looks at the pattern, so it says yes whether or not the
// request actually had that many segments -- get() is what returns NULL, and every read of it has
// to be checked.
static uint64_t node_id_from_uri(httpd_req_t *req, char *template_path)
{
    auto templateItr = std::make_shared<TokenIterator>(template_path, strlen(template_path), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    const char *node_id = bindings.get("nodeId");

    return node_id == NULL ? 0 : strtoull(node_id, NULL, 10);
}

// The app calls this as soon as the user types in an address, so a bad entry fails there rather
// than on the device list. It ignores the body and takes any 2xx as success; the fields are for
// the Settings page, which shows `url` as the address to enter into the app.
//
// `url` is built from the raw IPv4 address and **not** from MDNS_HOSTNAME, because this board does
// not answer to heating-monitor.local: CONFIG_USE_MINIMAL_MDNS=y hands UDP 5353 to CHIP's own
// responder, so start_mdns_service() in app_main.cpp is disabled and nothing advertises that name.
// Handing it out anyway sends the app off to whatever else on the network answers, which returns a
// web page and reads back as an unparseable response with no trace of a request reaching us.
static esp_err_t companion_info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting device info...");

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "name", DEVICE_NAME);

    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_ip_info_t ip_info;

    if (eth_netif != NULL && esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
    {
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));

        char url[24];
        snprintf(url, sizeof(url), "http://%s", ip);

        cJSON_AddStringToObject(root, "ip", ip);
        cJSON_AddStringToObject(root, "url", url);
    }
    else
    {
        cJSON_AddNullToObject(root, "ip");
        cJSON_AddNullToObject(root, "url");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t companion_nodes_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all nodes ...");

    cJSON *root = build_companion_nodes_json();
    char *json = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    // httpd_resp_sendstr() turns a NULL into an empty 200, which the app can only report as an
    // unreadable response. A print that failed is out of heap, so say so.
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Could not print the node list");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Ran out of memory building the device list", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sending %u bytes of nodes", (unsigned)strlen(json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_sendstr(req, json);

    cJSON_free(json);

    return ESP_OK;
}

// Holds the connection open until pairing finishes, which is what lets the app report a real
// result: its Matter extension has no other way to learn whether the device joined the fabric.
static esp_err_t companion_nodes_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Commissioning a node");

    cJSON *root = read_json_body(req);

    if (root == NULL)
    {
        return ESP_OK;
    }

    const cJSON *setupCodeJSON = cJSON_GetObjectItemCaseSensitive(root, "setupCode");

    if (!cJSON_IsString(setupCodeJSON) || setupCodeJSON->valuestring == NULL)
    {
        ESP_LOGE(TAG, "Request is missing a setupCode");
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "setupCode is required", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // `inUse` is in the request but there is nothing to do with it: the app only ever sends false,
    // because the system setup flow hands over devices that aren't on a fabric yet, and this
    // controller has no separate path for one that is.
    ESP_LOGI(TAG, "Setup Code: %s", setupCodeJSON->valuestring);

    commission_outcome_t outcome;
    commission_node(setupCodeJSON->valuestring, &outcome);

    cJSON_Delete(root);

    return send_commission_outcome(req, &outcome);
}

// The app calls this straight after commissioning with the name the user typed into the system
// setup sheet, so the device is called the same thing here as it is on the phone.
static esp_err_t companion_node_put_handler(httpd_req_t *req)
{
    char templatePath[] = "/api/companion/nodes/:nodeId/:action";
    auto templateItr = std::make_shared<TokenIterator>(templatePath, strlen(templatePath), '/');
    UrlTokenBindings bindings(templateItr, req->uri);

    const char *action = bindings.get("action");

    if (action == NULL || strcmp("update", action) != 0)
    {
        ESP_LOGW(TAG, "Unknown action in %s", req->uri);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Unknown action", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const char *node_id_token = bindings.get("nodeId");
    uint64_t node_id = node_id_token == NULL ? 0 : strtoull(node_id_token, NULL, 10);

    ESP_LOGI(TAG, "Renaming node %llu", node_id);

    cJSON *root = read_json_body(req);

    if (root == NULL)
    {
        return ESP_OK;
    }

    const cJSON *nameJSON = cJSON_GetObjectItemCaseSensitive(root, "name");

    if (!cJSON_IsString(nameJSON) || nameJSON->valuestring == NULL)
    {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "name is required", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t err = rename_node(node_id, nameJSON->valuestring);

    cJSON_Delete(root);

    if (err == ESP_ERR_NOT_FOUND)
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "No such node", HTTPD_RESP_USE_STRLEN);
    }
    else if (err != ESP_OK)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Could not save the name", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Done", HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

static esp_err_t companion_node_delete_handler(httpd_req_t *req)
{
    char templatePath[] = "/api/companion/nodes/:nodeId";
    uint64_t node_id = node_id_from_uri(req, templatePath);

    if (unpair_node(node_id) != ESP_OK)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Could not remove the device", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

esp_err_t companion_api_register(httpd_handle_t server, node_manager_t *node_manager)
{
    s_node_manager = node_manager;

    // "/api/companion/nodes" is registered before "/api/companion/nodes/*" so the exact path can
    // never be swallowed by the wildcard one.
    static const httpd_uri_t uris[] = {
        {.uri = "/api/companion/info",
         .method = HTTP_GET,
         .handler = companion_info_get_handler,
         .user_ctx = NULL},
        {.uri = "/api/companion/nodes",
         .method = HTTP_GET,
         .handler = companion_nodes_get_handler,
         .user_ctx = NULL},
        {.uri = "/api/companion/nodes",
         .method = HTTP_POST,
         .handler = companion_nodes_post_handler,
         .user_ctx = NULL},
        {.uri = "/api/companion/nodes/*",
         .method = HTTP_PUT,
         .handler = companion_node_put_handler,
         .user_ctx = NULL},
        {.uri = "/api/companion/nodes/*",
         .method = HTTP_DELETE,
         .handler = companion_node_delete_handler,
         .user_ctx = NULL},
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
    {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);

        if (err != ESP_OK)
        {
            // ESP_ERR_HTTPD_HANDLERS_FULL here means config.max_uri_handlers in start_webserver()
            // is too low. A route that fails to register falls through to the "/*" wildcard, which
            // used to answer it with the web app and now answers 404.
            ESP_LOGE(TAG, "Failed to register %s: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }

        // Logged per route so the boot output proves the surface is live: if the app still can't
        // list devices with these lines present, the request is not arriving at all.
        ESP_LOGI(TAG, "Registered %s", uris[i].uri);
    }

    return ESP_OK;
}
