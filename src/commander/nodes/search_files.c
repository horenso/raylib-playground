// search_files.c – Search Files node.
// Recursively scans text files in a directory (or one specific text file) for
// matching lines and emits them as records.
// Each output record has: path (STRING), line_number (INT), line (STRING).

#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ============================================================
// Helpers
// ============================================================

static bool FileLooksLikeText(FILE *file) {
    // Like many search tools, classify a file from an initial sample. NUL bytes
    // reliably identify the binary files that would otherwise pollute results.
    unsigned char sample[8192];
    size_t size = fread(sample, 1, sizeof(sample), file);
    rewind(file);
    return memchr(sample, '\0', size) == NULL;
}

static bool LineMatches(const Node *node, const char *line, regex_t *expression) {
    if (node->filter_use_regex) {
        return expression && regexec(expression, line, 0, NULL, 0) == 0;
    }

    const char *needle = node->secondary_parameter;
    int line_length = (int)strlen(line);
    int needle_length = (int)strlen(needle);
    for (int i = 0; i <= line_length - needle_length; i++) {
        bool equal = true;
        for (int j = 0; j < needle_length && equal; j++) {
            char left = line[i + j];
            char right = needle[j];
            if (!node->filter_case_sensitive) {
                left = (char)tolower((unsigned char)left);
                right = (char)tolower((unsigned char)right);
            }
            equal = left == right;
        }
        if (!equal) {
            continue;
        }
        if (!node->filter_whole_word) {
            return true;
        }
        bool left_boundary = i == 0 || !isalnum((unsigned char)line[i - 1]);
        bool right_boundary = i + needle_length >= line_length || !isalnum((unsigned char)line[i + needle_length]);
        if (left_boundary && right_boundary) {
            return true;
        }
    }
    return false;
}

static void SearchFileContent(Node *node, Port *output, const char *path, regex_t *expression) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return;
    }
    FILE *f = fopen(path, "r");
    if (!f || !FileLooksLikeText(f)) {
        if (f) {
            fclose(f);
        }
        return;
    }
    char line[MAX_PATH_LENGTH];
    long long line_number = 0;
    while (output->item_count < MAX_ITEMS && fgets(line, sizeof(line), f)) {
        line_number++;
        // Strip trailing newline characters for matching and display.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (!LineMatches(node, line, expression)) {
            continue;
        }
        StreamItem *item = &output->items[output->item_count++];
        memset(item, 0, sizeof(*item));
        SetTextValue(&item->values[0], VALUE_STRING, path);
        item->values[1].type = VALUE_INT;
        item->values[1].as.integer = line_number;
        SetTextValue(&item->values[2], VALUE_STRING, line);
    }
    fclose(f);
}

// ============================================================
// Schema
// ============================================================

static void RefreshSearchFilesSchema(GraphContext *graph, Node *node, Port *output) {
    (void)graph;
    (void)node;
    output->data_type = VALUE_RECORD;
    output->schema_valid = true;
    memset(&output->schema, 0, sizeof(output->schema));
    SchemaAddField(&output->schema, "path", VALUE_STRING, false);
    SchemaAddField(&output->schema, "line_number", VALUE_INT, false);
    SchemaAddField(&output->schema, "line", VALUE_STRING, false);
}

// ============================================================
// Init
// ============================================================

static void InitSearchFiles(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Search Files");
    TextCopy(node->parameter, ".");
    TextCopy(node->secondary_parameter, "");
    node->filter_case_sensitive = false;
    node->filter_whole_word = false;
    node->filter_use_regex = false;
    node->bounds.height = 220;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_OUTPUT, 148);
}

// ============================================================
// Evaluate
// ============================================================

static bool EvaluateSearchFiles(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)source;
    const char *pattern = node->secondary_parameter;
    if (!pattern[0]) {
        // No pattern — emit nothing rather than matching everything.
        return true;
    }
    regex_t expression;
    regex_t *compiled_expression = NULL;
    if (node->filter_use_regex) {
        int flags = REG_EXTENDED | REG_NOSUB;
        if (!node->filter_case_sensitive) {
            flags |= REG_ICASE;
        }
        int result = regcomp(&expression, pattern, flags);
        if (result != 0) {
            char error[96] = {0};
            regerror(result, &expression, error, sizeof(error));
            snprintf(graph->status, sizeof(graph->status), "Search Files: regex error: %s", error);
            graph->evaluation_error = true;
            return false;
        }
        compiled_expression = &expression;
    }

    struct stat info;
    if (stat(node->parameter, &info) == 0 && S_ISDIR(info.st_mode)) {
        FilePathList entries = LoadDirectoryFilesEx(node->parameter, NULL, true);
        for (unsigned int i = 0; i < entries.count && output->item_count < MAX_ITEMS; i++) {
            SearchFileContent(node, output, entries.paths[i], compiled_expression);
        }
        UnloadDirectoryFiles(entries);
    } else if (stat(node->parameter, &info) == 0 && S_ISREG(info.st_mode)) {
        SearchFileContent(node, output, node->parameter, compiled_expression);
    }
    if (compiled_expression) {
        regfree(&expression);
    }
    return true;
}

// ============================================================
// Draw
// ============================================================

static void DrawSearchFilesContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    float left_pad = 14.0f;
    float field_x = bounds.x + CanvasSize(graph, left_pad);
    float field_w = bounds.width - CanvasSize(graph, left_pad * 2.0f);
    float text_h = CanvasSize(graph, 30.0f);
    float button_h = CanvasSize(graph, 24.0f);
    float gap = CanvasSize(graph, 5.0f);
    float label_y_offset = FontTextCenterOffset(fonts.node_body, button_h);
    float label_x = bounds.x + CanvasSize(graph, left_pad);

    // Path row
    float path_y = NODE_HEADER_HEIGHT + 16.0f;
    float path_label_w = CanvasSize(graph, 38.0f);
    DrawInterfaceText(fonts.node_body, "Path", label_x,
                      bounds.y + CanvasSize(graph, path_y) + FontTextCenterOffset(fonts.node_body, text_h),
                      body_font_size, COLOR_MUTED);
    Rectangle path_box = {
        field_x + path_label_w,
        bounds.y + CanvasSize(graph, path_y),
        field_w - path_label_w,
        text_h,
    };
    if (DrawNodePathBox(graph, node, path_box, node->parameter, sizeof(node->parameter), 0, PATH_PICK_ANY)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }

    // Pattern text box
    float pattern_y = path_y + 38.0f;
    Rectangle pattern_box = {
        field_x,
        bounds.y + CanvasSize(graph, pattern_y),
        field_w,
        text_h,
    };
    if (DrawNodeTextBox(graph, node, pattern_box, node->secondary_parameter, sizeof(node->secondary_parameter), 1)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }

    // Match options: whole word, matching case, regular expression.
    float case_y = pattern_y + 38.0f;
    DrawInterfaceText(fonts.node_body, "Match", label_x, bounds.y + CanvasSize(graph, case_y) + label_y_offset,
                      body_font_size, COLOR_MUTED);
    struct {
        const char *label;
        bool *flag;
    } options[] = {
        {"W", &node->filter_whole_word},
        {"Aa", &node->filter_case_sensitive},
        {".*", &node->filter_use_regex},
    };
    float button_x = bounds.x + CanvasSize(graph, 60.0f);
    for (int i = 0; i < 3; i++) {
        Rectangle button = {button_x + i * (CanvasSize(graph, 34.0f) + gap), bounds.y + CanvasSize(graph, case_y),
                            CanvasSize(graph, 34.0f), button_h};
        if (DrawNodeOptionButton(graph, node, button, options[i].label, *options[i].flag, body_font_size)) {
            *options[i].flag = !*options[i].flag;
            MarkNodeDirty(graph, node->id);
            TextCopy(graph->status, "Search Files and downstream nodes are dirty");
        }
    }

    // Status line
    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, left_pad), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// Mouse hit-test
// ============================================================

static bool MouseInEditAreaSearchFiles(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float left_pad = 14.0f;
    float path_y = NODE_HEADER_HEIGHT + 16.0f;
    float pattern_y = path_y + 38.0f;
    float field_w = bounds.width - CanvasSize(graph, left_pad * 2.0f);
    float path_label_w = CanvasSize(graph, 38.0f);
    Rectangle path_box = {
        bounds.x + CanvasSize(graph, left_pad) + path_label_w,
        bounds.y + CanvasSize(graph, path_y),
        field_w - path_label_w - CanvasSize(graph, 30.0f + 4.0f),
        CanvasSize(graph, 30.0f),
    };
    Rectangle pattern_box = {
        bounds.x + CanvasSize(graph, left_pad),
        bounds.y + CanvasSize(graph, pattern_y),
        field_w,
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, path_box) || CheckCollisionPointRec(mouse, pattern_box);
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kSearchFilesNodeDef = {
    .name = "SearchFiles",
    .init = InitSearchFiles,
    .can_accept = NULL,
    .expected_input_type = VALUE_NONE,
    .is_schema_computing = false,
    .preferred_field_name = NULL,
    .field_is_selectable = NULL,
    .propagate_schema = NULL,
    .refresh_source_schema = RefreshSearchFilesSchema,
    .uses_field_selector = false,
    .field_selector_label = NULL,
    .field_selector_y_offset = 12.0f,
    .evaluate = EvaluateSearchFiles,
    .draw_content = DrawSearchFilesContent,
    .control_height = 118.0f,
    .mouse_in_edit_area = MouseInEditAreaSearchFiles,
};
