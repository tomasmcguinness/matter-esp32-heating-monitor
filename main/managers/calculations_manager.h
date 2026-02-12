#include "node_manager.h"
#include "radiator_manager.h"
#include "room_manager.h"
#include "home_manager.h"

void update_all_rooms_heat_loss(node_manager_t *node_manager,home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager);
void update_room_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager,room_t *room);
void update_radiator_outputs(node_manager_t *node_manager, home_manager_t *home_manager, radiator_manager_t *radiator_manager, room_manager_t *room_manager, radiator_t *radiator);
void update_home(home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager);