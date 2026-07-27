#pragma once

#include "raylib.h"

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
    NODE_STRING_FILTER,
    NODE_BASH_EXEC,
    NODE_HTTP_REQUEST,
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
    bool collapsed;
    char parameter[128];
    bool filter_case_sensitive;
    bool filter_whole_word;
    bool filter_use_regex;
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
    bool open_dialog_open;
    char open_dialog_path[MAX_PATH_LENGTH];
    char current_file[MAX_PATH_LENGTH];
    bool evaluation_error;
    char status[160];
    int inspected_port_id; // -1 = none; output port whose items are shown in the inspector panel
    int inspect_scroll;
    int inspect_active;
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
