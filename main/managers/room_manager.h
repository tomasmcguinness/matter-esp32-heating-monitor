#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

typedef struct room
{
    uint8_t room_id;
    uint8_t name_len;
    char *name;

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

room_t *add_room(room_manager_t *manager, char *name);
esp_err_t remove_room(room_manager_t *manager, uint8_t room_id);

esp_err_t load_rooms_from_nvs(room_manager_t *manager);
esp_err_t save_rooms_to_nvs(room_manager_t *manager);

esp_err_t room_manager_reset_and_reload(room_manager_t *manager);