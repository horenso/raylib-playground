#include "input.h"
#include "graph.h"
#include "render.h"

#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <string.h>

static bool MouseOverNodeTextBox(GraphContext *graph, Vector2 mouse) {
    for (int i = graph->node_count - 1; i >= 0; i--) {
        Node *node = &graph->nodes[i];
        Rectangle bounds = NodeScreenBounds(graph, node);
        float text_box_y = NODE_HEADER_HEIGHT + 16.0f;
        Rectangle text_box = {
            bounds.x + CanvasSize(graph, 14.0f),
            bounds.y + CanvasSize(graph, text_box_y),
            bounds.width - CanvasSize(graph, 28.0f),
            CanvasSize(graph, 30.0f),
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
    Rectangle text_box = {
        UiSize(graph, 100.0f),
        ToolbarHeight(graph) + UiSize(graph, 8.0f),
        UiSize(graph, 220.0f),
        UiSize(graph, 28.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

static InteractionMode ResolveInteractionMode(GraphContext *graph, bool panning) {
    if (graph->knife_active) {
        return INTERACTION_KNIFE;
    }
    if (graph->dragging_node_id >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        return INTERACTION_DRAGGING_NODE;
    }
    if (graph->active_port_id >= 0) {
        return INTERACTION_LINKING;
    }
    if (panning) {
        return INTERACTION_PANNING;
    }
    return INTERACTION_IDLE;
}

static void UpdateMouseCursor(GraphContext *graph, Vector2 mouse) {
    int cursor = MOUSE_CURSOR_DEFAULT;
    if (graph->interaction_mode == INTERACTION_PANNING || graph->interaction_mode == INTERACTION_DRAGGING_NODE) {
        cursor = MOUSE_CURSOR_RESIZE_ALL;
    } else if (graph->interaction_mode == INTERACTION_KNIFE || graph->interaction_mode == INTERACTION_LINKING) {
        cursor = MOUSE_CURSOR_CROSSHAIR;
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

    graph->interaction_mode = ResolveInteractionMode(graph, panning);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas && graph->interaction_mode == INTERACTION_IDLE) {
        graph->knife_active = true;
        graph->knife_start = mouse;
        TextCopy(graph->status, "Knife active - release to cut nodes and connections");
    }
    graph->interaction_mode = ResolveInteractionMode(graph, panning);

    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && graph->selected_node_id >= 0 &&
        !IsEditingText(graph)) {
        RemoveNode(graph, graph->selected_node_id);
    }

    if (in_canvas && graph->interaction_mode == INTERACTION_PANNING) {
        Vector2 delta = GetMouseDelta();
        graph->camera.target = Vector2Subtract(graph->camera.target, Vector2Scale(delta, 1.0f / CanvasUnit(graph)));
    }

    float wheel = GetMouseWheelMove();
    if (in_canvas && wheel != 0 && control && graph->interaction_mode == INTERACTION_IDLE) {
        Camera2D camera = CanvasCamera(graph);
        Vector2 before = GetScreenToWorld2D(mouse, camera);
        float direction = wheel > 0.0f ? 1.0f : -1.0f;
        graph->camera.zoom = Clamp(graph->camera.zoom + direction * NODE_ZOOM_STEP, NODE_ZOOM_MIN, NODE_ZOOM_MAX);
        camera = CanvasCamera(graph);
        Vector2 after = GetScreenToWorld2D(mouse, camera);
        graph->camera.target = Vector2Add(graph->camera.target, Vector2Subtract(before, after));
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_canvas && graph->interaction_mode == INTERACTION_IDLE) {
        int output = PortAtMouse(graph, mouse, PORT_DIR_OUTPUT);
        int input = PortAtMouse(graph, mouse, PORT_DIR_INPUT);
        int node_id = NodeAtMouse(graph, mouse);
        Node *node = FindNode(graph, node_id);
        if (output >= 0) {
            graph->active_port_id = output;
            Port *port = FindPort(graph, output);
            if (port) {
                graph->selected_node_id = port->node_id;
                BringNodeToFront(graph, port->node_id);
            }
        } else if (input >= 0) {
            graph->active_port_id = DetachInput(graph, input);
            Port *port = FindPort(graph, input);
            if (port) {
                graph->selected_node_id = port->node_id;
                BringNodeToFront(graph, port->node_id);
            }
            if (graph->active_port_id >= 0) {
                TextCopy(graph->status, "Link detached - drop it on an input to reconnect");
            }
        } else if (MouseOverPortInspector(graph, mouse)) {
            // clicks inside the pinned inspector panel: let raygui handle scrolling/selection
        } else if (node) {
            graph->selected_node_id = node_id;
            graph->inspected_port_id = -1;
            Rectangle b = NodeScreenBounds(graph, node);
            bool over_control = MouseOverNodeControl(graph, node, mouse);
            if (!over_control && mouse.y <= b.y + CanvasSize(graph, NODE_HEADER_HEIGHT)) {
                Vector2 world_mouse = GetScreenToWorld2D(mouse, CanvasCamera(graph));
                graph->dragging_node_id = node_id;
                graph->drag_offset = Vector2Subtract(world_mouse, (Vector2){node->bounds.x, node->bounds.y});
            }
            BringNodeToFront(graph, node_id);
        } else if (!node) {
            graph->selected_node_id = -1;
            graph->inspected_port_id = -1;
        }
    }

    if (graph->knife_active && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        int removed_nodes = CutNodes(graph, graph->knife_start, mouse);
        int removed_links = CutLinks(graph, graph->knife_start, mouse);
        if (removed_nodes > 0 && removed_links > 0) {
            snprintf(graph->status, sizeof(graph->status),
                     "Removed %d node%s and cut %d connection%s - run graph to refresh", removed_nodes,
                     removed_nodes == 1 ? "" : "s", removed_links, removed_links == 1 ? "" : "s");
        } else if (removed_nodes > 0) {
            snprintf(graph->status, sizeof(graph->status), "Removed %d node%s - run graph to refresh", removed_nodes,
                     removed_nodes == 1 ? "" : "s");
        } else if (removed_links > 0) {
            snprintf(graph->status, sizeof(graph->status), "Cut %d connection%s - run graph to refresh", removed_links,
                     removed_links == 1 ? "" : "s");
        } else {
            TextCopy(graph->status, "Nothing intersected by knife");
        }
        graph->knife_active = false;
    }

    if (graph->interaction_mode == INTERACTION_DRAGGING_NODE) {
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

    graph->interaction_mode = ResolveInteractionMode(graph, panning);
    UpdateMouseCursor(graph, mouse);
}
