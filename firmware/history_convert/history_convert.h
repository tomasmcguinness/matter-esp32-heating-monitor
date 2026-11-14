#pragma once

// Converts a copy of the SD card's home history from the version 2 record layout (28 bytes) to
// version 3 (40 bytes, with the defrost flag). Runs on a PC, never on the board: the firmware has
// no migration code. Built from the firmware's own history_format.h, so the two cannot disagree
// about the layout. See "Converting the SD card" in firmware/CLAUDE.md for the procedure.
//
// Only home days (home-YYYY-MM-DD) are written. Room and radiator files did not change format,
// and nothing else on the card -- the subscription log, the COP totals cache -- is touched.

#include <stdio.h>

typedef struct {
    unsigned converted; // version 2 days written out as version 3
    unsigned unchanged; // version 3 days, copied byte for byte
    unsigned failed;    // days that could not be converted or verified; nothing written for them
    unsigned trimmed;   // of those converted, days whose partial last record was dropped
} history_convert_summary_t;

// Converts every home day in `in_dir` into `out_dir`, creating `out_dir` if it does not exist.
// Never writes to `in_dir`: refuses outright, returning -1, if the two are the same directory.
// Each day is written to a temporary file, read back and checked against its input, and only then
// renamed into place; a day that fails is reported and left out of `out_dir`.
//
// One line per day goes to `report`. Returns -1 if it refused to run or could not read `in_dir`,
// otherwise the number of days that failed (0 on complete success).
int history_convert_dir(const char *in_dir, const char *out_dir, FILE *report, history_convert_summary_t *summary);

// Reports the format of every home day in `dir` without writing anything: for checking a card
// before and after converting. Returns -1 if `dir` cannot be read, otherwise the number of days
// that are not in the current format.
int history_convert_check_dir(const char *dir, FILE *report, history_convert_summary_t *summary);
