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
    float zoom = graph->camera.zoom;
    float radius = 0.08f;

    // Draw fills as plain rects (outline provides the rounding illusion)
    if (!node->collapsed) {
        DrawRectangleRec(bounds, COLOR_NODE);
    }
    Rectangle header = {bounds.x, bounds.y, bounds.width, NODE_HEADER_HEIGHT * zoom};
    DrawRectangleRec(header, COLOR_NODE_HEADER);

    // Single rounded outline drawn last, covers all fill edges cleanly
    float border_w = graph->selected_node_id == node->id ? 2.0f : 1.0f;
    Color border_color = graph->selected_node_id == node->id ? COLOR_NODE_SELECTED : (Color){66, 74, 91, 255};
    DrawRectangleRoundedLinesEx(bounds, radius, 8, border_w, border_color);

    // Header/body separator line
    if (!node->collapsed) {
        DrawLineEx((Vector2){bounds.x, bounds.y + NODE_HEADER_HEIGHT * zoom},
                   (Vector2){bounds.x + bounds.width, bounds.y + NODE_HEADER_HEIGHT * zoom}, 1.0f, border_color);
    }

    if (zoom < NODE_DETAIL_MIN_ZOOM) {
        return;
    }

    // collapse toggle tab — centered on the bottom edge of the node
    float tab_w = 28.0f * zoom;
    float tab_h = 10.0f * zoom;
    Rectangle chevron_btn = {
        bounds.x + (bounds.width - tab_w) * 0.5f,
        bounds.y + bounds.height - tab_h * 0.5f,
        tab_w,
        tab_h,
    };
    bool chevron_hovered = CheckCollisionPointRec(GetMousePosition(), chevron_btn);
    DrawRectangleRounded(chevron_btn, 0.5f, 6, chevron_hovered ? (Color){75, 84, 101, 255} : (Color){55, 62, 78, 255});

    // draw chevron arrow inside the tab
    float cx = chevron_btn.x + chevron_btn.width * 0.5f;
    float cy = chevron_btn.y + chevron_btn.height * 0.5f;
    float aw = 4.0f * zoom;
    float ah = 2.5f * zoom;
    if (node->collapsed) {
        // up arrow (expand)
        DrawTriangle((Vector2){cx - aw, cy + ah * 0.5f}, (Vector2){cx + aw, cy + ah * 0.5f},
                     (Vector2){cx, cy - ah * 0.5f}, COLOR_MUTED);
    } else {
        // down arrow (collapse)
        DrawTriangle((Vector2){cx - aw, cy - ah * 0.5f}, (Vector2){cx + aw, cy - ah * 0.5f},
                     (Vector2){cx, cy + ah * 0.5f}, COLOR_MUTED);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && chevron_hovered) {
        node->collapsed = !node->collapsed;
    }

    // port name chips inside the header
    float chip_y = bounds.y + (NODE_HEADER_HEIGHT * zoom - 16.0f * zoom) * 0.5f;
    float chip_h = 16.0f * zoom;
    float chip_pad = 6.0f * zoom;
    float chip_font = (float)BODY_TEXT_SIZE * zoom * 0.85f;
    float chip_gap = 4.0f * zoom;
    float edge_pad = 14.0f * zoom;

    // input port chips — flush left (starting after the port dot)
    float chip_x = bounds.x + edge_pad;
    for (int i = 0; i < node->input_count; i++) {
        Port *port = FindPort(graph, node->input_port_ids[i]);
        if (!port) {
            continue;
        }
        Color port_color = PortColor(port->data_type);
        float label_w = MeasureTextEx(fonts.body, port->name, chip_font, 0).x;
        float chip_w = label_w + chip_pad * 2;
        Rectangle chip = {chip_x, chip_y, chip_w, chip_h};
        DrawRectangleRounded(chip, 0.5f, 6, (Color){port_color.r, port_color.g, port_color.b, 40});
        DrawRectangleRoundedLinesEx(chip, 0.5f, 6, 1, (Color){port_color.r, port_color.g, port_color.b, 120});
        DrawInterfaceText(fonts.body, port->name, chip.x + chip_pad, chip.y + (chip_h - chip_font) * 0.5f, chip_font,
                          port_color);
        chip_x += chip_w + chip_gap;
    }

    // output port chips — flush right
    float out_right = bounds.x + bounds.width - edge_pad;
    for (int i = node->output_count - 1; i >= 0; i--) {
        Port *port = FindPort(graph, node->output_port_ids[i]);
        if (!port) {
            continue;
        }
        Color port_color = PortColor(port->data_type);
        float label_w = MeasureTextEx(fonts.body, port->name, chip_font, 0).x;
        float chip_w = label_w + chip_pad * 2;
        out_right -= chip_w;
        Rectangle chip = {out_right, chip_y, chip_w, chip_h};
        DrawRectangleRounded(chip, 0.5f, 6, (Color){port_color.r, port_color.g, port_color.b, 40});
        DrawRectangleRoundedLinesEx(chip, 0.5f, 6, 1, (Color){port_color.r, port_color.g, port_color.b, 120});
        DrawInterfaceText(fonts.body, port->name, chip.x + chip_pad, chip.y + (chip_h - chip_font) * 0.5f, chip_font,
                          port_color);
        out_right -= chip_gap;
    }

    // title — centered in the full header width
    float title_w = MeasureTextEx(fonts.title, node->title, TITLE_TEXT_SIZE * zoom, 0).x;
    DrawInterfaceText(fonts.title, node->title, bounds.x + (bounds.width - title_w) * 0.5f, bounds.y + 7 * zoom,
                      TITLE_TEXT_SIZE, COLOR_TEXT);
}

void DrawNodePorts(GraphContext *graph, Node *node) {
    for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT; direction++) {
        int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        for (int i = 0; i < count; i++) {
            Port *port = FindPort(graph, ids[i]);
            Vector2 p = PortScreenPosition(graph, port);
            Color color = PortColor(port->data_type);
            float r = PORT_RADIUS * graph->camera.zoom;
            DrawCircleSector(p, r, 0, 360, 36, color);
        }
    }
}

void DrawNodeContent(GraphContext *graph, Node *node) {
    if (node->collapsed) {
        return;
    }
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = graph->camera.zoom;
    if (zoom < NODE_DETAIL_MIN_ZOOM) {
        DrawInterfaceText(fonts.body, TextFormat("%d items", node->item_count), bounds.x + 12 * zoom,
                          bounds.y + 48 * zoom, BODY_TEXT_SIZE, COLOR_MUTED);
        return;
    }

    if (node->type == NODE_DIRECTORY_LIST || node->type == NODE_STRING_FILTER || node->type == NODE_BASH_EXEC ||
        node->type == NODE_HTTP_REQUEST) {
        const char *label = node->type == NODE_DIRECTORY_LIST  ? "Path"
                            : node->type == NODE_STRING_FILTER ? "Filter"
                            : node->type == NODE_HTTP_REQUEST  ? "URL"
                                                               : "Command";
        float label_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 50.0f : 76.0f;
        float text_box_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 68.0f : 94.0f;
        float count_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 114.0f : 176.0f;
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

        if (node->type == NODE_STRING_FILTER) {
            float btn_y = text_box_y + 36.0f;
            float btn_h = 24.0f;
            float btn_w = 34.0f;
            float gap = 6.0f;
            float start_x = 14.0f;

            Color active_bg = {85, 156, 228, 255};
            Color inactive_bg = {48, 55, 70, 255};
            Color btn_text = COLOR_TEXT;

            struct {
                const char *label;
                bool *flag;
            } buttons[3] = {
                {"Aa", &node->filter_case_sensitive},
                {"W", &node->filter_whole_word},
                {".*", &node->filter_use_regex},
            };

            for (int b = 0; b < 3; b++) {
                Rectangle btn = {
                    bounds.x + (start_x + b * (btn_w + gap)) * zoom,
                    bounds.y + btn_y * zoom,
                    btn_w * zoom,
                    btn_h * zoom,
                };
                Color bg = *buttons[b].flag ? active_bg : inactive_bg;
                DrawRectangleRec(btn, bg);
                DrawRectangleLinesEx(btn, 1, (Color){75, 84, 101, 255});
                float text_w = MeasureTextEx(fonts.body, buttons[b].label, BODY_TEXT_SIZE * zoom, 0).x;
                DrawInterfaceText(fonts.body, buttons[b].label, btn.x + (btn.width - text_w) * 0.5f,
                                  btn.y + (btn.height - BODY_TEXT_SIZE * zoom) * 0.5f, BODY_TEXT_SIZE * zoom, btn_text);

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), btn)) {
                    *buttons[b].flag = !(*buttons[b].flag);
                    node->is_dirty = true;
                    MarkDownstreamDirty(graph, node->id);
                    snprintf(graph->status, sizeof(graph->status), "Filter options changed - run graph to refresh");
                }
            }
        }

        DrawInterfaceText(fonts.body, TextFormat("%d item%s", node->item_count, node->item_count == 1 ? "" : "s"),
                          bounds.x + 14 * zoom, bounds.y + count_y * zoom, BODY_TEXT_SIZE, COLOR_MUTED);
    }
}

bool MouseOverCollapseButton(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!node || graph->camera.zoom < NODE_DETAIL_MIN_ZOOM) {
        return false;
    }
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = graph->camera.zoom;
    float tab_w = 28.0f * zoom;
    float tab_h = 10.0f * zoom;
    Rectangle btn = {
        bounds.x + (bounds.width - tab_w) * 0.5f,
        bounds.y + bounds.height - tab_h * 0.5f,
        tab_w,
        tab_h,
    };
    return CheckCollisionPointRec(mouse, btn);
}

bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!node || node->collapsed || graph->camera.zoom < NODE_DETAIL_MIN_ZOOM) {
        return false;
    }
    Rectangle b = NodeScreenBounds(graph, node);
    float z = graph->camera.zoom;
    float control_y = (node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST) ? 62.0f : 88.0f;
    float control_h = node->type == NODE_STRING_FILTER ? 76.0f : 42.0f;
    return CheckCollisionPointRec(mouse,
                                  (Rectangle){b.x + 10 * z, b.y + control_y * z, b.width - 20 * z, control_h * z});
}

static Rectangle PortInspectorBounds(GraphContext *graph, int port_id) {
    Port *port = FindPort(graph, port_id);
    if (!port) {
        return (Rectangle){0};
    }
    Vector2 p = PortScreenPosition(graph, port);
    float w = 280, h = 220;
    float x = p.x + PORT_RADIUS + 10;
    float y = p.y - h * 0.3f;
    if (x + w > GetScreenWidth()) {
        x = p.x - PORT_RADIUS - 10 - w;
    }
    if (y < TOOLBAR_HEIGHT) {
        y = TOOLBAR_HEIGHT + 4;
    }
    if (y + h > GetScreenHeight() - STATUS_HEIGHT) {
        y = GetScreenHeight() - STATUS_HEIGHT - h - 4;
    }
    return (Rectangle){x, y, w, h};
}

void DrawPortInspector(GraphContext *graph, int port_id, bool pinned) {
    Port *port = FindPort(graph, port_id);
    if (!port) {
        return;
    }
    Node *node = FindNode(graph, port->node_id);
    if (!node) {
        return;
    }

    Rectangle panel = PortInspectorBounds(graph, port_id);
    Color border = pinned ? COLOR_NODE_SELECTED : PortColor(port->data_type);

    DrawRectangleRec(panel, (Color){22, 26, 34, 245});
    DrawRectangleLinesEx(panel, pinned ? 2 : 1, border);

    float px = panel.x + 10, py = panel.y + 8;
    DrawInterfaceText(fonts.body,
                      TextFormat("%s  •  %d item%s", port->name, node->item_count, node->item_count == 1 ? "" : "s"),
                      px, py, BODY_TEXT_SIZE, COLOR_TEXT);

    if (pinned) {
        DrawInterfaceText(fonts.body, "[click port to close]", panel.x + panel.width - 148, py, 11, COLOR_MUTED);
    }

    Rectangle list_bounds = {panel.x + 8, panel.y + 30, panel.width - 16, panel.height - 38};
    char *entries[MAX_ITEMS];
    int count = node->item_count;
    for (int i = 0; i < count; i++) {
        entries[i] = node->items[i];
    }
    GuiListViewEx(list_bounds, entries, count, &graph->inspect_scroll, &graph->inspect_active, NULL);
}

bool MouseOverPortInspector(GraphContext *graph, Vector2 mouse) {
    if (graph->inspected_port_id < 0) {
        return false;
    }
    return CheckCollisionPointRec(mouse, PortInspectorBounds(graph, graph->inspected_port_id));
}

void DrawToolbar(GraphContext *graph) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, GUI_TEXT_SIZE);
    DrawRectangle(0, 0, GetScreenWidth(), (int)TOOLBAR_HEIGHT, (Color){25, 29, 37, 255});
    DrawLine(0, (int)TOOLBAR_HEIGHT - 1, GetScreenWidth(), (int)TOOLBAR_HEIGHT - 1, (Color){59, 67, 82, 255});

    if (GuiButton((Rectangle){12, 10, 116, 32}, "#08# Add node")) {
        graph->add_menu_open = !graph->add_menu_open;
    }
    if (GuiButton((Rectangle){138, 10, 116, 32}, "#131# Run")) {
        RunGraph(graph);
    }
    if (GuiButton((Rectangle){264, 10, 82, 32}, "Reset")) {
        SeedGraph(graph);
    }

    DrawInterfaceText(fonts.body, TextFormat("Zoom  %d%%", (int)(graph->camera.zoom * 100)), GetScreenWidth() - 112, 18,
                      BODY_TEXT_SIZE, COLOR_MUTED);

    if (graph->add_menu_open) {
        const char *labels[] = {"Directory List", "Filter", "Bash Exec", "HTTP Request"};
        int label_count = 4;
        Rectangle menu = {12, TOOLBAR_HEIGHT + 4, 176, 10 + label_count * 31};
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, 1, (Color){75, 84, 101, 255});
        for (int i = 0; i < label_count; i++) {
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
    DrawInterfaceText(fonts.body, "RMB drag: knife   Ctrl+Wheel: zoom   Del: remove", GetScreenWidth() - 390, y + 6,
                      BODY_TEXT_SIZE, COLOR_MUTED);
}
