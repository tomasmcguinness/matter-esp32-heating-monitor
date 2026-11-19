#include "node_manager.h"
#include "radiator_manager.h"
#include "room_manager.h"
#include "home_manager.h"

void update_all_rooms_heat_loss(node_manager_t *node_manager,home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client);
void update_room_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client, room_t *room);
void update_radiator_outputs(node_manager_t *node_manager, home_manager_t *home_manager, radiator_manager_t *radiator_manager, room_manager_t *room_manager, esp_mqtt_client_handle_t mqtt_client, radiator_t *radiator);
// Works out every UFH manifold's output, each room's UFH input and the home's UFH totals. Called by
// update_room_heat_loss() and update_home(), so it is current whenever a room's share is taken.
void update_ufh_outputs(home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager);
void update_home(home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client);