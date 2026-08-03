#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"

#include "raylib.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================
// Init
// ============================================================

static void InitExec(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Exec");
    TextCopy(node->parameter, "sort");
    node->bounds.width = 320;
    node->bounds.height = 184;
    AddPort(graph, node, "Stdin", VALUE_STRING, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Stdout", VALUE_STRING, PORT_DIR_OUTPUT, 148);
    AddPort(graph, node, "Stderr", VALUE_STRING, PORT_DIR_OUTPUT, 148);
}

// ============================================================
// Connection
// ============================================================

static bool CanAcceptExec(const Port *from) { return from->data_type == VALUE_STRING; }

// ============================================================
// Evaluate
// ============================================================

static bool EvaluateExec(GraphContext *graph, Node *node, Port *source, Port *output) {
    if (!node->parameter[0]) {
        return true;
    }
    Port *stderr_output = NodeOutputPort(graph, node, 1);

    char item_path[] = "/tmp/cdr_items_XXXXXX";
    int item_fd = mkstemp(item_path);
    FILE *items = item_fd >= 0 ? fdopen(item_fd, "w") : NULL;
    if (items) {
        for (int i = 0; source && i < source->item_count; i++) {
            fprintf(items, "%s\n", source->items[i].values[0].as.text);
        }
        fclose(items);
    } else if (item_fd >= 0) {
        close(item_fd);
    }

    char stderr_path[] = "/tmp/cdr_stderr_XXXXXX";
    int stderr_fd = mkstemp(stderr_path);
    if (!items || stderr_fd < 0) {
        if (stderr_fd >= 0) {
            close(stderr_fd);
            remove(stderr_path);
        }
        remove(item_path);
        TextCopy(graph->status, "Exec error: failed to create temporary files");
        graph->evaluation_error = true;
        return false;
    }
    close(stderr_fd);

    setenv("ITEMS", item_path, 1);
    char command[512];
    snprintf(command, sizeof(command), "(%s) 2> '%s'", node->parameter, stderr_path);
    FILE *process = popen(command, "r");
    bool success = true;
    if (!process) {
        TextCopy(graph->status, "Exec error: failed to run command");
        graph->evaluation_error = true;
        success = false;
    } else {
        ReadLines(process, output);
        int command_status = pclose(process);
        if (command_status == -1 || !WIFEXITED(command_status) || WEXITSTATUS(command_status) != 0) {
            int exit_code = command_status != -1 && WIFEXITED(command_status) ? WEXITSTATUS(command_status) : -1;
            snprintf(graph->status, sizeof(graph->status), "Exec error: command exited with status %d", exit_code);
            graph->evaluation_error = true;
            success = false;
        }
    }
    FILE *errors = fopen(stderr_path, "r");
    if (errors) {
        ReadLines(errors, stderr_output);
        fclose(errors);
    }
    remove(stderr_path);
    remove(item_path);
    return success;
}

// ============================================================
// Draw
// ============================================================

static void DrawExecContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Port *output = NodeOutputPort(graph, node, 0);
    Port *errors = NodeOutputPort(graph, node, 1);

    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }
    DrawInterfaceText(fonts.node_body,
                      TextFormat("%s | %d stdout | %d stderr", NodeStateLabel(node), output ? output->item_count : 0,
                                 errors ? errors->item_count : 0),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// Mouse hit-test
// ============================================================

static bool MouseInEditAreaExec(GraphContext *graph, Node *node, Vector2 mouse) {
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

const NodeDef kExecNodeDef = {
    .name = "Exec",
    .init = InitExec,
    .can_accept = CanAcceptExec,
    .expected_input_type = VALUE_STRING,
    .is_schema_computing = false,
    .preferred_field_name = NULL,
    .field_is_selectable = NULL,
    .propagate_schema = NULL,
    .uses_field_selector = false,
    .field_selector_label = NULL,
    .field_selector_y_offset = 12.0f,
    .evaluate = EvaluateExec,
    .draw_content = DrawExecContent,
    .control_height = 42.0f,
    .mouse_in_edit_area = MouseInEditAreaExec,
};
