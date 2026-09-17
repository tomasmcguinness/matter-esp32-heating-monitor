#include "ha_discovery.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "ha_discovery";

#define HOME_STATE_TOPIC "heating_monitor/home"
#define PROJECT_URL      "https://github.com/tomasmcguinness/matter-esp32-heating-monitor"

// The quantities update_home() puts on HOME_STATE_TOPIC, and how Home Assistant should read them.
// See the cJSON_Add* calls at the end of update_home() in managers/calculations_manager.cpp --
// the `key` column must match those key names exactly.
//
// Deliberately absent: internal_temperature and dhw_running. Nothing sets has_internal_temperature
// or has_dhw_running anywhere, so both would be permanently unknown entities, which in HA cannot
// be told apart from a sensor that has broken. Adding either later is one row here, picked up on
// the next reconnect -- note dhw_running needs "binary_sensor" rather than "sensor".
struct ha_sensor_t {
    const char *key;          // key in the home JSON payload, and the component id
    const char *name;         // entity name shown in Home Assistant
    const char *device_class; // NULL to omit
    const char *unit;         // NULL to omit
};

static const ha_sensor_t HOME_SENSORS[] = {
    {"heat_power",                           "Heat output",         "power",            "W"},
    {"electrical_power",                     "Electricity input",   "power",            "W"},
    {"flow_temperature",                     "Flow temperature",    "temperature",      "°C"},
    {"return_temperature",                   "Return temperature",  "temperature",      "°C"},
    {"flow_rate",                            "Flow rate",           "volume_flow_rate", "L/h"},
    {"outdoor_temperature",                  "Outdoor temperature", "temperature",      "°C"},
    {"cop",                                  "COP",                 NULL,               NULL},
    {"electrical_voltage",                   "Voltage",             "voltage",          "V"},
    {"electrical_current",                   "Current",             "current",          "A"},
    {"total_predicted_heat_loss_per_degree", "Predicted heat loss", NULL,               "W/°C"},
    {"total_measured_heat_loss_per_degree",  "Measured heat loss",  NULL,               "W/°C"},
};

// The device id has to survive reboots and firmware updates, or Home Assistant creates a new
// device each time and orphans the old entities. The Ethernet MAC is the only thing to hand that
// is both stable and unique per board -- MDNS_HOSTNAME is a compile-time constant and would
// collide if a second unit ever joined the same broker.
static void build_device_ids(char *id_out, size_t id_len, char *mac_out, size_t mac_len)
{
    uint8_t mac[6] = {0};

    esp_err_t err = esp_read_mac(mac, ESP_MAC_ETH);
    if (err != ESP_OK) {
        // Falling back to a fixed id is better than publishing nothing: a single unit still works,
        // and the log says why two would collide.
        ESP_LOGW(TAG, "esp_read_mac failed (%s); using a fixed device id", esp_err_to_name(err));
    }

    snprintf(id_out, id_len, "heating_monitor_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(mac_out, mac_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static cJSON *build_component(const ha_sensor_t *sensor, const char *device_id)
{
    cJSON *component = cJSON_CreateObject();

    cJSON_AddStringToObject(component, "p", "sensor");
    cJSON_AddStringToObject(component, "name", sensor->name);

    if (sensor->device_class) {
        cJSON_AddStringToObject(component, "device_class", sensor->device_class);
    }
    if (sensor->unit) {
        cJSON_AddStringToObject(component, "unit_of_measurement", sensor->unit);
    }

    // Everything here is an instantaneous reading, so it belongs in HA's long-term statistics as
    // a measurement rather than a total.
    cJSON_AddStringToObject(component, "state_class", "measurement");

    // add_reading_or_null() emits a real JSON null when a reading is absent, and a bare
    // {{ value_json.x }} renders that as the string "None", which HA logs errors over. Yielding
    // nothing instead leaves the entity unknown, which is what absent actually means.
    char value_template[160];
    snprintf(value_template, sizeof(value_template),
             "{%% if value_json.%s is not none %%}{{ value_json.%s }}{%% endif %%}",
             sensor->key, sensor->key);
    cJSON_AddStringToObject(component, "value_template", value_template);

    char unique_id[96];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", device_id, sensor->key);
    cJSON_AddStringToObject(component, "unique_id", unique_id);

    return component;
}

void ha_discovery_publish_online(esp_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }
    // Retained, so a subscriber that connects later still learns the device is up. The matching
    // "offline" is the client's last will, published by the broker if this device drops.
    esp_mqtt_client_publish(client, HA_AVAILABILITY_TOPIC, "online", 0, 0, 1);
}

void ha_discovery_announce_all(esp_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }

    char device_id[64];
    char mac_str[24];
    build_device_ids(device_id, sizeof(device_id), mac_str, sizeof(mac_str));

    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *root = cJSON_CreateObject();

    // Device-based discovery requires both dev and o, and dev must carry an identifier or HA
    // discards the payload.
    cJSON *device = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(device_id));
    cJSON_AddItemToObject(device, "ids", ids);
    cJSON_AddStringToObject(device, "name", "Heating Monitor");
    cJSON_AddStringToObject(device, "mf", "Tomas McGuinness");
    cJSON_AddStringToObject(device, "mdl", "ESP32-S3-ETH Heating Monitor");
    cJSON_AddStringToObject(device, "sw", app ? app->version : "unknown");
    cJSON_AddStringToObject(device, "cu", "http://heating-monitor.local");

    cJSON *connections = cJSON_CreateArray();
    cJSON *mac_pair = cJSON_CreateArray();
    cJSON_AddItemToArray(mac_pair, cJSON_CreateString("mac"));
    cJSON_AddItemToArray(mac_pair, cJSON_CreateString(mac_str));
    cJSON_AddItemToArray(connections, mac_pair);
    cJSON_AddItemToObject(device, "cns", connections);

    cJSON_AddItemToObject(root, "dev", device);

    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "Heating Monitor");
    cJSON_AddStringToObject(origin, "sw", app ? app->version : "unknown");
    cJSON_AddStringToObject(origin, "url", PROJECT_URL);
    cJSON_AddItemToObject(root, "o", origin);

    cJSON *components = cJSON_CreateObject();
    for (size_t i = 0; i < sizeof(HOME_SENSORS) / sizeof(HOME_SENSORS[0]); i++) {
        cJSON_AddItemToObject(components, HOME_SENSORS[i].key,
                              build_component(&HOME_SENSORS[i], device_id));
    }
    cJSON_AddItemToObject(root, "cmps", components);

    // Shared by every component, so each one does not have to repeat it.
    cJSON_AddStringToObject(root, "state_topic", HOME_STATE_TOPIC);
    cJSON_AddStringToObject(root, "availability_topic", HA_AVAILABILITY_TOPIC);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");

    char *payload = cJSON_PrintUnformatted(root);

    if (payload) {
        char config_topic[96];
        snprintf(config_topic, sizeof(config_topic), "homeassistant/device/%s/config", device_id);

        // Retained: Home Assistant replays retained discovery when its MQTT integration starts, so
        // without this the entities vanish on every HA restart until something republishes.
        int msg_id = esp_mqtt_client_publish(client, config_topic, payload, 0, 0, 1);

        ESP_LOGI(TAG, "Announced %u home entities to %s (%u bytes, msg_id %d)",
                 (unsigned)(sizeof(HOME_SENSORS) / sizeof(HOME_SENSORS[0])), config_topic,
                 (unsigned)strlen(payload), msg_id);

        if (msg_id < 0) {
            // The usual cause is the payload outrunning the client's out buffer -- see the
            // buffer.out_size set in start_mqtt_service().
            ESP_LOGE(TAG, "Discovery publish failed; is the MQTT out buffer large enough?");
        }

        cJSON_free(payload);
    } else {
        ESP_LOGE(TAG, "Could not serialise the discovery payload");
    }

    cJSON_Delete(root);
}
