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

enum Optimize { Optimize_Debug, Optimize_Release };

typedef struct {
    const char *input_file;
    const char *src_dir;
    Nob_Cmd extra_flags;
    enum Optimize optimize;
} BuildConfig;

static bool build(BuildConfig cfg, const char *name) {
    const char *subdir = cfg.optimize == Optimize_Release ? OUT_DIR "release/" : OUT_DIR "debug/";
    if (!nob_mkdir_if_not_exists(subdir)) {
        return false;
    }

    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);

    if (cfg.src_dir) {
        nob_walk_dir(cfg.src_dir, collect_c_files, .data = &cmd);
    } else if (cfg.input_file) {
        nob_cc_inputs(&cmd, cfg.input_file);
    }

    if (cfg.optimize == Optimize_Release) {
        nob_cmd_append(&cmd, "-O3");
    } else {
        nob_cmd_append(&cmd, "-g");
    }

    nob_cc_output(&cmd, nob_temp_sprintf("%s%s", subdir, name));
    nob_cmd_append(&cmd, "-I./raylib/include", "raylib/lib/libraylib.a", "-L../raylib", "-lm", "-lX11");
    nob_da_append_many(&cmd, cfg.extra_flags.items, cfg.extra_flags.count);

    return nob_cmd_run(&cmd);
}

static void format() {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang-format", "-i");
    nob_walk_dir("src", collect_c_and_h_files, .data = &cmd);
    if (!nob_cmd_run(&cmd)) {
        nob_log(NOB_ERROR, "Could not format sources");
    }
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *cmd = argc >= 2 ? argv[1] : "debug";

    enum Optimize variants[2];
    size_t variant_count = 0;

    if (strcmp(cmd, "debug") == 0) {
        variants[variant_count++] = Optimize_Debug;
    } else if (strcmp(cmd, "release") == 0) {
        variants[variant_count++] = Optimize_Release;
    } else if (strcmp(cmd, "all") == 0) {
        variants[variant_count++] = Optimize_Debug;
        variants[variant_count++] = Optimize_Release;
    } else if (strcmp(cmd, "format") == 0) {
        format();
        return 0;
    } else {
        nob_log(NOB_INFO, "Usage: %s [debug|release|all|format]", argv[0]);
        return 1;
    }

    if (!nob_mkdir_if_not_exists(OUT_DIR)) {
        return 1;
    }

    for (size_t i = 0; i < variant_count; i++) {
        enum Optimize opt = variants[i];

        if (!build((BuildConfig){.input_file = "src/pong/main.c", .optimize = opt}, "pong")) {
            nob_log(NOB_ERROR, "Could not build pong");
        }

        {
            Nob_Cmd commander_flags = {0};
            nob_cmd_append(&commander_flags, "-Isrc/commander", "-I/usr/include/freetype2", "-lcurl", "-lfreetype");
            if (!build((BuildConfig){.src_dir = "src/commander", .extra_flags = commander_flags, .optimize = opt},
                       "commander")) {
                nob_log(NOB_ERROR, "Could not build commander");
            }
        }
    }

    return 0;
}
