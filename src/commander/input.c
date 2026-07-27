#include "input.h"
#include "graph.h"
#include "render.h"

#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <string.h>

static bool MouseOverNodeTextBox(GraphContext *graph, Vector2 mouse) {
    float zoom = CanvasZoom(graph);
    for (int i = graph->node_count - 1; i >= 0; i--) {
        Node *node = &graph->nodes[i];
        if (node->collapsed) {
            continue;
        }
        Rectangle bounds = NodeScreenBounds(graph, node);
        float text_box_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 50.0f : 76.0f;
        Rectangle text_box = {
            bounds.x + 14 * zoom,
            bounds.y + text_box_y * zoom,
            bounds.width - 28 * zoom,
            30 * zoom,
        };
        if (CheckCollisionPointRec(mouse, text_box)) {
            return true;
        }
    }
    return false;
}

static bool MouseOverDialogTextBox(GraphContext *graph, Vector2 mouse) {
    if (!graph->open_dialog_open) {
        return false;
    }
    float scale = ApplicationScale(graph);
    Rectangle text_box = {100 * scale, ToolbarHeight(graph) + 8 * scale, 220 * scale, 28 * scale};
    return CheckCollisionPointRec(mouse, text_box);
}

static void UpdateMouseCursor(GraphContext *graph, Vector2 mouse, bool panning) {
    int cursor = MOUSE_CURSOR_DEFAULT;
    if (panning) {
        cursor = MOUSE_CURSOR_RESIZE_ALL;
    } else if (MouseOverDialogTextBox(graph, mouse) ||
               (!MouseOverPortInspector(graph, mouse) && MouseOverNodeTextBox(graph, mouse))) {
        cursor = MOUSE_CURSOR_IBEAM;
    }

    static int current_cursor = -1;
    if (cursor != current_cursor) {
        SetMouseCursor(cursor);
        current_cursor = cursor;
    }
}

void UpdateCanvas(GraphContext *graph) {
    bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool scale_up = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
    bool scale_down = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
    bool scale_reset = IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0);
    if (control && (scale_up || scale_down || scale_reset)) {
        if (scale_reset) {
            graph->application_scale = 1.0f;
        } else {
            float direction = scale_up ? 1.0f : -1.0f;
            graph->application_scale = Clamp(graph->application_scale + direction * APPLICATION_SCALE_STEP,
                                             APPLICATION_SCALE_MIN, APPLICATION_SCALE_MAX);
        }
        snprintf(graph->status, sizeof(graph->status), "Application scale %d%%",
                 (int)(ApplicationScale(graph) * 100.0f + 0.5f));
    }

    Vector2 mouse = GetMousePosition();
    bool in_canvas = mouse.y > ToolbarHeight(graph) && mouse.y < GetScreenHeight() - StatusHeight(graph);
    bool panning =
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));

    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && graph->selected_node_id >= 0 &&
        !IsEditingText(graph)) {
        RemoveNode(graph, graph->selected_node_id);
    }

    if (in_canvas && panning) {
        Vector2 delta = GetMouseDelta();
        graph->camera.target = Vector2Subtract(graph->camera.target, Vector2Scale(delta, 1.0f / CanvasZoom(graph)));
    }

    float wheel = GetMouseWheelMove();
    if (in_canvas && wheel != 0 && control) {
        Camera2D camera = CanvasCamera(graph);
        Vector2 before = GetScreenToWorld2D(mouse, camera);
        float direction = wheel > 0.0f ? 1.0f : -1.0f;
        graph->camera.zoom = Clamp(graph->camera.zoom + direction * NODE_ZOOM_STEP, NODE_ZOOM_MIN, NODE_ZOOM_MAX);
        camera = CanvasCamera(graph);
        Vector2 after = GetScreenToWorld2D(mouse, camera);
        graph->camera.target = Vector2Add(graph->camera.target, Vector2Subtract(before, after));
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_canvas && !panning) {
        int output = PortAtMouse(graph, mouse, PORT_DIR_OUTPUT);
        int input = PortAtMouse(graph, mouse, PORT_DIR_INPUT);
        int node_id = NodeAtMouse(graph, mouse);
        Node *node = FindNode(graph, node_id);
        if (output >= 0) {
            graph->active_port_id = output;
        } else if (input >= 0) {
            graph->active_port_id = DetachInput(graph, input);
            if (graph->active_port_id >= 0) {
                TextCopy(graph->status, "Link detached - drop it on an input to reconnect");
            }
        } else if (MouseOverPortInspector(graph, mouse)) {
            // clicks inside the pinned inspector panel: let raygui handle scrolling/selection
        } else if (node && !MouseOverNodeControl(graph, node, mouse) && !MouseOverCollapseButton(graph, node, mouse)) {
            graph->selected_node_id = node_id;
            graph->inspected_port_id = -1;
            Rectangle b = NodeScreenBounds(graph, node);
            if (mouse.y <= b.y + NODE_HEADER_HEIGHT * CanvasZoom(graph)) {
                Vector2 world_mouse = GetScreenToWorld2D(mouse, CanvasCamera(graph));
                graph->dragging_node_id = node_id;
                graph->drag_offset = Vector2Subtract(world_mouse, (Vector2){node->bounds.x, node->bounds.y});
                BringNodeToFront(graph, node_id);
            }
        } else if (!node) {
            graph->selected_node_id = -1;
            graph->inspected_port_id = -1;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas) {
        graph->knife_active = true;
        graph->knife_start = mouse;
        TextCopy(graph->status, "Knife active - release to cut connections");
    }
    if (graph->knife_active && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        int removed = CutLinks(graph, graph->knife_start, mouse);
        if (removed > 0) {
            snprintf(graph->status, sizeof(graph->status), "Cut %d connection%s - run graph to refresh", removed,
                     removed == 1 ? "" : "s");
        } else {
            TextCopy(graph->status, "No connections cut");
        }
        graph->knife_active = false;
    }

    if (graph->dragging_node_id >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Node *node = FindNode(graph, graph->dragging_node_id);
        Vector2 world_mouse = GetScreenToWorld2D(mouse, CanvasCamera(graph));
        if (node) {
            Vector2 position = Vector2Subtract(world_mouse, graph->drag_offset);
            node->bounds.x = position.x;
            node->bounds.y = position.y;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        graph->dragging_node_id = -1;
        if (graph->active_port_id >= 0) {
            int input = PortAtMouse(graph, mouse, PORT_DIR_INPUT);
            if (input >= 0 && AddLink(graph, graph->active_port_id, input)) {
                TextCopy(graph->status, "Connected - run graph to refresh downstream nodes");
            } else if (PortAtMouse(graph, mouse, PORT_DIR_OUTPUT) == graph->active_port_id) {
                // Released on the same output port with no link made: toggle pin
                if (graph->inspected_port_id == graph->active_port_id) {
                    graph->inspected_port_id = -1;
                } else {
                    graph->inspected_port_id = graph->active_port_id;
                    graph->inspect_scroll = 0;
                    graph->inspect_active = -1;
                }
            }
            graph->active_port_id = -1;
        }
    }

    UpdateMouseCursor(graph, mouse, panning);
}
