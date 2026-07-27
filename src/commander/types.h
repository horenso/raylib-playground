#pragma once

#include "config.h"

#include "raylib.h"

#define MAX_NODES 32
#define MAX_PORTS 64
#define MAX_LINKS 64
#define MAX_ITEMS 256
#define MAX_PATH_LENGTH 512
#define MAX_FIELDS 8
#define MAX_FIELD_NAME 32

typedef enum {
    VALUE_NONE,
    VALUE_STRING,
    VALUE_PATH,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_FILE_SIZE,
    VALUE_DATETIME,
    VALUE_FILE_KIND,
    VALUE_RECORD,
} ValueType;

// Ports always carry streams. The type describes one item in the stream;
// cardinality is no longer encoded in names such as STRING_LIST.
typedef ValueType PortDataType;
#define PORT_TYPE_STRING VALUE_STRING
#define PORT_TYPE_STRING_LIST VALUE_STRING

typedef struct {
    char name[MAX_FIELD_NAME];
    ValueType type;
    bool derived;
} FieldSchema;

typedef struct {
    FieldSchema fields[MAX_FIELDS];
    int field_count;
} RecordSchema;

typedef struct {
    ValueType type;
    union {
        char text[MAX_PATH_LENGTH];
        bool boolean;
        long long integer;
        unsigned long long file_size;
        long long datetime;
    } as;
} StreamValue;

typedef struct {
    StreamValue values[MAX_FIELDS];
} StreamItem;

typedef enum {
    PORT_DIR_INPUT,
    PORT_DIR_OUTPUT,
} PortDirection;

typedef enum {
    NODE_DIRECTORY_LIST,
    NODE_STRING_FILTER,
    NODE_EXEC,
    NODE_HTTP_REQUEST,
    NODE_INSERT,
    NODE_GET,
} NodeType;

typedef enum {
    INSERT_REPLACE_TEXT,
    INSERT_REPLACE_FILENAME,
    INSERT_REPLACE_EXTENSION,
} InsertOperation;

typedef enum {
    DIRECTORY_ENTRY_FILES,
    DIRECTORY_ENTRY_FOLDERS,
    DIRECTORY_ENTRY_BOTH,
} DirectoryEntryType;

typedef enum {
    INTERACTION_IDLE,
    INTERACTION_PANNING,
    INTERACTION_KNIFE,
    INTERACTION_DRAGGING_NODE,
    INTERACTION_LINKING,
} InteractionMode;

typedef struct {
    int id;
    int node_id;
    char name[32];
    PortDataType data_type;
    RecordSchema schema;
    bool schema_valid;
    PortDirection direction;
    Vector2 relative_pos;
    StreamItem items[MAX_ITEMS];
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
    bool evaluation_failed;
    bool has_evaluated;
    bool text_editing;
    int editing_control;
    char parameter[128];
    char secondary_parameter[128];
    char field_name[MAX_FIELD_NAME];
    char output_field_name[MAX_FIELD_NAME];
    InsertOperation insert_operation;
    bool schema_error;
    char schema_error_message[96];
    DirectoryEntryType directory_entry_type;
    bool directory_recursive;
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
    InteractionMode interaction_mode;
} GraphContext;

static inline float ApplicationScale(const GraphContext *graph) {
    return graph->application_scale > 0.0f ? graph->application_scale : 1.0f;
}

// Convert logical UI units to screen pixels. Canvas units additionally follow
// the user-controlled node zoom.
static inline float UiUnit(const GraphContext *graph) { return UI_BASE_PIXEL_SIZE * ApplicationScale(graph); }

static inline float UiSize(const GraphContext *graph, float units) { return units * UiUnit(graph); }

static inline float CanvasUnit(const GraphContext *graph) { return graph->camera.zoom * UiUnit(graph); }

static inline float CanvasSize(const GraphContext *graph, float units) { return units * CanvasUnit(graph); }

static inline float ToolbarHeight(const GraphContext *graph) { return UiSize(graph, TOOLBAR_HEIGHT); }

static inline float StatusHeight(const GraphContext *graph) { return UiSize(graph, STATUS_HEIGHT); }

static inline Camera2D CanvasCamera(const GraphContext *graph) {
    Camera2D camera = graph->camera;
    camera.offset.y = ToolbarHeight(graph);
    camera.zoom = CanvasUnit(graph);
    return camera;
}
