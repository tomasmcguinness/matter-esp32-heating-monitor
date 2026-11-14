#!/bin/sh
# Builds the history converter for this PC. Needs only a C compiler -- no ESP-IDF.
# It is built from the firmware's own main/storage/history_format.h, so rebuild it from the same
# commit as the firmware you are about to flash.
set -e
cd "$(dirname "$0")"
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror -I ../main/storage history_convert.c main.c -o history_convert
echo "Built $(pwd)/history_convert"
