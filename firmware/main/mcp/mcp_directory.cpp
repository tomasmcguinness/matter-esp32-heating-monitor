#include "mcp_directory.h"

#include <cstring>

#include "managers/radiator_manager.h"
#include "managers/room_manager.h"

extern room_manager_t g_room_manager;
extern radiator_manager_t g_radiator_manager;

size_t mcp_directory_rooms(mcp_room_info_t *out, size_t max)
{
    size_t count = 0;

    for (room_t *room = g_room_manager.room_list; room != NULL && count < max; room = room->next)
    {
        mcp_room_info_t *info = &out[count++];

        memset(info, 0, sizeof(*info));
        info->id = room->room_id;
        strlcpy(info->name, room->name ? room->name : "", sizeof(info->name));
        info->target_temp_c100 = room->target_temperature;
        info->heat_loss_w_per_c = room->predicted_heat_loss_per_degree;

        for (uint8_t r = 0; r < room->radiator_count && info->radiator_count < MCP_ROOM_RADIATORS_MAX; r++)
        {
            info->radiators[info->radiator_count++] = room->radiators[r];
        }
    }

    return count;
}

size_t mcp_directory_radiators(mcp_radiator_info_t *out, size_t max)
{
    size_t count = 0;

    for (radiator_t *radiator = g_radiator_manager.radiator_list; radiator != NULL && count < max; radiator = radiator->next)
    {
        mcp_radiator_info_t *info = &out[count++];

        memset(info, 0, sizeof(*info));
        info->id = radiator->radiator_id;
        strlcpy(info->name, radiator->name ? radiator->name : "", sizeof(info->name));
        info->rated_output_w = radiator->output_dt_50;
    }

    return count;
}
