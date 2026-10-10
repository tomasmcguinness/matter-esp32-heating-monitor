#include "ha_discovery.h"

#include <stdio.h>
#include <string.h>

#include <atomic>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "ha_discovery";

#define HOME_STATE_TOPIC "heating_monitor/home"
#define PROJECT_URL      "https://github.com/tomasmcguinness/matter-esp32-heating-monitor"

// A quantity a device publishes on its state topic, and how Home Assistant should read it. The
// `key` column must match the key in the state payload exactly -- see the cJSON_Add* and
// add_reading_or_null calls in managers/calculations_manager.cpp.
struct ha_sensor_t {
    const char *key;          // key in the state payload, and the component id
    const char *name;         // entity name shown in Home Assistant
    const char *device_class; // NULL to omit
    const char *unit;         // NULL to omit
    bool binary;              // a true/false in the payload, announced as a binary_sensor
};

// The quantities update_home() puts on HOME_STATE_TOPIC.
//
// heating_running and dhw_running are only set once a heat pump is picked on Edit Home. Without
// one they are permanently unknown entities, which in HA cannot be told apart from a sensor that
// has broken.
static const ha_sensor_t HOME_SENSORS[] = {
    {"heat_power",                           "Heat output",         "power",            "W"},
    {"electrical_power",                     "Electricity input",   "power",            "W"},
    {"flow_temperature",                     "Flow temperature",    "temperature",      "°C"},
    {"return_temperature",                   "Return temperature",  "temperature",      "°C"},
    {"flow_rate",                            "Flow rate",           "volume_flow_rate", "L/h"},
    {"outdoor_temperature",                  "Outdoor temperature", "temperature",      "°C"},
    {"average_internal_temperature",         "Average indoor temperature", "temperature", "°C"},
    {"cop",                                  "COP",                 NULL,               NULL},
    {"electrical_voltage",                   "Voltage",             "voltage",          "V"},
    {"electrical_current",                   "Current",             "current",          "A"},
    {"total_predicted_heat_loss_per_degree", "Predicted heat loss", NULL,               "W/°C"},
    {"total_measured_heat_loss_per_degree",  "Measured heat loss",  NULL,               "W/°C"},
    {"heating_running",                      "Heating running",     "running",          NULL, true},
    {"dhw_running",                          "Hot water running",   "running",          NULL, true},
};

// The quantities update_room_heat_loss() puts on a room's state topic.
static const ha_sensor_t ROOM_SENSORS[] = {
    {"current_temperature",            "Temperature",            "temperature", "°C"},
    {"measured_heat_loss_per_degree",  "Measured heat loss",     NULL,          "W/°C"},
    {"predicted_heat_loss_per_degree", "Predicted heat loss",    NULL,          "W/°C"},
    {"heat_loss_difference",           "Heat loss vs predicted", NULL,          "%"},
};

// The quantities update_radiator_outputs() puts on a radiator's state topic.
static const ha_sensor_t RADIATOR_SENSORS[] = {
    {"flow_temperature",       "Flow temperature",       "temperature", "°C"},
    {"return_temperature",     "Return temperature",     "temperature", "°C"},
    {"mean_water_temperature", "Mean water temperature", "temperature", "°C"},
    {"output",                 "Heat output",            "power",       "W"},
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

// The device id has to survive reboots and firmware updates, or Home Assistant creates a new
// device each time and orphans the old entities. The Ethernet MAC is the only thing to hand that
// is both stable and unique per board -- MDNS_HOSTNAME is a compile-time constant and would
// collide if a second unit ever joined the same broker.
//
// Read once and cached: every room and radiator id is built from it.
static char s_device_id[32];
static char s_mac_str[24];

// Set on every connect, cleared once the rooms and radiators have been announced.
static std::atomic<bool> s_children_pending{false};

static const char *home_device_id(void)
{
    if (s_device_id[0]) {
        return s_device_id;
    }

    uint8_t mac[6] = {0};

    esp_err_t err = esp_read_mac(mac, ESP_MAC_ETH);
    if (err != ESP_OK) {
        // Falling back to a fixed id is better than publishing nothing: a single unit still works,
        // and the log says why two would collide.
        ESP_LOGW(TAG, "esp_read_mac failed (%s); using a fixed device id", esp_err_to_name(err));
    }

    snprintf(s_mac_str, sizeof(s_mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_device_id, sizeof(s_device_id), "heating_monitor_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return s_device_id;
}

static bool has_mqtt_name(const char *mqtt_name)
{
    return mqtt_name && mqtt_name[0];
}

// Reduces an MQTT name to what HA accepts in an entity id -- lowercase letters, digits and
// underscores -- so "Master Bedroom" becomes master_bedroom rather than an id HA rejects.
static void slugify(const char *in, char *out, size_t out_len)
{
    size_t n = 0;

    for (; *in && n + 1 < out_len; in++) {
        char c = *in;
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[n++] = ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) ? c : '_';
    }

    out[n] = '\0';
}

// Rooms and radiators are keyed on their MQTT name, which is what the user sees in HA's entity
// ids. The kind stays in the device id because a room and its radiator commonly share a name.
// The id is only the fallback for one with no MQTT name.
//
// Keying on a name that can change means a rename is a new device: callers remove the config
// under the old name before announcing under the new one.
static void child_device_id(const char *kind, uint8_t id, const char *mqtt_name, char *out, size_t out_len)
{
    if (has_mqtt_name(mqtt_name)) {
        char slug[64];
        slugify(mqtt_name, slug, sizeof(slug));
        snprintf(out, out_len, "%s_%s_%s", home_device_id(), kind, slug);
    } else {
        snprintf(out, out_len, "%s_%s_%u", home_device_id(), kind, id);
    }
}

// What each entity id starts with: sensor.master_bedroom_current_temperature for a room,
// sensor.master_bedroom_radiator_flow_temperature for a radiator.
static void room_entity_prefix(uint8_t room_id, const char *mqtt_name, char *out, size_t out_len)
{
    if (has_mqtt_name(mqtt_name)) {
        slugify(mqtt_name, out, out_len);
    } else {
        snprintf(out, out_len, "room_%u", room_id);
    }
}

static void radiator_entity_prefix(uint8_t radiator_id, const char *mqtt_name, char *out, size_t out_len)
{
    if (has_mqtt_name(mqtt_name)) {
        char slug[64];
        slugify(mqtt_name, slug, sizeof(slug));
        snprintf(out, out_len, "%s_radiator", slug);
    } else {
        snprintf(out, out_len, "radiator_%u", radiator_id);
    }
}

static void config_topic_for(const char *device_id, char *out, size_t out_len)
{
    snprintf(out, out_len, "homeassistant/device/%s/config", device_id);
}

void ha_room_state_topic(const room_t *room, char *out, size_t out_len)
{
    if (has_mqtt_name(room->mqtt_name)) {
        snprintf(out, out_len, "heating_monitor/rooms/%s", room->mqtt_name);
    } else {
        snprintf(out, out_len, "heating_monitor/rooms/room_%u", room->room_id);
    }
}

void ha_radiator_state_topic(const radiator_t *radiator, char *out, size_t out_len)
{
    if (has_mqtt_name(radiator->mqtt_name)) {
        snprintf(out, out_len, "heating_monitor/radiators/%s", radiator->mqtt_name);
    } else {
        snprintf(out, out_len, "heating_monitor/radiators/radiator_%u", radiator->radiator_id);
    }
}

static cJSON *build_component(const ha_sensor_t *sensor, const char *device_id, const char *entity_prefix)
{
    cJSON *component = cJSON_CreateObject();

    const char *platform = sensor->binary ? "binary_sensor" : "sensor";

    cJSON_AddStringToObject(component, "p", platform);
    cJSON_AddStringToObject(component, "name", sensor->name);

    if (sensor->device_class) {
        cJSON_AddStringToObject(component, "device_class", sensor->device_class);
    }
    if (sensor->unit) {
        cJSON_AddStringToObject(component, "unit_of_measurement", sensor->unit);
    }

    char value_template[224];

    if (sensor->binary) {
        // A binary sensor takes ON and OFF, and "None" for unknown. An empty render would leave
        // the last state showing instead, so an absent flag has to say None outright.
        snprintf(value_template, sizeof(value_template),
                 "{%% if value_json.%s is none %%}None{%% else %%}"
                 "{{ 'ON' if value_json.%s else 'OFF' }}{%% endif %%}",
                 sensor->key, sensor->key);
    } else {
        // Everything here is an instantaneous reading, so it belongs in HA's long-term statistics
        // as a measurement rather than a total.
        cJSON_AddStringToObject(component, "state_class", "measurement");

        // add_reading_or_null() emits a real JSON null when a reading is absent, and a bare
        // {{ value_json.x }} renders that as the string "None", which HA logs errors over. Yielding
        // nothing instead leaves the entity unknown, which is what absent actually means.
        snprintf(value_template, sizeof(value_template),
                 "{%% if value_json.%s is not none %%}{{ value_json.%s }}{%% endif %%}",
                 sensor->key, sensor->key);
    }

    cJSON_AddStringToObject(component, "value_template", value_template);

    char unique_id[128];
    snprintf(unique_id, sizeof(unique_id), "%s_%s", device_id, sensor->key);
    cJSON_AddStringToObject(component, "unique_id", unique_id);

    // Without this HA derives the entity id from the device and entity names, so it would change
    // with the display name. HA only applies it when the entity is first created.
    if (entity_prefix) {
        char default_entity_id[128];
        snprintf(default_entity_id, sizeof(default_entity_id), "%s.%s_%s", platform, entity_prefix, sensor->key);
        cJSON_AddStringToObject(component, "default_entity_id", default_entity_id);
    }

    return component;
}

// What distinguishes one announced device from another. Everything else in the payload is common.
struct ha_device_t {
    const char *id;
    const char *name;
    const char *model;
    const char *suggested_area; // NULL to omit
    bool is_child;              // linked to the Heating Monitor via via_device
    const char *state_topic;
    const char *entity_prefix;  // NULL to leave the entity ids to HA
    const ha_sensor_t *sensors;
    size_t sensor_count;
};

static void publish_device(esp_mqtt_client_handle_t client, const ha_device_t *d)
{
    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *root = cJSON_CreateObject();

    // Device-based discovery requires both dev and o, and dev must carry an identifier or HA
    // discards the payload.
    cJSON *device = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(d->id));
    cJSON_AddItemToObject(device, "ids", ids);
    cJSON_AddStringToObject(device, "name", d->name);
    cJSON_AddStringToObject(device, "mf", "Tomas McGuinness");
    cJSON_AddStringToObject(device, "mdl", d->model);
    cJSON_AddStringToObject(device, "sw", app ? app->version : "unknown");

    if (d->suggested_area) {
        cJSON_AddStringToObject(device, "sa", d->suggested_area);
    }

    if (d->is_child) {
        cJSON_AddStringToObject(device, "via_device", home_device_id());
    } else {
        cJSON_AddStringToObject(device, "cu", "http://heating-monitor.local");

        cJSON *connections = cJSON_CreateArray();
        cJSON *mac_pair = cJSON_CreateArray();
        cJSON_AddItemToArray(mac_pair, cJSON_CreateString("mac"));
        cJSON_AddItemToArray(mac_pair, cJSON_CreateString(s_mac_str));
        cJSON_AddItemToArray(connections, mac_pair);
        cJSON_AddItemToObject(device, "cns", connections);
    }

    cJSON_AddItemToObject(root, "dev", device);

    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "Heating Monitor");
    cJSON_AddStringToObject(origin, "sw", app ? app->version : "unknown");
    cJSON_AddStringToObject(origin, "url", PROJECT_URL);
    cJSON_AddItemToObject(root, "o", origin);

    cJSON *components = cJSON_CreateObject();
    for (size_t i = 0; i < d->sensor_count; i++) {
        cJSON_AddItemToObject(components, d->sensors[i].key, build_component(&d->sensors[i], d->id, d->entity_prefix));
    }
    cJSON_AddItemToObject(root, "cmps", components);

    // Shared by every component, so each one does not have to repeat it.
    cJSON_AddStringToObject(root, "state_topic", d->state_topic);
    cJSON_AddStringToObject(root, "availability_topic", HA_AVAILABILITY_TOPIC);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");

    char *payload = cJSON_PrintUnformatted(root);

    if (payload) {
        char config_topic[128];
        config_topic_for(d->id, config_topic, sizeof(config_topic));

        // Retained: Home Assistant replays retained discovery when its MQTT integration starts, so
        // without this the entities vanish on every HA restart until something republishes.
        int msg_id = esp_mqtt_client_publish(client, config_topic, payload, 0, 0, 1);

        ESP_LOGI(TAG, "Announced %u entities of `%s` to %s (%u bytes, msg_id %d)",
                 (unsigned)d->sensor_count, d->name, config_topic, (unsigned)strlen(payload), msg_id);

        if (msg_id < 0) {
            // The usual cause is the payload outrunning the client's out buffer -- see the
            // buffer.out_size set in start_mqtt_service().
            ESP_LOGE(TAG, "Discovery publish failed; is the MQTT out buffer large enough?");
        }

        cJSON_free(payload);
    } else {
        ESP_LOGE(TAG, "Could not serialise the discovery payload for `%s`", d->name);
    }

    cJSON_Delete(root);
}

// An empty retained payload on the config topic both deletes the device in Home Assistant and
// clears the retained config at the broker, so it does not come back on the next HA restart.
static void remove_device(esp_mqtt_client_handle_t client, const char *device_id)
{
    char config_topic[128];
    config_topic_for(device_id, config_topic, sizeof(config_topic));

    esp_mqtt_client_publish(client, config_topic, "", 0, 0, 1);

    ESP_LOGI(TAG, "Removed %s", config_topic);
}

static void announce_home(esp_mqtt_client_handle_t client)
{
    ha_device_t d = {};
    d.id = home_device_id();
    d.name = "Heating Monitor";
    d.model = "ESP32-S3-ETH Heating Monitor";
    d.is_child = false;
    d.state_topic = HOME_STATE_TOPIC;
    d.sensors = HOME_SENSORS;
    d.sensor_count = COUNT_OF(HOME_SENSORS);

    publish_device(client, &d);
}

void ha_discovery_announce_room(esp_mqtt_client_handle_t client, const room_t *room)
{
    if (!client || !room) {
        return;
    }

    char device_id[96];
    child_device_id("room", room->room_id, room->mqtt_name, device_id, sizeof(device_id));

    char entity_prefix[80];
    room_entity_prefix(room->room_id, room->mqtt_name, entity_prefix, sizeof(entity_prefix));

    char state_topic[96];
    ha_room_state_topic(room, state_topic, sizeof(state_topic));

    ha_device_t d = {};
    d.id = device_id;
    d.name = room->name;
    d.model = "Room";
    d.suggested_area = room->name;
    d.is_child = true;
    d.state_topic = state_topic;
    d.entity_prefix = entity_prefix;
    d.sensors = ROOM_SENSORS;
    d.sensor_count = COUNT_OF(ROOM_SENSORS);

    publish_device(client, &d);
}

void ha_discovery_announce_radiator(esp_mqtt_client_handle_t client, const radiator_t *radiator)
{
    if (!client || !radiator) {
        return;
    }

    char device_id[96];
    child_device_id("radiator", radiator->radiator_id, radiator->mqtt_name, device_id, sizeof(device_id));

    char entity_prefix[80];
    radiator_entity_prefix(radiator->radiator_id, radiator->mqtt_name, entity_prefix, sizeof(entity_prefix));

    char state_topic[96];
    ha_radiator_state_topic(radiator, state_topic, sizeof(state_topic));

    char device_name[96];
    snprintf(device_name, sizeof(device_name), "%s Radiator", radiator->name);

    ha_device_t d = {};
    d.id = device_id;
    d.name = device_name;
    d.model = "Radiator";
    d.is_child = true;
    d.state_topic = state_topic;
    d.entity_prefix = entity_prefix;
    d.sensors = RADIATOR_SENSORS;
    d.sensor_count = COUNT_OF(RADIATOR_SENSORS);

    publish_device(client, &d);
}

void ha_discovery_remove_room(esp_mqtt_client_handle_t client, uint8_t room_id, const char *mqtt_name)
{
    if (!client) {
        return;
    }

    char device_id[96];
    child_device_id("room", room_id, mqtt_name, device_id, sizeof(device_id));
    remove_device(client, device_id);
}

void ha_discovery_remove_radiator(esp_mqtt_client_handle_t client, uint8_t radiator_id, const char *mqtt_name)
{
    if (!client) {
        return;
    }

    char device_id[96];
    child_device_id("radiator", radiator_id, mqtt_name, device_id, sizeof(device_id));
    remove_device(client, device_id);
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

void ha_discovery_announce_rooms_and_radiators(esp_mqtt_client_handle_t client,
                                               room_manager_t *room_manager,
                                               radiator_manager_t *radiator_manager)
{
    if (!client) {
        return;
    }

    if (room_manager) {
        for (room_t *room = room_manager->room_list; room; room = room->next) {
            ha_discovery_announce_room(client, room);
        }
    }

    if (radiator_manager) {
        for (radiator_t *radiator = radiator_manager->radiator_list; radiator; radiator = radiator->next) {
            ha_discovery_announce_radiator(client, radiator);
        }
    }
}

void ha_discovery_announce_pending(esp_mqtt_client_handle_t client, room_manager_t *room_manager,
                                   radiator_manager_t *radiator_manager)
{
    if (!client || !s_children_pending.exchange(false)) {
        return;
    }

    ha_discovery_announce_rooms_and_radiators(client, room_manager, radiator_manager);
}

void ha_discovery_announce_all(esp_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }

    announce_home(client);

    // Rooms and radiators go out on the next update_home(). Sensors report at least once a
    // minute, so that is at most a minute after connecting.
    s_children_pending = true;
}
