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
    return graph->interaction_mode == INTERACTION_IDLE && NodeAtMouse(graph, GetMousePosition()) == node->id;
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
    float edge_pad = CanvasSize(graph, 14.0f);

    for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT; direction++) {
        int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        for (int i = 0; i < count; i++) {
            Port *port = FindPort(graph, ids[i]);
            if (!port) {
                continue;
            }
            Color port_color = PortStateColor(graph, port);
            const char *chip_label =
                TextFormat("%s · %s", port->name, port->schema_valid ? ValueTypeName(port->data_type) : "?");
            float label_w = MeasureTextEx(fonts.node_small, chip_label, chip_font, 0).x;
            float chip_w = label_w + chip_pad * 2;
            Vector2 port_pos = PortScreenPosition(graph, port);
            float chip_x =
                direction == PORT_DIR_INPUT ? bounds.x + edge_pad : bounds.x + bounds.width - edge_pad - chip_w;
            Rectangle chip = {chip_x, port_pos.y - chip_h * 0.5f, chip_w, chip_h};
            DrawRectangleRec(chip, (Color){port_color.r, port_color.g, port_color.b, 40});
            DrawRectangleLinesEx(chip, unit, (Color){port_color.r, port_color.g, port_color.b, 120});
            DrawInterfaceText(fonts.node_small, chip_label, chip.x + chip_pad, chip.y + (chip_h - chip_font) * 0.5f,
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

static bool FieldIsSelectable(const Node *node, ValueType type) {
    return node->type == NODE_GET || ValueTypeIsText(type);
}

static const char *NextFieldName(Node *node, Port *input) {
    if (!input || !input->schema_valid || input->data_type != VALUE_RECORD) {
        return "Item";
    }
    int current = SchemaFieldIndex(&input->schema, node->field_name);
    for (int offset = 1; offset <= input->schema.field_count; offset++) {
        int index = (current + offset) % input->schema.field_count;
        if (FieldIsSelectable(node, input->schema.fields[index].type)) {
            return input->schema.fields[index].name;
        }
    }
    return "";
}

static void DrawFieldSelector(GraphContext *graph, Node *node, float y_units, const char *label) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Port *input = InputSourcePort(graph, node, 0);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    DrawInterfaceText(fonts.node_body, label, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + CanvasSize(graph, y_units + 6.0f), font_size, COLOR_MUTED);
    Rectangle button = {
        bounds.x + CanvasSize(graph, 72.0f),
        bounds.y + CanvasSize(graph, y_units),
        bounds.width - CanvasSize(graph, 86.0f),
        CanvasSize(graph, 26.0f),
    };
    const char *field = node->field_name[0] ? node->field_name : "connect input";
    DrawRectangleRec(button, (Color){48, 55, 70, 255});
    DrawRectangleLinesEx(button, CanvasUnit(graph),
                         node->schema_error ? (Color){235, 87, 87, 255} : (Color){75, 84, 101, 255});
    DrawInterfaceText(fonts.node_body, TextFormat("%s  ▾", field), button.x + CanvasSize(graph, 7.0f),
                      button.y + (button.height - font_size) * 0.5f, font_size, COLOR_TEXT);
    if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), button) && input) {
        TextCopy(node->field_name, NextFieldName(node, input));
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Field selection changed - downstream schemas updated");
    }
}

static bool DrawNodeTextBox(GraphContext *graph, Node *node, Rectangle bounds, char *text, int capacity,
                            int control_id) {
    char before[128];
    TextCopy(before, text);
    SetNodeGuiScale(CanvasUnit(graph));
    bool changed_editing = GuiTextBox(bounds, text, capacity, node->editing_control == control_id);
    if (changed_editing) {
        node->editing_control = node->editing_control == control_id ? -1 : control_id;
        node->text_editing = node->editing_control >= 0;
    }
    return strcmp(before, text) != 0;
}

void DrawNodeContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    if (node->type == NODE_DIRECTORY_LIST || node->type == NODE_STRING_FILTER || node->type == NODE_EXEC ||
        node->type == NODE_HTTP_REQUEST) {
        float text_box_y = node->type == NODE_STRING_FILTER ? NODE_HEADER_HEIGHT + 48.0f : NODE_HEADER_HEIGHT + 16.0f;
        if (node->type == NODE_STRING_FILTER) {
            DrawFieldSelector(graph, node, NODE_HEADER_HEIGHT + 12.0f, "Field");
        }
        Rectangle text_box = {
            bounds.x + CanvasSize(graph, 14.0f),
            bounds.y + CanvasSize(graph, text_box_y),
            bounds.width - CanvasSize(graph, 28.0f),
            CanvasSize(graph, 30.0f),
        };
        SetNodeGuiScale(unit);
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
        } else if (node->type == NODE_STRING_FILTER) {
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
    } else if (node->type == NODE_INSERT) {
        DrawFieldSelector(graph, node, NODE_HEADER_HEIGHT + 10.0f, "From");
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
        DrawFieldSelector(graph, node, NODE_HEADER_HEIGHT + 12.0f, "Field");
        const char *state_label = node->schema_error ? "SCHEMA ERROR" : node->is_dirty ? "NOT RUN" : "CURRENT";
        DrawInterfaceText(fonts.node_body, state_label, bounds.x + CanvasSize(graph, 14.0f),
                          bounds.y + bounds.height - CanvasSize(graph, 21.0f), body_font_size, NodeStateColor(node));
    }
}

void DrawNode(GraphContext *graph, Node *node) {
    DrawNodeShell(graph, node);
    DrawNodeContent(graph, node);
    DrawNodePorts(graph, node);
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
                      : node->type == NODE_STRING_FILTER ? 116.0f
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

static Rectangle PortInspectorBounds(GraphContext *graph, int port_id) {
    Port *port = FindPort(graph, port_id);
    if (!port) {
        return (Rectangle){0};
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
    float unit = UiUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);

    DrawRectangleRec(panel, (Color){22, 26, 34, 245});
    DrawRectangleLinesEx(panel, UiSize(graph, pinned ? 2.0f : 1.0f), border);

    float px = panel.x + UiSize(graph, 10.0f);
    float py = panel.y + UiSize(graph, 8.0f);
    DrawInterfaceText(fonts.body,
                      TextFormat("%s<%s>  |  %d item%s", port->name, ValueTypeName(port->data_type), port->item_count,
                                 port->item_count == 1 ? "" : "s"),
                      px, py, body_font_size, COLOR_TEXT);

    Rectangle list_bounds = {
        panel.x + UiSize(graph, 8.0f),
        panel.y + UiSize(graph, 30.0f),
        panel.width - UiSize(graph, 16.0f),
        panel.height - UiSize(graph, 38.0f),
    };
    if (port->data_type != VALUE_RECORD) {
        char display[MAX_ITEMS][MAX_PATH_LENGTH];
        char *entries[MAX_ITEMS];
        int count = port->item_count;
        for (int i = 0; i < count; i++) {
            char formatted[64];
            TextCopy(display[i], ValueDisplayText(&port->items[i].values[0], formatted, sizeof(formatted)));
            entries[i] = display[i];
        }
        SetGuiScale(unit);
        GuiListViewEx(list_bounds, entries, count, &graph->inspect_scroll, &graph->inspect_active, NULL);
        return;
    }

    float header_height = UiSize(graph, 34.0f);
    float row_height = UiSize(graph, 25.0f);
    float column_width = list_bounds.width / port->schema.field_count;
    Rectangle header = {list_bounds.x, list_bounds.y, list_bounds.width, header_height};
    DrawRectangleRec(header, (Color){35, 41, 52, 255});
    for (int column = 0; column < port->schema.field_count; column++) {
        FieldSchema *field = &port->schema.fields[column];
        float column_x = header.x + column * column_width;
        Color name_color = field->derived ? (Color){116, 206, 173, 255} : COLOR_TEXT;
        DrawInterfaceText(fonts.body, field->name, column_x + UiSize(graph, 6.0f), header.y + UiSize(graph, 4.0f),
                          body_font_size, name_color);
        DrawInterfaceText(fonts.body, ValueTypeName(field->type), column_x + UiSize(graph, 6.0f),
                          header.y + UiSize(graph, 18.0f), ScaledFontSize(BODY_TEXT_SIZE * 0.72f, unit), COLOR_MUTED);
        if (column > 0) {
            DrawLineEx((Vector2){column_x, header.y}, (Vector2){column_x, list_bounds.y + list_bounds.height}, unit,
                       (Color){58, 66, 81, 255});
        }
    }

    int visible_rows = (int)((list_bounds.height - header_height) / row_height);
    int max_scroll = port->item_count > visible_rows ? port->item_count - visible_rows : 0;
    if (CheckCollisionPointRec(GetMousePosition(), panel)) {
        graph->inspect_scroll -= (int)GetMouseWheelMove();
    }
    graph->inspect_scroll = (int)Clamp((float)graph->inspect_scroll, 0.0f, (float)max_scroll);
    for (int visible = 0; visible < visible_rows; visible++) {
        int row = visible + graph->inspect_scroll;
        if (row >= port->item_count) {
            break;
        }
        float row_y = list_bounds.y + header_height + visible * row_height;
        if (row % 2) {
            DrawRectangleRec((Rectangle){list_bounds.x, row_y, list_bounds.width, row_height},
                             (Color){30, 35, 45, 255});
        }
        for (int column = 0; column < port->schema.field_count; column++) {
            char formatted[64];
            const char *text = ValueDisplayText(&port->items[row].values[column], formatted, sizeof(formatted));
            Rectangle cell = {list_bounds.x + column * column_width + UiSize(graph, 5.0f), row_y,
                              column_width - UiSize(graph, 10.0f), row_height};
            BeginScissorMode((int)cell.x, (int)cell.y, (int)cell.width, (int)cell.height);
            DrawInterfaceText(fonts.body, text, cell.x, cell.y + UiSize(graph, 5.0f),
                              ScaledFontSize(BODY_TEXT_SIZE * 0.85f, unit),
                              port->schema.fields[column].derived ? (Color){145, 218, 191, 255} : COLOR_TEXT);
            EndScissorMode();
        }
    }
}

bool MouseOverPortInspector(GraphContext *graph, Vector2 mouse) {
    if (graph->inspected_port_id < 0) {
        return false;
    }
    return CheckCollisionPointRec(mouse, PortInspectorBounds(graph, graph->inspected_port_id));
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
    float fname_width = MeasureTextEx(fonts.body, fname, body_font_size, 0).x;
    if (fname_x + fname_width + UiSize(graph, 12.0f) < right_labels_x) {
        DrawInterfaceText(fonts.body, fname, fname_x, UiSize(graph, 18.0f), body_font_size, COLOR_MUTED);
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
        const char *labels[] = {"Files", "Where", "Insert", "Get", "Exec", "HTTP Request"};
        NodeType node_types[] = {NODE_DIRECTORY_LIST, NODE_STRING_FILTER, NODE_INSERT, NODE_GET,
                                 NODE_EXEC,           NODE_HTTP_REQUEST};
        int label_count = 6;
        Rectangle menu = {
            UiSize(graph, 12.0f),
            toolbar_height + UiSize(graph, 4.0f),
            UiSize(graph, 176.0f),
            UiSize(graph, 10.0f + label_count * 31.0f),
        };
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, unit, (Color){75, 84, 101, 255});
        for (int i = 0; i < label_count; i++) {
            Rectangle button = {
                UiSize(graph, 18.0f),
                toolbar_height + UiSize(graph, 10.0f + i * 31.0f),
                UiSize(graph, 164.0f),
                UiSize(graph, 27.0f),
            };
            if (GuiButton(button, labels[i])) {
                Vector2 center = GetScreenToWorld2D((Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f},
                                                    CanvasCamera(graph));
                AddNode(graph, node_types[i], center);
                graph->add_menu_open = false;
            }
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
        if (GuiTextBox(input, graph->open_dialog_path, sizeof(graph->open_dialog_path), editing)) {
            editing = !editing;
        }
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
