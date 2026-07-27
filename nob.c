#define NOB_IMPLEMENTATION
#include "nob.h"

#define OUT_DIR "build/"

static bool collect_c_files(Nob_Walk_Entry entry) {
    Nob_Cmd *cmd = entry.data;
    if (entry.type == NOB_FILE_REGULAR && nob_sv_ends_with_cstr(nob_sv_from_cstr(entry.path), ".c")) {
        nob_da_append(cmd, nob_temp_strdup(entry.path));
    }
    return true;
}

static bool collect_c_and_h_files(Nob_Walk_Entry entry) {
    Nob_Cmd *cmd = entry.data;
    if (entry.type == NOB_FILE_REGULAR) {
        Nob_String_View path = nob_sv_from_cstr(entry.path);
        if (nob_sv_ends_with_cstr(path, ".c") || nob_sv_ends_with_cstr(path, ".h")) {
            nob_da_append(cmd, nob_temp_strdup(entry.path));
        }
    }
    return true;
}

static bool build(const char *input_path, const char *output_path) {
    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);

    nob_cc_inputs(&cmd, input_path);
    nob_cc_output(&cmd, output_path);

    nob_cmd_append(&cmd, "-I./raylib/include", "raylib/lib/libraylib.a", "-L../raylib", "-lm", "-lX11");

    return nob_cmd_run(&cmd);
}

static bool format() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang-format", "-i");
    nob_walk_dir("src", collect_c_and_h_files, .data = &cmd);
    if (!nob_cmd_run(&cmd)) {
        nob_log(NOB_ERROR, "Could not format sources");
    }
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc >= 2 && strcmp(argv[1], "format") == 0) {
        format();
        return 0;
    }

    if (!nob_mkdir_if_not_exists(OUT_DIR)) {
        return 1;
    }

    if (!build("src/pong/main.c", OUT_DIR "pong")) {
        nob_log(NOB_ERROR, "Could not build pong");
    }

    {
        Nob_Cmd cmd = {0};
        nob_cc(&cmd);
        nob_cc_flags(&cmd);
        nob_walk_dir("src/commander", collect_c_files, .data = &cmd);
        nob_cc_output(&cmd, OUT_DIR "commander");
        nob_cmd_append(&cmd, "-I./raylib/include", "raylib/lib/libraylib.a", "-L../raylib", "-lm", "-lX11", "-lcurl");
        if (!nob_cmd_run(&cmd)) {
            nob_log(NOB_ERROR, "Could not build commander");
        }
    }

    return 0;
}
