#include "raylib.h"
#include "raymath.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>

#define MAX_NODES 32
#define MAX_PORTS 64
#define MAX_LINKS 64
#define MAX_ITEMS 256
#define MAX_PATH_LENGTH 512

#define TOOLBAR_HEIGHT 52.0f
#define STATUS_HEIGHT 28.0f
#define NODE_HEADER_HEIGHT 34.0f
#define PORT_RADIUS 7.0f
#define TITLE_TEXT_SIZE 18
#define BODY_TEXT_SIZE 14
#define PORT_TEXT_SIZE 14
#define GUI_TEXT_SIZE 16
#define NODE_DETAIL_MIN_ZOOM 0.70f

typedef enum {
    PORT_TYPE_STRING,
    PORT_TYPE_STRING_LIST,
} PortDataType;

typedef enum {
    PORT_DIR_INPUT,
    PORT_DIR_OUTPUT,
} PortDirection;

typedef enum {
    NODE_DIRECTORY_LIST,
    NODE_STRING_MATCH,
    NODE_INSPECT_VIEW,
} NodeType;

typedef struct {
    int id;
    int node_id;
    char name[32];
    PortDataType data_type;
    PortDirection direction;
    Vector2 relative_pos;
} Port;

typedef struct {
    int from_port_id;
    int to_port_id;
} Link;

typedef struct {
    int id;
    NodeType type;
    char title[64];
    Rectangle bounds;
    int input_port_ids[4];
    int input_count;
    int output_port_ids[4];
    int output_count;
    bool is_dirty;
    bool text_editing;
    char parameter[128];
    char items[MAX_ITEMS][MAX_PATH_LENGTH];
    int item_count;
    int list_scroll;
    int list_active;
} Node;

typedef struct {
    Node nodes[MAX_NODES];
    int node_count;
    Port ports[MAX_PORTS];
    int port_count;
    Link links[MAX_LINKS];
    int link_count;
    Camera2D camera;
    int selected_node_id;
    int active_port_id;
    int dragging_node_id;
    Vector2 drag_offset;
    bool knife_active;
    Vector2 knife_start;
    bool add_menu_open;
    bool evaluation_error;
    char status[160];
} GraphContext;

static const Color COLOR_CANVAS = {18, 21, 28, 255};
static const Color COLOR_GRID_MINOR = {31, 36, 46, 255};
static const Color COLOR_GRID_MAJOR = {43, 49, 62, 255};
static const Color COLOR_NODE = {35, 40, 51, 255};
static const Color COLOR_NODE_HEADER = {47, 54, 68, 255};
static const Color COLOR_NODE_SELECTED = {92, 170, 255, 255};
static const Color COLOR_STRING = {242, 178, 74, 255};
static const Color COLOR_STRING_LIST = {91, 207, 151, 255};
static const Color COLOR_TEXT = {225, 230, 239, 255};
static const Color COLOR_MUTED = {142, 151, 168, 255};

typedef struct {
    Font title;
    Font body;
    Font gui;
    bool custom_loaded;
} InterfaceFonts;

static InterfaceFonts fonts = {0};

static void MarkDownstreamDirty(GraphContext *graph, int node_id);

static void LoadInterfaceFonts(void) {
    const char *candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    };
    const char *path = NULL;
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0]));
         i++) {
        if (FileExists(candidates[i])) {
            path = candidates[i];
            break;
        }
    }

    if (path) {
        fonts.title = LoadFontEx(path, TITLE_TEXT_SIZE, NULL, 0);
        fonts.body = LoadFontEx(path, BODY_TEXT_SIZE, NULL, 0);
        fonts.gui = LoadFontEx(path, GUI_TEXT_SIZE, NULL, 0);
        fonts.custom_loaded = fonts.title.texture.id > 0 &&
                              fonts.body.texture.id > 0 &&
                              fonts.gui.texture.id > 0;
    }
    if (!fonts.custom_loaded) {
        fonts.title = GetFontDefault();
        fonts.body = GetFontDefault();
        fonts.gui = GetFontDefault();
    }
    GuiSetFont(fonts.gui);
}

static void UnloadInterfaceFonts(void) {
    if (fonts.custom_loaded) {
        UnloadFont(fonts.title);
        UnloadFont(fonts.body);
        UnloadFont(fonts.gui);
    }
}

static void DrawInterfaceText(Font font, const char *text, float x, float y,
                              float size, Color color) {
    DrawTextEx(font, text, (Vector2){x, y}, size, 0, color);
}

static Node *FindNode(GraphContext *graph, int id) {
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == id)
            return &graph->nodes[i];
    }
    return NULL;
}

static Port *FindPort(GraphContext *graph, int id) {
    for (int i = 0; i < graph->port_count; i++) {
        if (graph->ports[i].id == id)
            return &graph->ports[i];
    }
    return NULL;
}

static Color PortColor(PortDataType type) {
    return type == PORT_TYPE_STRING_LIST ? COLOR_STRING_LIST : COLOR_STRING;
}

static int AddPort(GraphContext *graph, Node *node, const char *name,
                   PortDataType type, PortDirection direction, float y) {
    if (graph->port_count >= MAX_PORTS)
        return -1;
    int id = 1;
    for (int i = 0; i < graph->port_count; i++) {
        if (graph->ports[i].id >= id)
            id = graph->ports[i].id + 1;
    }
    Port *port = &graph->ports[graph->port_count++];
    *port = (Port){
        .id = id,
        .node_id = node->id,
        .data_type = type,
        .direction = direction,
        .relative_pos = {direction == PORT_DIR_INPUT ? 0.0f
                                                     : node->bounds.width,
                         y},
    };
    TextCopy(port->name, name);
    if (direction == PORT_DIR_INPUT)
        node->input_port_ids[node->input_count++] = port->id;
    else
        node->output_port_ids[node->output_count++] = port->id;
    return port->id;
}

static Node *AddNode(GraphContext *graph, NodeType type, Vector2 position) {
    if (graph->node_count >= MAX_NODES)
        return NULL;

    int id = 1;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id >= id)
            id = graph->nodes[i].id + 1;
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
        AddPort(graph, node, "Files", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT,
                112);
        break;
    case NODE_STRING_MATCH:
        TextCopy(node->title, "Regex Match");
        TextCopy(node->parameter, "\\.c$");
        node->bounds.height = 180;
        AddPort(graph, node, "Files", PORT_TYPE_STRING_LIST, PORT_DIR_INPUT,
                55);
        AddPort(graph, node, "Matches", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT,
                148);
        break;
    case NODE_INSPECT_VIEW:
        TextCopy(node->title, "Inspect");
        node->bounds.width = 330;
        node->bounds.height = 265;
        AddPort(graph, node, "Items", PORT_TYPE_STRING_LIST, PORT_DIR_INPUT,
                55);
        break;
    }
    return node;
}

static bool AddLink(GraphContext *graph, int from_id, int to_id) {
    Port *from = FindPort(graph, from_id);
    Port *to = FindPort(graph, to_id);
    if (!from || !to || from->direction != PORT_DIR_OUTPUT ||
        to->direction != PORT_DIR_INPUT || from->data_type != to->data_type ||
        from->node_id == to->node_id)
        return false;

    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (graph->links[i].to_port_id == to_id) {
            graph->links[i] = graph->links[--graph->link_count];
        }
    }
    if (graph->link_count >= MAX_LINKS)
        return false;
    graph->links[graph->link_count++] =
        (Link){.from_port_id = from_id, .to_port_id = to_id};
    Node *target = FindNode(graph, to->node_id);
    if (target)
        target->is_dirty = true;
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

static void RemoveLinkAt(GraphContext *graph, int index) {
    if (index < 0 || index >= graph->link_count)
        return;
    DirtyLinkTarget(graph, graph->links[index]);
    graph->links[index] = graph->links[--graph->link_count];
}

static int DetachInput(GraphContext *graph, int input_port_id) {
    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (graph->links[i].to_port_id == input_port_id) {
            int output_port_id = graph->links[i].from_port_id;
            RemoveLinkAt(graph, i);
            return output_port_id;
        }
    }
    return -1;
}

static void RemoveNode(GraphContext *graph, int node_id) {
    Node *node = FindNode(graph, node_id);
    if (!node)
        return;

    for (int i = graph->link_count - 1; i >= 0; i--) {
        Port *from = FindPort(graph, graph->links[i].from_port_id);
        Port *to = FindPort(graph, graph->links[i].to_port_id);
        if ((from && from->node_id == node_id) ||
            (to && to->node_id == node_id))
            RemoveLinkAt(graph, i);
    }
    for (int i = graph->port_count - 1; i >= 0; i--) {
        if (graph->ports[i].node_id == node_id) {
            memmove(&graph->ports[i], &graph->ports[i + 1],
                    (size_t)(graph->port_count - i - 1) * sizeof(Port));
            graph->port_count--;
        }
    }
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == node_id) {
            memmove(&graph->nodes[i], &graph->nodes[i + 1],
                    (size_t)(graph->node_count - i - 1) * sizeof(Node));
            graph->node_count--;
            break;
        }
    }

    graph->selected_node_id = -1;
    graph->dragging_node_id = -1;
    graph->active_port_id = -1;
    TextCopy(graph->status, "Node deleted");
}

static bool IsEditingText(GraphContext *graph) {
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].text_editing)
            return true;
    }
    return false;
}

static Vector2 PortWorldPosition(GraphContext *graph, Port *port) {
    Node *node = FindNode(graph, port->node_id);
    return node ? Vector2Add((Vector2){node->bounds.x, node->bounds.y},
                             port->relative_pos)
                : (Vector2){0};
}

static Vector2 PortScreenPosition(GraphContext *graph, Port *port) {
    return GetWorldToScreen2D(PortWorldPosition(graph, port), graph->camera);
}

static Rectangle NodeScreenBounds(GraphContext *graph, Node *node) {
    Vector2 top_left = GetWorldToScreen2D(
        (Vector2){node->bounds.x, node->bounds.y}, graph->camera);
    return (Rectangle){top_left.x, top_left.y,
                       node->bounds.width * graph->camera.zoom,
                       node->bounds.height * graph->camera.zoom};
}

static int PortAtMouse(GraphContext *graph, Vector2 mouse,
                       PortDirection direction) {
    for (int i = graph->port_count - 1; i >= 0; i--) {
        Port *port = &graph->ports[i];
        if (port->direction == direction &&
            CheckCollisionPointCircle(mouse, PortScreenPosition(graph, port),
                                      PORT_RADIUS + 5))
            return port->id;
    }
    return -1;
}

static int NodeAtMouse(GraphContext *graph, Vector2 mouse) {
    for (int i = graph->node_count - 1; i >= 0; i--) {
        if (CheckCollisionPointRec(mouse,
                                   NodeScreenBounds(graph, &graph->nodes[i])))
            return graph->nodes[i].id;
    }
    return -1;
}

static void BringNodeToFront(GraphContext *graph, int id) {
    for (int i = 0; i < graph->node_count - 1; i++) {
        if (graph->nodes[i].id == id) {
            Node node = graph->nodes[i];
            memmove(&graph->nodes[i], &graph->nodes[i + 1],
                    (size_t)(graph->node_count - i - 1) * sizeof(Node));
            graph->nodes[graph->node_count - 1] = node;
            return;
        }
    }
}

static void MarkDownstreamDirty(GraphContext *graph, int node_id) {
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

static Node *InputSource(GraphContext *graph, Node *node, int input_index) {
    if (input_index >= node->input_count)
        return NULL;
    int input_id = node->input_port_ids[input_index];
    for (int i = 0; i < graph->link_count; i++) {
        if (graph->links[i].to_port_id == input_id) {
            Port *source = FindPort(graph, graph->links[i].from_port_id);
            return source ? FindNode(graph, source->node_id) : NULL;
        }
    }
    return NULL;
}

static void EvaluateNode(GraphContext *graph, Node *node, int depth) {
    if (!node || !node->is_dirty || depth > MAX_NODES)
        return;

    Node *source = InputSource(graph, node, 0);
    if (source)
        EvaluateNode(graph, source, depth + 1);
    node->item_count = 0;

    if (node->type == NODE_DIRECTORY_LIST) {
        FilePathList files = LoadDirectoryFiles(node->parameter);
        for (unsigned int i = 0;
             i < files.count && node->item_count < MAX_ITEMS; i++) {
            if (!DirectoryExists(files.paths[i])) {
                TextCopy(node->items[node->item_count++], files.paths[i]);
            }
        }
        UnloadDirectoryFiles(files);
    } else if (source && node->type == NODE_STRING_MATCH) {
        regex_t expression;
        int compile_result =
            regcomp(&expression, node->parameter, REG_EXTENDED | REG_NOSUB);
        if (compile_result != 0) {
            char error[96] = {0};
            regerror(compile_result, &expression, error, sizeof(error));
            snprintf(graph->status, sizeof(graph->status), "Regex error: %s",
                     error);
            graph->evaluation_error = true;
        } else {
            for (int i = 0;
                 i < source->item_count && node->item_count < MAX_ITEMS; i++) {
                if (regexec(&expression, source->items[i], 0, NULL, 0) == 0) {
                    TextCopy(node->items[node->item_count++], source->items[i]);
                }
            }
            regfree(&expression);
        }
    } else if (source && node->type == NODE_INSPECT_VIEW) {
        node->item_count = source->item_count;
        for (int i = 0; i < source->item_count; i++)
            TextCopy(node->items[i], source->items[i]);
    }
    node->is_dirty = false;
}

static void RunGraph(GraphContext *graph) {
    graph->evaluation_error = false;
    for (int i = 0; i < graph->node_count; i++)
        EvaluateNode(graph, &graph->nodes[i], 0);
    int total = 0;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].type == NODE_INSPECT_VIEW)
            total += graph->nodes[i].item_count;
    }
    if (!graph->evaluation_error) {
        snprintf(graph->status, sizeof(graph->status),
                 "Graph evaluated - %d item%s visible in inspectors", total,
                 total == 1 ? "" : "s");
    }
}

static void SeedGraph(GraphContext *graph) {
    memset(graph, 0, sizeof(*graph));
    graph->camera.offset = (Vector2){0, TOOLBAR_HEIGHT};
    graph->camera.target = (Vector2){-90, -55};
    graph->camera.zoom = 1.0f;
    graph->selected_node_id = -1;
    graph->active_port_id = -1;
    graph->dragging_node_id = -1;
    TextCopy(graph->status,
             "Ready - drag an output port to a compatible input port");
    Node *directory = AddNode(graph, NODE_DIRECTORY_LIST, (Vector2){70, 120});
    Node *match = AddNode(graph, NODE_STRING_MATCH, (Vector2){410, 120});
    Node *inspect = AddNode(graph, NODE_INSPECT_VIEW, (Vector2){750, 70});
    if (directory && match && inspect) {
        AddLink(graph, directory->output_port_ids[0], match->input_port_ids[0]);
        AddLink(graph, match->output_port_ids[0], inspect->input_port_ids[0]);
    }
    RunGraph(graph);
}

static void DrawCanvasGrid(GraphContext *graph) {
    Rectangle canvas = {0, TOOLBAR_HEIGHT, (float)GetScreenWidth(),
                        (float)GetScreenHeight() - TOOLBAR_HEIGHT -
                            STATUS_HEIGHT};
    DrawRectangleRec(canvas, COLOR_CANVAS);
    Vector2 top_left =
        GetScreenToWorld2D((Vector2){canvas.x, canvas.y}, graph->camera);
    Vector2 bottom_right = GetScreenToWorld2D(
        (Vector2){canvas.width, canvas.y + canvas.height}, graph->camera);
    const float step = 32.0f;
    int first_x = (int)(top_left.x / step) - 1;
    int last_x = (int)(bottom_right.x / step) + 1;
    int first_y = (int)(top_left.y / step) - 1;
    int last_y = (int)(bottom_right.y / step) + 1;

    for (int i = first_x; i <= last_x; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){i * step, 0}, graph->camera);
        DrawLine((int)p.x, (int)canvas.y, (int)p.x,
                 (int)(canvas.y + canvas.height),
                 i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
    for (int i = first_y; i <= last_y; i++) {
        Vector2 p = GetWorldToScreen2D((Vector2){0, i * step}, graph->camera);
        DrawLine((int)canvas.x, (int)p.y, (int)(canvas.x + canvas.width),
                 (int)p.y, i % 4 == 0 ? COLOR_GRID_MAJOR : COLOR_GRID_MINOR);
    }
}

static void DrawConnection(Vector2 from, Vector2 to, Color color,
                           float thickness) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, 55, 180);
    Vector2 points[4] = {
        from, {from.x + tangent, from.y}, {to.x - tangent, to.y}, to};
    DrawSplineBezierCubic(points, 4, thickness, color);
}

static Vector2 CubicBezierPoint(Vector2 from, Vector2 to, float amount) {
    float tangent = Clamp(fabsf(to.x - from.x) * 0.5f, 55, 180);
    Vector2 control_a = {from.x + tangent, from.y};
    Vector2 control_b = {to.x - tangent, to.y};
    float inverse = 1.0f - amount;
    float inverse_squared = inverse * inverse;
    float amount_squared = amount * amount;
    return (Vector2){
        inverse_squared * inverse * from.x +
            3 * inverse_squared * amount * control_a.x +
            3 * inverse * amount_squared * control_b.x +
            amount_squared * amount * to.x,
        inverse_squared * inverse * from.y +
            3 * inverse_squared * amount * control_a.y +
            3 * inverse * amount_squared * control_b.y +
            amount_squared * amount * to.y,
    };
}

static bool SegmentsIntersect(Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
    Vector2 first = Vector2Subtract(b, a);
    Vector2 second = Vector2Subtract(d, c);
    Vector2 offset = Vector2Subtract(c, a);
    float denominator = first.x * second.y - first.y * second.x;
    if (fabsf(denominator) < 0.0001f)
        return false;
    float first_amount =
        (offset.x * second.y - offset.y * second.x) / denominator;
    float second_amount =
        (offset.x * first.y - offset.y * first.x) / denominator;
    return first_amount >= 0 && first_amount <= 1 && second_amount >= 0 &&
           second_amount <= 1;
}

static bool LinkIntersectsKnife(GraphContext *graph, Link link, Vector2 start,
                                Vector2 end) {
    Port *from_port = FindPort(graph, link.from_port_id);
    Port *to_port = FindPort(graph, link.to_port_id);
    if (!from_port || !to_port)
        return false;

    Vector2 from = PortScreenPosition(graph, from_port);
    Vector2 to = PortScreenPosition(graph, to_port);
    Vector2 previous = from;
    const int segments = 32;
    for (int i = 1; i <= segments; i++) {
        Vector2 current =
            CubicBezierPoint(from, to, (float)i / (float)segments);
        if (SegmentsIntersect(start, end, previous, current))
            return true;
        previous = current;
    }
    return false;
}

static int CutLinks(GraphContext *graph, Vector2 start, Vector2 end) {
    if (Vector2Distance(start, end) < 4.0f)
        return 0;
    int removed = 0;
    for (int i = graph->link_count - 1; i >= 0; i--) {
        if (LinkIntersectsKnife(graph, graph->links[i], start, end)) {
            RemoveLinkAt(graph, i);
            removed++;
        }
    }
    return removed;
}

static void DrawKnife(Vector2 start, Vector2 end) {
    Color glow = {255, 91, 105, 70};
    Color blade = {255, 220, 224, 255};
    DrawLineEx(start, end, 7, glow);
    DrawLineEx(start, end, 2, blade);
    DrawCircleV(start, 4, blade);
    DrawCircleV(end, 4, blade);
}

static void DrawNodeShell(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float radius = 0.08f;
    DrawRectangleRounded(bounds, radius, 8, COLOR_NODE);
    Rectangle header = {bounds.x, bounds.y, bounds.width,
                        NODE_HEADER_HEIGHT * graph->camera.zoom};
    DrawRectangleRounded(header, radius, 8, COLOR_NODE_HEADER);
    DrawRectangleRec((Rectangle){header.x, header.y + header.height * 0.55f,
                                 header.width, header.height * 0.45f},
                     COLOR_NODE_HEADER);
    DrawRectangleRoundedLinesEx(
        bounds, radius, 8, graph->selected_node_id == node->id ? 2.0f : 1.0f,
        graph->selected_node_id == node->id ? COLOR_NODE_SELECTED
                                            : (Color){66, 74, 91, 255});

    if (graph->camera.zoom >= NODE_DETAIL_MIN_ZOOM) {
        DrawInterfaceText(
            fonts.title, node->title, bounds.x + 14 * graph->camera.zoom,
            bounds.y + 7 * graph->camera.zoom, TITLE_TEXT_SIZE, COLOR_TEXT);
    }
}

static void DrawNodePorts(GraphContext *graph, Node *node) {
    for (int direction = PORT_DIR_INPUT; direction <= PORT_DIR_OUTPUT;
         direction++) {
        int count = direction == PORT_DIR_INPUT ? node->input_count
                                                : node->output_count;
        int *ids = direction == PORT_DIR_INPUT ? node->input_port_ids
                                               : node->output_port_ids;
        for (int i = 0; i < count; i++) {
            Port *port = FindPort(graph, ids[i]);
            Vector2 p = PortScreenPosition(graph, port);
            Color color = PortColor(port->data_type);
            DrawCircleV(p, PORT_RADIUS, color);
            DrawCircleLinesV(p, PORT_RADIUS + 2, Fade(color, 0.45f));
            if (graph->camera.zoom >= NODE_DETAIL_MIN_ZOOM) {
                int width = (int)MeasureTextEx(fonts.body, port->name,
                                               PORT_TEXT_SIZE, 0)
                                .x;
                float x =
                    direction == PORT_DIR_INPUT ? p.x + 13 : p.x - width - 13;
                DrawInterfaceText(fonts.body, port->name, x,
                                  p.y - PORT_TEXT_SIZE / 2, PORT_TEXT_SIZE,
                                  COLOR_MUTED);
            }
        }
    }
}

static void DrawNodeContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float zoom = graph->camera.zoom;
    if (zoom < NODE_DETAIL_MIN_ZOOM) {
        DrawInterfaceText(fonts.body, TextFormat("%d items", node->item_count),
                          bounds.x + 12 * zoom, bounds.y + 48 * zoom,
                          BODY_TEXT_SIZE, COLOR_MUTED);
        return;
    }

    if (node->type == NODE_DIRECTORY_LIST || node->type == NODE_STRING_MATCH) {
        const char *label =
            node->type == NODE_DIRECTORY_LIST ? "Path" : "Pattern";
        float label_y = node->type == NODE_DIRECTORY_LIST ? 50.0f : 76.0f;
        float text_box_y = node->type == NODE_DIRECTORY_LIST ? 68.0f : 94.0f;
        float count_y = node->type == NODE_DIRECTORY_LIST ? 114.0f : 150.0f;
        DrawInterfaceText(fonts.body, label, bounds.x + 14 * zoom,
                          bounds.y + label_y * zoom, BODY_TEXT_SIZE,
                          COLOR_MUTED);
        Rectangle text_box = {bounds.x + 14 * zoom,
                              bounds.y + text_box_y * zoom,
                              bounds.width - 28 * zoom, 30 * zoom};
        char before[128];
        TextCopy(before, node->parameter);
        if (GuiTextBox(text_box, node->parameter, sizeof(node->parameter),
                       node->text_editing))
            node->text_editing = !node->text_editing;
        if (strcmp(before, node->parameter) != 0) {
            node->is_dirty = true;
            MarkDownstreamDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status),
                     "Parameters changed - run graph to refresh");
        }
        DrawInterfaceText(fonts.body,
                          TextFormat("%d item%s", node->item_count,
                                     node->item_count == 1 ? "" : "s"),
                          bounds.x + 14 * zoom, bounds.y + count_y * zoom,
                          BODY_TEXT_SIZE, COLOR_MUTED);
    } else {
        DrawInterfaceText(fonts.body,
                          TextFormat("%d item%s", node->item_count,
                                     node->item_count == 1 ? "" : "s"),
                          bounds.x + 14 * zoom, bounds.y + 71 * zoom,
                          BODY_TEXT_SIZE, COLOR_MUTED);
        Rectangle list_bounds = {bounds.x + 14 * zoom, bounds.y + 93 * zoom,
                                 bounds.width - 28 * zoom,
                                 bounds.height - 107 * zoom};
        char *entries[MAX_ITEMS];
        for (int i = 0; i < node->item_count; i++)
            entries[i] = node->items[i];
        GuiListViewEx(list_bounds, entries, node->item_count,
                      &node->list_scroll, &node->list_active, NULL);
    }
}

static bool MouseOverNodeControl(GraphContext *graph, Node *node,
                                 Vector2 mouse) {
    if (!node || graph->camera.zoom < NODE_DETAIL_MIN_ZOOM)
        return false;
    Rectangle b = NodeScreenBounds(graph, node);
    float z = graph->camera.zoom;
    if (node->type == NODE_INSPECT_VIEW)
        return CheckCollisionPointRec(
            mouse, (Rectangle){b.x + 10 * z, b.y + 86 * z, b.width - 20 * z,
                               b.height - 96 * z});
    float control_y = node->type == NODE_STRING_MATCH ? 88.0f : 62.0f;
    return CheckCollisionPointRec(mouse,
                                  (Rectangle){b.x + 10 * z, b.y + control_y * z,
                                              b.width - 20 * z, 42 * z});
}

static void UpdateCanvas(GraphContext *graph) {
    Vector2 mouse = GetMousePosition();
    bool in_canvas =
        mouse.y > TOOLBAR_HEIGHT && mouse.y < GetScreenHeight() - STATUS_HEIGHT;
    bool panning =
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ||
        (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));

    if ((IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) &&
        graph->selected_node_id >= 0 && !IsEditingText(graph)) {
        RemoveNode(graph, graph->selected_node_id);
    }

    if (in_canvas && panning) {
        Vector2 delta = GetMouseDelta();
        graph->camera.target =
            Vector2Subtract(graph->camera.target,
                            Vector2Scale(delta, 1.0f / graph->camera.zoom));
    }

    float wheel = GetMouseWheelMove();
    if (in_canvas && wheel != 0) {
        Vector2 before = GetScreenToWorld2D(mouse, graph->camera);
        graph->camera.zoom =
            Clamp(graph->camera.zoom * (1.0f + wheel * 0.12f), 0.75f, 2.0f);
        Vector2 after = GetScreenToWorld2D(mouse, graph->camera);
        graph->camera.target =
            Vector2Add(graph->camera.target, Vector2Subtract(before, after));
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_canvas && !panning) {
        int output = PortAtMouse(graph, mouse, PORT_DIR_OUTPUT);
        int input = PortAtMouse(graph, mouse, PORT_DIR_INPUT);
        int node_id = NodeAtMouse(graph, mouse);
        Node *node = FindNode(graph, node_id);
        if (output >= 0) {
            graph->active_port_id = output;
        } else if (input >= 0) {
            graph->active_port_id = DetachInput(graph, input);
            if (graph->active_port_id >= 0)
                TextCopy(graph->status,
                         "Link detached - drop it on an input to reconnect");
        } else if (node && !MouseOverNodeControl(graph, node, mouse)) {
            graph->selected_node_id = node_id;
            Rectangle b = NodeScreenBounds(graph, node);
            if (mouse.y <= b.y + NODE_HEADER_HEIGHT * graph->camera.zoom) {
                Vector2 world_mouse = GetScreenToWorld2D(mouse, graph->camera);
                graph->dragging_node_id = node_id;
                graph->drag_offset = Vector2Subtract(
                    world_mouse, (Vector2){node->bounds.x, node->bounds.y});
                BringNodeToFront(graph, node_id);
            }
        } else if (!node) {
            graph->selected_node_id = -1;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas) {
        graph->knife_active = true;
        graph->knife_start = mouse;
        TextCopy(graph->status, "Knife active - release to cut connections");
    }
    if (graph->knife_active && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        int removed = CutLinks(graph, graph->knife_start, mouse);
        if (removed > 0) {
            snprintf(graph->status, sizeof(graph->status),
                     "Cut %d connection%s - run graph to refresh", removed,
                     removed == 1 ? "" : "s");
        } else {
            TextCopy(graph->status, "No connections cut");
        }
        graph->knife_active = false;
    }

    if (graph->dragging_node_id >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Node *node = FindNode(graph, graph->dragging_node_id);
        Vector2 world_mouse = GetScreenToWorld2D(mouse, graph->camera);
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
            if (input >= 0 && AddLink(graph, graph->active_port_id, input))
                TextCopy(graph->status,
                         "Connected - run graph to refresh downstream nodes");
            graph->active_port_id = -1;
        }
    }
}

static void DrawToolbar(GraphContext *graph) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, GUI_TEXT_SIZE);
    DrawRectangle(0, 0, GetScreenWidth(), (int)TOOLBAR_HEIGHT,
                  (Color){25, 29, 37, 255});
    DrawLine(0, (int)TOOLBAR_HEIGHT - 1, GetScreenWidth(),
             (int)TOOLBAR_HEIGHT - 1, (Color){59, 67, 82, 255});

    if (GuiButton((Rectangle){12, 10, 116, 32}, "#08# Add node"))
        graph->add_menu_open = !graph->add_menu_open;
    if (GuiButton((Rectangle){138, 10, 116, 32}, "#131# Run graph"))
        RunGraph(graph);
    if (GuiButton((Rectangle){264, 10, 82, 32}, "Reset"))
        SeedGraph(graph);

    DrawInterfaceText(fonts.body,
                      TextFormat("Zoom  %d%%", (int)(graph->camera.zoom * 100)),
                      GetScreenWidth() - 112, 18, BODY_TEXT_SIZE, COLOR_MUTED);

    if (graph->add_menu_open) {
        Rectangle menu = {12, TOOLBAR_HEIGHT + 4, 176, 106};
        DrawRectangleRec(menu, (Color){30, 35, 44, 255});
        DrawRectangleLinesEx(menu, 1, (Color){75, 84, 101, 255});
        const char *labels[] = {"Directory List", "Regex Match", "Inspect"};
        for (int i = 0; i < 3; i++) {
            if (GuiButton(
                    (Rectangle){18, TOOLBAR_HEIGHT + 10 + i * 31, 164, 27},
                    labels[i])) {
                Vector2 center =
                    GetScreenToWorld2D((Vector2){GetScreenWidth() * 0.5f,
                                                 GetScreenHeight() * 0.5f},
                                       graph->camera);
                AddNode(graph, (NodeType)i, center);
                graph->add_menu_open = false;
            }
        }
    }
}

static void DrawStatusBar(GraphContext *graph) {
    int y = GetScreenHeight() - (int)STATUS_HEIGHT;
    DrawRectangle(0, y, GetScreenWidth(), (int)STATUS_HEIGHT,
                  (Color){25, 29, 37, 255});
    DrawLine(0, y, GetScreenWidth(), y, (Color){59, 67, 82, 255});
    DrawCircle(14, y + 14, 4, COLOR_STRING_LIST);
    DrawInterfaceText(fonts.body, graph->status, 25, y + 6, BODY_TEXT_SIZE,
                      COLOR_MUTED);
    DrawInterfaceText(fonts.body, "RMB drag: knife   Wheel: zoom   Del: remove",
                      GetScreenWidth() - 360, y + 6, BODY_TEXT_SIZE,
                      COLOR_MUTED);
}

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
            if (from && to)
                DrawConnection(PortScreenPosition(&graph, from),
                               PortScreenPosition(&graph, to),
                               PortColor(from->data_type), 3.0f);
        }
        if (graph.active_port_id >= 0) {
            Port *port = FindPort(&graph, graph.active_port_id);
            if (port)
                DrawConnection(PortScreenPosition(&graph, port),
                               GetMousePosition(), PortColor(port->data_type),
                               3.0f);
        }

        for (int i = 0; i < graph.node_count; i++)
            DrawNodeShell(&graph, &graph.nodes[i]);
        for (int i = 0; i < graph.node_count; i++) {
            DrawNodeContent(&graph, &graph.nodes[i]);
            DrawNodePorts(&graph, &graph.nodes[i]);
        }
        if (graph.knife_active)
            DrawKnife(graph.knife_start, GetMousePosition());

        DrawToolbar(&graph);
        DrawStatusBar(&graph);
        EndDrawing();
    }

    UnloadInterfaceFonts();
    CloseWindow();
    return 0;
}
