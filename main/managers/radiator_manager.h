#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

typedef struct radiator
{
    uint8_t radiator_id;
    uint8_t name_len;
    char *name;
    uint8_t type;
    uint16_t outputAtDelta50;
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

radiator_t *add_radiator(radiator_manager_t *manager, uint8_t name_len, char *name, uint8_t type, uint16_t output);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);
esp_err_t save_radiators_to_nvs(radiator_manager_t *manager);

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager);