#include "render.h"
#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
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

bool NodeOwnsMouse(GraphContext *graph, Node *node) {
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
    float chip_x =
        port->direction == PORT_DIR_INPUT ? bounds.x + edge_pad : bounds.x + bounds.width - edge_pad - chip_w;
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
                              chip.y + FontTextCenterOffset(fonts.node_small, chip_h), chip_font, port_color);
        }
    }

    // The top section is intentionally quiet: title on the left, branch run on the right.
    float title_font_size = ScaledFontSize(TITLE_TEXT_SIZE, unit);
    Rectangle run_btn = NodeRunButtonBounds(graph, node);
    DrawInterfaceText(fonts.title, node->title, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + FontTextCenterOffset(fonts.title, CanvasSize(graph, NODE_HEADER_HEIGHT)),
                      title_font_size, COLOR_TEXT);

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

bool DrawNodeOptionButton(GraphContext *graph, Node *node, Rectangle bounds, const char *label, bool active,
                          float font_size) {
    Color background = active ? (Color){85, 156, 228, 255} : (Color){48, 55, 70, 255};
    DrawRectangleRec(bounds, background);
    DrawRectangleLinesEx(bounds, CanvasUnit(graph), (Color){75, 84, 101, 255});
    float text_width = MeasureTextEx(fonts.node_body, label, font_size, 0).x;
    DrawInterfaceText(fonts.node_body, label, bounds.x + (bounds.width - text_width) * 0.5f,
                      bounds.y + FontTextCenterOffset(fonts.node_body, bounds.height), font_size, COLOR_TEXT);
    return NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
           CheckCollisionPointRec(GetMousePosition(), bounds);
}

static float FieldSelectorYUnits(const Node *node) {
    const NodeDef *def = GetNodeDef(node->type);
    return NODE_HEADER_HEIGHT + (def ? def->field_selector_y_offset : 12.0f);
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
                          button.y + FontTextCenterOffset(fonts.node_body, button.height), font_size, COLOR_MUTED);
        return;
    }

    const char *field = node->field_name[0] ? node->field_name : options[0];
    DrawRectangleRec(button, (Color){48, 55, 70, 255});
    DrawRectangleLinesEx(button, CanvasUnit(graph),
                         node->schema_error ? (Color){235, 87, 87, 255} : (Color){75, 84, 101, 255});
    DrawInterfaceText(fonts.node_body, field, button.x + CanvasSize(graph, 7.0f),
                      button.y + FontTextCenterOffset(fonts.node_body, button.height), font_size, COLOR_TEXT);
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
    Port *input = node->type == NODE_FILTER ? InputSourcePort(graph, node, 0) : NULL;
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
        const char *option_label = options[i];
        char typed_option[MAX_FIELD_NAME + 16];
        if (input) {
            ValueType type = input->data_type;
            if (input->data_type == VALUE_RECORD) {
                int field_index = SchemaFieldIndex(&input->schema, options[i]);
                type = field_index >= 0 ? input->schema.fields[field_index].type : VALUE_NONE;
            }
            snprintf(typed_option, sizeof(typed_option), "%s: %s", options[i], ValueTypeName(type));
            option_label = typed_option;
        }
        DrawInterfaceText(fonts.node_body, option_label, item.x + CanvasSize(graph, 7.0f),
                          item.y + FontTextCenterOffset(fonts.node_body, item.height), font_size, COLOR_TEXT);
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
                      button.y + FontTextCenterOffset(fonts.node_body, button.height), font_size, COLOR_TEXT);
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
                          item.y + FontTextCenterOffset(fonts.node_body, item.height), font_size, COLOR_TEXT);
    }
}

bool DrawNodeTextBox(GraphContext *graph, Node *node, Rectangle bounds, char *text, int capacity, int control_id) {
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
    const NodeDef *def = GetNodeDef(node->type);
    if (def && def->draw_content) {
        def->draw_content(graph, node);
    }
}

void DrawNode(GraphContext *graph, Node *node) {
    const NodeDef *def = GetNodeDef(node->type);
    DrawNodeShell(graph, node);
    DrawNodeContent(graph, node);
    DrawNodePorts(graph, node);
    if (def && def->uses_field_selector) {
        DrawFieldSelector(graph, node, def->field_selector_label ? def->field_selector_label : "Field");
    }
    if (def && def->uses_field_selector && NodeSelectedFieldType(graph, node) == VALUE_SIZE) {
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
    const NodeDef *def = GetNodeDef(node->type);
    if (!def || def->control_height <= 0.0f) {
        return false;
    }
    Rectangle b = NodeScreenBounds(graph, node);
    return CheckCollisionPointRec(mouse, (Rectangle){
                                             b.x + CanvasSize(graph, 10.0f),
                                             b.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 10.0f),
                                             b.width - CanvasSize(graph, 20.0f),
                                             CanvasSize(graph, def->control_height),
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
    Rectangle content = {panel.x + unit, panel.y + title_h, panel.width - unit * 2, panel.height - title_h - unit};

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
    Rectangle table_bounds = {list_bounds.x, list_bounds.y, list_bounds.width - scrollbar_w - scrollbar_gap,
                              list_bounds.height};
    float column_width = table_bounds.width / (float)field_count;

    // Build a sorted index over the items.
    static int sorted_indices[MAX_ITEMS];
    for (int i = 0; i < port->item_count; i++) {
        sorted_indices[i] = i;
    }
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
                if (vt == VALUE_FLOAT) {
                    double a = jv->as.floating, b = kv->as.floating;
                    cmp = (a > b) - (a < b);
                } else if (vt == VALUE_INT) {
                    long long a = jv->as.integer, b = kv->as.integer;
                    cmp = (a > b) - (a < b);
                } else if (vt == VALUE_SIZE) {
                    unsigned long long a = jv->as.file_size, b = kv->as.file_size;
                    cmp = (a > b) - (a < b);
                } else if (vt == VALUE_DATETIME) {
                    long long a = jv->as.datetime, b = kv->as.datetime;
                    cmp = (a > b) - (a < b);
                } else if (vt == VALUE_BOOL) {
                    bool a = jv->as.boolean, b = kv->as.boolean;
                    cmp = (a > b) - (a < b);
                } else {
                    cmp = strcmp(jv->as.text, kv->as.text);
                }
                if (asc ? (cmp <= 0) : (cmp >= 0)) {
                    break;
                }
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
    float text_y = header.y + FontTextCenterOffset(fonts.mono, header_height);
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
        char header_label[MAX_FIELD_NAME + 16];
        snprintf(header_label, sizeof(header_label), "%s: %s", field->name, ValueTypeName(field->type));
        BeginScissorMode((int)(column_x + UiSize(graph, 4.0f)), (int)header.y,
                         (int)(column_width - UiSize(graph, 8.0f)), (int)header.height);
        DrawInterfaceText(fonts.mono, header_label, column_x + UiSize(graph, 8.0f), text_y, fonts.mono_size,
                          name_color);
        EndScissorMode();
        // Sort indicator — use the same raygui arrow icons as field dropdowns.
        if (win->sort_field == column) {
            float name_w = MeasureTextEx(fonts.mono, header_label, fonts.mono_size, 0).x;
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
    float thumb_h =
        max_scroll > 0 ? Clamp(track_h * ((float)visible_rows / (float)port->item_count), UiSize(graph, 16.0f), track_h)
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

    Rectangle rows_area = {table_bounds.x, table_bounds.y + header_height, table_bounds.width,
                           table_bounds.height - header_height};
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
        float cell_text_y = row_y + FontTextCenterOffset(fonts.mono, row_height);
        for (int column = 0; column < port->schema.field_count; column++) {
            float cell_x = rows_area.x + column * column_width;
            float cell_w = column_width;
            // Clip each cell individually so text doesn't bleed into the next column.
            BeginScissorMode((int)(cell_x + cell_pad * 0.5f), (int)row_y, (int)(cell_w - cell_pad), (int)row_height);
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
        if (ty < ToolbarHeight(graph)) {
            ty = tooltip_anchor.y + tooltip_anchor.height + unit;
        }
        if (tx + tw + tp * 2 > GetScreenWidth()) {
            tx = GetScreenWidth() - tw - tp * 2 - unit;
        }
        DrawRectangleRec((Rectangle){tx, ty, tw + tp * 2, fonts.body_size + tp * 2}, (Color){20, 24, 32, 240});
        DrawRectangleLinesEx((Rectangle){tx, ty, tw + tp * 2, fonts.body_size + tp * 2}, unit,
                             (Color){70, 80, 98, 255});
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
    float py = panel.y + FontTextCenterOffset(fonts.body, title_h);
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
    float hdr_text_y = header.y + FontTextCenterOffset(fonts.mono, header_height);
    for (int column = 0; column < port->schema.field_count; column++) {
        FieldSchema *field = &port->schema.fields[column];
        float column_x = header.x + column * column_width;
        Color name_color = field->derived ? (Color){116, 206, 173, 255} : (Color){200, 208, 220, 255};
        char header_label[MAX_FIELD_NAME + 16];
        snprintf(header_label, sizeof(header_label), "%s: %s", field->name, ValueTypeName(field->type));
        BeginScissorMode((int)(column_x + UiSize(graph, 4.0f)), (int)header.y,
                         (int)(column_width - UiSize(graph, 8.0f)), (int)header.height);
        DrawInterfaceText(fonts.mono, header_label, column_x + UiSize(graph, 8.0f), hdr_text_y, fonts.mono_size,
                          name_color);
        EndScissorMode();
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
        float cell_text_y = row_y + FontTextCenterOffset(fonts.mono, row_height);
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
    if (!graph || graph->interaction_mode != INTERACTION_IDLE ||
        MouseOverAnyInspectorWindow(graph, GetMousePosition())) {
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
                       text_x + UiSize(graph, 8.0f), text_y, field->derived ? (Color){145, 218, 191, 255} : COLOR_TEXT);
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
        const char *labels[] = {"Files", "CSV", "Filter", "Stringify", "Insert", "Get", "Exec", "HTTP Request"};
        NodeType node_types[] = {NODE_DIRECTORY_LIST, NODE_CSV, NODE_FILTER, NODE_STRINGIFY,
                                 NODE_INSERT,         NODE_GET, NODE_EXEC,   NODE_HTTP_REQUEST};
        int label_count = 8;
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
