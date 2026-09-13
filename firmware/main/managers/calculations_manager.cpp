#include "calculations_manager.h"
#include <math.h>

static const char *TAG = "calculations_manager";

// Electricity below this is treated as standby rather than as running, so no COP is derived
// from it. 50 W.
#define COP_MIN_ELEC_MW 50000

// COP 20.00. Nothing domestic reaches this; the clamp is here so a bad meter reading cannot
// push the value past what the int16 history field holds.
#define COP_MAX_X100 2000

void update_radiator_outputs(node_manager_t *node_manager, home_manager_t *home_manager, radiator_manager_t *radiator_manager, room_manager_t *room_manager, esp_mqtt_client_handle_t mqtt_client, radiator_t *radiator)
{
    ESP_LOGI(TAG, "Calculating output for radiator %u", radiator->radiator_id);

    if (radiator->flow_temperature <= 0 || radiator->return_temperature <= 0)
    {
        ESP_LOGI(TAG, "Skipping calculation as radiator has 0 flow or return temperature value");
        return;
    }

    // Find the room that the radiator is in.
    //
    room_t *room = room_manager->room_list;

    while (room != NULL)
    {
        for (uint8_t r = 0; r < room->radiator_count; r++)
        {
            if (room->radiators[r] == radiator->radiator_id)
            {
                ESP_LOGI(TAG, "Radiator %u is in room %s", radiator->radiator_id, room->name);

                // Calclate the mean water temperature of the radiator
                // The probes can be in the wrong position for this. It doesn't matter.
                //
                radiator->mean_water_temperature = ((double)radiator->flow_temperature + (double)radiator->return_temperature) / 2;

                ESP_LOGI(TAG, "Radiator %u has an MWT of %f", radiator->radiator_id, radiator->mean_water_temperature);

                // Perform calculations if we have data!
                //
                if (room->current_temperature > 0)
                {
                    double deltaT = abs((double)radiator->mean_water_temperature / 100 - (double)room->current_temperature / 100);

                    ESP_LOGI(TAG, "Radiator %u has a MWT->Room ΔT of %f", radiator->radiator_id, deltaT);

                    // The power is given at dT 50
                    double dt = 50.0 / deltaT;

                    ESP_LOGI(TAG, "Radiator %u has a ΔT division of %f", radiator->radiator_id, dt);

                    double factor = pow(dt, 1.3);

                    ESP_LOGI(TAG, "Radiator %u has a power adjustment factor of %f", radiator->radiator_id, factor);

                    double adjusted_output = (double)radiator->output_dt_50 / factor;

                    radiator->heat_output = (uint16_t)adjusted_output;

                    ESP_LOGI(TAG, "Radiator %u has a calculated output of %uW", radiator->radiator_id, radiator->heat_output);
                }

                cJSON *root = cJSON_CreateObject();

                cJSON_AddNumberToObject(root, "flow_temperature", (double)radiator->flow_temperature / 100);
                cJSON_AddNumberToObject(root, "return_temperature", (double)radiator->return_temperature / 100);
                cJSON_AddNumberToObject(root, "mean_water_temperature", (double)radiator->mean_water_temperature / 100);
                cJSON_AddNumberToObject(root, "output", radiator->heat_output);

                char state_topic[61];
                snprintf(state_topic, sizeof(state_topic), "heating_monitor/radiators/%s", radiator->mqtt_name);

                char *payload = cJSON_PrintUnformatted(root);
                ESP_LOGI(TAG, "Publishing to MQTT topic %s", state_topic);
                esp_mqtt_client_publish(mqtt_client, state_topic, payload, 0, 0, 0);

                cJSON_free(payload);
                cJSON_Delete(root);

                update_room_heat_loss(node_manager, home_manager, room_manager, radiator_manager, mqtt_client, room);

                break;
            }
        }

        room = room->next;
    }

    update_home(home_manager, room_manager, radiator_manager, mqtt_client);
}

void update_room_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, esp_mqtt_client_handle_t mqtt_client, room_t *room)
{
    ESP_LOGI(TAG, "Calculating heat loss for room %u", room->room_id);

    int16_t current_temperature = 0;
    get_endpoint_measured_value(node_manager, room->room_temperature_node_id, room->room_temperature_endpoint_id, &current_temperature);

    room->current_temperature = current_temperature;

    ESP_LOGI(TAG, "Room %u has a current temperature of %d", room->room_id, room->current_temperature);

    double target_temperature_delta_t = abs((double)room->target_temperature / 100) - abs((double)home_manager->outdoor_temperature / 100);
    double current_delta_t = abs((double)room->current_temperature / 100) - abs((double)home_manager->outdoor_temperature / 100);

    ESP_LOGI(TAG, "Outdoor temperature is %d", home_manager->outdoor_temperature);
    ESP_LOGI(TAG, "Room %u has a target temperature of %d", room->room_id, room->target_temperature);
    ESP_LOGI(TAG, "Room %u has a target temperature -> outdoor temperature ΔT of %f", room->room_id, target_temperature_delta_t);
    ESP_LOGI(TAG, "Room %u has a current temperature -> outdoor temperature ΔT of %f", room->room_id, current_delta_t);
    ESP_LOGI(TAG, "Room %u has a predicated heat loss of %u W/°C", room->room_id, room->predicted_heat_loss_per_degree);

    room->predicted_heat_loss_at_target_temperature = target_temperature_delta_t * room->predicted_heat_loss_per_degree;
    room->predicted_heat_loss_at_current_temperature = current_delta_t * room->predicted_heat_loss_per_degree;

    ESP_LOGI(TAG, "Room %u has a predicted heat loss of %u W at target temperature", room->room_id, room->predicted_heat_loss_at_target_temperature);

    // Calculate the actual heat loss based on the radiator outputs if we can.
    //
    if (room->radiator_count > 0)
    {
        uint16_t total_radiator_output = 0;

        for (uint8_t r = 0; r < room->radiator_count; r++)
        {
            radiator_t *radiator = find_radiator(radiator_manager, room->radiators[r]);

            if (radiator)
            {
                total_radiator_output += radiator->heat_output;
            }
        }

        ESP_LOGI(TAG, "Room %u has an input of %u W", room->room_id, total_radiator_output);

        room->measured_heat_loss_per_degree = total_radiator_output / current_delta_t;

        ESP_LOGI(TAG, "Room %u has an actual heat loss of %u W/°C", room->room_id, room->measured_heat_loss_per_degree);
    }
    else
    {
        ESP_LOGI(TAG, "Room %u has no radiators, heat loss cannot be measured", room->room_id);
    }

    room->measured_heat_loss_at_target_temperature = target_temperature_delta_t * room->measured_heat_loss_per_degree;
    room->measured_heat_loss_at_current_temperature = current_delta_t * room->measured_heat_loss_per_degree;

    ESP_LOGI(TAG, "Room %u has a measured heat loss of %u W at target temperature", room->room_id, room->measured_heat_loss_at_target_temperature);

    room->heat_loss_difference = (room->predicted_heat_loss_per_degree > 0)
        ? ((double)room->measured_heat_loss_per_degree / room->predicted_heat_loss_per_degree) * 100.0
        : 0.0;

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "current_temperature", (double)room->current_temperature / 100);
    cJSON_AddNumberToObject(root, "predicted_heat_loss_per_degree", room->predicted_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "measured_heat_loss_per_degree", room->measured_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "heat_loss_difference", room->heat_loss_difference);

    char state_topic[61];
    snprintf(state_topic, sizeof(state_topic), "heating_monitor/rooms/%s", room->mqtt_name);

    char *payload = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publishing to MQTT topic %s", state_topic);
    esp_mqtt_client_publish(mqtt_client, state_topic, payload, 0, 0, 0);

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

    room_t *room = room_manager->room_list;

    while (room)
    {
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

    // Compute total radiator output across all rooms.
    radiator_t *radiator = radiator_manager->radiator_list;
    home_manager->total_radiator_output = 0;
    home_manager->radiator_count = 0;

    while (radiator)
    {
        home_manager->total_radiator_output += radiator->heat_output;
        home_manager->radiator_count++;
        radiator = radiator->next;
    }

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

    // Only publish if we have a conection??

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
    add_reading_or_null(root, "internal_temperature", home_manager->has_internal_temperature,
                        (double)home_manager->internal_temperature / 100.0);
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

    cJSON_AddNumberToObject(root, "total_predicted_heat_loss_per_degree", home_manager->total_predicted_heat_loss_per_degree);
    cJSON_AddNumberToObject(root, "total_measured_heat_loss_per_degree", home_manager->total_measured_heat_loss_per_degree);

    char *payload = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publishing to MQTT topic heating_monitor/home");
    esp_mqtt_client_publish(mqtt_client, "heating_monitor/home", payload, 0, 0, 0);

    cJSON_free(payload);
    cJSON_Delete(root);
}
