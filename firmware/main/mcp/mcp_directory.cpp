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

        for (uint8_t s = 0; s < room->ufh_share_count && info->ufh_share_count < MCP_ROOM_UFH_SHARES_MAX; s++)
        {
            info->ufh_shares[info->ufh_share_count].manifold_id = room->ufh_shares[s].emitter_id;
            info->ufh_shares[info->ufh_share_count].pct = room->ufh_shares[s].pct;
            info->ufh_share_count++;
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
        info->is_ufh_manifold = is_ufh_manifold(radiator);
        info->ufh_loop_count = 0;
        info->ufh_point_count = 0;

        if (info->is_ufh_manifold && ufh_is_calibrated(radiator))
        {
            info->ufh_loop_count = radiator->ufh_loop_count < MCP_UFH_LOOPS_MAX ? radiator->ufh_loop_count : MCP_UFH_LOOPS_MAX;

            for (uint8_t p = 0; p < radiator->ufh_point_count && info->ufh_point_count < MCP_UFH_POINTS_MAX; p++)
            {
                info->ufh_points[p].ref_flow_lph = radiator->ufh_points[p].ref_flow_lph;
                memcpy(info->ufh_points[p].loop_flow_dlpm, radiator->ufh_points[p].loop_flow_dlpm, info->ufh_loop_count * sizeof(uint16_t));
                info->ufh_point_count++;
            }
        }
    }

    return count;
}
