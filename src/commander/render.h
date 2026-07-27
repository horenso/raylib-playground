#pragma once

#include "types.h"

void DrawCanvasGrid(GraphContext *graph);
void DrawConnection(Vector2 from, Vector2 to, Color color, float thickness);
void DrawKnife(Vector2 start, Vector2 end, float scale);
void DrawNodeShell(GraphContext *graph, Node *node);
void DrawNodePorts(GraphContext *graph, Node *node);
void DrawNodeContent(GraphContext *graph, Node *node);
void DrawPortInspector(GraphContext *graph, int port_id, bool pinned);
void DrawToolbar(GraphContext *graph);
void DrawStatusBar(GraphContext *graph);
bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse);
bool MouseOverCollapseButton(GraphContext *graph, Node *node, Vector2 mouse);
bool MouseOverPortInspector(GraphContext *graph, Vector2 mouse);
