#pragma once

#include "types.h"

void DrawCanvasGrid(GraphContext *graph);
void DrawConnection(GraphContext *graph, Vector2 from, Vector2 to, Color color, float thickness_units);
void DrawKnife(GraphContext *graph, Vector2 start, Vector2 end);
void DrawNode(GraphContext *graph, Node *node);
void DrawNodeOverlay(GraphContext *graph, Node *node);
void DrawNodeShell(GraphContext *graph, Node *node);
void DrawNodePorts(GraphContext *graph, Node *node);
void DrawNodeContent(GraphContext *graph, Node *node);
bool DrawInspectorWindow(GraphContext *graph, InspectorWindow *win);
bool DrawFileExplorerWindow(GraphContext *graph);
void DrawPortHoverPreview(GraphContext *graph, int port_id);
void DrawPortTypeTooltip(GraphContext *graph);
bool MouseOverAnyInspectorWindow(GraphContext *graph, Vector2 mouse);
void DrawToolbar(GraphContext *graph);
void DrawStatusBar(GraphContext *graph);
bool MouseOverNodeControl(GraphContext *graph, Node *node, Vector2 mouse);
Rectangle FieldSelectorButtonBounds(GraphContext *graph, Node *node);
Rectangle SizeUnitButtonBounds(GraphContext *graph, Node *node);
// Exported for use by node_def.c draw_content callbacks
bool NodeOwnsMouse(GraphContext *graph, Node *node);
bool DrawNodeTextBox(GraphContext *graph, Node *node, Rectangle bounds, char *text, int capacity, int control_id);
bool DrawNodePathBox(GraphContext *graph, Node *node, Rectangle bounds, char *text, int capacity, int control_id,
                     PathPickerMode mode);
bool DrawNodeOptionButton(GraphContext *graph, Node *node, Rectangle bounds, const char *label, bool active,
                          float font_size);
