#include <stdint.h>
#include <stdbool.h>

typedef struct matter_endpoint
{
    uint16_t endpoint_id;
    uint32_t *device_type_ids;
} endpoint_entry_t;

typedef struct matter_node
{
    uint64_t node_id;
    endpoint_entry_t *endpoints;
    uint16_t endpoints_count;
} matter_node_t;

typedef struct
{
    matter_node_t *node_list;
    uint16_t node_count;
} matter_node_manager_t;