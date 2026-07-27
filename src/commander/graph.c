#include "graph.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

Node *FindNode(GraphContext *graph, int id) {
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == id) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

Port *FindPort(GraphContext *graph, int id) {
    for (int i = 0; i < graph->port_count; i++) {
        if (graph->ports[i].id == id) {
            return &graph->ports[i];
        }
    }
    return NULL;
}

Color PortColor(PortDataType type) { return type == PORT_TYPE_STRING_LIST ? COLOR_STRING_LIST : COLOR_STRING; }

int AddPort(GraphContext *graph, Node *node, const char *name, PortDataType type, PortDirection direction, float y) {
    if (graph->port_count >= MAX_PORTS) {
        return -1;
    }
    int id = 1;
    for (int i = 0; i < graph->port_count; i++) {
        if (graph->ports[i].id >= id) {
            id = graph->ports[i].id + 1;
        }
    }
    Port *port = &graph->ports[graph->port_count++];
    *port = (Port){
        .id = id,
        .node_id = node->id,
        .data_type = type,
        .direction = direction,
        .relative_pos = {direction == PORT_DIR_INPUT ? 0.0f : node->bounds.width, y},
    };
    TextCopy(port->name, name);
    if (direction == PORT_DIR_INPUT) {
        node->input_port_ids[node->input_count++] = port->id;
    } else {
        node->output_port_ids[node->output_count++] = port->id;
    }
    return port->id;
}

Node *AddNode(GraphContext *graph, NodeType type, Vector2 position) {
    if (graph->node_count >= MAX_NODES) {
        return NULL;
    }

    int id = 1;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id >= id) {
            id = graph->nodes[i].id + 1;
        }
    }
    Node *node = &graph->nodes[graph->node_count++];
    memset(node, 0, sizeof(*node));
    node->id = id;
    node->type = type;
    node->bounds = (Rectangle){position.x, position.y, 250, 148};
    node->is_dirty = true;
    node->list_active = -1;

    switch (type) {
    case NODE_DIRECTORY_LIST:
        TextCopy(node->title, "Directory List");
        TextCopy(node->parameter, ".");
        AddPort(graph, node, "Files", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT, 112);
        break;
    case NODE_STRING_FILTER:
        TextCopy(node->title, "Filter");
        TextCopy(node->parameter, "\\.c$");
        node->filter_use_regex = true;
        node->bounds.height = 210;
        AddPort(graph, node, "Files", PORT_TYPE_STRING_LIST, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Matches", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT, 178);
        break;
    case NODE_BASH_EXEC:
        TextCopy(node->title, "Bash Exec");
        TextCopy(node->parameter, "sort");
        node->bounds.height = 180;
        AddPort(graph, node, "Stdin", PORT_TYPE_STRING_LIST, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Stdout", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT, 148);
        break;
    case NODE_HTTP_REQUEST:
        TextCopy(node->title, "HTTP Request");
        TextCopy(node->parameter, "https://");
        node->bounds.height = 148;
        AddPort(graph, node, "Lines", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT, 112);
        break;
    }
    return node;
}

bool AddLink(GraphContext *graph, int from_id, int to_id) {
    Port *from = FindPort(graph, from_id);
    Port *to = FindPort(graph, to_id);
    if (!from || !to || from->direction != PORT_DIR_OUTPUT || to->direction != PORT_DIR_INPUT ||
        from->data_type != to->data_type || from->node_id == to->node_id) {
        return false;
    }

    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (graph->links[i].to_port_id == to_id) {
            graph->links[i] = graph->links[--graph->link_count];
        }
    }
    if (graph->link_count >= MAX_LINKS) {
        return false;
    }
    graph->links[graph->link_count++] = (Link){.from_port_id = from_id, .to_port_id = to_id};
    Node *target = FindNode(graph, to->node_id);
    if (target) {
        target->is_dirty = true;
    }
    return true;
}

static void DirtyLinkTarget(GraphContext *graph, Link link) {
    Port *to = FindPort(graph, link.to_port_id);
    Node *target = to ? FindNode(graph, to->node_id) : NULL;
    if (target && !target->is_dirty) {
        target->is_dirty = true;
        MarkDownstreamDirty(graph, target->id);
    }
}

void RemoveLinkAt(GraphContext *graph, int index) {
    if (index < 0 || index >= graph->link_count) {
        return;
    }
    DirtyLinkTarget(graph, graph->links[index]);
    graph->links[index] = graph->links[--graph->link_count];
}

int DetachInput(GraphContext *graph, int input_port_id) {
    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (graph->links[i].to_port_id == input_port_id) {
            int output_port_id = graph->links[i].from_port_id;
            RemoveLinkAt(graph, i);
            return output_port_id;
        }
    }
    return -1;
}

void RemoveNode(GraphContext *graph, int node_id) {
    Node *node = FindNode(graph, node_id);
    if (!node) {
        return;
    }

    for (int i = graph->link_count - 1; i >= 0; i--) {
        Port *from = FindPort(graph, graph->links[i].from_port_id);
        Port *to = FindPort(graph, graph->links[i].to_port_id);
        if ((from && from->node_id == node_id) || (to && to->node_id == node_id)) {
            RemoveLinkAt(graph, i);
        }
    }
    for (int i = graph->port_count - 1; i >= 0; i--) {
        if (graph->ports[i].node_id == node_id) {
            memmove(&graph->ports[i], &graph->ports[i + 1], (size_t)(graph->port_count - i - 1) * sizeof(Port));
            graph->port_count--;
        }
    }
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == node_id) {
            memmove(&graph->nodes[i], &graph->nodes[i + 1], (size_t)(graph->node_count - i - 1) * sizeof(Node));
            graph->node_count--;
            break;
        }
    }

    graph->selected_node_id = -1;
    graph->dragging_node_id = -1;
    graph->active_port_id = -1;
    TextCopy(graph->status, "Node deleted");
}

bool IsEditingText(GraphContext *graph) {
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].text_editing) {
            return true;
        }
    }
    return false;
}

void MarkDownstreamDirty(GraphContext *graph, int node_id) {
    for (int i = 0; i < graph->link_count; i++) {
        Port *from = FindPort(graph, graph->links[i].from_port_id);
        Port *to = FindPort(graph, graph->links[i].to_port_id);
        if (from && to && from->node_id == node_id) {
            Node *target = FindNode(graph, to->node_id);
            if (target && !target->is_dirty) {
                target->is_dirty = true;
                MarkDownstreamDirty(graph, target->id);
            }
        }
    }
}

Node *InputSource(GraphContext *graph, Node *node, int input_index) {
    if (input_index >= node->input_count) {
        return NULL;
    }
    int input_id = node->input_port_ids[input_index];
    for (int i = 0; i < graph->link_count; i++) {
        if (graph->links[i].to_port_id == input_id) {
            Port *source = FindPort(graph, graph->links[i].from_port_id);
            return source ? FindNode(graph, source->node_id) : NULL;
        }
    }
    return NULL;
}

void BringNodeToFront(GraphContext *graph, int id) {
    for (int i = 0; i < graph->node_count - 1; i++) {
        if (graph->nodes[i].id == id) {
            Node node = graph->nodes[i];
            memmove(&graph->nodes[i], &graph->nodes[i + 1], (size_t)(graph->node_count - i - 1) * sizeof(Node));
            graph->nodes[graph->node_count - 1] = node;
            return;
        }
    }
}

Vector2 PortWorldPosition(GraphContext *graph, Port *port) {
    Node *node = FindNode(graph, port->node_id);
    return node ? Vector2Add((Vector2){node->bounds.x, node->bounds.y}, port->relative_pos) : (Vector2){0};
}

Vector2 PortScreenPosition(GraphContext *graph, Port *port) {
    Node *node = FindNode(graph, port->node_id);
    if (node) {
        Rectangle b = NodeScreenBounds(graph, node);
        float mid_y = b.y + NODE_HEADER_HEIGHT * graph->camera.zoom * 0.5f;
        float x = port->direction == PORT_DIR_INPUT ? b.x : b.x + b.width;
        return (Vector2){x, mid_y};
    }
    return GetWorldToScreen2D(PortWorldPosition(graph, port), graph->camera);
}

Rectangle NodeScreenBounds(GraphContext *graph, Node *node) {
    Vector2 top_left = GetWorldToScreen2D((Vector2){node->bounds.x, node->bounds.y}, graph->camera);
    float h = node->collapsed ? NODE_HEADER_HEIGHT : node->bounds.height;
    return (Rectangle){top_left.x, top_left.y, node->bounds.width * graph->camera.zoom, h * graph->camera.zoom};
}

int PortAtMouse(GraphContext *graph, Vector2 mouse, PortDirection direction) {
    for (int i = graph->port_count - 1; i >= 0; i--) {
        Port *port = &graph->ports[i];
        if (port->direction == direction &&
            CheckCollisionPointCircle(mouse, PortScreenPosition(graph, port), PORT_RADIUS + 5)) {
            return port->id;
        }
    }
    return -1;
}

int NodeAtMouse(GraphContext *graph, Vector2 mouse) {
    for (int i = graph->node_count - 1; i >= 0; i--) {
        if (CheckCollisionPointRec(mouse, NodeScreenBounds(graph, &graph->nodes[i]))) {
            return graph->nodes[i].id;
        }
    }
    return -1;
}

static Vector2 CubicBezierPoint(Vector2 from, Vector2 to, float amount) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, 55, 180);
    Vector2 control_a = {from.x + tangent, from.y};
    Vector2 control_b = {to.x - tangent, to.y};
    float inverse = 1.0f - amount;
    float inverse_squared = inverse * inverse;
    float amount_squared = amount * amount;
    return (Vector2){
        inverse_squared * inverse * from.x + 3 * inverse_squared * amount * control_a.x +
            3 * inverse * amount_squared * control_b.x + amount_squared * amount * to.x,
        inverse_squared * inverse * from.y + 3 * inverse_squared * amount * control_a.y +
            3 * inverse * amount_squared * control_b.y + amount_squared * amount * to.y,
    };
}

static bool SegmentsIntersect(Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
    Vector2 first = Vector2Subtract(b, a);
    Vector2 second = Vector2Subtract(d, c);
    Vector2 offset = Vector2Subtract(c, a);
    float denominator = first.x * second.y - first.y * second.x;
    if (fabsf(denominator) < 0.0001f) {
        return false;
    }
    float first_amount = (offset.x * second.y - offset.y * second.x) / denominator;
    float second_amount = (offset.x * first.y - offset.y * first.x) / denominator;
    return first_amount >= 0 && first_amount <= 1 && second_amount >= 0 && second_amount <= 1;
}

static bool LinkIntersectsKnife(GraphContext *graph, Link link, Vector2 start, Vector2 end) {
    Port *from_port = FindPort(graph, link.from_port_id);
    Port *to_port = FindPort(graph, link.to_port_id);
    if (!from_port || !to_port) {
        return false;
    }

    Vector2 from = PortScreenPosition(graph, from_port);
    Vector2 to = PortScreenPosition(graph, to_port);
    Vector2 previous = from;
    const int segments = 32;
    for (int i = 1; i <= segments; i++) {
        Vector2 current = CubicBezierPoint(from, to, (float)i / (float)segments);
        if (SegmentsIntersect(start, end, previous, current)) {
            return true;
        }
        previous = current;
    }
    return false;
}

int CutLinks(GraphContext *graph, Vector2 start, Vector2 end) {
    if (Vector2Distance(start, end) < 4.0f) {
        return 0;
    }
    int removed = 0;
    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (LinkIntersectsKnife(graph, graph->links[i], start, end)) {
            RemoveLinkAt(graph, i);
            removed++;
        }
    }
    return removed;
}
