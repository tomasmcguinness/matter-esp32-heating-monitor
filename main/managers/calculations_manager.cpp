#include "calculations_manager.h"
#include <math.h>

static const char *TAG = "calculations_manager";

void update_radiator_outputs(radiator_manager_t *radiator_manager, room_manager_t *room_manager, radiator_t *radiator)
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
                if (room->room_temperature > 0)
                {
                    double deltaT = mwt - (double)room->room_temperature / 100;

                    ESP_LOGI(TAG, "Radiator %u has a MWT->Room ΔT of %f", radiator->radiator_id, deltaT);

                    double dt = 50.0 / deltaT;

                    ESP_LOGI(TAG, "Radiator %u has a ΔT division of %f", radiator->radiator_id, dt);

                    double factor = pow(dt, 1.3);

                    ESP_LOGI(TAG, "Radiator %u has a power adjustment factor of %f", radiator->radiator_id, factor);

                    double new_power = (double)radiator->output_dt_50 / factor;

                    radiator->current_output = (uint16_t)new_power;

                    ESP_LOGI(TAG, "Radiator %u has a calculated output of %u", radiator->radiator_id, radiator->current_output);
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
}