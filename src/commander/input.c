#include "input.h"
#include "graph.h"
#include "node_def.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <string.h>

static bool MouseOverNodeTextBox(GraphContext *graph, Vector2 mouse) {
    for (int i = graph->node_count - 1; i >= 0; i--) {
        Node *node = &graph->nodes[i];
        if (node->field_dropdown_open || node->unit_dropdown_open) {
            continue;
        }
        if (NodeUsesFieldSelector(node) && CheckCollisionPointRec(mouse, FieldSelectorButtonBounds(graph, node))) {
            continue;
        }
        const NodeDef *def = GetNodeDef(node->type);
        if (def && def->mouse_in_edit_area && def->mouse_in_edit_area(graph, node, mouse)) {
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

static void CloseNodeDropdowns(GraphContext *graph) {
    for (int i = 0; i < graph->node_count; i++) {
        graph->nodes[i].field_dropdown_open = false;
        graph->nodes[i].unit_dropdown_open = false;
    }
}

// Dropdowns form one modal overlay layer. Resolve an open popup before testing
// any underlying buttons, then open controls in the same front-to-back order in
// which nodes are rendered.
static bool UpdateNodeDropdowns(GraphContext *graph, Vector2 mouse) {
    bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    // A unit selector is only meaningful while the selected field is a size.
    for (int i = 0; i < graph->node_count; i++) {
        Node *node = &graph->nodes[i];
        if (node->unit_dropdown_open && NodeSelectedFieldType(graph, node) != VALUE_SIZE) {
            node->unit_dropdown_open = false;
        }
    }

    // Only one popup can be open. Handle it before controls behind the popup.
    for (int i = graph->node_count - 1; i >= 0; i--) {
        Node *node = &graph->nodes[i];
        if (node->field_dropdown_open) {
            Rectangle button = FieldSelectorButtonBounds(graph, node);
            const char *options[MAX_FIELDS];
            int option_count = CollectNodeFieldOptions(graph, node, options, MAX_FIELDS);
            if (pressed && CheckCollisionPointRec(mouse, button)) {
                node->field_dropdown_open = false;
                return true;
            }
            for (int option = 0; option < option_count; option++) {
                Rectangle item = {button.x, button.y + button.height * (option + 1), button.width, button.height};
                if (CheckCollisionPointRec(mouse, item)) {
                    if (released) {
                        if (!TextIsEqual(node->field_name, options[option])) {
                            ValueType previous_type = NodeSelectedFieldType(graph, node);
                            TextCopy(node->field_name, options[option]);
                            MatchFieldTypeChanged(node, previous_type, NodeSelectedFieldType(graph, node));
                            MarkNodeDirty(graph, node->id);
                            TextCopy(graph->status, "Field selection changed - downstream schemas updated");
                        }
                        node->field_dropdown_open = false;
                    }
                    return true;
                }
            }
            if (pressed) {
                node->field_dropdown_open = false;
                return true;
            }
            return false;
        }
        if (node->unit_dropdown_open) {
            Rectangle button = SizeUnitButtonBounds(graph, node);
            if (pressed && CheckCollisionPointRec(mouse, button)) {
                node->unit_dropdown_open = false;
                return true;
            }
            for (int unit = FILE_SIZE_BYTES; unit <= FILE_SIZE_TB; unit++) {
                Rectangle item = {button.x, button.y + button.height * (unit + 1), button.width, button.height};
                if (CheckCollisionPointRec(mouse, item)) {
                    if (released) {
                        node->file_size_unit = (FileSizeUnit)unit;
                        node->unit_dropdown_open = false;
                        MarkNodeDirty(graph, node->id);
                    }
                    return true;
                }
            }
            if (pressed) {
                node->unit_dropdown_open = false;
                return true;
            }
            return false;
        }
    }

    if (!pressed) {
        return false;
    }

    for (int i = graph->node_count - 1; i >= 0; i--) {
        Node *node = &graph->nodes[i];
        const NodeDef *def = GetNodeDef(node->type);
        if (def && def->uses_field_selector && NodeSelectedFieldType(graph, node) == VALUE_SIZE &&
            CheckCollisionPointRec(mouse, SizeUnitButtonBounds(graph, node))) {
            CloseNodeDropdowns(graph);
            node->unit_dropdown_open = true;
            graph->selected_node_id = node->id;
            BringNodeToFront(graph, node->id);
            return true;
        }

        if (NodeUsesFieldSelector(node)) {
            const char *options[MAX_FIELDS];
            int option_count = CollectNodeFieldOptions(graph, node, options, MAX_FIELDS);
            if (option_count > 0 && CheckCollisionPointRec(mouse, FieldSelectorButtonBounds(graph, node))) {
                CloseNodeDropdowns(graph);
                node->field_dropdown_open = true;
                graph->selected_node_id = node->id;
                BringNodeToFront(graph, node->id);
                return true;
            }
        }
    }
    return false;
}

static void UpdateMouseCursor(GraphContext *graph, Vector2 mouse) {
    int cursor = MOUSE_CURSOR_DEFAULT;
    if (graph->interaction_mode == INTERACTION_PANNING || graph->interaction_mode == INTERACTION_DRAGGING_NODE) {
        cursor = MOUSE_CURSOR_RESIZE_ALL;
    } else if (graph->interaction_mode == INTERACTION_KNIFE || graph->interaction_mode == INTERACTION_LINKING) {
        cursor = MOUSE_CURSOR_CROSSHAIR;
    } else if (MouseOverDialogTextBox(graph, mouse) ||
               (!MouseOverAnyInspectorWindow(graph, mouse) && MouseOverNodeTextBox(graph, mouse))) {
        cursor = MOUSE_CURSOR_IBEAM;
    }

    static int current_cursor = -1;
    if (cursor != current_cursor) {
        SetMouseCursor(cursor);
        current_cursor = cursor;
    }
}

static Rectangle InspectorWindowRect(GraphContext *graph, InspectorWindow *win) {
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

// Returns the InspectorWindow whose title bar (excluding close button) is under the mouse.
static InspectorWindow *InspectorWindowTitleBarAtMouse(GraphContext *graph, Vector2 mouse) {
    // GuiWindowBox title bar is 24px tall; the rightmost 24px is the close button — skip it.
    float title_h = 24.0f;
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        InspectorWindow *win = &graph->inspector_windows[i];
        if (win->port_id <= 0) {
            continue;
        }
        Rectangle panel = InspectorWindowRect(graph, win);
        Rectangle title_bar = {panel.x, panel.y, panel.width - title_h, title_h};
        if (CheckCollisionPointRec(mouse, title_bar)) {
            return win;
        }
    }
    return (InspectorWindow *)0;
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
    bool dropdown_owns_mouse = UpdateNodeDropdowns(graph, mouse);
    if (IsKeyPressed(KEY_ESCAPE)) {
        CloseNodeDropdowns(graph);
    }
    bool in_canvas = mouse.y > ToolbarHeight(graph) && mouse.y < GetScreenHeight() - StatusHeight(graph);
    bool panning =
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));

    graph->interaction_mode = ResolveInteractionMode(graph, panning);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas && graph->interaction_mode == INTERACTION_IDLE) {
        graph->knife_active = true;
        graph->knife_start = mouse;
        TextCopy(graph->status, "Knife active - release to cut nodes and connections");
    }

    if (IsKeyPressed(KEY_SPACE) && in_canvas && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        graph->interaction_mode == INTERACTION_IDLE && !IsEditingText(graph)) {
        graph->add_menu_open = !graph->add_menu_open;
        graph->add_menu_pos = mouse;
        graph->open_dialog_open = false;
    }

    if (IsKeyPressed(KEY_ESCAPE) && graph->add_menu_open) {
        graph->add_menu_open = false;
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

    // Inspector window drag: title-bar drag moves the window.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && graph->interaction_mode == INTERACTION_IDLE) {
        InspectorWindow *win = InspectorWindowTitleBarAtMouse(graph, mouse);
        if (win && !win->resizing) {
            win->dragging = true;
            win->drag_offset = (Vector2){mouse.x - win->pos.x, mouse.y - win->pos.y};
        }
    }
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        InspectorWindow *win = &graph->inspector_windows[i];
        if (!win->dragging) {
            continue;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            win->pos.x = mouse.x - win->drag_offset.x;
            win->pos.y = mouse.y - win->drag_offset.y;
        } else {
            win->dragging = false;
        }
    }

    if (!dropdown_owns_mouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_canvas &&
        graph->interaction_mode == INTERACTION_IDLE) {
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
        } else if (MouseOverAnyInspectorWindow(graph, mouse)) {
            // clicks inside an inspector window: let raygui handle it
        } else if (node) {
            graph->selected_node_id = node_id;
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
                // Released on the same output port: open (or close if already open) inspector window.
                InspectorWindow *existing = FindInspectorWindow(graph, graph->active_port_id);
                if (existing) {
                    CloseInspectorWindow(existing);
                } else {
                    Port *port = FindPort(graph, graph->active_port_id);
                    Vector2 screen_pos = port ? PortScreenPosition(graph, port) : mouse;
                    float w = UiSize(graph, port && port->data_type == VALUE_RECORD ? 720.0f : 280.0f);
                    float h = UiSize(graph, 280.0f);
                    float x = screen_pos.x + UiSize(graph, PORT_RADIUS + 10.0f);
                    float y = screen_pos.y - h * 0.3f;
                    if (x + w > GetScreenWidth()) {
                        x = screen_pos.x - UiSize(graph, PORT_RADIUS + 10.0f) - w;
                    }
                    if (y < ToolbarHeight(graph)) {
                        y = ToolbarHeight(graph) + UiSize(graph, 4.0f);
                    }
                    if (y + h > GetScreenHeight() - StatusHeight(graph)) {
                        y = GetScreenHeight() - StatusHeight(graph) - h - UiSize(graph, 4.0f);
                    }
                    OpenInspectorWindow(graph, graph->active_port_id, (Vector2){x, y});
                }
            }
            graph->active_port_id = -1;
        }
    }

    graph->interaction_mode = ResolveInteractionMode(graph, panning);
    UpdateMouseCursor(graph, mouse);
}
