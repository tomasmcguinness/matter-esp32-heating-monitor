#include "config_topic.h"

#include <string.h>

#include <atomic>

#include "cJSON.h"
#include "esp_log.h"
#include "ha_discovery.h"
#include "mqtt_publisher.h"

static const char *TAG = "config_topic";

#define CONFIG_SCHEMA 1

static std::atomic<bool> s_pending{false};

void config_topic_mark_pending(void)
{
    s_pending = true;
}

static void add_name_or_null(cJSON *obj, const char *key, const char *value)
{
    if (value && value[0]) {
        cJSON_AddStringToObject(obj, key, value);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

static cJSON *build_room(const room_t *room)
{
    cJSON *r = cJSON_CreateObject();

    char state_topic[96];
    ha_room_state_topic(room, state_topic, sizeof(state_topic));

    cJSON_AddNumberToObject(r, "id", room->room_id);
    add_name_or_null(r, "name", room->name);
    add_name_or_null(r, "mqtt_name", room->mqtt_name);
    cJSON_AddStringToObject(r, "state_topic", state_topic);
    cJSON_AddNumberToObject(r, "target_c", (double)room->target_temperature / 100.0);
    cJSON_AddNumberToObject(r, "heat_loss_w_per_c", room->predicted_heat_loss_per_degree);

    cJSON *radiators = cJSON_AddArrayToObject(r, "radiator_ids");
    for (uint8_t i = 0; i < room->radiator_count; i++) {
        cJSON_AddItemToArray(radiators, cJSON_CreateNumber(room->radiators[i]));
    }

    cJSON *shares = cJSON_AddArrayToObject(r, "ufh_shares");
    for (uint8_t i = 0; i < room->ufh_share_count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "manifold_id", room->ufh_shares[i].emitter_id);
        cJSON_AddNumberToObject(s, "pct", room->ufh_shares[i].pct);
        cJSON_AddItemToArray(shares, s);
    }

    return r;
}

static cJSON *build_emitter(const radiator_t *radiator)
{
    cJSON *e = cJSON_CreateObject();

    char state_topic[96];
    ha_radiator_state_topic(radiator, state_topic, sizeof(state_topic));

    cJSON_AddNumberToObject(e, "id", radiator->radiator_id);
    add_name_or_null(e, "name", radiator->name);
    add_name_or_null(e, "mqtt_name", radiator->mqtt_name);
    cJSON_AddStringToObject(e, "state_topic", state_topic);
    cJSON_AddStringToObject(e, "type", is_ufh_manifold(radiator) ? "ufh" : "radiator");
    cJSON_AddNumberToObject(e, "rated_w", radiator->output_dt_50);

    return e;
}

void config_topic_publish_pending(esp_mqtt_client_handle_t client, room_manager_t *room_manager,
                                  radiator_manager_t *radiator_manager)
{
    if (!client || !s_pending.exchange(false)) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", CONFIG_SCHEMA);
    cJSON_AddStringToObject(root, "device_id", ha_discovery_device_id());

    cJSON *rooms = cJSON_AddArrayToObject(root, "rooms");
    if (room_manager) {
        for (room_t *room = room_manager->room_list; room; room = room->next) {
            cJSON_AddItemToArray(rooms, build_room(room));
        }
    }

    cJSON *emitters = cJSON_AddArrayToObject(root, "emitters");
    if (radiator_manager) {
        for (radiator_t *radiator = radiator_manager->radiator_list; radiator; radiator = radiator->next) {
            cJSON_AddItemToArray(emitters, build_emitter(radiator));
        }
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGE(TAG, "Could not serialise the config payload");
        // Ask again on the next pass rather than leaving subscribers with a stale config.
        s_pending = true;
        return;
    }

    // Retained, so a subscriber that connects later gets it straight away. mqtt_publisher logs a
    // retained publish that does not fit the client's out buffer.
    if (mqtt_publisher_publish(client, CONFIG_TOPIC, payload, true)) {
        ESP_LOGI(TAG, "Queued %s (%u bytes)", CONFIG_TOPIC, (unsigned)strlen(payload));
    } else {
        ESP_LOGE(TAG, "Could not queue %s; will try again", CONFIG_TOPIC);
        s_pending = true;
    }

    cJSON_free(payload);
}
