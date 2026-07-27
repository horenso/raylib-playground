#include "render.h"
#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "serialize.h"

#include "raygui.h"
#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void DrawCanvasGrid(GraphContext *graph) {
    float toolbar_height = ToolbarHeight(graph);
    float status_height = StatusHeight(graph);
    Camera2D camera = CanvasCamera(graph);
    Rectangle canvas = {0, toolbar_height, (float)GetScreenWidth(),
                        (float)GetScreenHeight() - toolbar_height - status_height};
    DrawRectangleRec(canvas, COLOR_CANVAS);
    Vector2 top_left = GetScreenToWorld2D((Vector2){canvas.x, canvas.y}, camera);
    Vector2 bottom_right = GetScreenToWorld2D((Vector2){canvas.width, canvas.y + canvas.height}, camera);
    const float step = 32.0f;
    int first_x = (int)(top_left.x / step) - 1;
    int last_x = (int)(bottom_right.x / step) + 1;
    int first_y = (int)(top_left.y / step) - 1;
    int last_y = (int)(bottom_right.y / step) + 1;

    for (int i = first_x; i <= last_x; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){i * step, 0}, camera);
        DrawLine((int)p.x, (int)canvas.y, (int)p.x, (int)(canvas.y + canvas.height),
                 i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
    for (int i = first_y; i <= last_y; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){0, i * step}, camera);
        DrawLine((int)canvas.x, (int)p.y, (int)(canvas.x + canvas.width), (int)p.y,
                 i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
}

void DrawConnection(Vector2 from, Vector2 to, Color color, float thickness) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, 55, 180);
    Vector2 points[4] = {from, {from.x + tangent, from.y}, {to.x - tangent, to.y}, to};
    DrawSplineBezierCubic(points, 4, thickness, color);
}

void DrawKnife(Vector2 start, Vector2 end, float scale) {
    Color glow = {255, 91, 105, 70};
    Color blade = {255, 220, 224, 255};
    DrawLineEx(start, end, 7 * scale, glow);
    DrawLineEx(start, end, 2 * scale, blade);
    DrawCircleV(start, 4 * scale, blade);
    DrawCircleV(end, 4 * scale, blade);
}

static Rectangle NodeRunButtonBounds(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = CanvasZoom(graph);
    float title_font_size = ScaledFontSize(TITLE_TEXT_SIZE, zoom);
    float title_width = MeasureTextEx(fonts.title, node->title, title_font_size, 0).x;
    float button_size = 18.0f * zoom;
    float gap = 6.0f * zoom;
    float group_width = title_width + gap + button_size;
    float title_x = bounds.x + (bounds.width - group_width) * 0.5f;
    return (Rectangle){
        title_x + title_width + gap,
        bounds.y + (NODE_HEADER_HEIGHT * zoom - button_size) * 0.5f,
        button_size,
        button_size,
    };
}

static bool NodeOwnsMouse(GraphContext *graph, Node *node) {
    return graph->interaction_mode == INTERACTION_IDLE && NodeAtMouse(graph, GetMousePosition()) == node->id;
}

void DrawNodeShell(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = CanvasZoom(graph);

    if (!node->collapsed) {
        DrawRectangleRec(bounds, COLOR_NODE);
    }
    Rectangle header = {bounds.x, bounds.y, bounds.width, NODE_HEADER_HEIGHT * zoom};
    DrawRectangleRec(header, COLOR_NODE_HEADER);

    bool knife_hit = graph->knife_active && NodeIntersectsKnife(graph, node, graph->knife_start, GetMousePosition());
    float border_w = knife_hit || graph->selected_node_id == node->id ? 2.0f : 1.0f;
    Color border_color = knife_hit                             ? (Color){255, 76, 92, 255}
                         : graph->selected_node_id == node->id ? COLOR_NODE_SELECTED
                         : node->evaluation_failed             ? (Color){235, 87, 87, 255}
                         : node->is_dirty                      ? COLOR_STRING
                                                               : (Color){66, 74, 91, 255};
    DrawRectangleLinesEx(bounds, border_w, border_color);

    // Header/body separator line
    if (!node->collapsed) {
        DrawLineEx((Vector2){bounds.x, bounds.y + NODE_HEADER_HEIGHT * zoom},
                   (Vector2){bounds.x + bounds.width, bounds.y + NODE_HEADER_HEIGHT * zoom}, 1.0f, border_color);
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
    bool chevron_hovered =
        graph->interaction_mode == INTERACTION_IDLE && CheckCollisionPointRec(GetMousePosition(), chevron_btn);
    DrawRectangleRec(chevron_btn, chevron_hovered ? (Color){75, 84, 101, 255} : (Color){55, 62, 78, 255});

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
    float chip_font = ScaledFontSize(BODY_TEXT_SIZE * 0.85f, zoom);
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
        float label_w = MeasureTextEx(fonts.node_small, port->name, chip_font, 0).x;
        float chip_w = label_w + chip_pad * 2;
        Rectangle chip = {chip_x, chip_y, chip_w, chip_h};
        DrawRectangleRec(chip, (Color){port_color.r, port_color.g, port_color.b, 40});
        DrawRectangleLinesEx(chip, 1, (Color){port_color.r, port_color.g, port_color.b, 120});
        DrawInterfaceText(fonts.node_small, port->name, chip.x + chip_pad, chip.y + (chip_h - chip_font) * 0.5f,
                          chip_font, port_color);
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
        float label_w = MeasureTextEx(fonts.node_small, port->name, chip_font, 0).x;
        float chip_w = label_w + chip_pad * 2;
        out_right -= chip_w;
        Rectangle chip = {out_right, chip_y, chip_w, chip_h};
        DrawRectangleRec(chip, (Color){port_color.r, port_color.g, port_color.b, 40});
        DrawRectangleLinesEx(chip, 1, (Color){port_color.r, port_color.g, port_color.b, 120});
        DrawInterfaceText(fonts.node_small, port->name, chip.x + chip_pad, chip.y + (chip_h - chip_font) * 0.5f,
                          chip_font, port_color);
        out_right -= chip_gap;
    }

    // Title and branch-run button are centered as a group.
    float title_font_size = ScaledFontSize(TITLE_TEXT_SIZE, zoom);
    float title_w = MeasureTextEx(fonts.title, node->title, title_font_size, 0).x;
    Rectangle run_btn = NodeRunButtonBounds(graph, node);
    float title_x = run_btn.x - 6.0f * zoom - title_w;
    DrawInterfaceText(fonts.title, node->title, title_x, bounds.y + 7 * zoom, title_font_size, COLOR_TEXT);

    bool run_hovered = NodeOwnsMouse(graph, node) && CheckCollisionPointRec(GetMousePosition(), run_btn);
    Color run_color = node->evaluation_failed ? (Color){235, 87, 87, 255}
                      : node->is_dirty        ? COLOR_STRING
                                              : COLOR_STRING_LIST;
    Color run_background = run_hovered ? (Color){75, 84, 101, 255} : (Color){38, 44, 56, 255};
    DrawRectangleRec(run_btn, run_background);
    DrawRectangleLinesEx(run_btn, zoom, run_color);
    float play_pad = 5.0f * zoom;
    DrawTriangle((Vector2){run_btn.x + play_pad, run_btn.y + play_pad},
                 (Vector2){run_btn.x + play_pad, run_btn.y + run_btn.height - play_pad},
                 (Vector2){run_btn.x + run_btn.width - play_pad, run_btn.y + run_btn.height * 0.5f}, run_color);

    if (run_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        RunNode(graph, node->id);
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
            float r = PORT_RADIUS * CanvasZoom(graph);
            DrawCircleSector(p, r, 0, 360, 36, color);
        }
    }
}

void DrawNodeContent(GraphContext *graph, Node *node) {
    if (node->collapsed) {
        return;
    }
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = CanvasZoom(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, zoom);
    Port *output = NodeOutputPort(graph, node, 0);

    if (node->type == NODE_DIRECTORY_LIST || node->type == NODE_STRING_FILTER || node->type == NODE_EXEC ||
        node->type == NODE_HTTP_REQUEST) {
        float text_box_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 50.0f : 76.0f;
        float count_y = node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST ? 114.0f
                        : node->type == NODE_EXEC                                            ? 150.0f
                                                                                             : 176.0f;
        Rectangle text_box = {bounds.x + 14 * zoom, bounds.y + text_box_y * zoom, bounds.width - 28 * zoom, 30 * zoom};
        SetNodeGuiScale(zoom);
        char before[128];
        TextCopy(before, node->parameter);
        bool gui_was_locked = GuiIsLocked();
        bool covered_click =
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && NodeAtMouse(graph, GetMousePosition()) != node->id;
        if (covered_click && !gui_was_locked) {
            GuiLock();
        }
        if (GuiTextBox(text_box, node->parameter, sizeof(node->parameter), node->text_editing)) {
            node->text_editing = !node->text_editing;
        }
        if (covered_click && !gui_was_locked) {
            GuiUnlock();
        }
        if (strcmp(before, node->parameter) != 0) {
            MarkNodeDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
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
                DrawRectangleLinesEx(btn, zoom, (Color){75, 84, 101, 255});
                float text_w = MeasureTextEx(fonts.node_body, buttons[b].label, body_font_size, 0).x;
                DrawInterfaceText(fonts.node_body, buttons[b].label, btn.x + (btn.width - text_w) * 0.5f,
                                  btn.y + (btn.height - body_font_size) * 0.5f, body_font_size, btn_text);

                if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(GetMousePosition(), btn)) {
                    *buttons[b].flag = !(*buttons[b].flag);
                    MarkNodeDirty(graph, node->id);
                    snprintf(graph->status, sizeof(graph->status), "Filter and downstream nodes are dirty");
                }
            }

            const char *mode_label = node->filter_exclude ? "Exclude" : "Include";
            Rectangle mode_btn = {
                bounds.x + (start_x + 3 * (btn_w + gap)) * zoom,
                bounds.y + btn_y * zoom,
                96.0f * zoom,
                btn_h * zoom,
            };
            Color mode_bg = node->filter_exclude ? (Color){190, 82, 92, 255} : active_bg;
            DrawRectangleRec(mode_btn, mode_bg);
            DrawRectangleLinesEx(mode_btn, zoom, (Color){75, 84, 101, 255});
            float mode_text_w = MeasureTextEx(fonts.node_body, mode_label, body_font_size, 0).x;
            DrawInterfaceText(fonts.node_body, mode_label, mode_btn.x + (mode_btn.width - mode_text_w) * 0.5f,
                              mode_btn.y + (mode_btn.height - body_font_size) * 0.5f, body_font_size, btn_text);

            if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), mode_btn)) {
                node->filter_exclude = !node->filter_exclude;
                MarkNodeDirty(graph, node->id);
                snprintf(graph->status, sizeof(graph->status), "Filter mode changed to %s - branch is dirty",
                         node->filter_exclude ? "exclude" : "include");
            }
        }

        const char *state_label = node->evaluation_failed                 ? "FAILED"
                                  : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
                                  : node->is_dirty                        ? "NOT RUN"
                                                                          : "CURRENT";
        Color state_color = node->evaluation_failed ? (Color){235, 87, 87, 255}
                            : node->is_dirty        ? COLOR_STRING
                                                    : COLOR_STRING_LIST;
        if (node->type == NODE_EXEC) {
            Port *errors = NodeOutputPort(graph, node, 1);
            DrawInterfaceText(fonts.node_body,
                              TextFormat("%s | %d stdout | %d stderr", state_label, output ? output->item_count : 0,
                                         errors ? errors->item_count : 0),
                              bounds.x + 14 * zoom, bounds.y + count_y * zoom, body_font_size, state_color);
        } else {
            int count = output ? output->item_count : 0;
            DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", state_label, count, count == 1 ? "" : "s"),
                              bounds.x + 14 * zoom, bounds.y + count_y * zoom, body_font_size, state_color);
        }
    }
}

void DrawNode(GraphContext *graph, Node *node) {
    DrawNodeShell(graph, node);
    DrawNodeContent(graph, node);
    DrawNodePorts(graph, node);
}

bool MouseOverCollapseButton(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!node) {
        return false;
    }
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = CanvasZoom(graph);
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
    if (!node) {
        return false;
    }
    if (CheckCollisionPointRec(mouse, NodeRunButtonBounds(graph, node))) {
        return true;
    }
    if (node->collapsed) {
        return false;
    }
    Rectangle b = NodeScreenBounds(graph, node);
    float z = CanvasZoom(graph);
    float control_y = (node->type == NODE_DIRECTORY_LIST || node->type == NODE_HTTP_REQUEST) ? 44.0f : 70.0f;
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
    float scale = ApplicationScale(graph);
    float w = 280 * scale, h = 220 * scale;
    float x = p.x + (PORT_RADIUS + 10) * scale;
    float y = p.y - h * 0.3f;
    if (x + w > GetScreenWidth()) {
        x = p.x - (PORT_RADIUS + 10) * scale - w;
    }
    if (y < ToolbarHeight(graph)) {
        y = ToolbarHeight(graph) + 4 * scale;
    }
    if (y + h > GetScreenHeight() - StatusHeight(graph)) {
        y = GetScreenHeight() - StatusHeight(graph) - h - 4 * scale;
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
    float scale = ApplicationScale(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, scale);

    DrawRectangleRec(panel, (Color){22, 26, 34, 245});
    DrawRectangleLinesEx(panel, (pinned ? 2 : 1) * scale, border);

    float px = panel.x + 10 * scale, py = panel.y + 8 * scale;
    DrawInterfaceText(fonts.body,
                      TextFormat("%s  |  %d item%s", port->name, port->item_count, port->item_count == 1 ? "" : "s"),
                      px, py, body_font_size, COLOR_TEXT);

    Rectangle list_bounds = {panel.x + 8 * scale, panel.y + 30 * scale, panel.width - 16 * scale,
                             panel.height - 38 * scale};
    char *entries[MAX_ITEMS];
    int count = port->item_count;
    for (int i = 0; i < count; i++) {
        entries[i] = port->items[i];
    }
    SetGuiScale(scale);
    GuiListViewEx(list_bounds, entries, count, &graph->inspect_scroll, &graph->inspect_active, NULL);
}

bool MouseOverPortInspector(GraphContext *graph, Vector2 mouse) {
    if (graph->inspected_port_id < 0) {
        return false;
    }
    return CheckCollisionPointRec(mouse, PortInspectorBounds(graph, graph->inspected_port_id));
}

void DrawToolbar(GraphContext *graph) {
    float scale = ApplicationScale(graph);
    float toolbar_height = ToolbarHeight(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, scale);
    SetGuiScale(scale);
    DrawRectangle(0, 0, GetScreenWidth(), (int)toolbar_height, (Color){25, 29, 37, 255});
    DrawLine(0, (int)toolbar_height - 1, GetScreenWidth(), (int)toolbar_height - 1, (Color){59, 67, 82, 255});

    if (GuiButton((Rectangle){12 * scale, 10 * scale, 132 * scale, 32 * scale}, "#08# Add node")) {
        graph->add_menu_open = !graph->add_menu_open;
        graph->open_dialog_open = false;
    }
    if (GuiButton((Rectangle){154 * scale, 10 * scale, 100 * scale, 32 * scale}, "#131# Run all")) {
        RunGraph(graph);
    }
    if (GuiButton((Rectangle){264 * scale, 10 * scale, 100 * scale, 32 * scale}, "#01# Open")) {
        graph->open_dialog_open = !graph->open_dialog_open;
        graph->add_menu_open = false;
    }
    if (GuiButton((Rectangle){374 * scale, 10 * scale, 100 * scale, 32 * scale}, "#02# Save")) {
        if (graph->current_file[0]) {
            if (SaveGraph(graph, graph->current_file)) {
                snprintf(graph->status, sizeof(graph->status), "Saved: %.140s", graph->current_file);
            } else {
                snprintf(graph->status, sizeof(graph->status), "Save failed: %.133s", graph->current_file);
            }
        } else {
            graph->open_dialog_open = true;
            graph->add_menu_open = false;
            TextCopy(graph->status, "No file open - use Open to pick a file path first");
        }
    }

    // Ctrl+S shortcut
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
        if (graph->current_file[0]) {
            if (SaveGraph(graph, graph->current_file)) {
                snprintf(graph->status, sizeof(graph->status), "Saved: %.140s", graph->current_file);
            } else {
                snprintf(graph->status, sizeof(graph->status), "Save failed: %.133s", graph->current_file);
            }
        } else {
            graph->open_dialog_open = true;
            graph->add_menu_open = false;
        }
    }

    const char *fname = graph->current_file[0] ? graph->current_file : "(unsaved)";
    float right_text_y = 18 * scale;
    float ui_label_width = 72 * scale;
    float node_label_width = 92 * scale;
    float right_labels_x = GetScreenWidth() - ui_label_width - node_label_width;
    float fname_x = 492 * scale;
    float fname_width = MeasureTextEx(fonts.body, fname, body_font_size, 0).x;
    if (fname_x + fname_width + 12 * scale < right_labels_x) {
        DrawInterfaceText(fonts.body, fname, fname_x, right_text_y, body_font_size, COLOR_MUTED);
    }
    DrawInterfaceText(fonts.body, TextFormat("Node %d%%", (int)(graph->camera.zoom * 100 + 0.5f)), right_labels_x,
                      right_text_y, body_font_size, COLOR_MUTED);
    DrawInterfaceText(fonts.body, TextFormat("UI %d%%", (int)(scale * 100 + 0.5f)), GetScreenWidth() - ui_label_width,
                      right_text_y, body_font_size, COLOR_MUTED);

    if (graph->add_menu_open) {
        const char *labels[] = {"Files", "Filter", "Exec", "HTTP Request"};
        int label_count = 4;
        Rectangle menu = {12 * scale, toolbar_height + 4 * scale, 176 * scale, (10 + label_count * 31) * scale};
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, scale, (Color){75, 84, 101, 255});
        for (int i = 0; i < label_count; i++) {
            if (GuiButton((Rectangle){18 * scale, toolbar_height + (10 + i * 31) * scale, 164 * scale, 27 * scale},
                          labels[i])) {
                Vector2 center = GetScreenToWorld2D((Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f},
                                                    CanvasCamera(graph));
                AddNode(graph, (NodeType)i, center);
                graph->add_menu_open = false;
            }
        }
    }

    if (graph->open_dialog_open) {
        Rectangle dialog = {12 * scale, toolbar_height + 4 * scale, 400 * scale, 52 * scale};
        DrawRectangleRec(dialog, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(dialog, scale, (Color){75, 84, 101, 255});
        DrawInterfaceText(fonts.body, "File path:", 22 * scale, toolbar_height + 12 * scale, body_font_size,
                          COLOR_MUTED);
        Rectangle input = {100 * scale, toolbar_height + 8 * scale, 220 * scale, 28 * scale};
        static bool editing = false;
        if (GuiTextBox(input, graph->open_dialog_path, sizeof(graph->open_dialog_path), editing)) {
            editing = !editing;
        }
        if (GuiButton((Rectangle){328 * scale, toolbar_height + 8 * scale, 76 * scale, 28 * scale}, "Load")) {
            if (LoadGraph(graph, graph->open_dialog_path)) {
                TextCopy(graph->current_file, graph->open_dialog_path);
                snprintf(graph->status, sizeof(graph->status), "Loaded: %.140s", graph->current_file);
            } else {
                snprintf(graph->status, sizeof(graph->status), "Failed to load: %.130s", graph->open_dialog_path);
            }
            graph->open_dialog_open = false;
            editing = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            graph->open_dialog_open = false;
            editing = false;
        }
        if (IsKeyPressed(KEY_ENTER) && editing) {
            if (LoadGraph(graph, graph->open_dialog_path)) {
                TextCopy(graph->current_file, graph->open_dialog_path);
                snprintf(graph->status, sizeof(graph->status), "Loaded: %.140s", graph->current_file);
            } else {
                snprintf(graph->status, sizeof(graph->status), "Failed to load: %.130s", graph->open_dialog_path);
            }
            graph->open_dialog_open = false;
            editing = false;
        }
    }
}

void DrawStatusBar(GraphContext *graph) {
    float scale = ApplicationScale(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, scale);
    int height = (int)StatusHeight(graph);
    int y = GetScreenHeight() - height;
    DrawRectangle(0, y, GetScreenWidth(), height, (Color){25, 29, 37, 255});
    DrawLine(0, y, GetScreenWidth(), y, (Color){59, 67, 82, 255});
    DrawCircle((int)(14 * scale), (int)(y + 14 * scale), 4 * scale, COLOR_STRING_LIST);
    DrawInterfaceText(fonts.body, graph->status, 25 * scale, y + 6 * scale, body_font_size, COLOR_MUTED);
    const char *help = "RMB drag: knife   Ctrl+Wheel: node zoom   Ctrl+/-/0: UI scale   Del: remove";
    float help_width = MeasureTextEx(fonts.body, help, body_font_size, 0).x;
    float help_x = GetScreenWidth() - help_width - 12 * scale;
    float status_width = MeasureTextEx(fonts.body, graph->status, body_font_size, 0).x;
    if (help_x > 25 * scale + status_width + 20 * scale) {
        DrawInterfaceText(fonts.body, help, help_x, y + 6 * scale, body_font_size, COLOR_MUTED);
    }
}
