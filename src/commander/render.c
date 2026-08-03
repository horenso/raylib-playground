#include "render.h"
#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "serialize.h"
#include "streams.h"

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
        DrawLineEx((Vector2){p.x, canvas.y}, (Vector2){p.x, canvas.y + canvas.height}, UiSize(graph, 1.0f),
                   i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
    for (int i = first_y; i <= last_y; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){0, i * step}, camera);
        DrawLineEx((Vector2){canvas.x, p.y}, (Vector2){canvas.x + canvas.width, p.y}, UiSize(graph, 1.0f),
                   i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
}

void DrawConnection(GraphContext *graph, Vector2 from, Vector2 to, Color color, float thickness_units) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, UiSize(graph, 55.0f), UiSize(graph, 180.0f));
    Vector2 points[4] = {from, {from.x + tangent, from.y}, {to.x - tangent, to.y}, to};
    DrawSplineBezierCubic(points, 4, UiSize(graph, thickness_units), color);
}

void DrawKnife(GraphContext *graph, Vector2 start, Vector2 end) {
    Color glow = {255, 91, 105, 70};
    Color blade = {255, 220, 224, 255};
    DrawLineEx(start, end, UiSize(graph, 7.0f), glow);
    DrawLineEx(start, end, UiSize(graph, 2.0f), blade);
    DrawCircleV(start, UiSize(graph, 4.0f), blade);
    DrawCircleV(end, UiSize(graph, 4.0f), blade);
}

static Rectangle NodeRunButtonBounds(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float button_size = CanvasSize(graph, 22.0f);
    return (Rectangle){
        bounds.x + bounds.width - CanvasSize(graph, 12.0f) - button_size,
        bounds.y + (CanvasSize(graph, NODE_HEADER_HEIGHT) - button_size) * 0.5f,
        button_size,
        button_size,
    };
}

static bool NodeOwnsMouse(GraphContext *graph, Node *node) {
    return graph->interaction_mode == INTERACTION_IDLE && !node->field_dropdown_open && !node->unit_dropdown_open &&
           NodeAtMouse(graph, GetMousePosition()) == node->id;
}

static Rectangle PortChipBounds(GraphContext *graph, Node *node, Port *port) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float chip_h = CanvasSize(graph, 16.0f);
    float chip_pad = CanvasSize(graph, 6.0f);
    float chip_font = ScaledFontSize(BODY_TEXT_SIZE * 0.85f, CanvasUnit(graph));
    float edge_pad = CanvasSize(graph, 14.0f);
    float label_w = MeasureTextEx(fonts.node_small, port->name, chip_font, 0).x;
    float chip_w = label_w + chip_pad * 2;
    Vector2 port_pos = PortScreenPosition(graph, port);
    float chip_x = port->direction == PORT_DIR_INPUT ? bounds.x + edge_pad
                                                      : bounds.x + bounds.width - edge_pad - chip_w;
    return (Rectangle){chip_x, port_pos.y - chip_h * 0.5f, chip_w, chip_h};
}

void DrawNodeShell(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);

    DrawRectangleRec(bounds, COLOR_NODE);
    Rectangle header = {bounds.x, bounds.y, bounds.width, CanvasSize(graph, NODE_HEADER_HEIGHT)};
    DrawRectangleRec(header, COLOR_NODE_HEADER);

    bool knife_hit = graph->knife_active && NodeIntersectsKnife(graph, node, graph->knife_start, GetMousePosition());
    float border_w = CanvasSize(graph, knife_hit || graph->selected_node_id == node->id ? 2.0f : 1.0f);
    Color border_color = knife_hit                             ? (Color){255, 76, 92, 255}
                         : graph->selected_node_id == node->id ? COLOR_NODE_SELECTED
                                                               : NodeStateColor(node);
    // Header/options separator and the dedicated connector/status section.
    DrawLineEx((Vector2){bounds.x, bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT)},
               (Vector2){bounds.x + bounds.width, bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT)}, unit,
               border_color);
    float connector_y = bounds.y + bounds.height - CanvasSize(graph, NodeConnectorSectionHeight(node));
    DrawRectangleRec((Rectangle){bounds.x, connector_y, bounds.width, bounds.y + bounds.height - connector_y},
                     (Color){29, 34, 44, 255});
    DrawLineEx((Vector2){bounds.x, connector_y}, (Vector2){bounds.x + bounds.width, connector_y}, unit, border_color);
    DrawRectangleLinesEx(bounds, border_w, border_color);

    // Port labels live beside their connection points in the bottom section.
    float chip_h = CanvasSize(graph, 16.0f);
    float chip_pad = CanvasSize(graph, 6.0f);
    float chip_font = ScaledFontSize(BODY_TEXT_SIZE * 0.85f, unit);

    for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT; direction++) {
        int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        for (int i = 0; i < count; i++) {
            Port *port = FindPort(graph, ids[i]);
            if (!port) {
                continue;
            }
            Color port_color = PortStateColor(graph, port);
            Rectangle chip = PortChipBounds(graph, node, port);
            DrawRectangleRec(chip, (Color){port_color.r, port_color.g, port_color.b, 40});
            DrawRectangleLinesEx(chip, unit, (Color){port_color.r, port_color.g, port_color.b, 120});
            DrawInterfaceText(fonts.node_small, port->name, chip.x + chip_pad,
                              chip.y + (chip_h - chip_font) * 0.5f,
                              chip_font, port_color);
        }
    }

    // The top section is intentionally quiet: title on the left, branch run on the right.
    float title_font_size = ScaledFontSize(TITLE_TEXT_SIZE, unit);
    Rectangle run_btn = NodeRunButtonBounds(graph, node);
    DrawInterfaceText(fonts.title, node->title, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + (CanvasSize(graph, NODE_HEADER_HEIGHT) - title_font_size) * 0.5f, title_font_size,
                      COLOR_TEXT);

    bool run_hovered = NodeOwnsMouse(graph, node) && CheckCollisionPointRec(GetMousePosition(), run_btn);
    Color run_color = NodeStateColor(node);
    Color run_background = run_hovered ? (Color){75, 84, 101, 255} : (Color){38, 44, 56, 255};
    DrawRectangleRec(run_btn, run_background);
    DrawRectangleLinesEx(run_btn, unit, run_color);
    float play_pad = CanvasSize(graph, 6.0f);
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
            Color color = PortStateColor(graph, port);
            float r = CanvasSize(graph, PORT_RADIUS);
            DrawCircleSector(p, r, 0, 360, 36, color);
        }
    }
}

static bool DrawNodeOptionButton(GraphContext *graph, Node *node, Rectangle bounds, const char *label, bool active,
                                 float font_size) {
    Color background = active ? (Color){85, 156, 228, 255} : (Color){48, 55, 70, 255};
    DrawRectangleRec(bounds, background);
    DrawRectangleLinesEx(bounds, CanvasUnit(graph), (Color){75, 84, 101, 255});
    float text_width = MeasureTextEx(fonts.node_body, label, font_size, 0).x;
    DrawInterfaceText(fonts.node_body, label, bounds.x + (bounds.width - text_width) * 0.5f,
                      bounds.y + (bounds.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    return NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), bounds);
}

static float FieldSelectorYUnits(const Node *node) {
    return NODE_HEADER_HEIGHT + (node && node->type == NODE_INSERT ? 10.0f : 12.0f);
}

Rectangle FieldSelectorButtonBounds(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    return (Rectangle){
        bounds.x + CanvasSize(graph, 72.0f),
        bounds.y + CanvasSize(graph, FieldSelectorYUnits(node)),
        bounds.width - CanvasSize(graph, 86.0f),
        CanvasSize(graph, 26.0f),
    };
}

static void DrawFieldSelector(GraphContext *graph, Node *node, const char *label) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float y_units = FieldSelectorYUnits(node);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    DrawInterfaceText(fonts.node_body, label, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + CanvasSize(graph, y_units + 6.0f), font_size, COLOR_MUTED);
    Rectangle button = FieldSelectorButtonBounds(graph, node);
    const char *options[MAX_FIELDS];
    int option_count = CollectNodeFieldOptions(graph, node, options, MAX_FIELDS);
    if (option_count <= 0) {
        DrawInterfaceText(fonts.node_body, "connect input", button.x + CanvasSize(graph, 7.0f),
                          button.y + (button.height - font_size) * 0.5f, font_size, COLOR_MUTED);
        return;
    }

    const char *field = node->field_name[0] ? node->field_name : options[0];
    DrawRectangleRec(button, (Color){48, 55, 70, 255});
    DrawRectangleLinesEx(button, CanvasUnit(graph),
                         node->schema_error ? (Color){235, 87, 87, 255} : (Color){75, 84, 101, 255});
    DrawInterfaceText(fonts.node_body, field, button.x + CanvasSize(graph, 7.0f),
                      button.y + (button.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    int icon_scale = CanvasUnit(graph) >= 1.5f ? 2 : 1;
    int icon_size = 16 * icon_scale;
    int icon_x = (int)(button.x + button.width - CanvasSize(graph, 6.0f) - icon_size);
    int icon_y = (int)(button.y + (button.height - icon_size) * 0.5f);
    GuiDrawIcon(node->field_dropdown_open ? ICON_ARROW_UP_FILL : ICON_ARROW_DOWN_FILL, icon_x, icon_y, icon_scale,
                COLOR_TEXT);
}

static void DrawFieldDropdown(GraphContext *graph, Node *node) {
    Rectangle button = FieldSelectorButtonBounds(graph, node);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    const char *options[MAX_FIELDS];
    int option_count = CollectNodeFieldOptions(graph, node, options, MAX_FIELDS);
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < option_count; i++) {
        Rectangle item = {button.x, button.y + button.height * (i + 1), button.width, button.height};
        bool selected = TextIsEqual(options[i], node->field_name);
        bool hovered = CheckCollisionPointRec(mouse, item);
        Color background = selected  ? (Color){85, 156, 228, 255}
                           : hovered ? (Color){59, 70, 90, 255}
                                     : (Color){38, 44, 56, 255};
        DrawRectangleRec(item, background);
        DrawRectangleLinesEx(item, CanvasUnit(graph), (Color){75, 84, 101, 255});
        DrawInterfaceText(fonts.node_body, options[i], item.x + CanvasSize(graph, 7.0f),
                          item.y + (item.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    }
}

static const char *FileSizeUnitLabel(FileSizeUnit unit) {
    static const char *labels[] = {"B", "KB", "MB", "GB", "TB"};
    return unit >= FILE_SIZE_BYTES && unit <= FILE_SIZE_TB ? labels[unit] : labels[0];
}

Rectangle SizeUnitButtonBounds(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    return (Rectangle){bounds.x + bounds.width - CanvasSize(graph, 78.0f),
                       bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 80.0f), CanvasSize(graph, 64.0f),
                       CanvasSize(graph, 30.0f)};
}

static void DrawSizeUnitSelector(GraphContext *graph, Node *node) {
    Rectangle button = SizeUnitButtonBounds(graph, node);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    DrawRectangleRec(button, (Color){48, 55, 70, 255});
    DrawRectangleLinesEx(button, CanvasUnit(graph), (Color){75, 84, 101, 255});
    DrawInterfaceText(fonts.node_body, FileSizeUnitLabel(node->file_size_unit), button.x + CanvasSize(graph, 7.0f),
                      button.y + (button.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    int icon_scale = CanvasUnit(graph) >= 1.5f ? 2 : 1;
    int icon_size = 16 * icon_scale;
    int icon_x = (int)(button.x + button.width - CanvasSize(graph, 4.0f) - icon_size);
    int icon_y = (int)(button.y + (button.height - icon_size) * 0.5f);
    GuiDrawIcon(node->unit_dropdown_open ? ICON_ARROW_UP_FILL : ICON_ARROW_DOWN_FILL, icon_x, icon_y, icon_scale,
                COLOR_TEXT);
}

static void DrawSizeUnitDropdown(GraphContext *graph, Node *node) {
    Rectangle button = SizeUnitButtonBounds(graph, node);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Vector2 mouse = GetMousePosition();
    for (int i = FILE_SIZE_BYTES; i <= FILE_SIZE_TB; i++) {
        Rectangle item = {button.x, button.y + button.height * (i + 1), button.width, button.height};
        bool selected = node->file_size_unit == (FileSizeUnit)i;
        bool hovered = CheckCollisionPointRec(mouse, item);
        Color background = selected  ? (Color){85, 156, 228, 255}
                           : hovered ? (Color){59, 70, 90, 255}
                                     : (Color){38, 44, 56, 255};
        DrawRectangleRec(item, background);
        DrawRectangleLinesEx(item, CanvasUnit(graph), (Color){75, 84, 101, 255});
        DrawInterfaceText(fonts.node_body, FileSizeUnitLabel((FileSizeUnit)i), item.x + CanvasSize(graph, 7.0f),
                          item.y + (item.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    }
}

static bool DrawNodeTextBox(GraphContext *graph, Node *node, Rectangle bounds, char *text, int capacity,
                            int control_id) {
    char before[128];
    TextCopy(before, text);
    SetNodeGuiScale(CanvasUnit(graph));
    bool active = node->editing_control == control_id;
    bool was_locked = GuiIsLocked();
    if (node->field_dropdown_open || node->unit_dropdown_open) {
        GuiLock();
    }
    bool changed_editing = GuiTextBox(bounds, text, capacity, active);
    if ((node->field_dropdown_open || node->unit_dropdown_open) && !was_locked) {
        GuiUnlock();
    }
    if (changed_editing) {
        bool now_active = !active;
        node->editing_control = now_active ? control_id : -1;
        node->text_editing = now_active;
        if (now_active) {
            CloseNodeEditors(graph, node->id);
        }
    }
    return strcmp(before, text) != 0;
}

void DrawNodeContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    if (node->type == NODE_MATCH && !InputSourcePort(graph, node, 0)) {
        const char *state_label = node->schema_error                      ? "SCHEMA ERROR"
                                  : node->evaluation_failed               ? "FAILED"
                                  : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
                                  : node->is_dirty                        ? "NOT RUN"
                                                                          : "CURRENT";
        int count = output ? output->item_count : 0;
        DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", state_label, count, count == 1 ? "" : "s"),
                          bounds.x + CanvasSize(graph, 14.0f),
                          bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                          body_font_size, NodeStateColor(node));
        return;
    }

    ValueType match_type = node->type == NODE_MATCH ? NodeSelectedFieldType(graph, node) : VALUE_NONE;
    bool text_match = node->type == NODE_MATCH && (match_type == VALUE_NONE || ValueTypeIsText(match_type));

    if (node->type == NODE_DIRECTORY_LIST || text_match || node->type == NODE_EXEC ||
        node->type == NODE_HTTP_REQUEST) {
        float text_box_y = text_match ? NODE_HEADER_HEIGHT + 48.0f : NODE_HEADER_HEIGHT + 16.0f;
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

        if (node->type == NODE_DIRECTORY_LIST) {
            float label_x = bounds.x + CanvasSize(graph, 14.0f);
            float button_x = bounds.x + CanvasSize(graph, 60.0f);
            float type_y = bounds.y + CanvasSize(graph, text_box_y + 38.0f);
            float depth_y = bounds.y + CanvasSize(graph, text_box_y + 68.0f);
            float button_height = CanvasSize(graph, 24.0f);
            float gap = CanvasSize(graph, 5.0f);
            float label_y_offset = CanvasSize(graph, (24.0f - BODY_TEXT_SIZE) * 0.5f);

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
                Rectangle button = {x, type_y, CanvasSize(graph, type_buttons[i].width), button_height};
                if (DrawNodeOptionButton(graph, node, button, type_buttons[i].label,
                                         node->directory_entry_type == type_buttons[i].type, body_font_size) &&
                    node->directory_entry_type != type_buttons[i].type) {
                    node->directory_entry_type = type_buttons[i].type;
                    MarkNodeDirty(graph, node->id);
                    TextCopy(graph->status, "Files type changed - downstream nodes are dirty");
                }
                x += button.width + gap;
            }

            DrawInterfaceText(fonts.node_body, "Depth", label_x, depth_y + label_y_offset, body_font_size, COLOR_MUTED);
            Rectangle one_layer = {button_x, depth_y, CanvasSize(graph, 82.0f), button_height};
            Rectangle recursive = {
                one_layer.x + one_layer.width + gap,
                depth_y,
                CanvasSize(graph, 94.0f),
                button_height,
            };
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
        } else if (text_match) {
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
                    bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                    bounds.y + CanvasSize(graph, btn_y),
                    CanvasSize(graph, btn_w),
                    CanvasSize(graph, btn_h),
                };
                Color bg = *buttons[b].flag ? active_bg : inactive_bg;
                DrawRectangleRec(btn, bg);
                DrawRectangleLinesEx(btn, unit, (Color){75, 84, 101, 255});
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
                bounds.x + CanvasSize(graph, start_x + 3 * (btn_w + gap)),
                bounds.y + CanvasSize(graph, btn_y),
                CanvasSize(graph, 96.0f),
                CanvasSize(graph, btn_h),
            };
            Color mode_bg = node->filter_exclude ? (Color){190, 82, 92, 255} : active_bg;
            DrawRectangleRec(mode_btn, mode_bg);
            DrawRectangleLinesEx(mode_btn, unit, (Color){75, 84, 101, 255});
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

        const char *state_label = node->schema_error                      ? "SCHEMA ERROR"
                                  : node->evaluation_failed               ? "FAILED"
                                  : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
                                  : node->is_dirty                        ? "NOT RUN"
                                                                          : "CURRENT";
        Color state_color = NodeStateColor(node);
        float state_y = bounds.y + bounds.height - CanvasSize(graph, 21.0f);
        if (node->type == NODE_EXEC) {
            Port *errors = NodeOutputPort(graph, node, 1);
            DrawInterfaceText(fonts.node_body,
                              TextFormat("%s | %d stdout | %d stderr", state_label, output ? output->item_count : 0,
                                         errors ? errors->item_count : 0),
                              bounds.x + CanvasSize(graph, 14.0f), state_y, body_font_size, state_color);
        } else {
            int count = output ? output->item_count : 0;
            DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", state_label, count, count == 1 ? "" : "s"),
                              bounds.x + CanvasSize(graph, 14.0f), state_y, body_font_size, state_color);
        }
    } else if (node->type == NODE_MATCH) {
        const char *numeric_op_labels[] = {"=", "!=", "<", "<=", ">", ">="};
        NumberFilterOp numeric_ops[] = {NUMBER_FILTER_EQ, NUMBER_FILTER_NEQ, NUMBER_FILTER_LT,
                                        NUMBER_FILTER_LTE, NUMBER_FILTER_GT, NUMBER_FILTER_GTE};
        const char *datetime_op_labels[] = {"<", ">="};
        NumberFilterOp datetime_ops[] = {NUMBER_FILTER_LT, NUMBER_FILTER_GTE};
        bool datetime_match = match_type == VALUE_DATETIME;
        const char **op_labels = datetime_match ? datetime_op_labels : numeric_op_labels;
        NumberFilterOp *ops = datetime_match ? datetime_ops : numeric_ops;
        int op_count = datetime_match ? 2 : 6;
        float btn_y = NODE_HEADER_HEIGHT + 48.0f;
        float btn_h = 24.0f;
        float btn_w = 33.0f;
        float gap = 4.0f;
        float start_x = 14.0f;
        Color active_bg = {85, 156, 228, 255};
        Color inactive_bg = {48, 55, 70, 255};
        for (int b = 0; b < op_count; b++) {
            Rectangle btn = {
                bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                bounds.y + CanvasSize(graph, btn_y),
                CanvasSize(graph, btn_w),
                CanvasSize(graph, btn_h),
            };
            Color bg = node->number_filter_op == ops[b] ? active_bg : inactive_bg;
            DrawRectangleRec(btn, bg);
            DrawRectangleLinesEx(btn, unit, (Color){75, 84, 101, 255});
            float tw = MeasureTextEx(fonts.node_body, op_labels[b], body_font_size, 0).x;
            DrawInterfaceText(fonts.node_body, op_labels[b], btn.x + (btn.width - tw) * 0.5f,
                              btn.y + (btn.height - body_font_size) * 0.5f, body_font_size, COLOR_TEXT);
            if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), btn) && node->number_filter_op != ops[b]) {
                node->number_filter_op = ops[b];
                MarkNodeDirty(graph, node->id);
            }
        }

        if (datetime_match) {
            DrawInterfaceText(fonts.node_small, "YYYY-MM-DD  HH:MM", bounds.x + CanvasSize(graph, 96.0f),
                              bounds.y + CanvasSize(graph, btn_y + 6.0f),
                              ScaledFontSize(BODY_TEXT_SIZE * 0.78f, unit), COLOR_MUTED);
        }

        bool size_match = match_type == VALUE_SIZE;
        float unit_width = size_match ? 64.0f : 0.0f;
        float unit_gap = size_match ? 6.0f : 0.0f;
        Rectangle text_box = {
            bounds.x + CanvasSize(graph, start_x),
            bounds.y + CanvasSize(graph, btn_y + btn_h + 8.0f),
            bounds.width - CanvasSize(graph, start_x * 2 + unit_width + unit_gap),
            CanvasSize(graph, 30.0f),
        };
        if (DrawNodeTextBox(graph, node, text_box, node->number_parameter, sizeof(node->number_parameter), 0)) {
            MarkNodeDirty(graph, node->id);
        }

        const char *state_label = node->schema_error                      ? "SCHEMA ERROR"
                                  : node->evaluation_failed               ? "FAILED"
                                  : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
                                  : node->is_dirty                        ? "NOT RUN"
                                                                          : "CURRENT";
        int count = output ? output->item_count : 0;
        DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", state_label, count, count == 1 ? "" : "s"),
                          bounds.x + CanvasSize(graph, 14.0f),
                          bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                          body_font_size, NodeStateColor(node));
    } else if (node->type == NODE_INSERT) {
        float x = bounds.x + CanvasSize(graph, 14.0f);
        float width = bounds.width - CanvasSize(graph, 28.0f);
        float output_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 43.0f);
        DrawInterfaceText(fonts.node_small, "New field", x, output_y, ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit),
                          COLOR_MUTED);
        Rectangle output_box = {x + CanvasSize(graph, 72.0f), output_y - CanvasSize(graph, 5.0f),
                                width - CanvasSize(graph, 72.0f), CanvasSize(graph, 27.0f)};
        if (DrawNodeTextBox(graph, node, output_box, node->output_field_name, sizeof(node->output_field_name), 0)) {
            MarkNodeDirty(graph, node->id);
        }

        float find_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 76.0f);
        float replace_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 111.0f);
        DrawInterfaceText(fonts.node_small, "Find", x, find_y + CanvasSize(graph, 7.0f),
                          ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit), COLOR_MUTED);
        DrawInterfaceText(fonts.node_small, "With", x, replace_y + CanvasSize(graph, 7.0f),
                          ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit), COLOR_MUTED);
        Rectangle find_box = {x + CanvasSize(graph, 50.0f), find_y, width - CanvasSize(graph, 50.0f),
                              CanvasSize(graph, 28.0f)};
        Rectangle replace_box = {x + CanvasSize(graph, 50.0f), replace_y, width - CanvasSize(graph, 50.0f),
                                 CanvasSize(graph, 28.0f)};
        bool find_changed = DrawNodeTextBox(graph, node, find_box, node->parameter, sizeof(node->parameter), 1);
        bool replacement_changed =
            DrawNodeTextBox(graph, node, replace_box, node->secondary_parameter, sizeof(node->secondary_parameter), 2);
        if (find_changed || replacement_changed) {
            MarkNodeDirty(graph, node->id);
        }

        const char *operations[] = {"Text", "Filename", "Extension"};
        float operation_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 150.0f);
        for (int i = 0; i < 3; i++) {
            Rectangle button = {x + CanvasSize(graph, i * 88.0f), operation_y, CanvasSize(graph, 82.0f),
                                CanvasSize(graph, 25.0f)};
            if (DrawNodeOptionButton(graph, node, button, operations[i], node->insert_operation == (InsertOperation)i,
                                     body_font_size) &&
                node->insert_operation != (InsertOperation)i) {
                node->insert_operation = (InsertOperation)i;
                MarkNodeDirty(graph, node->id);
            }
        }
        const char *state_label = node->schema_error ? node->schema_error_message
                                  : node->is_dirty   ? "NOT RUN"
                                                     : "CURRENT";
        DrawInterfaceText(fonts.node_small, state_label, x, bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                          ScaledFontSize(BODY_TEXT_SIZE * 0.82f, unit), NodeStateColor(node));
    } else if (node->type == NODE_GET) {
        const char *state_label = node->schema_error ? "SCHEMA ERROR" : node->is_dirty ? "NOT RUN" : "CURRENT";
        DrawInterfaceText(fonts.node_body, state_label, bounds.x + CanvasSize(graph, 14.0f),
                          bounds.y + bounds.height - CanvasSize(graph, 21.0f), body_font_size, NodeStateColor(node));
    }
}

void DrawNode(GraphContext *graph, Node *node) {
    DrawNodeShell(graph, node);
    DrawNodeContent(graph, node);
    DrawNodePorts(graph, node);
    if (NodeUsesFieldSelector(node)) {
        DrawFieldSelector(graph, node, node->type == NODE_INSERT ? "From" : "Field");
    }
    if (node->type == NODE_MATCH && NodeSelectedFieldType(graph, node) == VALUE_SIZE) {
        DrawSizeUnitSelector(graph, node);
    }
}

void DrawNodeOverlay(GraphContext *graph, Node *node) {
    if (node->field_dropdown_open) {
        DrawFieldDropdown(graph, node);
    }
    if (node->unit_dropdown_open) {
        DrawSizeUnitDropdown(graph, node);
    }
}

bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!node) {
        return false;
    }
    if (CheckCollisionPointRec(mouse, NodeRunButtonBounds(graph, node))) {
        return true;
    }
    Rectangle b = NodeScreenBounds(graph, node);
    float control_y = NODE_HEADER_HEIGHT + 10.0f;
    float control_h = node->type == NODE_DIRECTORY_LIST  ? 110.0f
                      : node->type == NODE_MATCH          ? 116.0f
                      : node->type == NODE_INSERT        ? 190.0f
                      : node->type == NODE_GET           ? 42.0f
                                                         : 42.0f;
    return CheckCollisionPointRec(mouse, (Rectangle){
                                             b.x + CanvasSize(graph, 10.0f),
                                             b.y + CanvasSize(graph, control_y),
                                             b.width - CanvasSize(graph, 20.0f),
                                             CanvasSize(graph, control_h),
                                         });
}

static Rectangle InspectorWindowBounds(GraphContext *graph, InspectorWindow *win) {
    Port *port = FindPort(graph, win->port_id);
    float min_w = UiSize(graph, 200.0f);
    float min_h = UiSize(graph, 120.0f);
    float w, h;
    if (win->size.x > 0 && win->size.y > 0) {
        w = win->size.x;
        h = win->size.y;
    } else {
        w = UiSize(graph, port && port->data_type == VALUE_RECORD ? 720.0f : 280.0f);
        h = UiSize(graph, 280.0f);
    }
    w = Clamp(w, min_w, (float)GetScreenWidth() - UiSize(graph, 24.0f));
    h = Clamp(h, min_h, (float)GetScreenHeight() - UiSize(graph, 48.0f));
    return (Rectangle){win->pos.x, win->pos.y, w, h};
}

static void DrawInspectorSpinner(GraphContext *graph, Rectangle area) {
    float cx = area.x + area.width * 0.5f;
    float cy = area.y + area.height * 0.5f;
    float r = UiSize(graph, 14.0f);
    float t = (float)GetTime();
    float start_deg = fmodf(t * 300.0f, 360.0f);
    DrawRing((Vector2){cx, cy}, r - UiSize(graph, 3.0f), r, start_deg, start_deg + 270.0f, 32,
             (Color){COLOR_NODE_SELECTED.r, COLOR_NODE_SELECTED.g, COLOR_NODE_SELECTED.b, 220});
}

// Returns true if the X button was clicked (caller should close the window).
bool DrawInspectorWindow(GraphContext *graph, InspectorWindow *win) {
    Port *port = FindPort(graph, win->port_id);
    if (!port) {
        return false;
    }
    Node *node = FindNode(graph, port->node_id);
    if (!node) {
        return false;
    }

    float unit = UiUnit(graph);
    Rectangle panel = InspectorWindowBounds(graph, win);

    // GuiWindowBox draws the title bar + X button + panel body using the raygui theme.
    // It returns RESULT_PRESSED when the X button is clicked.
    SetGuiScale(unit);
    const char *title = TextFormat("  %s  |  Stream<%s>  |  %d item%s", port->name, ValueTypeName(port->data_type),
                                   port->item_count, port->item_count == 1 ? "" : "s");
    bool close_clicked = (GuiWindowBox(panel, title) != 0);

    // GuiWindowBox title bar is always RAYGUI_WINDOWBOX_STATUSBAR_HEIGHT (24px) tall.
    // The content area starts below that.
    float title_h = 24.0f;
    Rectangle content = {panel.x + unit, panel.y + title_h, panel.width - unit * 2,
                         panel.height - title_h - unit};

    if (node->is_dirty && !node->has_evaluated) {
        float text_size = ScaledFontSize(BODY_TEXT_SIZE, UiUnit(graph));
        const char *msg = "Not evaluated";
        Vector2 sz = MeasureTextEx(fonts.body, msg, text_size, 0);
        DrawInterfaceText(fonts.body, msg, content.x + (content.width - sz.x) * 0.5f,
                          content.y + (content.height - sz.y) * 0.5f, text_size, COLOR_MUTED);
        return close_clicked;
    }
    if (node->is_dirty) {
        DrawInspectorSpinner(graph, content);
        return close_clicked;
    }

    Rectangle list_bounds = {content.x, content.y + UiSize(graph, 4.0f), content.width,
                             content.height - UiSize(graph, 4.0f)};

    if (port->data_type != VALUE_RECORD) {
        char display[MAX_ITEMS][64];
        char *entries[MAX_ITEMS];
        int count = port->item_count;
        for (int i = 0; i < count; i++) {
            char formatted[64];
            TextCopy(display[i], ValueDisplayText(&port->items[i].values[0], formatted, sizeof(formatted)));
            entries[i] = display[i];
        }
        SetCodeGuiScale(unit);
        GuiListViewEx(list_bounds, entries, count, &win->scroll, &win->active, NULL);
        return close_clicked;
    }

    float header_height = UiSize(graph, 26.0f);
    float row_height = UiSize(graph, 24.0f);
    float scrollbar_w = UiSize(graph, 10.0f);
    float scrollbar_gap = UiSize(graph, 3.0f);
    int field_count = port->schema.field_count > 0 ? port->schema.field_count : 1;

    // Reserve space for the scroll bar on the right edge.
    Rectangle table_bounds = {list_bounds.x, list_bounds.y,
                              list_bounds.width - scrollbar_w - scrollbar_gap, list_bounds.height};
    float column_width = table_bounds.width / (float)field_count;

    // Build a sorted index over the items.
    static int sorted_indices[MAX_ITEMS];
    for (int i = 0; i < port->item_count; i++) sorted_indices[i] = i;
    if (win->sort_field >= 0 && win->sort_field < port->schema.field_count) {
        int sf = win->sort_field;
        bool asc = win->sort_asc;
        ValueType vt = port->schema.fields[sf].type;
        // Insertion sort — item counts are small (≤256).
        for (int i = 1; i < port->item_count; i++) {
            int key = sorted_indices[i];
            int j = i - 1;
            StreamValue *kv = &port->items[key].values[sf];
            while (j >= 0) {
                StreamValue *jv = &port->items[sorted_indices[j]].values[sf];
                int cmp = 0;
                if (vt == VALUE_INT || vt == VALUE_SIZE || vt == VALUE_DATETIME) {
                    long long a = jv->as.integer, b = kv->as.integer;
                    cmp = (a > b) - (a < b);
                } else {
                    cmp = strcmp(jv->as.text, kv->as.text);
                }
                if (asc ? (cmp <= 0) : (cmp >= 0)) break;
                sorted_indices[j + 1] = sorted_indices[j];
                j--;
            }
            sorted_indices[j + 1] = key;
        }
    }

    // Header row — clicking a column header toggles sort.
    Rectangle header = {table_bounds.x, table_bounds.y, table_bounds.width, header_height};
    DrawRectangleRec(header, (Color){28, 34, 44, 255});
    // Bottom border under header.
    DrawLineEx((Vector2){header.x, header.y + header_height},
               (Vector2){header.x + header.width, header.y + header_height}, unit, (Color){58, 66, 81, 255});
    Vector2 mouse = GetMousePosition();
    bool left_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    float text_y = header.y + (header_height - fonts.mono_size) * 0.5f;
    for (int column = 0; column < port->schema.field_count; column++) {
        FieldSchema *field = &port->schema.fields[column];
        float column_x = header.x + column * column_width;
        Rectangle col_header = {column_x, header.y, column_width, header_height};
        bool hovered = CheckCollisionPointRec(mouse, col_header);
        if (hovered) {
            DrawRectangleRec(col_header, (Color){38, 46, 58, 255});
            if (left_pressed) {
                if (win->sort_field == column) {
                    win->sort_asc = !win->sort_asc;
                } else {
                    win->sort_field = column;
                    win->sort_asc = true;
                }
            }
        }
        Color name_color = field->derived ? (Color){116, 206, 173, 255} : (Color){200, 208, 220, 255};
        DrawInterfaceText(fonts.mono, field->name, column_x + UiSize(graph, 8.0f), text_y, fonts.mono_size,
                          name_color);
        // Sort indicator — use the same raygui arrow icons as field dropdowns.
        if (win->sort_field == column) {
            float name_w = MeasureTextEx(fonts.mono, field->name, fonts.mono_size, 0).x;
            int icon_scale = UiUnit(graph) >= 1.5f ? 2 : 1;
            int icon_size = 16 * icon_scale;
            int icon_x = (int)(column_x + UiSize(graph, 12.0f) + name_w);
            int icon_y = (int)(header.y + (header.height - icon_size) * 0.5f);
            GuiDrawIcon(win->sort_asc ? ICON_ARROW_UP_FILL : ICON_ARROW_DOWN_FILL, icon_x, icon_y, icon_scale,
                        (Color){130, 160, 200, 255});
        }
        if (column > 0) {
            DrawLineEx((Vector2){column_x, header.y}, (Vector2){column_x, table_bounds.y + table_bounds.height}, unit,
                       (Color){45, 52, 65, 255});
        }
    }

    int visible_rows = (int)((table_bounds.height - header_height) / row_height);
    int max_scroll = port->item_count > visible_rows ? port->item_count - visible_rows : 0;

    // Scrollbar geometry (needed for drag hit-test before drawing).
    float track_x = table_bounds.x + table_bounds.width + scrollbar_gap;
    float track_y = table_bounds.y;
    float track_h = table_bounds.height;
    float thumb_h = max_scroll > 0
                        ? Clamp(track_h * ((float)visible_rows / (float)port->item_count), UiSize(graph, 16.0f), track_h)
                        : track_h;
    float thumb_t = max_scroll > 0 ? (float)win->scroll / (float)max_scroll : 0.0f;
    float thumb_y = track_y + thumb_t * (track_h - thumb_h);
    Rectangle thumb_rect = {track_x + unit, thumb_y + unit, scrollbar_w - unit * 2, thumb_h - unit * 2};

    // Scrollbar drag: start on press over the thumb.
    if (left_pressed && max_scroll > 0 && CheckCollisionPointRec(mouse, thumb_rect)) {
        win->scrollbar_dragging = true;
        win->scrollbar_drag_start_y = mouse.y;
        win->scrollbar_drag_start_scroll = win->scroll;
    }
    if (win->scrollbar_dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dy = mouse.y - win->scrollbar_drag_start_y;
            float scroll_range = track_h - thumb_h;
            if (scroll_range > 0) {
                int delta = (int)(dy / scroll_range * (float)max_scroll + 0.5f);
                win->scroll = win->scrollbar_drag_start_scroll + delta;
            }
        } else {
            win->scrollbar_dragging = false;
        }
    }

    // Click on track (outside thumb) jumps scroll by a page.
    Rectangle track_rect = {track_x, track_y, scrollbar_w, track_h};
    if (left_pressed && max_scroll > 0 && CheckCollisionPointRec(mouse, track_rect) &&
        !CheckCollisionPointRec(mouse, thumb_rect)) {
        if (mouse.y < thumb_y) {
            win->scroll -= visible_rows;
        } else {
            win->scroll += visible_rows;
        }
    }

    // Wheel scroll when the mouse is over the panel.
    if (CheckCollisionPointRec(mouse, panel) && !win->scrollbar_dragging) {
        win->scroll -= (int)GetMouseWheelMove();
    }
    win->scroll = (int)Clamp((float)win->scroll, 0.0f, (float)max_scroll);

    // Recompute thumb position after scroll update.
    thumb_t = max_scroll > 0 ? (float)win->scroll / (float)max_scroll : 0.0f;
    thumb_y = track_y + thumb_t * (track_h - thumb_h);

    Rectangle rows_area = {table_bounds.x, table_bounds.y + header_height,
                           table_bounds.width, table_bounds.height - header_height};
    float cell_pad = UiSize(graph, 8.0f);
    // Track hovered cell for tooltip.
    const char *tooltip_text = NULL;
    Rectangle tooltip_anchor = {0};
    for (int visible = 0; visible < visible_rows; visible++) {
        int row_idx = visible + win->scroll;
        if (row_idx >= port->item_count) {
            break;
        }
        int row = sorted_indices[row_idx];
        float row_y = rows_area.y + visible * row_height;
        Color row_bg = (visible % 2) ? (Color){28, 34, 44, 255} : (Color){33, 39, 50, 255};
        DrawRectangleRec((Rectangle){rows_area.x, row_y, rows_area.width, row_height}, row_bg);
        float cell_text_y = row_y + (row_height - fonts.mono_size) * 0.5f;
        for (int column = 0; column < port->schema.field_count; column++) {
            float cell_x = rows_area.x + column * column_width;
            float cell_w = column_width;
            // Clip each cell individually so text doesn't bleed into the next column.
            BeginScissorMode((int)(cell_x + cell_pad * 0.5f), (int)row_y,
                             (int)(cell_w - cell_pad), (int)row_height);
            char formatted[64];
            const char *text = ValueDisplayText(&port->items[row].values[column], formatted, sizeof(formatted));
            Color text_color = port->schema.fields[column].derived ? (Color){145, 218, 191, 255} : COLOR_TEXT;
            DrawInterfaceText(fonts.mono, text, cell_x + cell_pad, cell_text_y, fonts.mono_size, text_color);
            EndScissorMode();
            // Show tooltip when text is truncated and the mouse hovers over this cell.
            float text_w = MeasureTextEx(fonts.mono, text, fonts.mono_size, 0).x;
            Rectangle cell_rect = {cell_x, row_y, cell_w, row_height};
            if (text_w > cell_w - cell_pad * 2 && CheckCollisionPointRec(mouse, cell_rect)) {
                tooltip_text = text;
                tooltip_anchor = cell_rect;
            }
        }
    }
    // Tooltip for truncated cell — drawn after all rows to render on top.
    if (tooltip_text) {
        float tw = MeasureTextEx(fonts.mono, tooltip_text, fonts.mono_size, 0).x;
        float tp = UiSize(graph, 6.0f);
        float tx = tooltip_anchor.x;
        float ty = tooltip_anchor.y - fonts.body_size - tp * 2 - unit;
        if (ty < ToolbarHeight(graph)) ty = tooltip_anchor.y + tooltip_anchor.height + unit;
        if (tx + tw + tp * 2 > GetScreenWidth()) tx = GetScreenWidth() - tw - tp * 2 - unit;
        DrawRectangleRec((Rectangle){tx, ty, tw + tp * 2, fonts.body_size + tp * 2}, (Color){20, 24, 32, 240});
        DrawRectangleLinesEx((Rectangle){tx, ty, tw + tp * 2, fonts.body_size + tp * 2}, unit, (Color){70, 80, 98, 255});
        DrawInterfaceText(fonts.mono, tooltip_text, tx + tp, ty + tp, fonts.mono_size, COLOR_TEXT);
    }

    // Border around the whole table (header + rows).
    DrawRectangleLinesEx(table_bounds, unit, (Color){58, 66, 81, 255});

    // Scroll bar track.
    DrawRectangleRec((Rectangle){track_x, track_y, scrollbar_w, track_h}, (Color){28, 33, 42, 255});
    DrawRectangleLinesEx((Rectangle){track_x, track_y, scrollbar_w, track_h}, unit, (Color){58, 66, 81, 255});
    if (max_scroll > 0) {
        Color thumb_color = win->scrollbar_dragging ? (Color){110, 122, 144, 255} : (Color){75, 84, 101, 255};
        DrawRectangleRec((Rectangle){track_x + unit, thumb_y + unit, scrollbar_w - unit * 2, thumb_h - unit * 2},
                         thumb_color);
    }

    // Resize handle — bottom-right corner triangle.
    float rh = UiSize(graph, 12.0f);
    Vector2 br = {panel.x + panel.width, panel.y + panel.height};
    DrawTriangle((Vector2){br.x - rh, br.y}, (Vector2){br.x, br.y - rh}, br, (Color){75, 84, 101, 255});
    Rectangle resize_zone = {br.x - rh, br.y - rh, rh, rh};
    if (left_pressed && CheckCollisionPointRec(mouse, resize_zone)) {
        win->resizing = true;
        win->resize_start_mouse = mouse;
        win->resize_start_size = (Vector2){panel.width, panel.height};
    }
    if (win->resizing) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dw = mouse.x - win->resize_start_mouse.x;
            float dh = mouse.y - win->resize_start_mouse.y;
            win->size.x = win->resize_start_size.x + dw;
            win->size.y = win->resize_start_size.y + dh;
        } else {
            win->resizing = false;
        }
    }

    return close_clicked;
}

// Draw a transient hover preview (not pinned, no close button, follows the port).
void DrawPortHoverPreview(GraphContext *graph, int port_id) {
    Port *port = FindPort(graph, port_id);
    if (!port) {
        return;
    }
    Vector2 p = PortScreenPosition(graph, port);
    float w = UiSize(graph, port->data_type == VALUE_RECORD ? 720.0f : 280.0f);
    if (w > GetScreenWidth() - UiSize(graph, 24.0f)) {
        w = GetScreenWidth() - UiSize(graph, 24.0f);
    }
    float h = UiSize(graph, 240.0f);
    float x = p.x + UiSize(graph, PORT_RADIUS + 10.0f);
    float y = p.y - h * 0.3f;
    if (x + w > GetScreenWidth()) {
        x = p.x - UiSize(graph, PORT_RADIUS + 10.0f) - w;
    }
    if (y < ToolbarHeight(graph)) {
        y = ToolbarHeight(graph) + UiSize(graph, 4.0f);
    }
    if (y + h > GetScreenHeight() - StatusHeight(graph)) {
        y = GetScreenHeight() - StatusHeight(graph) - h - UiSize(graph, 4.0f);
    }

    float unit = UiUnit(graph);
    Rectangle panel = {x, y, w, h};
    SetGuiScale(unit);
    GuiPanel(panel, NULL);
    DrawRectangleLinesEx(panel, unit, PortColor(port->data_type));

    float title_h = UiSize(graph, 28.0f);
    DrawRectangleRec((Rectangle){panel.x + unit, panel.y + unit, panel.width - unit * 2, title_h - unit},
                     (Color){35, 41, 52, 255});
    float px = panel.x + UiSize(graph, 10.0f);
    float py = panel.y + (title_h - fonts.body_size) * 0.5f;
    DrawInterfaceText(fonts.body,
                      TextFormat("%s | Stream<%s> | %d item%s", port->name, ValueTypeName(port->data_type),
                                 port->item_count, port->item_count == 1 ? "" : "s"),
                      px, py, fonts.body_size, COLOR_TEXT);

    Rectangle list_bounds = {panel.x + UiSize(graph, 8.0f), panel.y + title_h + UiSize(graph, 2.0f),
                             panel.width - UiSize(graph, 16.0f), panel.height - title_h - UiSize(graph, 10.0f)};
    if (port->data_type != VALUE_RECORD) {
        char display[MAX_ITEMS][64];
        char *entries[MAX_ITEMS];
        int count = port->item_count;
        for (int i = 0; i < count; i++) {
            char formatted[64];
            TextCopy(display[i], ValueDisplayText(&port->items[i].values[0], formatted, sizeof(formatted)));
            entries[i] = display[i];
        }
        int dummy_scroll = 0, dummy_active = -1;
        SetCodeGuiScale(unit);
        GuiListViewEx(list_bounds, entries, count, &dummy_scroll, &dummy_active, NULL);
        return;
    }
    int field_count = port->schema.field_count > 0 ? port->schema.field_count : 1;
    float header_height = UiSize(graph, 26.0f);
    float row_height = UiSize(graph, 24.0f);
    float column_width = list_bounds.width / (float)field_count;
    Rectangle header = {list_bounds.x, list_bounds.y, list_bounds.width, header_height};
    DrawRectangleRec(header, (Color){28, 34, 44, 255});
    DrawLineEx((Vector2){header.x, header.y + header_height},
               (Vector2){header.x + header.width, header.y + header_height}, unit, (Color){58, 66, 81, 255});
    float hdr_text_y = header.y + (header_height - fonts.mono_size) * 0.5f;
    for (int column = 0; column < port->schema.field_count; column++) {
        FieldSchema *field = &port->schema.fields[column];
        float column_x = header.x + column * column_width;
        Color name_color = field->derived ? (Color){116, 206, 173, 255} : (Color){200, 208, 220, 255};
        DrawInterfaceText(fonts.mono, field->name, column_x + UiSize(graph, 8.0f), hdr_text_y,
                          fonts.mono_size, name_color);
        if (column > 0) {
            DrawLineEx((Vector2){column_x, header.y}, (Vector2){column_x, list_bounds.y + list_bounds.height}, unit,
                       (Color){45, 52, 65, 255});
        }
    }
    int visible_rows = (int)((list_bounds.height - header_height) / row_height);
    for (int visible = 0; visible < visible_rows; visible++) {
        if (visible >= port->item_count) {
            break;
        }
        float row_y = list_bounds.y + header_height + visible * row_height;
        Color row_bg = (visible % 2) ? (Color){28, 34, 44, 255} : (Color){33, 39, 50, 255};
        DrawRectangleRec((Rectangle){list_bounds.x, row_y, list_bounds.width, row_height}, row_bg);
        float cell_text_y = row_y + (row_height - fonts.mono_size) * 0.5f;
        for (int column = 0; column < port->schema.field_count; column++) {
            char formatted[64];
            const char *text = ValueDisplayText(&port->items[visible].values[column], formatted, sizeof(formatted));
            Rectangle cell = {list_bounds.x + column * column_width + UiSize(graph, 8.0f), row_y,
                              column_width - UiSize(graph, 16.0f), row_height};
            BeginScissorMode((int)cell.x, (int)cell.y, (int)cell.width, (int)cell.height);
            DrawInterfaceText(fonts.mono, text, cell.x, cell_text_y, fonts.mono_size,
                              port->schema.fields[column].derived ? (Color){145, 218, 191, 255} : COLOR_TEXT);
            EndScissorMode();
        }
    }
}

void DrawPortTypeTooltip(GraphContext *graph) {
    if (!graph || graph->interaction_mode != INTERACTION_IDLE || MouseOverAnyInspectorWindow(graph, GetMousePosition())) {
        return;
    }

    Vector2 mouse = GetMousePosition();
    Port *hovered = NULL;
    // Nodes later in the array are drawn on top, so hit-test them first.
    for (int node_index = graph->node_count - 1; node_index >= 0 && !hovered; node_index--) {
        Node *node = &graph->nodes[node_index];
        for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT && !hovered; direction++) {
            int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
            int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
            for (int i = 0; i < count; i++) {
                Port *port = FindPort(graph, ids[i]);
                if (port && CheckCollisionPointRec(mouse, PortChipBounds(graph, node, port))) {
                    hovered = port;
                    break;
                }
            }
        }
    }
    if (!hovered) {
        return;
    }

    float unit = UiUnit(graph);
    UiTextStyle body_style = GetUiTextStyle(TEXT_ROLE_BODY, false);
    UiTextStyle code_style = GetUiTextStyle(TEXT_ROLE_CODE, false);
    float pad = UiSize(graph, 10.0f);
    float row_h = fmaxf(body_style.line_height, code_style.line_height);
    float width = UiSize(graph, 270.0f);
    int field_count = hovered->data_type == VALUE_RECORD && hovered->schema_valid ? hovered->schema.field_count : 0;
    float height = pad * 2 + row_h * (field_count > 0 ? 4 + field_count : 3);
    float x = mouse.x + UiSize(graph, 14.0f);
    float y = mouse.y + UiSize(graph, 14.0f);
    if (x + width > GetScreenWidth() - UiSize(graph, 4.0f)) {
        x = mouse.x - width - UiSize(graph, 14.0f);
    }
    if (y + height > GetScreenHeight() - StatusHeight(graph) - UiSize(graph, 4.0f)) {
        y = mouse.y - height - UiSize(graph, 14.0f);
    }
    y = fmaxf(y, ToolbarHeight(graph) + UiSize(graph, 4.0f));

    Rectangle panel = {x, y, width, height};
    Color port_color = PortStateColor(graph, hovered);
    DrawRectangleRec(panel, (Color){20, 24, 32, 248});
    DrawRectangleLinesEx(panel, unit, (Color){port_color.r, port_color.g, port_color.b, 210});

    float text_x = x + pad;
    float text_y = y + pad;
    DrawUiText(TEXT_ROLE_LABEL, false, hovered->direction == PORT_DIR_INPUT ? "INPUT" : "OUTPUT", text_x, text_y,
               port_color);
    text_y += row_h;
    DrawUiText(TEXT_ROLE_BODY, false, TextFormat("Port: %s", hovered->name), text_x, text_y, COLOR_TEXT);
    text_y += row_h;

    const char *type_label = ValueTypeName(hovered->data_type);
    if (!hovered->schema_valid && hovered->data_type == VALUE_NONE) {
        DrawUiText(TEXT_ROLE_BODY, false, "Type: unresolved", text_x, text_y, COLOR_MUTED);
    } else {
        const char *prefix = !hovered->schema_valid && hovered->direction == PORT_DIR_INPUT ? "Accepts" : "Type";
        DrawUiText(TEXT_ROLE_CODE, false, TextFormat("%s: Stream<%s>", prefix, type_label), text_x, text_y,
                   hovered->schema_valid ? COLOR_TEXT : COLOR_MUTED);
    }

    if (field_count > 0) {
        text_y += row_h;
        DrawUiText(TEXT_ROLE_LABEL, false, "Fields", text_x, text_y, COLOR_MUTED);
        for (int i = 0; i < field_count; i++) {
            FieldSchema *field = &hovered->schema.fields[i];
            text_y += row_h;
            DrawUiText(TEXT_ROLE_CODE, false, TextFormat("%s: %s", field->name, ValueTypeName(field->type)),
                       text_x + UiSize(graph, 8.0f), text_y,
                       field->derived ? (Color){145, 218, 191, 255} : COLOR_TEXT);
        }
    }
}

bool MouseOverAnyInspectorWindow(GraphContext *graph, Vector2 mouse) {
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        InspectorWindow *win = &graph->inspector_windows[i];
        if (win->port_id <= 0) {
            continue;
        }
        if (CheckCollisionPointRec(mouse, InspectorWindowBounds(graph, win))) {
            return true;
        }
    }
    return false;
}

void DrawToolbar(GraphContext *graph) {
    float unit = UiUnit(graph);
    float toolbar_height = ToolbarHeight(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    SetGuiScale(unit);
    DrawRectangle(0, 0, GetScreenWidth(), (int)toolbar_height, (Color){25, 29, 37, 255});
    DrawLineEx((Vector2){0, toolbar_height - unit * 0.5f}, (Vector2){GetScreenWidth(), toolbar_height - unit * 0.5f},
               unit, (Color){59, 67, 82, 255});

    if (GuiButton(
            (Rectangle){
                UiSize(graph, 12.0f),
                UiSize(graph, 10.0f),
                UiSize(graph, 132.0f),
                UiSize(graph, 32.0f),
            },
            "#08# Add node")) {
        graph->add_menu_open = !graph->add_menu_open;
        graph->add_menu_pos = (Vector2){UiSize(graph, 12.0f), toolbar_height};
        graph->open_dialog_open = false;
    }
    if (GuiButton(
            (Rectangle){
                UiSize(graph, 154.0f),
                UiSize(graph, 10.0f),
                UiSize(graph, 100.0f),
                UiSize(graph, 32.0f),
            },
            "#131# Run")) {
        RunGraph(graph);
    }
    if (GuiButton(
            (Rectangle){
                UiSize(graph, 264.0f),
                UiSize(graph, 10.0f),
                UiSize(graph, 100.0f),
                UiSize(graph, 32.0f),
            },
            "#01# Open")) {
        graph->open_dialog_open = !graph->open_dialog_open;
        graph->add_menu_open = false;
    }
    if (GuiButton(
            (Rectangle){
                UiSize(graph, 374.0f),
                UiSize(graph, 10.0f),
                UiSize(graph, 100.0f),
                UiSize(graph, 32.0f),
            },
            "#02# Save")) {
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
    float scale_button_y = UiSize(graph, 10.0f);
    float scale_button_height = UiSize(graph, 32.0f);
    float scale_button_gap = UiSize(graph, 6.0f);
    float ui_label_width = UiSize(graph, 104.0f);
    float node_label_width = UiSize(graph, 128.0f);
    float right_labels_x =
        GetScreenWidth() - UiSize(graph, 12.0f) - ui_label_width - scale_button_gap - node_label_width;
    float fname_x = UiSize(graph, 492.0f);
    float fname_width = MeasureTextEx(fonts.mono, fname, fonts.mono_size, 0).x;
    if (fname_x + fname_width + UiSize(graph, 12.0f) < right_labels_x) {
        DrawInterfaceText(fonts.mono, fname, fname_x, UiSize(graph, 18.0f), fonts.mono_size, COLOR_MUTED);
    }
    if (GuiButton((Rectangle){right_labels_x, scale_button_y, node_label_width, scale_button_height},
                  TextFormat("Node %d%%", (int)(graph->camera.zoom * 100 + 0.5f)))) {
        graph->camera.zoom = 1.0f;
        TextCopy(graph->status, "Node zoom reset to 100%");
    }
    if (GuiButton((Rectangle){right_labels_x + node_label_width + scale_button_gap, scale_button_y, ui_label_width,
                              scale_button_height},
                  TextFormat("UI %d%%", (int)(ApplicationScale(graph) * 100 + 0.5f)))) {
        graph->application_scale = 1.0f;
        TextCopy(graph->status, "Application scale reset to 100%");
    }

    if (graph->add_menu_open) {
        const char *labels[] = {"Files", "Match", "Insert", "Get", "Exec", "HTTP Request"};
        NodeType node_types[] = {NODE_DIRECTORY_LIST, NODE_MATCH, NODE_INSERT, NODE_GET, NODE_EXEC, NODE_HTTP_REQUEST};
        int label_count = 6;
        float menu_w = UiSize(graph, 176.0f);
        float menu_h = UiSize(graph, 10.0f + label_count * 31.0f);
        float menu_x = graph->add_menu_pos.x;
        float menu_y = graph->add_menu_pos.y + UiSize(graph, 4.0f);
        if (menu_x + menu_w > GetScreenWidth() - UiSize(graph, 4.0f)) {
            menu_x = GetScreenWidth() - UiSize(graph, 4.0f) - menu_w;
        }
        if (menu_y + menu_h > GetScreenHeight() - StatusHeight(graph) - UiSize(graph, 4.0f)) {
            menu_y = graph->add_menu_pos.y - menu_h - UiSize(graph, 4.0f);
        }
        Rectangle menu = {menu_x, menu_y, menu_w, menu_h};
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, unit, (Color){75, 84, 101, 255});
        for (int i = 0; i < label_count; i++) {
            Rectangle button = {
                menu_x + UiSize(graph, 6.0f),
                menu_y + UiSize(graph, 6.0f + i * 31.0f),
                UiSize(graph, 164.0f),
                UiSize(graph, 27.0f),
            };
            if (GuiButton(button, labels[i])) {
                Vector2 spawn_pos = GetScreenToWorld2D(graph->add_menu_pos, CanvasCamera(graph));
                AddNode(graph, node_types[i], spawn_pos);
                graph->add_menu_open = false;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), menu)) {
            graph->add_menu_open = false;
        }
    }

    if (graph->open_dialog_open) {
        Rectangle dialog = {
            UiSize(graph, 12.0f),
            toolbar_height + UiSize(graph, 4.0f),
            UiSize(graph, 400.0f),
            UiSize(graph, 52.0f),
        };
        DrawRectangleRec(dialog, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(dialog, unit, (Color){75, 84, 101, 255});
        DrawInterfaceText(fonts.body, "File path:", UiSize(graph, 22.0f), toolbar_height + UiSize(graph, 12.0f),
                          body_font_size, COLOR_MUTED);
        Rectangle input = {
            UiSize(graph, 100.0f),
            toolbar_height + UiSize(graph, 8.0f),
            UiSize(graph, 220.0f),
            UiSize(graph, 28.0f),
        };
        static bool editing = false;
        SetCodeGuiScale(unit);
        if (GuiTextBox(input, graph->open_dialog_path, sizeof(graph->open_dialog_path), editing)) {
            editing = !editing;
        }
        SetGuiScale(unit);
        Rectangle load_button = {
            UiSize(graph, 328.0f),
            toolbar_height + UiSize(graph, 8.0f),
            UiSize(graph, 76.0f),
            UiSize(graph, 28.0f),
        };
        if (GuiButton(load_button, "Load")) {
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
    float unit = UiUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    int height = (int)StatusHeight(graph);
    int y = GetScreenHeight() - height;
    DrawRectangle(0, y, GetScreenWidth(), height, (Color){25, 29, 37, 255});
    DrawLineEx((Vector2){0, y + unit * 0.5f}, (Vector2){GetScreenWidth(), y + unit * 0.5f}, unit,
               (Color){59, 67, 82, 255});
    DrawCircle((int)UiSize(graph, 14.0f), (int)(y + UiSize(graph, 14.0f)), UiSize(graph, 4.0f), COLOR_STRING_LIST);
    DrawInterfaceText(fonts.body, graph->status, UiSize(graph, 25.0f), y + UiSize(graph, 6.0f), body_font_size,
                      COLOR_MUTED);
    const char *help = "RMB drag: knife   Ctrl+Wheel: node zoom   Ctrl+/-/0: UI scale   Del: remove";
    float help_width = MeasureTextEx(fonts.body, help, body_font_size, 0).x;
    float help_x = GetScreenWidth() - help_width - UiSize(graph, 12.0f);
    float status_width = MeasureTextEx(fonts.body, graph->status, body_font_size, 0).x;
    if (help_x > UiSize(graph, 25.0f) + status_width + UiSize(graph, 20.0f)) {
        DrawInterfaceText(fonts.body, help, help_x, y + UiSize(graph, 6.0f), body_font_size, COLOR_MUTED);
    }
}
