#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct matter_endpoint
{
    uint16_t endpoint_id;

    uint8_t name_length;
    char *name;

    uint8_t power_source;

    int16_t measured_value;

    uint32_t *device_type_ids;
    uint8_t device_type_count;
} endpoint_entry_t;

typedef struct matter_node
{
    uint64_t node_id;
    uint16_t vendor_name_length;
    char *vendor_name;
    uint16_t product_name_length;
    char *product_name;
    
    // This is set by the user.
    uint8_t name_length;
    char *name;

    // This comes from the BasicInformation cluster
    uint8_t label_length;
    char *label;

    uint8_t power_source;

    bool has_subscription;
    uint32_t subscription_id;

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

void node_manager_init(node_manager_t *manager);

void subscribe_all_temperature_measurements(node_manager_t *manager);

uint64_t get_next_node_id(node_manager_t *manager);

matter_node_t *find_node(node_manager_t *manager, uint64_t node_id);
esp_err_t remove_node(node_manager_t *manager, uint64_t node_id);

matter_node_t *add_node(node_manager_t *manager, uint64_t node_id);
esp_err_t set_node_name(matter_node_t *node, char *name);
esp_err_t set_node_label(matter_node_t *node, char *label);
esp_err_t set_node_power_source(matter_node_t *node, uint8_t power_source);

endpoint_entry_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id);
esp_err_t add_device_type(matter_node_t *node, uint16_t endpoint_id, uint32_t device_type_id);
esp_err_t set_endpoint_name(matter_node_t *node, uint16_t endpoint_id, char *fixed_label_name);
esp_err_t set_endpoint_power_source(matter_node_t *node, uint16_t endpoint_id, uint8_t power_source);
esp_err_t set_endpoint_measured_value(node_manager_t *manager, uint64_t node_id, uint16_t endpoint_id, uint16_t measured_value);

esp_err_t mark_node_has_subscription(node_manager_t *manager, uint64_t node_id, uint32_t subscription_id);
esp_err_t mark_node_has_no_subscription(node_manager_t *manager, uint64_t node_id, uint32_t subscription_id, bool *create_new_subscription);

esp_err_t get_endpoint_measured_value(node_manager_t *manager, uint64_t node_id, uint16_t endpoint_id, int16_t *measured_value);

esp_err_t clear_node_details(node_manager_t *manager, uint64_t node_id);

esp_err_t save_nodes_to_nvs(node_manager_t *manager);
esp_err_t load_nodes_from_nvs(node_manager_t *manager);