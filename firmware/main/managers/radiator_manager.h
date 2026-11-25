#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"
#include "cJSON.h"
#include "mqtt_client.h"

#pragma once

// A radiator record of this type is an underfloor heating manifold rather than a radiator. It keeps
// every radiator mechanism -- probes, NVS, the /api/radiators routes, rad-<id> history -- but its
// output is not the dT50 formula: it is its flow times the temperature drop across its probes,
// worked out by update_ufh_outputs(). The flow comes from its calibration points (ufh_points);
// output_dt_50 is not used for a manifold.
// Rooms do not list it in radiators[]; they take a percentage of it (room_t::ufh_shares).
#define EMITTER_TYPE_UFH_MANIFOLD 100

// A manifold's calibration is a set of points. Each is the flow of every loop, read off the
// manifold's own flow gauges, and the heat meter's flow at the moment they were read. The manifold's
// flow at any heat meter flow is interpolated between the points, and scaled in proportion from the
// nearest end point outside them (ufh_curve.h). Every point in a set has the same number of loops.
#define UFH_MAX_LOOPS 16
#define UFH_MAX_POINTS 8
// The most a loop's gauge can be entered as, in 0.1 l/min. Manifold gauges top out well below it.
#define UFH_MAX_LOOP_FLOW_DLPM 999
// A new point this close to an existing one's heat meter flow (percent) is a re-read and replaces it.
#define UFH_POINT_REPLACE_PCT 3

typedef struct
{
    uint16_t ref_flow_lph;                      // the heat meter's flow when the loops were read, l/h
    uint32_t calibrated_at;                     // when, Unix time; 0 if the clock was not set
    uint16_t loop_flow_dlpm[UFH_MAX_LOOPS];     // each loop's flow, 0.1 l/min
} ufh_point_t;

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

    // UFH manifolds only: the calibration points, sorted by ref_flow_lph, saved under their own
    // NVS key (ufh_loops) so the radiator_list blob keeps its layout. ufh_points is allocated only
    // while there are points; ufh_loop_count is the loop count every point shares.
    uint8_t ufh_loop_count;
    uint8_t ufh_point_count;
    ufh_point_t *ufh_points;

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
    return radiator->ufh_point_count > 0 && radiator->ufh_points != NULL;
}

// The sum of a point's loop flows, in l/h.
uint32_t ufh_point_flow_lph(const radiator_t *radiator, const ufh_point_t *point);

// The manifold's flow, l/h, at a heat meter flow of hm_flow_lph, from its points. False if it has
// none.
bool ufh_flow_at(const radiator_t *radiator, uint32_t hm_flow_lph, uint32_t *flow_lph);

// Whether hm_flow_lph is outside the points, where the flow is only scaled from an end point.
bool ufh_flow_is_extrapolated(const radiator_t *radiator, uint32_t hm_flow_lph);

// Adds a calibration point and saves. One within UFH_POINT_REPLACE_PCT of an existing point's heat
// meter flow replaces it. ESP_ERR_INVALID_ARG unless it is a manifold, there are 1 to
// UFH_MAX_LOOPS loops each 1 to UFH_MAX_LOOP_FLOW_DLPM, and ref_flow_lph is not 0;
// ESP_ERR_INVALID_STATE if the saved points have a different loop count; ESP_ERR_NO_MEM if there are
// already UFH_MAX_POINTS (or the allocation fails). *replaced says whether it replaced one.
esp_err_t ufh_add_point(radiator_manager_t *manager, radiator_t *radiator, uint8_t loop_count, const uint16_t *loop_flow_dlpm, uint16_t ref_flow_lph, uint32_t calibrated_at, bool *replaced);

// Deletes one point (by its index in the sorted list) and saves. ESP_ERR_NOT_FOUND if there is none.
esp_err_t ufh_delete_point(radiator_manager_t *manager, radiator_t *radiator, uint8_t index);

// Deletes every point and saves.
esp_err_t ufh_delete_all_points(radiator_manager_t *manager, radiator_t *radiator);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);
esp_err_t save_radiators_to_nvs(radiator_manager_t *manager);

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager);