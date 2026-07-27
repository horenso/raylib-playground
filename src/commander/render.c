#include "render.h"
#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raygui.h"

void DrawCanvasGrid(GraphContext *graph) {
    Rectangle canvas = {0, TOOLBAR_HEIGHT, (float)GetScreenWidth(),
                        (float)GetScreenHeight() - TOOLBAR_HEIGHT - STATUS_HEIGHT};
    DrawRectangleRec(canvas, COLOR_CANVAS);
    Vector2 top_left = GetScreenToWorld2D((Vector2){canvas.x, canvas.y}, graph->camera);
    Vector2 bottom_right = GetScreenToWorld2D((Vector2){canvas.width, canvas.y + canvas.height}, graph->camera);
    const float step = 32.0f;
    int first_x = (int)(top_left.x / step) - 1;
    int last_x = (int)(bottom_right.x / step) + 1;
    int first_y = (int)(top_left.y / step) - 1;
    int last_y = (int)(bottom_right.y / step) + 1;

    for (int i = first_x; i <= last_x; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){i * step, 0}, graph->camera);
        DrawLine((int)p.x, (int)canvas.y, (int)p.x, (int)(canvas.y + canvas.height),
                 i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
    for (int i = first_y; i <= last_y; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){0, i * step}, graph->camera);
        DrawLine((int)canvas.x, (int)p.y, (int)(canvas.x + canvas.width), (int)p.y,
                 i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
}

void DrawConnection(Vector2 from, Vector2 to, Color color, float thickness) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, 55, 180);
    Vector2 points[4] = {from, {from.x + tangent, from.y}, {to.x - tangent, to.y}, to};
    DrawSplineBezierCubic(points, 4, thickness, color);
}

void DrawKnife(Vector2 start, Vector2 end) {
    Color glow = {255, 91, 105, 70};
    Color blade = {255, 220, 224, 255};
    DrawLineEx(start, end, 7, glow);
    DrawLineEx(start, end, 2, blade);
    DrawCircleV(start, 4, blade);
    DrawCircleV(end, 4, blade);
}

void DrawNodeShell(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float radius = 0.08f;
    DrawRectangleRounded(bounds, radius, 8, COLOR_NODE);
    Rectangle header = {bounds.x, bounds.y, bounds.width, NODE_HEADER_HEIGHT * graph->camera.zoom};
    DrawRectangleRounded(header, radius, 8, COLOR_NODE_HEADER);
    DrawRectangleRec((Rectangle){header.x, header.y + header.height * 0.55f, header.width, header.height * 0.45f},
                     COLOR_NODE_HEADER);
    DrawRectangleRoundedLinesEx(bounds, radius, 8, graph->selected_node_id == node->id ? 2.0f : 1.0f,
                                graph->selected_node_id == node->id ? COLOR_NODE_SELECTED : (Color){66, 74, 91, 255});

    if (graph->camera.zoom >= NODE_DETAIL_MIN_ZOOM) {
        DrawInterfaceText(fonts.title, node->title, bounds.x + 14 * graph->camera.zoom,
                          bounds.y + 7 * graph->camera.zoom, TITLE_TEXT_SIZE, COLOR_TEXT);
    }
}

void DrawNodePorts(GraphContext *graph, Node *node) {
    for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT; direction++) {
        int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        for (int i = 0; i < count; i++) {
            Port *port = FindPort(graph, ids[i]);
            Vector2 p = PortScreenPosition(graph, port);
            Color color = PortColor(port->data_type);
            DrawCircleV(p, PORT_RADIUS, color);
            DrawCircleLinesV(p, PORT_RADIUS + 2, Fade(color, 0.45f));
            if (graph->camera.zoom >= NODE_DETAIL_MIN_ZOOM) {
                int width = (int)MeasureTextEx(fonts.body, port->name, PORT_TEXT_SIZE, 0).x;
                float x = direction == PORT_DIR_INPUT ? p.x + 13 : p.x - width - 13;
                DrawInterfaceText(fonts.body, port->name, x, p.y - PORT_TEXT_SIZE / 2, PORT_TEXT_SIZE, COLOR_MUTED);
            }
        }
    }
}

void DrawNodeContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = graph->camera.zoom;
    if (zoom < NODE_DETAIL_MIN_ZOOM) {
        DrawInterfaceText(fonts.body, TextFormat("%d items", node->item_count), bounds.x + 12 * zoom,
                          bounds.y + 48 * zoom, BODY_TEXT_SIZE, COLOR_MUTED);
        return;
    }

    if (node->type == NODE_DIRECTORY_LIST || node->type == NODE_STRING_MATCH) {
        const char *label = node->type == NODE_DIRECTORY_LIST ? "Path" : "Pattern";
        float label_y = node->type == NODE_DIRECTORY_LIST ? 50.0f : 76.0f;
        float text_box_y = node->type == NODE_DIRECTORY_LIST ? 68.0f : 94.0f;
        float count_y = node->type == NODE_DIRECTORY_LIST ? 114.0f : 150.0f;
        DrawInterfaceText(fonts.body, label, bounds.x + 14 * zoom, bounds.y + label_y * zoom, BODY_TEXT_SIZE,
                          COLOR_MUTED);
        Rectangle text_box = {bounds.x + 14 * zoom, bounds.y + text_box_y * zoom, bounds.width - 28 * zoom, 30 * zoom};
        char before[128];
        TextCopy(before, node->parameter);
        if (GuiTextBox(text_box, node->parameter, sizeof(node->parameter), node->text_editing)) {
            node->text_editing = !node->text_editing;
        }
        if (strcmp(before, node->parameter) != 0) {
            node->is_dirty = true;
            MarkDownstreamDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status), "Parameters changed - run graph to refresh");
        }
        DrawInterfaceText(fonts.body, TextFormat("%d item%s", node->item_count, node->item_count == 1 ? "" : "s"),
                          bounds.x + 14 * zoom, bounds.y + count_y * zoom, BODY_TEXT_SIZE, COLOR_MUTED);
    } else {
        DrawInterfaceText(fonts.body, TextFormat("%d item%s", node->item_count, node->item_count == 1 ? "" : "s"),
                          bounds.x + 14 * zoom, bounds.y + 71 * zoom, BODY_TEXT_SIZE, COLOR_MUTED);
        Rectangle list_bounds = {bounds.x + 14 * zoom, bounds.y + 93 * zoom, bounds.width - 28 * zoom,
                                 bounds.height - 107 * zoom};
        char *entries[MAX_ITEMS];
        for (int i = 0; i < node->item_count; i++) {
            entries[i] = node->items[i];
        }
        GuiListViewEx(list_bounds, entries, node->item_count, &node->list_scroll, &node->list_active, NULL);
    }
}

bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!node || graph->camera.zoom < NODE_DETAIL_MIN_ZOOM) {
        return false;
    }
    Rectangle b = NodeScreenBounds(graph, node);
    float z = graph->camera.zoom;
    if (node->type == NODE_INSPECT_VIEW) {
        return CheckCollisionPointRec(mouse,
                                      (Rectangle){b.x + 10 * z, b.y + 86 * z, b.width - 20 * z, b.height - 96 * z});
    }
    float control_y = node->type == NODE_STRING_MATCH ? 88.0f : 62.0f;
    return CheckCollisionPointRec(mouse, (Rectangle){b.x + 10 * z, b.y + control_y * z, b.width - 20 * z, 42 * z});
}

void DrawToolbar(GraphContext *graph) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, GUI_TEXT_SIZE);
    DrawRectangle(0, 0, GetScreenWidth(), (int)TOOLBAR_HEIGHT, (Color){25, 29, 37, 255});
    DrawLine(0, (int)TOOLBAR_HEIGHT - 1, GetScreenWidth(), (int)TOOLBAR_HEIGHT - 1, (Color){59, 67, 82, 255});

    if (GuiButton((Rectangle){12, 10, 116, 32}, "#08# Add node")) {
        graph->add_menu_open = !graph->add_menu_open;
    }
    if (GuiButton((Rectangle){138, 10, 116, 32}, "#131# Run graph")) {
        RunGraph(graph);
    }
    if (GuiButton((Rectangle){264, 10, 82, 32}, "Reset")) {
        SeedGraph(graph);
    }

    DrawInterfaceText(fonts.body, TextFormat("Zoom  %d%%", (int)(graph->camera.zoom * 100)), GetScreenWidth() - 112, 18,
                      BODY_TEXT_SIZE, COLOR_MUTED);

    if (graph->add_menu_open) {
        Rectangle menu = {12, TOOLBAR_HEIGHT + 4, 176, 106};
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, 1, (Color){75, 84, 101, 255});
        const char *labels[] = {"Directory List", "Regex Match", "Inspect"};
        for (int i = 0; i < 3; i++) {
            if (GuiButton((Rectangle){18, TOOLBAR_HEIGHT + 10 + i * 31, 164, 27}, labels[i])) {
                Vector2 center =
                    GetScreenToWorld2D((Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f}, graph->camera);
                AddNode(graph, (NodeType)i, center);
                graph->add_menu_open = false;
            }
        }
    }
}

void DrawStatusBar(GraphContext *graph) {
    int y = GetScreenHeight() - (int)STATUS_HEIGHT;
    DrawRectangle(0, y, GetScreenWidth(), (int)STATUS_HEIGHT, (Color){25, 29, 37, 255});
    DrawLine(0, y, GetScreenWidth(), y, (Color){59, 67, 82, 255});
    DrawCircle(14, y + 14, 4, COLOR_STRING_LIST);
    DrawInterfaceText(fonts.body, graph->status, 25, y + 6, BODY_TEXT_SIZE, COLOR_MUTED);
    DrawInterfaceText(fonts.body, "RMB drag: knife   Wheel: zoom   Del: remove", GetScreenWidth() - 360, y + 6,
                      BODY_TEXT_SIZE, COLOR_MUTED);
}
