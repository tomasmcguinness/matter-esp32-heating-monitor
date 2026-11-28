#!/bin/sh
# Builds and runs the host tests for the MCP server, the history converter and the history totals. No board needed.
#
#   test_convert the offline history converter (../history_convert) and the format header checks
#   test_totals  the history totals' defrost accounting (the History page's defrost electricity)
#   test_comfort time at target per room (history_comfort.c), against day files in a temp directory
#   test_ufh_curve  a UFH manifold's flow from its calibration points (ufh_curve.h)
#   test_heat_balance  what the heat balance counts, and how a bucket ends (heat_balance_core.h)
#   test_mcp     the plain-C modules: cycle detection, point caps, timestamps, the zone table
#   test_server  the HTTP and JSON-RPC layer, against a day file in a temporary directory, written
#                in the old record layout and converted with history_convert first
#
# test_server uses ESP-IDF's copy of cJSON, so it needs IDF_PATH (or the usual install location).
set -e
cd "$(dirname "$0")"

IDF="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
CJSON="$IDF/components/json/cJSON"
MCP=../main/mcp
CONVERT=../history_convert
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined"

cc $FLAGS -I $CONVERT -I ../main/storage test_convert.c $CONVERT/history_convert.c -o test_convert
./test_convert

cc $FLAGS -I stubs -I ../main/storage test_totals.c ../main/storage/history_totals.c -o test_totals
./test_totals

cc $FLAGS -I stubs -I ../main/storage test_comfort.c ../main/storage/history_comfort.c -o test_comfort
./test_comfort

cc $FLAGS -I ../main/managers test_ufh_curve.c -o test_ufh_curve
./test_ufh_curve

cc $FLAGS -I ../main/storage test_heat_balance.c -o test_heat_balance
./test_heat_balance

cc $FLAGS -I $MCP -I ../main/storage test_mcp.c $MCP/mcp_cycles.c $MCP/mcp_series.c $MCP/mcp_time.c \
   ../main/storage/timezones.c -lm -o test_mcp
./test_mcp

# The firmware build embeds mcp_tools.json as a NUL-terminated symbol; this does the same.
{
    printf 'const char mcp_tools_json[] asm("_binary_mcp_tools_json_start") = "'
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' $MCP/mcp_tools.json | tr -d '\n'
    printf '";\n'
} > tools_json.gen.c

# mcp_server.c is built on its own so that only its opendir is pointed at the fixture directory.
cc $FLAGS -I stubs -I ../main -I "$CJSON" -Dopendir=test_opendir -c $MCP/mcp_server.c -o mcp_server.o
cc $FLAGS -I stubs -I ../main -I ../main/storage -I $CONVERT -I "$CJSON" test_server.c mcp_server.o tools_json.gen.c \
   $CONVERT/history_convert.c $MCP/mcp_cycles.c $MCP/mcp_series.c $MCP/mcp_time.c "$CJSON/cJSON.c" -lm -o test_server
./test_server
