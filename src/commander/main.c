#include "evaluate.h"
#include "fonts.h"
#include "graph.h"
#include "input.h"
#include "raylib.h"
#include "render.h"
#include "types.h"

#include "raygui.h"

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 760, "Commander - visual dataflow shell");
    SetWindowMinSize(900, 560);
    SetTargetFPS(60);

    LoadInterfaceFonts();
    GuiSetStyle(DEFAULT, TEXT_SIZE, GUI_TEXT_SIZE);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x191D25FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x303746FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x3B465AFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x559CE4FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x505A6DFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xE1E6EFFF);
    GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, 0x00000000);
    GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, 0xE1E6EFFF);

    static GraphContext graph = {0};
    SeedGraph(&graph);

    while (!WindowShouldClose()) {
        UpdateCanvas(&graph);

        BeginDrawing();
        ClearBackground(COLOR_CANVAS);
        DrawCanvasGrid(&graph);

        for (int i = 0; i < graph.link_count; i++) {
            Port *from = FindPort(&graph, graph.links[i].from_port_id);
            Port *to = FindPort(&graph, graph.links[i].to_port_id);
            if (from && to) {
                DrawConnection(PortScreenPosition(&graph, from), PortScreenPosition(&graph, to),
                               PortColor(from->data_type), 3.0f);
            }
        }
        if (graph.active_port_id >= 0) {
            Port *port = FindPort(&graph, graph.active_port_id);
            if (port) {
                DrawConnection(PortScreenPosition(&graph, port), GetMousePosition(), PortColor(port->data_type), 3.0f);
            }
        }

        for (int i = 0; i < graph.node_count; i++) {
            DrawNodeShell(&graph, &graph.nodes[i]);
        }
        for (int i = 0; i < graph.node_count; i++) {
            DrawNodeContent(&graph, &graph.nodes[i]);
            DrawNodePorts(&graph, &graph.nodes[i]);
        }
        if (graph.knife_active) {
            DrawKnife(graph.knife_start, GetMousePosition());
        }

        // Draw pinned inspector or hover preview
        int hovered_output = PortAtMouse(&graph, GetMousePosition(), PORT_DIR_OUTPUT);
        if (graph.inspected_port_id >= 0) {
            DrawPortInspector(&graph, graph.inspected_port_id, true);
        } else if (hovered_output >= 0 && graph.active_port_id < 0) {
            DrawPortInspector(&graph, hovered_output, false);
        }

        DrawToolbar(&graph);
        DrawStatusBar(&graph);
        EndDrawing();
    }

    UnloadInterfaceFonts();
    CloseWindow();
    return 0;
}
