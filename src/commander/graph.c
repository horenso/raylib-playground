#include "graph.h"
#include "config.h"
#include "streams.h"

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

Color PortColor(PortDataType type) {
    switch (type) {
    case VALUE_RECORD:
        return (Color){102, 198, 160, 255};
    case VALUE_SIZE:
    case VALUE_INT:
        return (Color){180, 132, 230, 255};
    default:
        return COLOR_STRING_LIST;
    }
}

Color NodeStateColor(const Node *node) {
    if (node && (node->evaluation_failed || node->schema_error)) {
        return (Color){235, 87, 87, 255};
    }
    return node && !node->is_dirty ? COLOR_STRING_LIST : COLOR_STRING;
}

Color PortStateColor(GraphContext *graph, Port *port) {
    if (!port) {
        return COLOR_STRING;
    }

    Node *state_node = FindNode(graph, port->node_id);
    if (port->direction == PORT_DIR_INPUT) {
        for (int i = 0; i < graph->link_count; i++) {
            if (graph->links[i].to_port_id != port->id) {
                continue;
            }
            Port *source = FindPort(graph, graph->links[i].from_port_id);
            if (source) {
                state_node = FindNode(graph, source->node_id);
            }
            break;
        }
    }
    return NodeStateColor(state_node);
}

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
    memset(port, 0, sizeof(*port));
    port->id = id;
    port->node_id = node->id;
    port->data_type = type;
    port->schema_valid = type != VALUE_NONE;
    port->direction = direction;
    port->relative_pos = (Vector2){direction == PORT_DIR_INPUT ? 0.0f : node->bounds.width, y};
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
    node->bounds = (Rectangle){position.x, position.y, 250, 164};
    node->is_dirty = true;
    node->list_active = -1;
    node->editing_control = -1;

    switch (type) {
    case NODE_DIRECTORY_LIST:
        TextCopy(node->title, "Files");
        TextCopy(node->parameter, ".");
        node->directory_entry_type = DIRECTORY_ENTRY_FILES;
        node->bounds.height = 220;
        AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_OUTPUT, 112);
        Port *files = NodeOutputPort(graph, node, 0);
        if (files) {
            SchemaAddField(&files->schema, "path", VALUE_STRING, false);
            SchemaAddField(&files->schema, "name", VALUE_STRING, false);
            SchemaAddField(&files->schema, "type", VALUE_STRING, false);
            SchemaAddField(&files->schema, "size", VALUE_SIZE, false);
            SchemaAddField(&files->schema, "modified", VALUE_DATETIME, false);
        }
        break;
    case NODE_MATCH:
        TextCopy(node->title, "Match");
        TextCopy(node->parameter, "\\.c$");
        TextCopy(node->number_parameter, "0");
        node->filter_use_regex = true;
        node->number_filter_op = NUMBER_FILTER_GTE;
        node->bounds.height = 220;
        AddPort(graph, node, "Stream", VALUE_NONE, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Rows", VALUE_NONE, PORT_DIR_OUTPUT, 178);
        break;
    case NODE_EXEC:
        TextCopy(node->title, "Exec");
        TextCopy(node->parameter, "sort");
        node->bounds.width = 320;
        node->bounds.height = 184;
        AddPort(graph, node, "Stdin", VALUE_STRING, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Stdout", VALUE_STRING, PORT_DIR_OUTPUT, 148);
        AddPort(graph, node, "Stderr", VALUE_STRING, PORT_DIR_OUTPUT, 148);
        break;
    case NODE_HTTP_REQUEST:
        TextCopy(node->title, "HTTP Request");
        TextCopy(node->parameter, "https://");
        node->bounds.height = 164;
        AddPort(graph, node, "Lines", VALUE_STRING, PORT_DIR_OUTPUT, 112);
        break;
    case NODE_INSERT:
        TextCopy(node->title, "Insert");
        TextCopy(node->parameter, "IMG_");
        TextCopy(node->secondary_parameter, "holiday_");
        TextCopy(node->output_field_name, "destination");
        node->bounds.width = 300;
        node->bounds.height = 300;
        AddPort(graph, node, "Stream", VALUE_NONE, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Rows", VALUE_NONE, PORT_DIR_OUTPUT, 178);
        break;
    case NODE_GET:
        TextCopy(node->title, "Get");
        node->bounds.height = 150;
        AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_INPUT, 55);
        AddPort(graph, node, "Values", VALUE_NONE, PORT_DIR_OUTPUT, 112);
        break;
    case NODE_LEGACY_NUMBER_FILTER: // Serialized type 6; upgraded by LoadGraph().
        break;
    }
    PropagateSchemas(graph);
    return node;
}

static bool PortCanFeedNode(const Port *from, const Node *consumer) {
    if (!from || !consumer || !from->schema_valid) {
        return false;
    }
    switch (consumer->type) {
    case NODE_EXEC:
        return from->data_type == VALUE_STRING;
    case NODE_GET:
        return from->data_type == VALUE_RECORD;
    case NODE_MATCH:
    case NODE_INSERT:
        return true;
    default:
        return false;
    }
}

bool AddLink(GraphContext *graph, int from_id, int to_id) {
    Port *from = FindPort(graph, from_id);
    Port *to = FindPort(graph, to_id);
    Node *consumer = to ? FindNode(graph, to->node_id) : NULL;
    if (!from || !to || from->direction != PORT_DIR_OUTPUT || to->direction != PORT_DIR_INPUT ||
        from->node_id == to->node_id || !PortCanFeedNode(from, consumer)) {
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
    PropagateSchemas(graph);
    MarkNodeDirty(graph, to->node_id);
    return true;
}

static void DirtyLinkTarget(GraphContext *graph, Link link) {
    Port *to = FindPort(graph, link.to_port_id);
    if (to) {
        MarkNodeDirty(graph, to->node_id);
    }
}

void RemoveLinkAt(GraphContext *graph, int index) {
    if (index < 0 || index >= graph->link_count) {
        return;
    }
    DirtyLinkTarget(graph, graph->links[index]);
    graph->links[index] = graph->links[--graph->link_count];
    PropagateSchemas(graph);
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

void CloseNodeEditors(GraphContext *graph, int except_node_id) {
    for (int i = 0; i < graph->node_count; i++) {
        Node *node = &graph->nodes[i];
        if (node->id != except_node_id) {
            node->text_editing = false;
            node->editing_control = -1;
        }
    }
}

static void MarkDirtyRecursive(GraphContext *graph, int node_id, bool visited[MAX_NODES]) {
    int node_index = -1;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == node_id) {
            node_index = i;
            break;
        }
    }
    if (node_index < 0 || visited[node_index]) {
        return;
    }
    visited[node_index] = true;

    graph->nodes[node_index].is_dirty = true;
    graph->nodes[node_index].evaluation_failed = false;

    for (int i = 0; i < graph->link_count; i++) {
        Port *from = FindPort(graph, graph->links[i].from_port_id);
        Port *to = FindPort(graph, graph->links[i].to_port_id);
        if (from && to && from->node_id == node_id) {
            MarkDirtyRecursive(graph, to->node_id, visited);
        }
    }
}

void MarkNodeDirty(GraphContext *graph, int node_id) {
    bool visited[MAX_NODES] = {0};
    MarkDirtyRecursive(graph, node_id, visited);
    PropagateSchemas(graph);
}

static void SetSchemaError(Node *node, const char *message) {
    node->schema_error = true;
    TextCopy(node->schema_error_message, message);
}

static const FieldSchema *ChooseDefaultField(Node *node, const Port *input) {
    if (input->data_type != VALUE_RECORD) {
        if (!node->field_name[0]) {
            TextCopy(node->field_name, "Item");
        }
        return NULL;
    }
    if (!node->field_name[0]) {
        int preferred = SchemaFieldIndex(&input->schema, node->type == NODE_INSERT ? "path" : "name");
        if (preferred < 0 || !NodeFieldIsSelectable(node, input->schema.fields[preferred].type)) {
            preferred = -1;
            for (int i = 0; i < input->schema.field_count; i++) {
                if (NodeFieldIsSelectable(node, input->schema.fields[i].type)) {
                    preferred = i;
                    break;
                }
            }
        }
        if (preferred >= 0 && preferred < input->schema.field_count) {
            TextCopy(node->field_name, input->schema.fields[preferred].name);
        }
    }
    int index = SchemaFieldIndex(&input->schema, node->field_name);
    return index >= 0 ? &input->schema.fields[index] : NULL;
}

bool NodeFieldIsSelectable(const Node *node, ValueType type) {
    if (!node) {
        return false;
    }
    switch (node->type) {
    case NODE_MATCH:
        return ValueTypeIsText(type) || ValueTypeIsNumeric(type);
    case NODE_INSERT:
        return ValueTypeIsText(type);
    case NODE_GET:
        return true;
    default:
        return false;
    }
}

bool NodeUsesFieldSelector(const Node *node) {
    return node &&
           (node->type == NODE_MATCH || node->type == NODE_INSERT || node->type == NODE_GET);
}

int CollectNodeFieldOptions(GraphContext *graph, Node *node, const char **options, int capacity) {
    if (!graph || !NodeUsesFieldSelector(node) || !options || capacity <= 0) {
        return 0;
    }
    Port *input = InputSourcePort(graph, node, 0);
    if (!input || !input->schema_valid) {
        return 0;
    }
    if (input->data_type != VALUE_RECORD) {
        if (NodeFieldIsSelectable(node, input->data_type)) {
            options[0] = "Item";
            return 1;
        }
        return 0;
    }

    int count = 0;
    for (int i = 0; i < input->schema.field_count && count < capacity; i++) {
        if (NodeFieldIsSelectable(node, input->schema.fields[i].type)) {
            options[count++] = input->schema.fields[i].name;
        }
    }
    return count;
}

ValueType NodeSelectedFieldType(GraphContext *graph, Node *node) {
    if (!graph || !node || node->input_count <= 0) {
        return VALUE_NONE;
    }
    Port *input = FindPort(graph, node->input_port_ids[0]);
    if (!input || !input->schema_valid) {
        return VALUE_NONE;
    }
    if (input->data_type != VALUE_RECORD) {
        return TextIsEqual(node->field_name, "Item") ? input->data_type : VALUE_NONE;
    }
    int index = SchemaFieldIndex(&input->schema, node->field_name);
    return index >= 0 ? input->schema.fields[index].type : VALUE_NONE;
}

void PropagateSchemas(GraphContext *graph) {
    // Source schemas are intrinsic. Everything else is recomputed without
    // evaluating runtime values.
    for (int i = 0; i < graph->node_count; i++) {
        Node *node = &graph->nodes[i];
        node->schema_error = false;
        node->schema_error_message[0] = '\0';
        for (int p = 0; p < node->input_count; p++) {
            Port *input = FindPort(graph, node->input_port_ids[p]);
            if (input) {
                input->schema_valid = false;
                input->data_type = node->type == NODE_GET    ? VALUE_RECORD
                                   : node->type == NODE_EXEC ? VALUE_STRING
                                                             : VALUE_NONE;
                memset(&input->schema, 0, sizeof(input->schema));
            }
        }
        if (node->type == NODE_MATCH ||
            node->type == NODE_INSERT || node->type == NODE_GET) {
            Port *output = NodeOutputPort(graph, node, 0);
            if (output) {
                output->schema_valid = false;
                output->data_type = VALUE_NONE;
                memset(&output->schema, 0, sizeof(output->schema));
            }
        }
    }

    for (int pass = 0; pass < graph->node_count; pass++) {
        bool changed = false;
        for (int i = 0; i < graph->link_count; i++) {
            Port *source = FindPort(graph, graph->links[i].from_port_id);
            Port *input = FindPort(graph, graph->links[i].to_port_id);
            if (source && input && source->schema_valid && !input->schema_valid) {
                input->data_type = source->data_type;
                input->schema = source->schema;
                input->schema_valid = true;
                changed = true;
            }
        }

        for (int i = 0; i < graph->node_count; i++) {
            Node *node = &graph->nodes[i];
            if (node->type != NODE_MATCH &&
                node->type != NODE_INSERT && node->type != NODE_GET) {
                continue;
            }
            Port *input = FindPort(graph, node->input_port_ids[0]);
            Port *output = NodeOutputPort(graph, node, 0);
            if (!input || !output || !input->schema_valid) {
                continue;
            }
            const FieldSchema *selected = ChooseDefaultField(node, input);
            ValueType selected_type = input->data_type;
            if (input->data_type == VALUE_RECORD) {
                if (!selected) {
                    SetSchemaError(node, "Selected field is not in the input schema");
                    continue;
                }
                selected_type = selected->type;
            } else if (!TextIsEqual(node->field_name, "Item")) {
                SetSchemaError(node, "Primitive streams expose only the Item field");
                continue;
            }

            if (node->type == NODE_MATCH && !ValueTypeIsText(selected_type) && !ValueTypeIsNumeric(selected_type)) {
                SetSchemaError(node, "Match requires a String or numeric field");
                continue;
            }
            if (node->type == NODE_INSERT) {
                if (!ValueTypeIsText(selected_type)) {
                    SetSchemaError(node, "Insert currently supports text-like fields");
                    continue;
                }
                if (!node->output_field_name[0]) {
                    SetSchemaError(node, "Insert needs a new field name");
                    continue;
                }
                if (input->data_type == VALUE_RECORD &&
                    SchemaFieldIndex(&input->schema, node->output_field_name) >= 0) {
                    SetSchemaError(node, "Insert cannot overwrite an existing field");
                    continue;
                }
                output->data_type = VALUE_RECORD;
                output->schema = input->schema;
                if (input->data_type != VALUE_RECORD) {
                    memset(&output->schema, 0, sizeof(output->schema));
                    SchemaAddField(&output->schema, "Item", input->data_type, false);
                }
                if (!SchemaAddField(&output->schema, node->output_field_name, selected_type, true)) {
                    SetSchemaError(node, "Record has no room for another field");
                    continue;
                }
            } else if (node->type == NODE_GET) {
                output->data_type = selected_type;
                memset(&output->schema, 0, sizeof(output->schema));
            } else {
                output->data_type = input->data_type;
                output->schema = input->schema;
            }
            if (!output->schema_valid) {
                output->schema_valid = true;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
}

Port *InputSourcePort(GraphContext *graph, Node *node, int input_index) {
    if (input_index < 0 || input_index >= node->input_count) {
        return NULL;
    }
    int input_id = node->input_port_ids[input_index];
    for (int i = 0; i < graph->link_count; i++) {
        if (graph->links[i].to_port_id == input_id) {
            return FindPort(graph, graph->links[i].from_port_id);
        }
    }
    return NULL;
}

Port *NodeOutputPort(GraphContext *graph, Node *node, int output_index) {
    if (!node || output_index < 0 || output_index >= node->output_count) {
        return NULL;
    }
    return FindPort(graph, node->output_port_ids[output_index]);
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

float NodeConnectorSectionHeight(Node *node) {
    int rows = node->input_count > node->output_count ? node->input_count : node->output_count;
    if (rows < 1) {
        rows = 1;
    }
    return NODE_CONNECTOR_HEIGHT + (rows - 1) * NODE_CONNECTOR_ROW_HEIGHT;
}

Vector2 PortScreenPosition(GraphContext *graph, Port *port) {
    Node *node = FindNode(graph, port->node_id);
    if (node) {
        Rectangle b = NodeScreenBounds(graph, node);
        int count = port->direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *ids = port->direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        int index = 0;
        while (index < count && ids[index] != port->id) {
            index++;
        }
        float connector_top = b.y + b.height - CanvasSize(graph, NodeConnectorSectionHeight(node));
        float y = connector_top + CanvasSize(graph, 15.0f + index * NODE_CONNECTOR_ROW_HEIGHT);
        float x = port->direction == PORT_DIR_INPUT ? b.x : b.x + b.width;
        return (Vector2){x, y};
    }
    return GetWorldToScreen2D(PortWorldPosition(graph, port), CanvasCamera(graph));
}

Rectangle NodeScreenBounds(GraphContext *graph, Node *node) {
    Vector2 top_left = GetWorldToScreen2D((Vector2){node->bounds.x, node->bounds.y}, CanvasCamera(graph));
    return (Rectangle){
        top_left.x,
        top_left.y,
        CanvasSize(graph, node->bounds.width),
        CanvasSize(graph, node->bounds.height),
    };
}

int PortAtMouse(GraphContext *graph, Vector2 mouse, PortDirection direction) {
    for (int node_index = graph->node_count - 1; node_index >= 0; node_index--) {
        Node *node = &graph->nodes[node_index];
        int count = direction == PORT_DIR_INPUT ? node->input_count : node->output_count;
        int *port_ids = direction == PORT_DIR_INPUT ? node->input_port_ids : node->output_port_ids;
        for (int port_index = 0; port_index < count; port_index++) {
            Port *port = FindPort(graph, port_ids[port_index]);
            if (port && CheckCollisionPointCircle(mouse, PortScreenPosition(graph, port),
                                                  CanvasSize(graph, PORT_RADIUS + 5.0f))) {
                return port->id;
            }
        }
        if (CheckCollisionPointRec(mouse, NodeScreenBounds(graph, node))) {
            return -1;
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

static Vector2 CubicBezierPoint(GraphContext *graph, Vector2 from, Vector2 to, float amount) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, UiSize(graph, 55.0f), UiSize(graph, 180.0f));
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

bool NodeIntersectsKnife(GraphContext *graph, Node *node, Vector2 start, Vector2 end) {
    if (!node || Vector2Distance(start, end) < UiSize(graph, 4.0f)) {
        return false;
    }
    Rectangle bounds = NodeScreenBounds(graph, node);
    if (CheckCollisionPointRec(start, bounds) || CheckCollisionPointRec(end, bounds)) {
        return true;
    }

    Vector2 top_left = {bounds.x, bounds.y};
    Vector2 top_right = {bounds.x + bounds.width, bounds.y};
    Vector2 bottom_left = {bounds.x, bounds.y + bounds.height};
    Vector2 bottom_right = {bounds.x + bounds.width, bounds.y + bounds.height};
    return SegmentsIntersect(start, end, top_left, top_right) ||
           SegmentsIntersect(start, end, top_right, bottom_right) ||
           SegmentsIntersect(start, end, bottom_right, bottom_left) ||
           SegmentsIntersect(start, end, bottom_left, top_left);
}

bool LinkIntersectsKnife(GraphContext *graph, Link link, Vector2 start, Vector2 end) {
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
        Vector2 current = CubicBezierPoint(graph, from, to, (float)i / (float)segments);
        if (SegmentsIntersect(start, end, previous, current)) {
            return true;
        }
        previous = current;
    }
    return false;
}

int CutLinks(GraphContext *graph, Vector2 start, Vector2 end) {
    if (Vector2Distance(start, end) < UiSize(graph, 4.0f)) {
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

int CutNodes(GraphContext *graph, Vector2 start, Vector2 end) {
    if (Vector2Distance(start, end) < UiSize(graph, 4.0f)) {
        return 0;
    }
    int removed = 0;
    for (int i = graph->node_count - 1; i >= 0; i--) {
        if (NodeIntersectsKnife(graph, &graph->nodes[i], start, end)) {
            int node_id = graph->nodes[i].id;
            RemoveNode(graph, node_id);
            removed++;
        }
    }
    return removed;
}
