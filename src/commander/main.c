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
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 760, "Commander - visual dataflow shell");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(900, 560);
    SetTargetFPS(60);

    LoadInterfaceFonts();
    SetGuiScale(1.0f);
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
    graph.inspected_port_id = -1;

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
        UpdateInterfaceFontScale(ApplicationScale(&graph), CanvasZoom(&graph));
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
                float thickness = (knife_hit ? 4.0f : 3.0f) * ApplicationScale(&graph);
                DrawConnection(PortScreenPosition(&graph, from), PortScreenPosition(&graph, to), color, thickness);
            }
        }
        if (graph.active_port_id >= 0) {
            Port *port = FindPort(&graph, graph.active_port_id);
            if (port) {
                DrawConnection(PortScreenPosition(&graph, port), GetMousePosition(), PortStateColor(&graph, port),
                               3.0f * ApplicationScale(&graph));
            }
        }

        for (int i = 0; i < graph.node_count; i++) {
            DrawNode(&graph, &graph.nodes[i]);
        }
        if (graph.knife_active) {
            DrawKnife(graph.knife_start, GetMousePosition(), ApplicationScale(&graph));
        }

        // Draw pinned inspector or hover preview
        int hovered_output = PortAtMouse(&graph, GetMousePosition(), PORT_DIR_OUTPUT);
        if (graph.inspected_port_id >= 0) {
            DrawPortInspector(&graph, graph.inspected_port_id, true);
        } else if (hovered_output >= 0 && graph.interaction_mode == INTERACTION_IDLE) {
            DrawPortInspector(&graph, hovered_output, false);
        }

        DrawToolbar(&graph);
        DrawStatusBar(&graph);

        EndDrawing();
        GuiUnlock();
    }

    UnloadInterfaceFonts();
    CloseWindow();
    return 0;
}
