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
#define MAX_INSPECTOR_WINDOWS 8

typedef struct {
    int port_id;        // -1 = slot is empty
    Vector2 pos;        // top-left screen position (absolute, user-dragged)
    Vector2 size;       // (0,0) = use default; set once user resizes
    int scroll;
    int active;
    bool dragging;
    Vector2 drag_offset; // mouse-to-panel-origin delta at drag start
    bool resizing;
    Vector2 resize_start_mouse;
    Vector2 resize_start_size;
    bool scrollbar_dragging;
    float scrollbar_drag_start_y;
    int scrollbar_drag_start_scroll;
    int sort_field;     // -1 = no sort
    bool sort_asc;
} InspectorWindow;

typedef enum {
    VALUE_NONE,
    VALUE_STRING,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_SIZE,
    VALUE_DATETIME,
    VALUE_RECORD,
    VALUE_FLOAT,
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
        double floating;
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
    NODE_FILTER,
    NODE_EXEC,
    NODE_HTTP_REQUEST,
    NODE_INSERT,
    NODE_GET,
    NODE_LEGACY_NUMBER_FILTER,
    NODE_CSV,
} NodeType;

typedef enum {
    NUMBER_FILTER_EQ,
    NUMBER_FILTER_NEQ,
    NUMBER_FILTER_LT,
    NUMBER_FILTER_LTE,
    NUMBER_FILTER_GT,
    NUMBER_FILTER_GTE,
} NumberFilterOp;

typedef enum {
    FILE_SIZE_BYTES,
    FILE_SIZE_KB,
    FILE_SIZE_MB,
    FILE_SIZE_GB,
    FILE_SIZE_TB,
} FileSizeUnit;

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
    char number_parameter[128];
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
    NumberFilterOp number_filter_op;
    FileSizeUnit file_size_unit;
    bool field_dropdown_open;
    bool unit_dropdown_open;
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
    Vector2 add_menu_pos;  // screen position where the context menu was invoked
    bool open_dialog_open;
    char open_dialog_path[MAX_PATH_LENGTH];
    char current_file[MAX_PATH_LENGTH];
    bool evaluation_error;
    char status[160];
    InspectorWindow inspector_windows[MAX_INSPECTOR_WINDOWS];
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

static inline InspectorWindow *FindInspectorWindow(GraphContext *graph, int port_id) {
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        if (graph->inspector_windows[i].port_id == port_id) {
            return &graph->inspector_windows[i];
        }
    }
    return (InspectorWindow *)0;
}

static inline InspectorWindow *OpenInspectorWindow(GraphContext *graph, int port_id, Vector2 pos) {
    InspectorWindow *existing = FindInspectorWindow(graph, port_id);
    if (existing) {
        return existing;
    }
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        if (graph->inspector_windows[i].port_id <= 0) {
            graph->inspector_windows[i] = (InspectorWindow){
                .port_id = port_id,
                .pos = pos,
                .size = {0, 0},
                .scroll = 0,
                .active = -1,
                .sort_field = -1,
                .sort_asc = true,
            };
            return &graph->inspector_windows[i];
        }
    }
    return (InspectorWindow *)0;
}

static inline void CloseInspectorWindow(InspectorWindow *win) {
    if (win) {
        win->port_id = -1;
    }
}
