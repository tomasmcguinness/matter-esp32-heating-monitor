#!/bin/sh
# Builds and runs the host tests for the MCP server. No board needed.
#
#   test_mcp     the plain-C modules: cycle detection, point caps, timestamps
#   test_server  the HTTP and JSON-RPC layer, against a day file in a temporary directory
#
# test_server uses ESP-IDF's copy of cJSON, so it needs IDF_PATH (or the usual install location).
set -e
cd "$(dirname "$0")"

IDF="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
CJSON="$IDF/components/json/cJSON"
MCP=../main/mcp
FLAGS="-std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined"

cc $FLAGS -I $MCP test_mcp.c $MCP/mcp_cycles.c $MCP/mcp_series.c $MCP/mcp_time.c -lm -o test_mcp
./test_mcp

# The firmware build embeds mcp_tools.json as a NUL-terminated symbol; this does the same.
{
    printf 'const char mcp_tools_json[] asm("_binary_mcp_tools_json_start") = "'
    sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' $MCP/mcp_tools.json | tr -d '\n'
    printf '";\n'
} > tools_json.gen.c

# mcp_server.c is built on its own so that only its opendir is pointed at the fixture directory.
cc $FLAGS -I stubs -I ../main -I "$CJSON" -Dopendir=test_opendir -c $MCP/mcp_server.c -o mcp_server.o
cc $FLAGS -I stubs -I ../main -I "$CJSON" test_server.c mcp_server.o tools_json.gen.c \
   $MCP/mcp_cycles.c $MCP/mcp_series.c $MCP/mcp_time.c "$CJSON/cJSON.c" -lm -o test_server
./test_server
