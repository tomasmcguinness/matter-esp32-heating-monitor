#include "calculations_manager.h"
#include <math.h>

static const char *TAG = "calculations_manager";

void update_radiator_outputs(node_manager_t *node_manager, home_manager_t *home_manager, radiator_manager_t *radiator_manager, room_manager_t *room_manager, radiator_t *radiator)
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
                double mwt = ((double)radiator->flow_temperature / 100 + (double)radiator->return_temperature / 100) / 2;

                ESP_LOGI(TAG, "Radiator %u has an MWT of %f", radiator->radiator_id, mwt);

                // Perform calculations if we have data!
                //
                if (room->current_temperature > 0)
                {
                    double deltaT = mwt - (double)room->current_temperature / 100;

                    ESP_LOGI(TAG, "Radiator %u has a MWT->Room ΔT of %f", radiator->radiator_id, deltaT);

                    double dt = 50.0 / deltaT;

                    ESP_LOGI(TAG, "Radiator %u has a ΔT division of %f", radiator->radiator_id, dt);

                    double factor = pow(dt, 1.3);

                    ESP_LOGI(TAG, "Radiator %u has a power adjustment factor of %f", radiator->radiator_id, factor);

                    double adjusted_output = (double)radiator->output_dt_50 / factor;

                    radiator->heat_output = (uint16_t)adjusted_output;

                    ESP_LOGI(TAG, "Radiator %u has a calculated output of %u", radiator->radiator_id, radiator->heat_output);
                }
                else
                {
                    ESP_LOGI(TAG, "Can't continue the output calculation for Radiator %u as the room has no reported temperature", radiator->radiator_id);
                }

                break;
            }
        }

        room = room->next;
    }

    update_home(home_manager, room_manager, radiator_manager);
}

void update_room_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager, room_t *room)
{
    ESP_LOGI(TAG, "Calculating heat loss for room %u", room->room_id);

    int16_t current_temperature = 0;
    get_endpoint_measured_value(node_manager, room->room_temperature_node_id, room->room_temperature_endpoint_id, &current_temperature);

    room->current_temperature = current_temperature;

    ESP_LOGI(TAG, "Room %u has a current temperature of %d", room->room_id, room->current_temperature);

    double target_temperature_delta_t = abs((double)room->target_temperature / 100) + abs((double)home_manager->outdoor_temperature / 100);
    double current_delta_t = abs((double)room->current_temperature / 100) + abs((double)home_manager->outdoor_temperature / 100);

    ESP_LOGI(TAG, "Predicted target heat loss for room %u", room->room_id);

    ESP_LOGI(TAG, "Outdoor temperature is %d", home_manager->outdoor_temperature);
    ESP_LOGI(TAG, "Room %u has a target temperature of %d", room->room_id, room->target_temperature);
    ESP_LOGI(TAG, "Room %u has a target temperature -> outdoor temperature ΔT of %f", room->room_id, target_temperature_delta_t);
    ESP_LOGI(TAG, "Room %u has a current temperature -> outdoor temperature ΔT of %f", room->room_id, current_delta_t);
    ESP_LOGI(TAG, "Room %u has a survey heat loss of %u W/°C", room->room_id, room->survey_heat_loss_per_degree);

    room->predicted_heat_loss_at_target = target_temperature_delta_t * room->survey_heat_loss_per_degree;

    ESP_LOGI(TAG, "Room %u has a predicted heat loss of %u W at target temperature", room->room_id, room->predicted_heat_loss_at_target);

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

        room->current_heat_loss_per_degree = total_radiator_output / current_delta_t;
    }

    ESP_LOGI(TAG, "Room %u has a current heat loss of %u W/°C", room->room_id, room->current_heat_loss_per_degree);

    room->estimated_heat_loss_at_target = target_temperature_delta_t * room->current_heat_loss_per_degree;

    ESP_LOGI(TAG, "Room %u has a estimated heat loss of %u W at target temperature", room->room_id, room->estimated_heat_loss_at_target);

    update_home(home_manager, room_manager, radiator_manager);
}

void update_all_rooms_heat_loss(node_manager_t *node_manager, home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager)
{
    room_t *room = room_manager->room_list;

    while (room != NULL)
    {
        update_room_heat_loss(node_manager, home_manager, room_manager, radiator_manager, room);
        room = room->next;
    }

    update_home(home_manager, room_manager, radiator_manager);
}

void update_home(home_manager_t *home_manager, room_manager_t *room_manager, radiator_manager_t *radiator_manager)
{
    // Compute heat source output.
    //
    if (home_manager->heat_source_flow_rate == 0 || home_manager->heat_source_flow_temperature == 0 || home_manager->heat_source_return_temperature == 0)
    {
        ESP_LOGI(TAG, "Can't calculate heat source output as we don't have all the required data yet");
        home_manager->heat_source_output = 0;
    }
    else
    {
        double flow_rate_litres_per_second = (double)home_manager->heat_source_flow_rate / 360.0; // This isn't strictly correct.
        double delta_t = ((double)home_manager->heat_source_flow_temperature / 100.0) - ((double)home_manager->heat_source_return_temperature / 100.0);

        uint16_t output = flow_rate_litres_per_second * delta_t * 4186; // 4186 is the specific heat capacity of water in J/kg°C, and our flow rate is in L/s which is kg/s, so this gives us watts.
        home_manager->heat_source_output = output;
    }

    // Compute total predicted heat loss across all rooms.
    // Compute total estimated heat loss across all rooms.
    room_t *room = room_manager->room_list;

    while (room)
    {
        home_manager->total_predicted_heat_loss += room->predicted_heat_loss_at_target;
        home_manager->total_estimated_heat_loss += room->estimated_heat_loss_at_target;

        room = room->next;
    }

    // Compute total radiator output across all rooms.
    radiator_t *radiator = radiator_manager->radiator_list;

    while (radiator)
    {
        home_manager->total_radiator_output += radiator->heat_output;
        radiator = radiator->next;
    }

    // TODO UFH.
}
