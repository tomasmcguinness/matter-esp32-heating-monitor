#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "esp_log.h"
#include "esp_netif.h"

#include "cJSON.h"

#include "device_identity.h"
#include "companion_api.h"
#include "node_api.h"

#include "utilities/TokenIterator.h"
#include "utilities/UrlTokenBindings.h"

static const char *TAG = "companion_api";

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
// the Settings page, which shows them as the address to enter into the app.
static esp_err_t companion_info_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting device info...");

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "name", DEVICE_NAME);
    cJSON_AddStringToObject(root, "host", MDNS_HOSTNAME ".local");
    cJSON_AddStringToObject(root, "url", "http://" MDNS_HOSTNAME ".local");

    // mDNS on this board is an IPv4 delegated hostname and resolution is not always quick off a
    // phone, so hand out the raw address as something the user can fall back to.
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_ip_info_t ip_info;

    if (eth_netif != NULL && esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
    {
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(root, "ip", ip);
    }
    else
    {
        cJSON_AddNullToObject(root, "ip");
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

    cJSON *root = build_nodes_json();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

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

esp_err_t companion_api_register(httpd_handle_t server)
{
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
            ESP_LOGE(TAG, "Failed to register %s: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}
