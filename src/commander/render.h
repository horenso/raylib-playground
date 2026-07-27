#pragma once

#include "types.h"

void DrawCanvasGrid(GraphContext *graph);
void DrawConnection(Vector2 from, Vector2 to, Color color, float thickness);
void DrawKnife(Vector2 start, Vector2 end);
void DrawNodeShell(GraphContext *graph, Node *node);
void DrawNodePorts(GraphContext *graph, Node *node);
void DrawNodeContent(GraphContext *graph, Node *node);
void DrawToolbar(GraphContext *graph);
void DrawStatusBar(GraphContext *graph);
bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse);
