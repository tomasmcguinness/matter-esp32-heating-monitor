#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

typedef struct radiator
{
    uint8_t radiator_id;
    uint8_t name_len;
    //char *name;
    uint8_t type;
    uint16_t outputAtDelta50;
    
    uint64_t flow_temp_nodeId;
    uint16_t flow_temp_endpointId;
    uint64_t return_temp_nodeId;
    uint16_t return_temp_endpointId;

    struct radiator *next;

} radiator;

typedef radiator radiator_t;

typedef struct
{
    radiator_t *radiator_list;
    uint8_t radiator_count;
} radiator_manager_t;

void radiator_manager_init(radiator_manager_t *manager);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);

radiator_t *add_radiator(radiator_manager_t *manager, uint8_t name_len, char *name, uint8_t type, uint16_t output, uint64_t flowNodeId, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId);
esp_err_t remove_radiator(radiator_manager_t *controller, uint8_t radiator_id);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);
esp_err_t save_radiators_to_nvs(radiator_manager_t *manager);

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager);