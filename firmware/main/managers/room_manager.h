#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

// A room's share of one UFH manifold's output, in whole percent. The manifold is a radiator record
// of type EMITTER_TYPE_UFH_MANIFOLD, named by its radiator id.
typedef struct
{
    uint8_t emitter_id;
    uint8_t pct;
} ufh_share_t;

// The most manifolds a single room can take a share of.
#define ROOM_MAX_UFH_SHARES 8

typedef struct room
{
    uint8_t room_id;

    uint8_t name_len;
    char *name;
    uint8_t mqtt_name_len;
    char *mqtt_name;

    uint8_t radiator_count;
    uint8_t *radiators;

    // Shares of UFH manifolds. Persisted under their own NVS key (room_ufh), not in room_list.
    uint8_t ufh_share_count;
    ufh_share_t *ufh_shares;

    uint64_t room_temperature_node_id;
    uint16_t room_temperature_endpoint_id;

    uint8_t predicted_heat_loss_per_degree;

    int16_t target_temperature;

    // Transient data
    bool has_current_temperature;
    int16_t current_temperature;
    int16_t total_radiator_output;

    // The watts this room's UFH shares come to now, set by update_ufh_outputs(). Absent while any
    // manifold it has a share of has no output, and when it has no shares at all.
    bool has_ufh_input;
    uint16_t ufh_input_w;

    int16_t measured_heat_loss_per_degree;

    int16_t measured_heat_loss_at_target_temperature;
    int16_t measured_heat_loss_at_current_temperature;

    int16_t predicted_heat_loss_at_target_temperature;
    int16_t predicted_heat_loss_at_current_temperature;

    double heat_loss_difference;

    // Whether the figures above were worked out from real readings. The predicted ones need an
    // outdoor temperature; the measured ones a room temperature and a radiator output as well.
    // The fields hold 0 when they could not be.
    bool has_predicted_heat_loss;
    bool has_measured_heat_loss;

    struct room *next;

} room;

typedef room room_t;
typedef struct
{
    room_t *room_list;
    uint8_t room_count;
} room_manager_t;

void room_manager_init(room_manager_t *manager);
uint8_t get_next_room_id(room_manager_t *manager);

room_t *find_room(room_manager_t *manager, uint8_t room_id);

room_t *add_room(room_manager_t *manager, char *name, char *mqtt_name, int16_t target_temperature, uint8_t survey_heat_loss_per_degree, uint64_t room_temperature_node_id, uint16_t room_temperature_endpoint_id);
// mqtt_name may be NULL, which leaves the room's MQTT name as it is.
room_t *update_room(room_manager_t *manager, uint8_t room_id, char *name, const char *mqtt_name, int16_t target_temperature, uint8_t heat_loss_per_degree, uint8_t radiator_count, uint8_t *radiator_ids, uint64_t room_temperature_node_id, uint16_t room_temperature_endpoint_id);

esp_err_t set_room_radiators(room_t *room, uint8_t radiator_count, uint8_t *radiator_ids);
// Replaces the room's UFH shares. Shares of 0% are dropped. Does not save.
esp_err_t set_room_ufh_shares(room_t *room, uint8_t share_count, const ufh_share_t *shares);
// The room's share of the manifold, in percent; 0 when it has none.
uint8_t room_ufh_share_pct(const room_t *room, uint8_t emitter_id);
// Drops every room's share of a manifold that is being deleted. True if any room had one; does
// not save.
bool room_manager_forget_ufh_emitter(room_manager_t *manager, uint8_t emitter_id);

esp_err_t remove_room(room_manager_t *manager, uint8_t room_id);

esp_err_t load_rooms_from_nvs(room_manager_t *manager);
esp_err_t save_rooms_to_nvs(room_manager_t *manager);

esp_err_t room_manager_reset_and_reload(room_manager_t *manager);