#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <string.h>
#include <sys/stat.h>

// ============================================================
// Helpers
// ============================================================

static void AppendFileRecord(Node *node, Port *output, const char *path) {
    if (!output || output->item_count >= MAX_ITEMS) {
        return;
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        return;
    }
    bool is_folder = S_ISDIR(info.st_mode);
    bool include = node->directory_entry_type == DIRECTORY_ENTRY_BOTH ||
                   (node->directory_entry_type == DIRECTORY_ENTRY_FOLDERS && is_folder) ||
                   (node->directory_entry_type == DIRECTORY_ENTRY_FILES && !is_folder);
    if (!include) {
        return;
    }
    StreamItem *item = &output->items[output->item_count++];
    memset(item, 0, sizeof(*item));
    SetTextValue(&item->values[0], VALUE_STRING, path);
    SetTextValue(&item->values[1], VALUE_STRING, GetFileName(path));
    SetTextValue(&item->values[2], VALUE_STRING, is_folder ? "folder" : "file");
    item->values[3].type = VALUE_SIZE;
    item->values[3].as.file_size = is_folder ? 0 : (unsigned long long)info.st_size;
    item->values[4].type = VALUE_DATETIME;
    item->values[4].as.datetime = (long long)info.st_mtime;
}

static void AppendDirectoryEntries(Node *node, Port *output, FilePathList entries) {
    for (unsigned int i = 0; output && i < entries.count && output->item_count < MAX_ITEMS; i++) {
        AppendFileRecord(node, output, entries.paths[i]);
    }
}

// ============================================================
// Init
// ============================================================

static void InitFiles(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Files");
    TextCopy(node->parameter, ".");
    node->directory_entry_type = DIRECTORY_ENTRY_FILES;
    node->bounds.height = 220;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_OUTPUT, 112);
    Port *out = NodeOutputPort(graph, node, 0);
    if (out) {
        SchemaAddField(&out->schema, "path", VALUE_STRING, false);
        SchemaAddField(&out->schema, "name", VALUE_STRING, false);
        SchemaAddField(&out->schema, "type", VALUE_STRING, false);
        SchemaAddField(&out->schema, "size", VALUE_SIZE, false);
        SchemaAddField(&out->schema, "modified", VALUE_DATETIME, false);
    }
}

// ============================================================
// Evaluate
// ============================================================

static bool EvaluateFiles(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)graph;
    (void)source;
    if (node->directory_recursive) {
        FilePathList entries = LoadDirectoryFilesEx(node->parameter, NULL, true);
        AppendDirectoryEntries(node, output, entries);
        UnloadDirectoryFiles(entries);
    } else {
        FilePathList entries = LoadDirectoryFiles(node->parameter);
        AppendDirectoryEntries(node, output, entries);
        UnloadDirectoryFiles(entries);
    }
    return true;
}

// ============================================================
// Draw
// ============================================================

static void DrawFilesContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    float text_box_y = NODE_HEADER_HEIGHT + 16.0f;
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, text_box_y),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }

    float label_x = bounds.x + CanvasSize(graph, 14.0f);
    float button_x = bounds.x + CanvasSize(graph, 60.0f);
    float type_y = bounds.y + CanvasSize(graph, text_box_y + 38.0f);
    float depth_y = bounds.y + CanvasSize(graph, text_box_y + 68.0f);
    float button_h = CanvasSize(graph, 24.0f);
    float gap = CanvasSize(graph, 5.0f);
    float label_y_offset = FontTextCenterOffset(fonts.node_body, button_h);

    DrawInterfaceText(fonts.node_body, "Type", label_x, type_y + label_y_offset, body_font_size, COLOR_MUTED);
    struct {
        const char *label;
        DirectoryEntryType type;
        float width;
    } type_buttons[] = {
        {"Files", DIRECTORY_ENTRY_FILES, 50.0f},
        {"Folders", DIRECTORY_ENTRY_FOLDERS, 70.0f},
        {"Both", DIRECTORY_ENTRY_BOTH, 50.0f},
    };
    float x = button_x;
    for (int i = 0; i < 3; i++) {
        Rectangle btn = {x, type_y, CanvasSize(graph, type_buttons[i].width), button_h};
        if (DrawNodeOptionButton(graph, node, btn, type_buttons[i].label,
                                 node->directory_entry_type == type_buttons[i].type, body_font_size) &&
            node->directory_entry_type != type_buttons[i].type) {
            node->directory_entry_type = type_buttons[i].type;
            MarkNodeDirty(graph, node->id);
            TextCopy(graph->status, "Files type changed - downstream nodes are dirty");
        }
        x += btn.width + gap;
    }

    DrawInterfaceText(fonts.node_body, "Depth", label_x, depth_y + label_y_offset, body_font_size, COLOR_MUTED);
    Rectangle one_layer = {button_x, depth_y, CanvasSize(graph, 82.0f), button_h};
    Rectangle recursive = {one_layer.x + one_layer.width + gap, depth_y, CanvasSize(graph, 94.0f), button_h};
    if (DrawNodeOptionButton(graph, node, one_layer, "One layer", !node->directory_recursive, body_font_size) &&
        node->directory_recursive) {
        node->directory_recursive = false;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Files depth changed - downstream nodes are dirty");
    }
    if (DrawNodeOptionButton(graph, node, recursive, "Recursive", node->directory_recursive, body_font_size) &&
        !node->directory_recursive) {
        node->directory_recursive = true;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Files depth changed - downstream nodes are dirty");
    }

    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// Mouse hit-test
// ============================================================

static bool MouseInEditAreaFiles(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kFilesNodeDef = {
    .name = "Files",
    .init = InitFiles,
    .can_accept = NULL,
    .expected_input_type = VALUE_NONE,
    .is_schema_computing = false,
    .preferred_field_name = NULL,
    .field_is_selectable = NULL,
    .propagate_schema = NULL,
    .uses_field_selector = false,
    .field_selector_label = NULL,
    .field_selector_y_offset = 12.0f,
    .evaluate = EvaluateFiles,
    .draw_content = DrawFilesContent,
    .control_height = 110.0f,
    .mouse_in_edit_area = MouseInEditAreaFiles,
};
