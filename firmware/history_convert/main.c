// history_convert: converts a copy of the heating monitor's SD card history to the current home
// record layout. See "Converting the SD card" in firmware/CLAUDE.md.
//
//   history_convert CARD_COPY OUTPUT_DIR   convert every home day in CARD_COPY into OUTPUT_DIR
//   history_convert --check DIR            report each home day's format, writing nothing
//
// Exit status: 0 when every day converted (or every day checked is current), 1 when any did not,
// 2 when it refused to run.

#include <stdio.h>
#include <string.h>

#include "history_convert.h"

static int usage(void)
{
    fprintf(stderr,
            "usage: history_convert CARD_COPY OUTPUT_DIR\n"
            "       history_convert --check DIR\n"
            "\n"
            "Converts the home history days (home-YYYY-MM-DD) in CARD_COPY to the current\n"
            "record layout and writes them to OUTPUT_DIR. CARD_COPY is never written to.\n");
    return 2;
}

int main(int argc, char **argv)
{
    history_convert_summary_t summary;
    int                       failed;

    if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        failed = history_convert_check_dir(argv[2], stdout, &summary);
    } else if (argc == 3 && argv[1][0] != '-') {
        failed = history_convert_dir(argv[1], argv[2], stdout, &summary);
    } else {
        return usage();
    }

    return failed < 0 ? 2 : failed > 0 ? 1 : 0;
}
