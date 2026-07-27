#pragma once

#include "config.h"

#include "raylib.h"

#define MAX_NODES 32
#define MAX_PORTS 64
#define MAX_LINKS 64
#define MAX_ITEMS 256
#define MAX_PATH_LENGTH 512

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
    NODE_STRING_FILTER,
    NODE_EXEC,
    NODE_HTTP_REQUEST,
} NodeType;

typedef struct {
    int id;
    int node_id;
    char name[32];
    PortDataType data_type;
    PortDirection direction;
    Vector2 relative_pos;
    char items[MAX_ITEMS][MAX_PATH_LENGTH];
    int item_count;
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
    bool collapsed;
    char parameter[128];
    bool filter_case_sensitive;
    bool filter_whole_word;
    bool filter_use_regex;
    bool filter_exclude;
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
    bool open_dialog_open;
    char open_dialog_path[MAX_PATH_LENGTH];
    char current_file[MAX_PATH_LENGTH];
    bool evaluation_error;
    char status[160];
    int inspected_port_id; // -1 = none; output port whose items are shown in the inspector panel
    int inspect_scroll;
    int inspect_active;
    float application_scale;
} GraphContext;

static inline float ApplicationScale(const GraphContext *graph) {
    return graph->application_scale > 0.0f ? graph->application_scale : 1.0f;
}

static inline float ToolbarHeight(const GraphContext *graph) { return TOOLBAR_HEIGHT * ApplicationScale(graph); }

static inline float StatusHeight(const GraphContext *graph) { return STATUS_HEIGHT * ApplicationScale(graph); }

static inline float CanvasZoom(const GraphContext *graph) { return graph->camera.zoom * ApplicationScale(graph); }

static inline Camera2D CanvasCamera(const GraphContext *graph) {
    Camera2D camera = graph->camera;
    camera.offset.y = ToolbarHeight(graph);
    camera.zoom = CanvasZoom(graph);
    return camera;
}
