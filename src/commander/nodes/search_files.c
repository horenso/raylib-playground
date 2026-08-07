// search_files.c – Search Files node.
// Scans files in a directory for lines matching a regex and emits them as records.
// Each output record has: path (STRING), line_number (INT), line (STRING).

#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ============================================================
// Helpers
// ============================================================

static void SearchFileContent(Port *output, const char *path, regex_t *expression) {
    struct stat info;
    if (stat(path, &info) != 0 || S_ISDIR(info.st_mode)) {
        return;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
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
        if (regexec(expression, line, 0, NULL, 0) != 0) {
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
    node->directory_recursive = false;
    node->bounds.height = 240;
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
    if (node->directory_recursive) {
        FilePathList entries = LoadDirectoryFilesEx(node->parameter, NULL, true);
        for (unsigned int i = 0; i < entries.count && output->item_count < MAX_ITEMS; i++) {
            SearchFileContent(output, entries.paths[i], &expression);
        }
        UnloadDirectoryFiles(entries);
    } else {
        FilePathList entries = LoadDirectoryFiles(node->parameter);
        for (unsigned int i = 0; i < entries.count && output->item_count < MAX_ITEMS; i++) {
            SearchFileContent(output, entries.paths[i], &expression);
        }
        UnloadDirectoryFiles(entries);
    }
    regfree(&expression);
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
    float button_x = bounds.x + CanvasSize(graph, 60.0f);

    // Path text box
    float path_y = NODE_HEADER_HEIGHT + 16.0f;
    Rectangle path_box = {
        field_x,
        bounds.y + CanvasSize(graph, path_y),
        field_w,
        text_h,
    };
    if (DrawNodeTextBox(graph, node, path_box, node->parameter, sizeof(node->parameter), 0)) {
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

    // Case-sensitive toggle
    float case_y = pattern_y + 38.0f;
    DrawInterfaceText(fonts.node_body, "Case", label_x, bounds.y + CanvasSize(graph, case_y) + label_y_offset,
                      body_font_size, COLOR_MUTED);
    Rectangle case_btn = {button_x, bounds.y + CanvasSize(graph, case_y), CanvasSize(graph, 34.0f), button_h};
    if (DrawNodeOptionButton(graph, node, case_btn, "Aa", node->filter_case_sensitive, body_font_size)) {
        node->filter_case_sensitive = !node->filter_case_sensitive;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Search Files and downstream nodes are dirty");
    }

    // Depth toggle
    float depth_y = case_y + 30.0f;
    DrawInterfaceText(fonts.node_body, "Depth", label_x, bounds.y + CanvasSize(graph, depth_y) + label_y_offset,
                      body_font_size, COLOR_MUTED);
    Rectangle one_layer = {button_x, bounds.y + CanvasSize(graph, depth_y), CanvasSize(graph, 82.0f), button_h};
    Rectangle recursive = {one_layer.x + one_layer.width + gap, bounds.y + CanvasSize(graph, depth_y),
                           CanvasSize(graph, 94.0f), button_h};
    if (DrawNodeOptionButton(graph, node, one_layer, "One layer", !node->directory_recursive, body_font_size) &&
        node->directory_recursive) {
        node->directory_recursive = false;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Search Files depth changed - downstream nodes are dirty");
    }
    if (DrawNodeOptionButton(graph, node, recursive, "Recursive", node->directory_recursive, body_font_size) &&
        !node->directory_recursive) {
        node->directory_recursive = true;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Search Files depth changed - downstream nodes are dirty");
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
    Rectangle path_box = {
        bounds.x + CanvasSize(graph, left_pad),
        bounds.y + CanvasSize(graph, path_y),
        field_w,
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
    .control_height = 148.0f,
    .mouse_in_edit_area = MouseInEditAreaSearchFiles,
};
