#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"
#include "cJSON.h"
#include "mqtt_client.h"

#pragma once

// A radiator record of this type is an underfloor heating manifold rather than a radiator. It keeps
// every radiator mechanism -- probes, NVS, the /api/radiators routes, rad-<id> history -- but its
// output is not the dT50 formula: it is its flow times the temperature drop across its probes,
// worked out by update_ufh_outputs(). The flow comes from a calibration (the ufh_* fields below);
// output_dt_50 is not used for a manifold.
// Rooms do not list it in radiators[]; they take a percentage of it (room_t::ufh_shares).
#define EMITTER_TYPE_UFH_MANIFOLD 100

// A manifold's calibration is the flow of each of its loops, read off the manifold's own flow
// gauges, and the heat meter's flow at the moment they were read. The manifold's flow is then
// taken to move in proportion to the heat meter's: sum(loops) x heat meter now / heat meter then.
#define UFH_MAX_LOOPS 16
// The most a loop's gauge can be entered as, in 0.1 l/min. Manifold gauges top out well below it.
#define UFH_MAX_LOOP_FLOW_DLPM 999

typedef struct radiator
{
    uint8_t radiator_id;

    uint8_t name_len;
    char *name;
    uint8_t mqtt_name_len;
    char *mqtt_name;

    uint8_t type;

    uint16_t output_dt_50;

    uint64_t flow_temp_node_id;
    uint16_t flow_temp_endpoint_id;
    uint64_t return_temp_node_id;
    uint16_t return_temp_endpoint_id;

    uint8_t room_id;

    // UFH manifolds only: the calibration, saved under its own NVS key (ufh_loops) so the
    // radiator_list blob keeps its layout. ufh_loop_count 0 means not calibrated.
    uint8_t ufh_loop_count;
    uint16_t ufh_loop_flow_dlpm[UFH_MAX_LOOPS]; // each loop's flow, 0.1 l/min
    uint16_t ufh_ref_flow_lph;                  // the heat meter's flow when they were read, l/h
    uint32_t ufh_calibrated_at;                 // when, Unix time; 0 if the clock was not set

    // Transient data
    // Derived by update_ufh_outputs() for a manifold: its flow now, scaled from the calibration.
    bool has_ufh_flow;
    uint16_t ufh_flow_lph;
    bool has_flow_temperature;
    int16_t flow_temperature;
    bool has_return_temperature;
    int16_t return_temperature;
    // Derived by update_radiator_outputs(). The mean needs flow and return; the output needs the
    // room's temperature as well.
    bool has_mean_water_temperature;
    int16_t mean_water_temperature;
    bool has_heat_output;
    uint16_t heat_output;

    struct radiator *next;

} radiator;

typedef radiator radiator_t;

static inline bool is_ufh_manifold(const radiator_t *radiator)
{
    return radiator->type == EMITTER_TYPE_UFH_MANIFOLD;
}

typedef struct
{
    radiator_t *radiator_list;
    uint8_t radiator_count;
} radiator_manager_t;

void radiator_manager_init(radiator_manager_t *manager);

radiator_t *find_radiator(radiator_manager_t *manager, uint8_t radiator_id);

radiator_t *add_radiator(radiator_manager_t *manager, char *name, char *mqtt_name, uint8_t type, uint16_t output, uint64_t flowNodeId, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId);
radiator_t *update_radiator(radiator_manager_t *manager, uint8_t radiator_id, char *name, char *mqtt_name, uint8_t type, uint16_t output_dt_50, uint64_t flow_temp_node_id, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId);

esp_err_t remove_radiator(radiator_manager_t *controller, uint8_t radiator_id, char *mqtt_name);

static inline bool ufh_is_calibrated(const radiator_t *radiator)
{
    return radiator->ufh_loop_count > 0 && radiator->ufh_ref_flow_lph > 0;
}

// The sum of a manifold's loop flows at calibration, in l/h.
uint32_t ufh_calibrated_flow_lph(const radiator_t *radiator);

// Sets a manifold's calibration and saves it. ESP_ERR_INVALID_ARG unless it is a manifold, there
// are 1 to UFH_MAX_LOOPS loops, each flow is 1 to UFH_MAX_LOOP_FLOW_DLPM, and ref_flow_lph is not 0.
esp_err_t set_ufh_calibration(radiator_manager_t *manager, radiator_t *radiator, uint8_t loop_count, const uint16_t *loop_flow_dlpm, uint16_t ref_flow_lph, uint32_t calibrated_at);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);
esp_err_t save_radiators_to_nvs(radiator_manager_t *manager);

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager);