#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

#define OUT_DIR "build/"

static bool build(const char *input_path, const char *output_path) {
    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);

    nob_cc_inputs(&cmd, input_path);
    nob_cc_output(&cmd, output_path);

    nob_cmd_append(&cmd, "-I./raylib/include", "raylib/lib/libraylib.a",
                   "-L../raylib", "-lm", "-lX11");

    return nob_cmd_run(&cmd);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_mkdir_if_not_exists(OUT_DIR)) {
        return 1;
    }

    if (!build("src/pong/main.c", OUT_DIR "pong")) {
        nob_log(NOB_ERROR, "Could not build pong");
    }
    if (!build("src/commander/main.c", OUT_DIR "commander")) {
        nob_log(NOB_ERROR, "Could not build pong");
    }

    return 0;
}
