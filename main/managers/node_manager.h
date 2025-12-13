#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

typedef struct matter_endpoint
{
    uint16_t endpoint_id;
    uint32_t *device_type_ids;
    uint8_t device_type_count;
} endpoint_entry_t;

typedef struct matter_node
{
    uint64_t node_id;
    endpoint_entry_t *endpoints;
    uint16_t endpoints_count;

    struct matter_node *next;
} matter_node;

typedef matter_node matter_node_t;

typedef struct
{
    matter_node_t *node_list;
    uint16_t node_count;     
} node_manager_t;

void node_manager_init(node_manager_t *controller);
matter_node_t *find_node(node_manager_t *controller, uint64_t node_id);
esp_err_t remove_node(node_manager_t *controller, uint64_t node_id);

matter_node_t *add_node(node_manager_t *controller, uint64_t node_id);
endpoint_entry_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id);
uint32_t add_device_type(matter_node_t *node, uint16_t endpoint_id, uint32_t device_type_id);

esp_err_t save_nodes_to_nvs(node_manager_t *controller);
esp_err_t load_nodes_from_nvs(node_manager_t *controller);