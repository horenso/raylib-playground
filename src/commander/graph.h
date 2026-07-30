#pragma once

#include "types.h"

Node *FindNode(GraphContext *graph, int id);
Port *FindPort(GraphContext *graph, int id);
Color PortColor(PortDataType type);
Color NodeStateColor(const Node *node);
Color PortStateColor(GraphContext *graph, Port *port);
int AddPort(GraphContext *graph, Node *node, const char *name, PortDataType type, PortDirection direction, float y);
Node *AddNode(GraphContext *graph, NodeType type, Vector2 position);
bool AddLink(GraphContext *graph, int from_id, int to_id);
void RemoveLinkAt(GraphContext *graph, int index);
int DetachInput(GraphContext *graph, int input_port_id);
void RemoveNode(GraphContext *graph, int node_id);
bool IsEditingText(GraphContext *graph);
void CloseNodeEditors(GraphContext *graph, int except_node_id);
void MarkNodeDirty(GraphContext *graph, int node_id);
void PropagateSchemas(GraphContext *graph);
Port *InputSourcePort(GraphContext *graph, Node *node, int input_index);
Port *NodeOutputPort(GraphContext *graph, Node *node, int output_index);
void BringNodeToFront(GraphContext *graph, int id);
Vector2 PortWorldPosition(GraphContext *graph, Port *port);
Vector2 PortScreenPosition(GraphContext *graph, Port *port);
float NodeConnectorSectionHeight(Node *node);
Rectangle NodeScreenBounds(GraphContext *graph, Node *node);
int PortAtMouse(GraphContext *graph, Vector2 mouse, PortDirection direction);
int NodeAtMouse(GraphContext *graph, Vector2 mouse);
bool NodeIntersectsKnife(GraphContext *graph, Node *node, Vector2 start, Vector2 end);
bool LinkIntersectsKnife(GraphContext *graph, Link link, Vector2 start, Vector2 end);
int CutLinks(GraphContext *graph, Vector2 start, Vector2 end);
int CutNodes(GraphContext *graph, Vector2 start, Vector2 end);
