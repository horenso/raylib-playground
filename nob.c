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

    return build("src/pong/main.c", OUT_DIR "pong") ? 0 : 1;
}
