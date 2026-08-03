#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "input.h"
#include "raylib.h"
#include "render.h"
#include "serialize.h"
#include "types.h"

#include "raygui.h"

#include <stdio.h>

int main(int argc, char **argv) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 760, "Commander - visual dataflow shell");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(900, 560);
    SetTargetFPS(60);

    LoadInterfaceFonts();
    SetGuiScale(UI_BASE_PIXEL_SIZE);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x191D25FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x303746FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x3B465AFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x559CE4FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x505A6DFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xE1E6EFFF);
    GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, 0x00000000);
    GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, 0xE1E6EFFF);

    static GraphContext graph = {0};
    graph.application_scale = 1.0f;
    graph.camera.offset = (Vector2){0, TOOLBAR_HEIGHT};
    graph.selected_node_id = -1;
    graph.active_port_id = -1;
    graph.dragging_node_id = -1;
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        graph.inspector_windows[i].port_id = -1;
        graph.inspector_windows[i].active = -1;
    }

    if (argc >= 2) {
        if (LoadGraph(&graph, argv[1])) {
            TextCopy(graph.current_file, argv[1]);
            snprintf(graph.status, sizeof(graph.status), "Loaded: %s", argv[1]);
        } else {
            SeedGraph(&graph);
            snprintf(graph.status, sizeof(graph.status), "Could not load %s - starting blank", argv[1]);
        }
    } else {
        SeedGraph(&graph);
    }

    while (!WindowShouldClose()) {
        UpdateCanvas(&graph);
        UpdateInterfaceFontScale(UiUnit(&graph), CanvasUnit(&graph));
        if (graph.interaction_mode == INTERACTION_IDLE) {
            GuiUnlock();
        } else {
            GuiLock();
        }

        BeginDrawing();
        ClearBackground(COLOR_CANVAS);
        DrawCanvasGrid(&graph);

        for (int i = 0; i < graph.link_count; i++) {
            Port *from = FindPort(&graph, graph.links[i].from_port_id);
            Port *to = FindPort(&graph, graph.links[i].to_port_id);
            if (from && to) {
                bool knife_hit = graph.knife_active &&
                                 LinkIntersectsKnife(&graph, graph.links[i], graph.knife_start, GetMousePosition());
                Color color = knife_hit ? (Color){255, 76, 92, 255} : PortStateColor(&graph, from);
                DrawConnection(&graph, PortScreenPosition(&graph, from), PortScreenPosition(&graph, to), color,
                               knife_hit ? 4.0f : 3.0f);
            }
        }
        if (graph.active_port_id >= 0) {
            Port *port = FindPort(&graph, graph.active_port_id);
            if (port) {
                DrawConnection(&graph, PortScreenPosition(&graph, port), GetMousePosition(),
                               PortStateColor(&graph, port), 3.0f);
            }
        }

        for (int i = 0; i < graph.node_count; i++) {
            DrawNode(&graph, &graph.nodes[i]);
        }

        // Popups are a separate canvas layer. Drawing them after every node keeps
        // them above controls in their owner and above neighboring nodes.
        for (int i = 0; i < graph.node_count; i++) {
            DrawNodeOverlay(&graph, &graph.nodes[i]);
        }
        if (graph.knife_active) {
            DrawKnife(&graph, graph.knife_start, GetMousePosition());
        }

        // Draw persistent inspector windows
        for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
            InspectorWindow *win = &graph.inspector_windows[i];
            if (win->port_id <= 0) {
                continue;
            }
            if (DrawInspectorWindow(&graph, win)) {
                CloseInspectorWindow(win);
            }
        }

        // Draw hover preview when not dragging a port and no inspector open for it
        int hovered_output = PortAtMouse(&graph, GetMousePosition(), PORT_DIR_OUTPUT);
        if (hovered_output >= 0 && graph.interaction_mode == INTERACTION_IDLE &&
            !FindInspectorWindow(&graph, hovered_output)) {
            DrawPortHoverPreview(&graph, hovered_output);
        }
        DrawPortTypeTooltip(&graph);

        DrawToolbar(&graph);
        DrawStatusBar(&graph);

        EndDrawing();
        GuiUnlock();
    }

    UnloadInterfaceFonts();
    CloseWindow();
    return 0;
}
