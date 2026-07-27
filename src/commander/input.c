#include "input.h"
#include "graph.h"
#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

void UpdateCanvas(GraphContext *graph) {
    Vector2 mouse = GetMousePosition();
    bool in_canvas = mouse.y > TOOLBAR_HEIGHT && mouse.y < GetScreenHeight() - STATUS_HEIGHT;
    bool panning =
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));

    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && graph->selected_node_id >= 0 &&
        !IsEditingText(graph)) {
        RemoveNode(graph, graph->selected_node_id);
    }

    if (in_canvas && panning) {
        Vector2 delta = GetMouseDelta();
        graph->camera.target = Vector2Subtract(graph->camera.target, Vector2Scale(delta, 1.0f / graph->camera.zoom));
    }

    float wheel = GetMouseWheelMove();
    if (in_canvas && wheel != 0) {
        Vector2 before = GetScreenToWorld2D(mouse, graph->camera);
        graph->camera.zoom = Clamp(graph->camera.zoom * (1.0f + wheel * 0.12f), 0.75f, 2.0f);
        Vector2 after = GetScreenToWorld2D(mouse, graph->camera);
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
        } else if (node && !MouseOverNodeControl(graph, node, mouse)) {
            graph->selected_node_id = node_id;
            Rectangle b = NodeScreenBounds(graph, node);
            if (mouse.y <= b.y + NODE_HEADER_HEIGHT * graph->camera.zoom) {
                Vector2 world_mouse = GetScreenToWorld2D(mouse, graph->camera);
                graph->dragging_node_id = node_id;
                graph->drag_offset = Vector2Subtract(world_mouse, (Vector2){node->bounds.x, node->bounds.y});
                BringNodeToFront(graph, node_id);
            }
        } else if (!node) {
            graph->selected_node_id = -1;
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
        Vector2 world_mouse = GetScreenToWorld2D(mouse, graph->camera);
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
            }
            graph->active_port_id = -1;
        }
    }
}
