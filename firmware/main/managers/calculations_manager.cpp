#include "calculations_manager.h"
#include <math.h>

#include "storage/history_logger.h"
#include "mqtt/ha_discovery.h"
#include "mqtt/mqtt_publisher.h"

static const char *TAG = "calculations_manager";

static void publish_home_state(void *arg);

// Only read on the publisher task. The handle is created once and never destroyed.
static esp_mqtt_client_handle_t s_home_mqtt_client = NULL;

// Electricity below this is treated as standby rather than as running, so no COP is derived
// from it. 50 W.
#define COP_MIN_ELEC_MW 50000

// COP 20.00. Nothing domestic reaches this; the clamp is here so a bad meter reading cannot
// push the value past what the int16 history field holds.
#define COP_MAX_X100 2000

// Publishes a reading, or null where there is none. Absent is not zero: 0 W is a real
// electrical reading and 0.00 degC a real temperature, which is the same distinction the
// history record makes with its sentinels and the web UI makes by dashing on null.
//
static void add_reading_or_null(cJSON *root, const char *key, bool present, double value)
{
    if (present)
    {
        cJSON_AddNumberToObject(root, key, value);
    }
    else
    {
        cJSON_AddNullToObject(root, key);
    }
}

// Publishes a radiator's readings to its state topic, null for whichever it does not have.
//
static void publish_radiator_state(esp_mqtt_client_handle_t mqtt_client, radiator_t *radiator)
{
    cJSON *root = cJSON_CreateObject();

    add_reading_or_null(root, "flow_temperature", radiator->has_flow_temperature, (double)radiator->flow_temperature / 100);
    add_reading_or_null(root, "return_temperature", radiator->has_return_temperature, (double)radiator->return_temperature / 100);
    add_reading_or_null(root, "mean_water_temperature", radiator->has_mean_water_temperature, (double)radiator->mean_water_temperature / 100);
    add_reading_or_null(root, "output", radiator->has_heat_output, radiator->heat_output);

    char state_topic[96];
    ha_radiator_state_topic(radiator, state_topic, sizeof(state_topic));

    char *payload = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publishing to MQTT topic %s", state_topic);
    // There is no client until a broker has been set on the Settings page.
    if (mqtt_client)
    {
        mqtt_publisher_publish(mqtt_client, state_topic, payload, false);
    }

    cJSON_free(payload);
    cJSON_Delete(root);
}

void update_radiator_outputs(node_manager_t *node_manager, home_manager_t *home_manager, radiator_manager_t *radiator_manager, room_manager_t *room_manager, esp_mqtt_client_handle_t mqtt_client, radiator_t *radiator)
{
    ESP_LOGI(TAG, "Calculating output for radiator %u", radiator->radiator_id);

    // Nothing is derived until this pass works it out. Zeroed as well as flagged, so a figure from
    // before a reading went missing cannot linger in the struct and be summed into a total.
    //
    radiator->has_mean_water_temperature = false;
    radiator->mean_water_temperature = 0;
    radiator->has_heat_output = false;
    radiator->heat_output = 0;

    // Find the room that the radiator is in.
    //
    room_t *room = NULL;

    for (room_t *candidate = room_manager->room_list; candidate != NULL && room == NULL; candidate = candidate->next)
    {
        for (uint8_t r = 0; r < candidate->radiator_count; r++)
        {
            if (candidate->radiators[r] == radiator->radiator_id)
            {
                room = candidate;
                break;
            }
        }
    }

    if (!radiator->has_flow_temperature || !radiator->has_return_temperature)
    {
        ESP_LOGI(TAG, "Skipping calculation as radiator has no flow or return temperature reading");
    }
    else if (room)
    {
        ESP_LOGI(TAG, "Radiator %u is in room %s", radiator->radiator_id, room->name);

        // Calclate the mean water temperature of the radiator
        // The probes can be in the wrong position for this. It doesn't matter.
        //
        radiator->mean_water_temperature = ((double)radiator->flow_temperature + (double)radiator->return_temperature) / 2;
        radiator->has_mean_water_temperature = true;

        ESP_LOGI(TAG, "Radiator %u has an MWT of %d", radiator->radiator_id, radiator->mean_water_temperature);

        // Perform calculations if we have data!
        //
        if (room->has_current_temperature)
        {
            // fabs, not abs: the magnitude of the whole difference is what is wanted here
            // (a probe fitted the wrong way round should not flip the sign of the output),
            // but abs() is the integer one from stdlib.h and truncates its argument unless
            // the C++ overload from math.h happens to be the one in scope.
            double deltaT = fabs((double)radiator->mean_water_temperature / 100 - (double)room->current_temperature / 100);

            ESP_LOGI(TAG, "Radiator %u has a MWT->Room ΔT of %f", radiator->radiator_id, deltaT);

            // The power is given at dT 50
            double dt = 50.0 / deltaT;

            ESP_LOGI(TAG, "Radiator %u has a ΔT division of %f", radiator->radiator_id, dt);

            double factor = pow(dt, 1.3);

            ESP_LOGI(TAG, "Radiator %u has a power adjustment factor of %f", radiator->radiator_id, factor);

            double adjusted_output = (double)radiator->output_dt_50 / factor;

            // A radiator only gives out heat while water is being pumped through it. Once the heat
            // pump stops, or turns to hot water, it stays warm for a while and the temperatures
            // alone would go on reporting an output. A heating state that is not known -- no heat
            // pump picked, or none reported yet -- counts as not heating.
            bool heating = home_manager->has_heating_running && home_manager->heating_running;

            radiator->heat_output = heating ? (uint16_t)adjusted_output : 0;
            radiator->has_heat_output = true;

            ESP_LOGI(TAG, "Radiator %u has a calculated output of %uW", radiator->radiator_id, radiator->heat_output);
        }
    }

    // Not in any room, or short of a reading: nothing was derived, but whatever flow and return it
    // does have are still worth reporting.
    publish_radiator_state(mqtt_client, radiator);

    // The room counts this radiator's output, whether it has just gained one or lost it.
    if (room)
    {
        update_room_heat_loss(node_manager, home_manager, room_manager, radiator_manager, mqtt_client, room);
    }

    update_home(home_manager, room_manager, radiator_manager, mqtt_client);
}

void update_room_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client, room_t *room)
{
    ESP_LOGI(TAG, "Calculating heat loss for room %u", room->room_id);

    int16_t current_temperature = 0;
    room->has_current_temperature = get_endpoint_measured_value(node_manager, room->room_temperature_node_id, room->room_temperature_endpoint_id, &current_temperature);
    room->current_temperature = current_temperature;

    if (room->has_current_temperature)
    {
        ESP_LOGI(TAG, "Room %u has a current temperature of %d", room->room_id, room->current_temperature);
    }
    else
    {
        ESP_LOGI(TAG, "Room %u has no current temperature", room->room_id);
    }

    // Nothing is worked out from a reading that isn't there. Each figure is zeroed and only set
    // again below if its inputs are present: update_home() sums these into the home totals, and a
    // room that has lost its sensor must drop out of them rather than leave its last figure in.
    //
    room->has_predicted_heat_loss = false;
    room->has_measured_heat_loss = false;
    room->predicted_heat_loss_at_target_temperature = 0;
    room->predicted_heat_loss_at_current_temperature = 0;
    room->measured_heat_loss_per_degree = 0;
    room->measured_heat_loss_at_target_temperature = 0;
    room->measured_heat_loss_at_current_temperature = 0;
    room->heat_loss_difference = 0.0;

    ESP_LOGI(TAG, "Room %u has a predicated heat loss of %u W/°C", room->room_id, room->predicted_heat_loss_per_degree);

    if (!home_manager->has_outdoor_temperature)
    {
        ESP_LOGI(TAG, "No outdoor temperature, so room %u has no heat loss figures", room->room_id);
    }
    else
    {
        // No abs() on either side. The quantity wanted is the signed difference indoor - outdoor,
        // and taking the magnitude of each term separately gets it wrong in exactly the weather
        // that matters: at 21 degC indoors and -5 outdoors it yielded 21 - 5 = 16 rather than 26,
        // which then inflated measured_heat_loss_per_degree below because the division under-counts
        // the delta. Taking abs() of the whole difference would be wrong too -- when the outdoor
        // temperature is above the room's, the house is gaining heat, and a negative delta is the
        // honest answer rather than a heat loss of the same size.
        double target_temperature_delta_t = (double)room->target_temperature / 100 - (double)home_manager->outdoor_temperature / 100;

        ESP_LOGI(TAG, "Outdoor temperature is %d", home_manager->outdoor_temperature);
        ESP_LOGI(TAG, "Room %u has a target temperature of %d", room->room_id, room->target_temperature);
        ESP_LOGI(TAG, "Room %u has a target temperature -> outdoor temperature ΔT of %f", room->room_id, target_temperature_delta_t);

        room->predicted_heat_loss_at_target_temperature = target_temperature_delta_t * room->predicted_heat_loss_per_degree;
        room->has_predicted_heat_loss = true;

        ESP_LOGI(TAG, "Room %u has a predicted heat loss of %d W at target temperature", room->room_id, room->predicted_heat_loss_at_target_temperature);

        if (room->has_current_temperature)
        {
            double current_delta_t = (double)room->current_temperature / 100 - (double)home_manager->outdoor_temperature / 100;

            ESP_LOGI(TAG, "Room %u has a current temperature -> outdoor temperature ΔT of %f", room->room_id, current_delta_t);

            room->predicted_heat_loss_at_current_temperature = current_delta_t * room->predicted_heat_loss_per_degree;

            // The measured heat loss is the radiators' output over that delta, so it also needs at
            // least one radiator with an output, and a delta to divide by. The output has to be
            // above zero as well: every radiator reads 0 W while the heat pump is not heating, and
            // that says nothing about how much heat the room loses.
            //
            uint16_t total_radiator_output = 0;
            bool has_radiator_output = false;

            for (uint8_t r = 0; r < room->radiator_count; r++)
            {
                radiator_t *radiator = find_radiator(radiator_manager, room->radiators[r]);

                if (radiator && radiator->has_heat_output)
                {
                    total_radiator_output += radiator->heat_output;
                    has_radiator_output = true;
                }
            }

            if (has_radiator_output && total_radiator_output > 0 && current_delta_t != 0)
            {
                ESP_LOGI(TAG, "Room %u has an input of %u W", room->room_id, total_radiator_output);

                room->measured_heat_loss_per_degree = total_radiator_output / current_delta_t;
                room->measured_heat_loss_at_target_temperature = target_temperature_delta_t * room->measured_heat_loss_per_degree;
                room->measured_heat_loss_at_current_temperature = current_delta_t * room->measured_heat_loss_per_degree;

                room->heat_loss_difference = (room->predicted_heat_loss_per_degree > 0)
                    ? ((double)room->measured_heat_loss_per_degree / room->predicted_heat_loss_per_degree) * 100.0
                    : 0.0;

                room->has_measured_heat_loss = true;

                ESP_LOGI(TAG, "Room %u has an actual heat loss of %d W/°C", room->room_id, room->measured_heat_loss_per_degree);
                ESP_LOGI(TAG, "Room %u has a measured heat loss of %d W at target temperature", room->room_id, room->measured_heat_loss_at_target_temperature);
            }
            else
            {
                ESP_LOGI(TAG, "Room %u has no radiator output, heat loss cannot be measured", room->room_id);
            }
        }
    }

    cJSON *root = cJSON_CreateObject();

    add_reading_or_null(root, "current_temperature", room->has_current_temperature, (double)room->current_temperature / 100);
    cJSON_AddNumberToObject(root, "predicted_heat_loss_per_degree", room->predicted_heat_loss_per_degree);
    add_reading_or_null(root, "measured_heat_loss_per_degree", room->has_measured_heat_loss, room->measured_heat_loss_per_degree);
    add_reading_or_null(root, "heat_loss_difference", room->has_measured_heat_loss && room->predicted_heat_loss_per_degree > 0, room->heat_loss_difference);

    char state_topic[96];
    ha_room_state_topic(room, state_topic, sizeof(state_topic));

    char *payload = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publishing to MQTT topic %s", state_topic);
    if (mqtt_client)
    {
        mqtt_publisher_publish(mqtt_client, state_topic, payload, false);
    }

    cJSON_free(payload);
    cJSON_Delete(root);

    update_home(home_manager, room_manager, radiator_manager, mqtt_client);
}

// This is used when the outdoor temperature changes as that impacts all rooms.
//
void update_all_rooms_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client)
{
    room_t *room = room_manager->room_list;

    while (room != NULL)
    {
        update_room_heat_loss(node_manager, home_manager, room_manager, radiator_manager, mqtt_client, room);
        room = room->next;
    }

    update_home(home_manager, room_manager, radiator_manager, mqtt_client);
}

void update_home(home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client)
{
    // Compute total predicted & total measured heat loss across all rooms.
    //
    home_manager->total_predicted_heat_loss_per_degree = 0;
    home_manager->total_measured_heat_loss_per_degree = 0;
    home_manager->total_predicted_heat_loss_at_target_temperature = 0;
    home_manager->total_predicted_heat_loss_at_current_temperature = 0;
    home_manager->total_measured_heat_loss_at_target_temperature = 0;
    home_manager->total_measured_heat_loss_at_current_temperature = 0;

    // Average indoor temperature: a simple mean of every room with a reading, each room counting
    // equally. A room is left out when no sensor is bound to it, or while its sensor has no
    // reading. Absent, not zero, when no room qualifies.
    //
    int32_t indoor_sum = 0;
    uint8_t indoor_count = 0;

    room_t *room = room_manager->room_list;

    while (room)
    {
        if (room->room_temperature_node_id != 0 && room->has_current_temperature)
        {
            indoor_sum += room->current_temperature;
            indoor_count++;
        }

        home_manager->total_predicted_heat_loss_per_degree += room->predicted_heat_loss_per_degree;
        if (room->measured_heat_loss_per_degree > 0)
        {
            home_manager->total_measured_heat_loss_per_degree += room->measured_heat_loss_per_degree;
        }
        home_manager->total_predicted_heat_loss_at_target_temperature += room->predicted_heat_loss_at_target_temperature;
        home_manager->total_predicted_heat_loss_at_current_temperature += room->predicted_heat_loss_at_current_temperature;
        home_manager->total_measured_heat_loss_at_target_temperature += room->measured_heat_loss_at_target_temperature;
        home_manager->total_measured_heat_loss_at_current_temperature += room->measured_heat_loss_at_current_temperature;

        room = room->next;
    }

    home_manager->has_average_internal_temperature = indoor_count > 0;
    home_manager->average_internal_temperature = indoor_count ? (int16_t)(indoor_sum / indoor_count) : 0;

    // Compute total radiator output across all rooms.
    radiator_t *radiator = radiator_manager->radiator_list;
    home_manager->total_radiator_output = 0;
    home_manager->radiator_count = 0;

    while (radiator)
    {
        if (radiator->has_heat_output)
        {
            home_manager->total_radiator_output += radiator->heat_output;
        }
        home_manager->radiator_count++;
        radiator = radiator->next;
    }

    // Hand the history logger a flat copy of what both lists currently hold. This is the one
    // function every calculation path ends in, and it has just walked both lists on this task,
    // so the sampler never has to: it runs on the esp_timer task, and these are linked lists
    // whose nodes the HTTP task frees in remove_room() and remove_radiator().
    history_logger_snapshot(room_manager, radiator_manager);

    // After an MQTT (re)connect, the rooms and radiators are announced from here rather than
    // from the MQTT task, for the same reason: this task may walk the lists.
    ha_discovery_announce_pending(mqtt_client, room_manager, radiator_manager);

    // TODO UFH.

    // Instantaneous coefficient of performance: heat out over electricity in, both as the
    // meters report them in mW.
    //
    // The electrical floor matters. At standby draw, a few watts of electricity against the
    // heat still coming off a warm system yields a COP in the hundreds, which is meaningless
    // and would flatten the chart's Y axis for the rest of the day. Below the floor the
    // figure is recorded as absent rather than as zero, so the history shows a gap.
    //
    home_manager->has_cop = false;
    home_manager->cop_x100 = 0;

    if (home_manager->has_heat_meter_power && home_manager->has_electrical_power &&
        home_manager->heat_meter_power_mw > 0 &&
        home_manager->electrical_power_mw >= COP_MIN_ELEC_MW)
    {
        int64_t cop = (home_manager->heat_meter_power_mw * 100) / home_manager->electrical_power_mw;

        if (cop > COP_MAX_X100)
        {
            cop = COP_MAX_X100;
        }

        home_manager->has_cop = true;
        home_manager->cop_x100 = (int16_t)cop;

        ESP_LOGI(TAG, "COP is %lld.%02lld", cop / 100, cop % 100);
    }

    // The arithmetic above is cheap, but a single report can land here several times over, and the
    // heat pump's reports come in bursts, so the payload is left to the publisher task. That builds
    // it once, from the latest figures, and reads home_manager unlocked one field at a time, as
    // the history sampler and the emoncms uploader do.
    if (mqtt_client)
    {
        s_home_mqtt_client = mqtt_client;
        mqtt_publisher_defer(publish_home_state, home_manager);
    }
}

static void publish_home_state(void *arg)
{
    const home_manager_t *home_manager = (const home_manager_t *)arg;

    cJSON *root = cJSON_CreateObject();

    // The same quantities the history logger records -- see rec_home_t in
    // storage/history_format.h -- so a subscriber gets live what the charts show back.
    //
    // Published in human units, following the radiator and room payloads above, rather than
    // the raw cluster units GET /api/home sends: temperatures in degC, power in W, voltage
    // in V, current in A.
    //
    add_reading_or_null(root, "heat_power", home_manager->has_heat_meter_power,
                        (double)home_manager->heat_meter_power_mw / 1000.0);
    add_reading_or_null(root, "electrical_power", home_manager->has_electrical_power,
                        (double)home_manager->electrical_power_mw / 1000.0);
    add_reading_or_null(root, "flow_temperature", home_manager->has_heat_meter_flow_temperature,
                        (double)home_manager->heat_meter_flow_temperature / 100.0);
    add_reading_or_null(root, "return_temperature", home_manager->has_heat_meter_return_temperature,
                        (double)home_manager->heat_meter_return_temperature / 100.0);
    add_reading_or_null(root, "flow_rate", home_manager->has_heat_meter_flow,
                        (double)home_manager->heat_meter_flow);

    add_reading_or_null(root, "outdoor_temperature", home_manager->has_outdoor_temperature,
                        (double)home_manager->outdoor_temperature / 100.0);
    add_reading_or_null(root, "average_internal_temperature", home_manager->has_average_internal_temperature,
                        (double)home_manager->average_internal_temperature / 100.0);
    add_reading_or_null(root, "cop", home_manager->has_cop,
                        (double)home_manager->cop_x100 / 100.0);
    add_reading_or_null(root, "electrical_voltage", home_manager->has_electrical_voltage,
                        (double)home_manager->electrical_voltage_mv / 1000.0);
    add_reading_or_null(root, "electrical_current", home_manager->has_electrical_current,
                        (double)home_manager->electrical_current_ma / 1000.0);

    if (home_manager->has_dhw_running)
    {
        cJSON_AddBoolToObject(root, "dhw_running", home_manager->dhw_running);
    }
    else
    {
        cJSON_AddNullToObject(root, "dhw_running");
    }

    if (home_manager->has_heating_running)
    {
        cJSON_AddBoolToObject(root, "heating_running", home_manager->heating_running);
    }
    else
    {
        cJSON_AddNullToObject(root, "heating_running");
    }

    cJSON_AddNumberToObject(root, "total_predicted_heat_loss_per_degree", home_manager->total_predicted_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "total_measured_heat_loss_per_degree", home_manager->total_measured_heat_loss_per_degree);

    char *payload = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publishing to MQTT topic heating_monitor/home");
    if (s_home_mqtt_client)
    {
        mqtt_publisher_publish(s_home_mqtt_client, "heating_monitor/home", payload, false);
    }

    cJSON_free(payload);
    cJSON_Delete(root);
}
