#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct room
{
    uint8_t room_id;
    uint8_t name_len;
    char *name;

    uint8_t radiator_count;
    uint8_t *radiators;

    uint64_t room_temperature_node_id;
    uint16_t room_temperature_endpoint_id;

    uint16_t heat_loss_per_degree;

    // Transient data
    int16_t room_temperature;

    struct room *next;

} room;

typedef room room_t;
typedef struct
{
    room_t *room_list;
    uint8_t room_count;
} room_manager_t;

void room_manager_init(room_manager_t *manager);

esp_err_t load_rooms_from_nvs(room_manager_t *manager);

room_t *add_room(room_manager_t *manager, char *name, uint64_t room_temperature_node_id, uint16_t room_temperature_endpoint_id);
room_t *set_radiators(room_manager_t *manager, uint8_t room_id, uint8_t radiator_count, uint8_t *radiator_ids);

esp_err_t remove_room(room_manager_t *manager, uint8_t room_id);

esp_err_t load_rooms_from_nvs(room_manager_t *manager);
esp_err_t save_rooms_to_nvs(room_manager_t *manager);

esp_err_t room_manager_reset_and_reload(room_manager_t *manager);