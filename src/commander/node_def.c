// node_def.c – global NodeDef registry.
//
// Each NodeType maps to one NodeDef whose implementation lives in
// src/commander/nodes/<name>.c. Adding a new node type means:
//   1. Create src/commander/nodes/<name>.c with a `const NodeDef k<Name>NodeDef`.
//   2. Add an extern declaration and a registry entry below.

#include "node_def.h"

#include "raylib.h" // TextIsEqual

// Per-node definitions — implemented in nodes/*.c
extern const NodeDef kFilesNodeDef;
extern const NodeDef kMatchNodeDef;
extern const NodeDef kExecNodeDef;
extern const NodeDef kHttpNodeDef;
extern const NodeDef kInsertNodeDef;
extern const NodeDef kGetNodeDef;

// The registry is indexed by NodeType enum value.
// NODE_LEGACY_NUMBER_FILTER has a NULL entry — it cannot be created
// interactively and is upgraded to NODE_MATCH by LoadGraph().
static const NodeDef *NODE_REGISTRY[] = {
    [NODE_DIRECTORY_LIST] = &kFilesNodeDef, [NODE_MATCH] = &kMatchNodeDef,   [NODE_EXEC] = &kExecNodeDef,
    [NODE_HTTP_REQUEST] = &kHttpNodeDef,    [NODE_INSERT] = &kInsertNodeDef, [NODE_GET] = &kGetNodeDef,
    [NODE_LEGACY_NUMBER_FILTER] = NULL,
};

const NodeDef *GetNodeDef(NodeType type) {
    int t = (int)type;
    if (t < 0 || t >= (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0]))) {
        return NULL;
    }
    return NODE_REGISTRY[t];
}

int NodeTypeFromName(const char *name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (int t = 0; t < (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0])); t++) {
        if (NODE_REGISTRY[t] && NODE_REGISTRY[t]->name && TextIsEqual(NODE_REGISTRY[t]->name, name)) {
            return t;
        }
    }
    return -1;
}

#include "streams.h"

#include "raylib.h"

#include <ctype.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ============================================================
// Shared evaluation helpers (previously static in evaluate.c)
// ============================================================

static void AppendPrimitiveText(Port *port, const char *text, size_t length) {
    if (!port || port->item_count >= MAX_ITEMS) {
        return;
    }
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        length--;
    }
    char copy[MAX_PATH_LENGTH];
    size_t copy_length = length < sizeof(copy) - 1 ? length : sizeof(copy) - 1;
    memcpy(copy, text, copy_length);
    copy[copy_length] = '\0';
    SetTextValue(&port->items[port->item_count++].values[0], port->data_type, copy);
}

static void ReadLines(FILE *stream, Port *port) {
    char line[MAX_PATH_LENGTH];
    while (port && port->item_count < MAX_ITEMS && fgets(line, sizeof(line), stream)) {
        AppendPrimitiveText(port, line, strlen(line));
    }
}

static void AppendFileRecord(Node *node, Port *output, const char *path) {
    if (!output || output->item_count >= MAX_ITEMS) {
        return;
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        return;
    }
    bool is_folder = S_ISDIR(info.st_mode);
    bool include = node->directory_entry_type == DIRECTORY_ENTRY_BOTH ||
                   (node->directory_entry_type == DIRECTORY_ENTRY_FOLDERS && is_folder) ||
                   (node->directory_entry_type == DIRECTORY_ENTRY_FILES && !is_folder);
    if (!include) {
        return;
    }
    StreamItem *item = &output->items[output->item_count++];
    memset(item, 0, sizeof(*item));
    SetTextValue(&item->values[0], VALUE_STRING, path);
    SetTextValue(&item->values[1], VALUE_STRING, GetFileName(path));
    SetTextValue(&item->values[2], VALUE_STRING, is_folder ? "folder" : "file");
    item->values[3].type = VALUE_SIZE;
    item->values[3].as.file_size = is_folder ? 0 : (unsigned long long)info.st_size;
    item->values[4].type = VALUE_DATETIME;
    item->values[4].as.datetime = (long long)info.st_mtime;
}

static void AppendDirectoryEntries(Node *node, Port *output, FilePathList entries) {
    for (unsigned int i = 0; output && i < entries.count && output->item_count < MAX_ITEMS; i++) {
        AppendFileRecord(node, output, entries.paths[i]);
    }
}

static bool TextMatches(const Node *node, const char *text, regex_t *expression) {
    if (node->filter_use_regex) {
        return expression && regexec(expression, text, 0, NULL, 0) == 0;
    }
    const char *needle = node->parameter;
    int text_length = (int)strlen(text);
    int needle_length = (int)strlen(needle);
    if (needle_length == 0) {
        return true;
    }
    for (int i = 0; i <= text_length - needle_length; i++) {
        bool equal = true;
        for (int j = 0; j < needle_length && equal; j++) {
            char left = text[i + j];
            char right = needle[j];
            if (!node->filter_case_sensitive) {
                left = (char)tolower((unsigned char)left);
                right = (char)tolower((unsigned char)right);
            }
            equal = left == right;
        }
        if (!equal) {
            continue;
        }
        if (!node->filter_whole_word) {
            return true;
        }
        bool left_ok = i == 0 || !isalnum((unsigned char)text[i - 1]);
        bool right_ok = i + needle_length >= text_length || !isalnum((unsigned char)text[i + needle_length]);
        if (left_ok && right_ok) {
            return true;
        }
    }
    return false;
}

static bool EvaluateWhere(GraphContext *graph, Node *node, Port *source, Port *output) {
    regex_t expression;
    bool regex_ready = false;
    if (node->filter_use_regex) {
        int flags = REG_EXTENDED | REG_NOSUB;
        if (!node->filter_case_sensitive) {
            flags |= REG_ICASE;
        }
        int result = regcomp(&expression, node->parameter, flags);
        if (result != 0) {
            char error[96] = {0};
            regerror(result, &expression, error, sizeof(error));
            snprintf(graph->status, sizeof(graph->status), "Regex error: %s", error);
            graph->evaluation_error = true;
            return false;
        }
        regex_ready = true;
    }
    for (int i = 0; output && i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        const StreamValue *value = ItemFieldValue(source, &source->items[i], node->field_name);
        const char *text = value && ValueTypeIsText(value->type) ? value->as.text : "";
        bool matched = TextMatches(node, text, regex_ready ? &expression : NULL);
        if (matched != node->filter_exclude) {
            output->items[output->item_count++] = source->items[i];
        }
    }
    if (regex_ready) {
        regfree(&expression);
    }
    return true;
}

static long double NumericValue(const StreamValue *value) {
    if (!value) {
        return 0;
    }
    if (value->type == VALUE_INT) {
        return value->as.integer;
    }
    if (value->type == VALUE_DATETIME) {
        return value->as.datetime;
    }
    if (value->type == VALUE_SIZE) {
        return value->as.file_size;
    }
    return 0;
}

static bool DateTimeTextConsumed(const char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    return *text == '\0';
}

static bool ParseDateTimeThreshold(const char *text, long double *threshold) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, consumed = 0;
    int matched = sscanf(text, "%d-%d-%d %d:%d%n", &year, &month, &day, &hour, &minute, &consumed);
    if (matched != 5 || !DateTimeTextConsumed(text + consumed)) {
        hour = 0;
        minute = 0;
        consumed = 0;
        matched = sscanf(text, "%d-%d-%d%n", &year, &month, &day, &consumed);
        if (matched != 3 || !DateTimeTextConsumed(text + consumed)) {
            return false;
        }
    }
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59) {
        return false;
    }
    struct tm local = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_isdst = -1,
    };
    time_t timestamp = mktime(&local);
    if (timestamp == (time_t)-1 || local.tm_year != year - 1900 || local.tm_mon != month - 1 || local.tm_mday != day ||
        local.tm_hour != hour || local.tm_min != minute) {
        return false;
    }
    *threshold = (long double)timestamp;
    return true;
}

static bool EvaluateNumberFilter(GraphContext *graph, Node *node, Port *source, Port *output) {
    ValueType field_type = NodeSelectedFieldType(graph, node);
    long double threshold = 0;
    if (field_type == VALUE_DATETIME) {
        if (!ParseDateTimeThreshold(node->number_parameter, &threshold)) {
            snprintf(graph->status, sizeof(graph->status), "Match: use date YYYY-MM-DD or YYYY-MM-DD HH:MM");
            graph->evaluation_error = true;
            return false;
        }
    } else {
        char *end;
        threshold = strtold(node->number_parameter, &end);
        if (*node->number_parameter == '\0' || *end != '\0') {
            snprintf(graph->status, sizeof(graph->status), "Match: invalid threshold '%s'", node->number_parameter);
            graph->evaluation_error = true;
            return false;
        }
    }
    if (field_type == VALUE_SIZE) {
        static const unsigned long long multipliers[] = {1ULL, 1024ULL, 1024ULL * 1024ULL, 1024ULL * 1024ULL * 1024ULL,
                                                         1024ULL * 1024ULL * 1024ULL * 1024ULL};
        int unit = node->file_size_unit >= FILE_SIZE_BYTES && node->file_size_unit <= FILE_SIZE_TB
                       ? node->file_size_unit
                       : FILE_SIZE_BYTES;
        threshold *= multipliers[unit];
    }
    for (int i = 0; output && i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        const StreamValue *value = ItemFieldValue(source, &source->items[i], node->field_name);
        if (!value || !ValueTypeIsNumeric(value->type)) {
            continue;
        }
        long double v = NumericValue(value);
        bool matched = false;
        switch (node->number_filter_op) {
        case NUMBER_FILTER_EQ:
            matched = v == threshold;
            break;
        case NUMBER_FILTER_NEQ:
            matched = v != threshold;
            break;
        case NUMBER_FILTER_LT:
            matched = v < threshold;
            break;
        case NUMBER_FILTER_LTE:
            matched = v <= threshold;
            break;
        case NUMBER_FILTER_GT:
            matched = v > threshold;
            break;
        case NUMBER_FILTER_GTE:
            matched = v >= threshold;
            break;
        }
        if (matched != node->filter_exclude) {
            output->items[output->item_count++] = source->items[i];
        }
    }
    return true;
}

static void ReplaceAll(const char *input, const char *find, const char *replacement, char *output, size_t output_size) {
    size_t used = 0;
    size_t find_length = strlen(find);
    if (find_length == 0) {
        TextCopy(output, input);
        return;
    }
    while (*input && used + 1 < output_size) {
        const char *match = strstr(input, find);
        if (!match) {
            size_t rest = strlen(input);
            if (rest > output_size - used - 1) {
                rest = output_size - used - 1;
            }
            memcpy(output + used, input, rest);
            used += rest;
            break;
        }
        size_t prefix = (size_t)(match - input);
        if (prefix > output_size - used - 1) {
            prefix = output_size - used - 1;
        }
        memcpy(output + used, input, prefix);
        used += prefix;
        size_t replacement_length = strlen(replacement);
        if (replacement_length > output_size - used - 1) {
            replacement_length = output_size - used - 1;
        }
        memcpy(output + used, replacement, replacement_length);
        used += replacement_length;
        input = match + find_length;
    }
    output[used] = '\0';
}

static void TransformInsertedValue(const Node *node, const StreamValue *source, StreamValue *destination) {
    char transformed[MAX_PATH_LENGTH] = {0};
    if (node->insert_operation == INSERT_REPLACE_TEXT) {
        ReplaceAll(source->as.text, node->parameter, node->secondary_parameter, transformed, sizeof(transformed));
    } else if (node->insert_operation == INSERT_REPLACE_FILENAME && source->type == VALUE_STRING) {
        const char *filename = GetFileName(source->as.text);
        char new_filename[MAX_PATH_LENGTH] = {0};
        ReplaceAll(filename, node->parameter, node->secondary_parameter, new_filename, sizeof(new_filename));
        const char *directory = GetDirectoryPath(source->as.text);
        if (directory[0]) {
            TextCopy(transformed, directory);
            size_t used = strlen(transformed);
            if (used + 1 < sizeof(transformed) && transformed[used - 1] != '/') {
                transformed[used++] = '/';
                transformed[used] = '\0';
            }
            strncat(transformed, new_filename, sizeof(transformed) - strlen(transformed) - 1);
        } else {
            TextCopy(transformed, new_filename);
        }
    } else if (node->insert_operation == INSERT_REPLACE_EXTENSION && source->type == VALUE_STRING) {
        char base[MAX_PATH_LENGTH] = {0};
        TextCopy(base, source->as.text);
        char *slash = strrchr(base, '/');
        char *dot = strrchr(slash ? slash + 1 : base, '.');
        if (dot) {
            *dot = '\0';
        }
        const char *extension = node->secondary_parameter;
        snprintf(transformed, sizeof(transformed), "%s%s%s", base, extension[0] && extension[0] != '.' ? "." : "",
                 extension);
    } else {
        TextCopy(transformed, source->as.text);
    }
    SetTextValue(destination, source->type, transformed);
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} CurlBuffer;

static size_t CurlWriteCallback(void *data, size_t size, size_t nmemb, void *userp) {
    size_t bytes = size * nmemb;
    CurlBuffer *buffer = userp;
    if (buffer->len + bytes + 1 > buffer->cap) {
        size_t new_cap = buffer->cap == 0 ? 65536 : buffer->cap * 2;
        while (new_cap < buffer->len + bytes + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(buffer->buf, new_cap);
        if (!grown) {
            return 0;
        }
        buffer->buf = grown;
        buffer->cap = new_cap;
    }
    memcpy(buffer->buf + buffer->len, data, bytes);
    buffer->len += bytes;
    buffer->buf[buffer->len] = '\0';
    return bytes;
}

// ============================================================
// Shared state label helper
// ============================================================

static const char *StateLabel(const Node *node) {
    return node->schema_error                      ? "SCHEMA ERROR"
           : node->evaluation_failed               ? "FAILED"
           : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
           : node->is_dirty                        ? "NOT RUN"
                                                   : "CURRENT";
}

// ============================================================
// NODE_DIRECTORY_LIST  (Files)
// ============================================================

static void InitFiles(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Files");
    TextCopy(node->parameter, ".");
    node->directory_entry_type = DIRECTORY_ENTRY_FILES;
    node->bounds.height = 220;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_OUTPUT, 112);
    Port *out = NodeOutputPort(graph, node, 0);
    if (out) {
        SchemaAddField(&out->schema, "path", VALUE_STRING, false);
        SchemaAddField(&out->schema, "name", VALUE_STRING, false);
        SchemaAddField(&out->schema, "type", VALUE_STRING, false);
        SchemaAddField(&out->schema, "size", VALUE_SIZE, false);
        SchemaAddField(&out->schema, "modified", VALUE_DATETIME, false);
    }
}

static bool EvaluateFiles(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)graph;
    (void)source;
    if (node->directory_recursive) {
        FilePathList entries = LoadDirectoryFilesEx(node->parameter, NULL, true);
        AppendDirectoryEntries(node, output, entries);
        UnloadDirectoryFiles(entries);
    } else {
        FilePathList entries = LoadDirectoryFiles(node->parameter);
        AppendDirectoryEntries(node, output, entries);
        UnloadDirectoryFiles(entries);
    }
    return true;
}

static void DrawFilesContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    float text_box_y = NODE_HEADER_HEIGHT + 16.0f;
    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, text_box_y),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }

    float label_x = bounds.x + CanvasSize(graph, 14.0f);
    float button_x = bounds.x + CanvasSize(graph, 60.0f);
    float type_y = bounds.y + CanvasSize(graph, text_box_y + 38.0f);
    float depth_y = bounds.y + CanvasSize(graph, text_box_y + 68.0f);
    float button_height = CanvasSize(graph, 24.0f);
    float gap = CanvasSize(graph, 5.0f);
    float label_y_offset = FontTextCenterOffset(fonts.node_body, button_height);

    DrawInterfaceText(fonts.node_body, "Type", label_x, type_y + label_y_offset, body_font_size, COLOR_MUTED);
    struct {
        const char *label;
        DirectoryEntryType type;
        float width;
    } type_buttons[] = {
        {"Files", DIRECTORY_ENTRY_FILES, 50.0f},
        {"Folders", DIRECTORY_ENTRY_FOLDERS, 70.0f},
        {"Both", DIRECTORY_ENTRY_BOTH, 50.0f},
    };
    float x = button_x;
    for (int i = 0; i < 3; i++) {
        Rectangle btn = {x, type_y, CanvasSize(graph, type_buttons[i].width), button_height};
        if (DrawNodeOptionButton(graph, node, btn, type_buttons[i].label,
                                 node->directory_entry_type == type_buttons[i].type, body_font_size) &&
            node->directory_entry_type != type_buttons[i].type) {
            node->directory_entry_type = type_buttons[i].type;
            MarkNodeDirty(graph, node->id);
            TextCopy(graph->status, "Files type changed - downstream nodes are dirty");
        }
        x += btn.width + gap;
    }

    DrawInterfaceText(fonts.node_body, "Depth", label_x, depth_y + label_y_offset, body_font_size, COLOR_MUTED);
    Rectangle one_layer = {button_x, depth_y, CanvasSize(graph, 82.0f), button_height};
    Rectangle recursive = {one_layer.x + one_layer.width + gap, depth_y, CanvasSize(graph, 94.0f), button_height};
    if (DrawNodeOptionButton(graph, node, one_layer, "One layer", !node->directory_recursive, body_font_size) &&
        node->directory_recursive) {
        node->directory_recursive = false;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Files depth changed - downstream nodes are dirty");
    }
    if (DrawNodeOptionButton(graph, node, recursive, "Recursive", node->directory_recursive, body_font_size) &&
        !node->directory_recursive) {
        node->directory_recursive = true;
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "Files depth changed - downstream nodes are dirty");
    }

    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", StateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

static bool MouseInEditAreaFiles(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NODE_MATCH
// ============================================================

static void InitMatch(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Match");
    TextCopy(node->parameter, "\\.c$");
    TextCopy(node->number_parameter, "0");
    node->filter_use_regex = true;
    node->number_filter_op = NUMBER_FILTER_GTE;
    node->bounds.height = 220;
    AddPort(graph, node, "Stream", VALUE_NONE, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Rows", VALUE_NONE, PORT_DIR_OUTPUT, 178);
}

static bool CanAcceptMatch(const Port *from) {
    (void)from;
    return true;
}

static bool FieldIsSelectableMatch(ValueType type) { return ValueTypeIsText(type) || ValueTypeIsNumeric(type); }

static bool PropagateSchemaMatch(Node *node, Port *input, Port *output, ValueType selected_type) {
    if (!ValueTypeIsText(selected_type) && !ValueTypeIsNumeric(selected_type)) {
        TextCopy(node->schema_error_message, "Match requires a String or numeric field");
        return false;
    }
    output->data_type = input->data_type;
    output->schema = input->schema;
    return true;
}

static bool EvaluateMatch(GraphContext *graph, Node *node, Port *source, Port *output) {
    ValueType field_type = NodeSelectedFieldType(graph, node);
    return ValueTypeIsText(field_type) ? EvaluateWhere(graph, node, source, output)
                                       : EvaluateNumberFilter(graph, node, source, output);
}

static void DrawMatchContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    if (!InputSourcePort(graph, node, 0)) {
        int count = output ? output->item_count : 0;
        DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", StateLabel(node), count, count == 1 ? "" : "s"),
                          bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                          body_font_size, NodeStateColor(node));
        return;
    }

    ValueType match_type = NodeSelectedFieldType(graph, node);
    bool text_match = match_type == VALUE_NONE || ValueTypeIsText(match_type);

    if (text_match) {
        float text_box_y = NODE_HEADER_HEIGHT + 48.0f;
        Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, text_box_y),
                              bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
        if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
            MarkNodeDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
        }

        float btn_y = text_box_y + 36.0f;
        float btn_h = 24.0f, btn_w = 34.0f, gap = 6.0f, start_x = 14.0f;
        Color active_bg = {85, 156, 228, 255};
        Color inactive_bg = {48, 55, 70, 255};
        struct {
            const char *label;
            bool *flag;
        } buttons[3] = {
            {"Aa", &node->filter_case_sensitive},
            {"W", &node->filter_whole_word},
            {".*", &node->filter_use_regex},
        };
        for (int b = 0; b < 3; b++) {
            Rectangle btn = {bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                             bounds.y + CanvasSize(graph, btn_y), CanvasSize(graph, btn_w), CanvasSize(graph, btn_h)};
            Color bg = *buttons[b].flag ? active_bg : inactive_bg;
            DrawRectangleRec(btn, bg);
            DrawRectangleLinesEx(btn, unit, (Color){75, 84, 101, 255});
            float text_w = MeasureTextEx(fonts.node_body, buttons[b].label, body_font_size, 0).x;
            DrawInterfaceText(fonts.node_body, buttons[b].label, btn.x + (btn.width - text_w) * 0.5f,
                              btn.y + FontTextCenterOffset(fonts.node_body, btn.height), body_font_size, COLOR_TEXT);
            if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), btn)) {
                *buttons[b].flag = !(*buttons[b].flag);
                MarkNodeDirty(graph, node->id);
                snprintf(graph->status, sizeof(graph->status), "Filter and downstream nodes are dirty");
            }
        }

        const char *mode_label = node->filter_exclude ? "Exclude" : "Include";
        Rectangle mode_btn = {bounds.x + CanvasSize(graph, start_x + 3 * (btn_w + gap)),
                              bounds.y + CanvasSize(graph, btn_y), CanvasSize(graph, 96.0f), CanvasSize(graph, btn_h)};
        Color mode_bg = node->filter_exclude ? (Color){190, 82, 92, 255} : active_bg;
        DrawRectangleRec(mode_btn, mode_bg);
        DrawRectangleLinesEx(mode_btn, unit, (Color){75, 84, 101, 255});
        float mode_text_w = MeasureTextEx(fonts.node_body, mode_label, body_font_size, 0).x;
        DrawInterfaceText(fonts.node_body, mode_label, mode_btn.x + (mode_btn.width - mode_text_w) * 0.5f,
                          mode_btn.y + FontTextCenterOffset(fonts.node_body, mode_btn.height), body_font_size,
                          COLOR_TEXT);
        if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), mode_btn)) {
            node->filter_exclude = !node->filter_exclude;
            MarkNodeDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status), "Filter mode changed to %s - branch is dirty",
                     node->filter_exclude ? "exclude" : "include");
        }
    } else {
        // Numeric / datetime match UI
        const char *numeric_op_labels[] = {"=", "!=", "<", "<=", ">", ">="};
        NumberFilterOp numeric_ops[] = {NUMBER_FILTER_EQ,  NUMBER_FILTER_NEQ, NUMBER_FILTER_LT,
                                        NUMBER_FILTER_LTE, NUMBER_FILTER_GT,  NUMBER_FILTER_GTE};
        const char *datetime_op_labels[] = {"<", ">="};
        NumberFilterOp datetime_ops[] = {NUMBER_FILTER_LT, NUMBER_FILTER_GTE};
        bool datetime_match = match_type == VALUE_DATETIME;
        const char **op_labels = datetime_match ? datetime_op_labels : numeric_op_labels;
        NumberFilterOp *ops = datetime_match ? datetime_ops : numeric_ops;
        int op_count = datetime_match ? 2 : 6;
        float btn_y = NODE_HEADER_HEIGHT + 48.0f;
        float btn_h = 24.0f, btn_w = 33.0f, gap = 4.0f, start_x = 14.0f;
        Color active_bg = {85, 156, 228, 255};
        Color inactive_bg = {48, 55, 70, 255};
        for (int b = 0; b < op_count; b++) {
            Rectangle btn = {bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                             bounds.y + CanvasSize(graph, btn_y), CanvasSize(graph, btn_w), CanvasSize(graph, btn_h)};
            Color bg = node->number_filter_op == ops[b] ? active_bg : inactive_bg;
            DrawRectangleRec(btn, bg);
            DrawRectangleLinesEx(btn, unit, (Color){75, 84, 101, 255});
            float tw = MeasureTextEx(fonts.node_body, op_labels[b], body_font_size, 0).x;
            DrawInterfaceText(fonts.node_body, op_labels[b], btn.x + (btn.width - tw) * 0.5f,
                              btn.y + FontTextCenterOffset(fonts.node_body, btn.height), body_font_size, COLOR_TEXT);
            if (NodeOwnsMouse(graph, node) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), btn) && node->number_filter_op != ops[b]) {
                node->number_filter_op = ops[b];
                MarkNodeDirty(graph, node->id);
            }
        }
        if (datetime_match) {
            DrawInterfaceText(fonts.node_small, "YYYY-MM-DD  HH:MM", bounds.x + CanvasSize(graph, 96.0f),
                              bounds.y + CanvasSize(graph, btn_y + 6.0f), ScaledFontSize(BODY_TEXT_SIZE * 0.78f, unit),
                              COLOR_MUTED);
        }
        bool size_match = match_type == VALUE_SIZE;
        float unit_width = size_match ? 64.0f : 0.0f;
        float unit_gap = size_match ? 6.0f : 0.0f;
        Rectangle text_box = {bounds.x + CanvasSize(graph, start_x), bounds.y + CanvasSize(graph, btn_y + btn_h + 8.0f),
                              bounds.width - CanvasSize(graph, start_x * 2 + unit_width + unit_gap),
                              CanvasSize(graph, 30.0f)};
        if (DrawNodeTextBox(graph, node, text_box, node->number_parameter, sizeof(node->number_parameter), 0)) {
            MarkNodeDirty(graph, node->id);
        }
    }

    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", StateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

static bool MouseInEditAreaMatch(GraphContext *graph, Node *node, Vector2 mouse) {
    if (!InputSourcePort(graph, node, 0)) {
        return false;
    }
    ValueType field_type = NodeSelectedFieldType(graph, node);
    if (field_type == VALUE_SIZE && CheckCollisionPointRec(mouse, SizeUnitButtonBounds(graph, node))) {
        return false;
    }
    bool text_match = field_type == VALUE_NONE || ValueTypeIsText(field_type);
    float text_box_y = text_match ? NODE_HEADER_HEIGHT + 48.0f : NODE_HEADER_HEIGHT + 80.0f;
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, text_box_y),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NODE_EXEC
// ============================================================

static void InitExec(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Exec");
    TextCopy(node->parameter, "sort");
    node->bounds.width = 320;
    node->bounds.height = 184;
    AddPort(graph, node, "Stdin", VALUE_STRING, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Stdout", VALUE_STRING, PORT_DIR_OUTPUT, 148);
    AddPort(graph, node, "Stderr", VALUE_STRING, PORT_DIR_OUTPUT, 148);
}

static bool CanAcceptExec(const Port *from) { return from->data_type == VALUE_STRING; }

static bool EvaluateExec(GraphContext *graph, Node *node, Port *source, Port *output) {
    if (!node->parameter[0]) {
        return true;
    }
    Port *stderr_output = NodeOutputPort(graph, node, 1);

    char item_path[] = "/tmp/cdr_items_XXXXXX";
    int item_fd = mkstemp(item_path);
    FILE *items = item_fd >= 0 ? fdopen(item_fd, "w") : NULL;
    if (items) {
        for (int i = 0; source && i < source->item_count; i++) {
            fprintf(items, "%s\n", source->items[i].values[0].as.text);
        }
        fclose(items);
    } else if (item_fd >= 0) {
        close(item_fd);
    }

    char stderr_path[] = "/tmp/cdr_stderr_XXXXXX";
    int stderr_fd = mkstemp(stderr_path);
    if (!items || stderr_fd < 0) {
        if (stderr_fd >= 0) {
            close(stderr_fd);
            remove(stderr_path);
        }
        remove(item_path);
        TextCopy(graph->status, "Exec error: failed to create temporary files");
        graph->evaluation_error = true;
        return false;
    }
    close(stderr_fd);
    setenv("ITEMS", item_path, 1);
    char command[512];
    snprintf(command, sizeof(command), "(%s) 2> '%s'", node->parameter, stderr_path);
    FILE *process = popen(command, "r");
    bool success = true;
    if (!process) {
        TextCopy(graph->status, "Exec error: failed to run command");
        graph->evaluation_error = true;
        success = false;
    } else {
        ReadLines(process, output);
        int command_status = pclose(process);
        if (command_status == -1 || !WIFEXITED(command_status) || WEXITSTATUS(command_status) != 0) {
            int exit_code = command_status != -1 && WIFEXITED(command_status) ? WEXITSTATUS(command_status) : -1;
            snprintf(graph->status, sizeof(graph->status), "Exec error: command exited with status %d", exit_code);
            graph->evaluation_error = true;
            success = false;
        }
    }
    FILE *errors = fopen(stderr_path, "r");
    if (errors) {
        ReadLines(errors, stderr_output);
        fclose(errors);
    }
    remove(stderr_path);
    remove(item_path);
    return success;
}

static void DrawExecContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Port *output = NodeOutputPort(graph, node, 0);
    Port *errors = NodeOutputPort(graph, node, 1);

    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }
    DrawInterfaceText(fonts.node_body,
                      TextFormat("%s | %d stdout | %d stderr", StateLabel(node), output ? output->item_count : 0,
                                 errors ? errors->item_count : 0),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

static bool MouseInEditAreaSimple(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NODE_HTTP_REQUEST
// ============================================================

static void InitHttp(GraphContext *graph, Node *node) {
    TextCopy(node->title, "HTTP Request");
    TextCopy(node->parameter, "https://");
    node->bounds.height = 164;
    AddPort(graph, node, "Lines", VALUE_STRING, PORT_DIR_OUTPUT, 112);
}

static bool EvaluateHttp(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)source;
    if (!node->parameter[0]) {
        return true;
    }
    CurlBuffer response = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        TextCopy(graph->status, "HTTP error: curl_easy_init failed");
        graph->evaluation_error = true;
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, node->parameter);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode result = curl_easy_perform(curl);
    bool success = true;
    if (result != CURLE_OK) {
        snprintf(graph->status, sizeof(graph->status), "HTTP error: %s", curl_easy_strerror(result));
        graph->evaluation_error = true;
        success = false;
    } else {
        char *line = response.buf;
        char *end = response.buf + response.len;
        while (output && line < end && output->item_count < MAX_ITEMS) {
            char *newline = memchr(line, '\n', (size_t)(end - line));
            AppendPrimitiveText(output, line, newline ? (size_t)(newline - line) : (size_t)(end - line));
            line = newline ? newline + 1 : end;
        }
    }
    curl_easy_cleanup(curl);
    free(response.buf);
    return success;
}

static void DrawHttpContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Port *output = NodeOutputPort(graph, node, 0);

    Rectangle text_box = {bounds.x + CanvasSize(graph, 14.0f), bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
                          bounds.width - CanvasSize(graph, 28.0f), CanvasSize(graph, 30.0f)};
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }
    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", StateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// NODE_INSERT
// ============================================================

static void InitInsert(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Insert");
    TextCopy(node->parameter, "IMG_");
    TextCopy(node->secondary_parameter, "holiday_");
    TextCopy(node->output_field_name, "destination");
    node->bounds.width = 300;
    node->bounds.height = 300;
    AddPort(graph, node, "Stream", VALUE_NONE, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Rows", VALUE_NONE, PORT_DIR_OUTPUT, 178);
}

static bool CanAcceptInsert(const Port *from) {
    (void)from;
    return true;
}

static bool FieldIsSelectableInsert(ValueType type) { return ValueTypeIsText(type); }

static bool PropagateSchemaInsert(Node *node, Port *input, Port *output, ValueType selected_type) {
    if (!ValueTypeIsText(selected_type)) {
        TextCopy(node->schema_error_message, "Insert currently supports text-like fields");
        return false;
    }
    if (!node->output_field_name[0]) {
        TextCopy(node->schema_error_message, "Insert needs a new field name");
        return false;
    }
    if (input->data_type == VALUE_RECORD && SchemaFieldIndex(&input->schema, node->output_field_name) >= 0) {
        TextCopy(node->schema_error_message, "Insert cannot overwrite an existing field");
        return false;
    }
    output->data_type = VALUE_RECORD;
    output->schema = input->schema;
    if (input->data_type != VALUE_RECORD) {
        memset(&output->schema, 0, sizeof(output->schema));
        SchemaAddField(&output->schema, "Item", input->data_type, false);
    }
    if (!SchemaAddField(&output->schema, node->output_field_name, selected_type, true)) {
        TextCopy(node->schema_error_message, "Record has no room for another field");
        return false;
    }
    return true;
}

static bool EvaluateInsert(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)graph;
    int new_index = output->schema.field_count - 1;
    for (int i = 0; i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        StreamItem *destination = &output->items[output->item_count++];
        memset(destination, 0, sizeof(*destination));
        if (source->data_type == VALUE_RECORD) {
            *destination = source->items[i];
        } else {
            destination->values[0] = source->items[i].values[0];
        }
        const StreamValue *value = ItemFieldValue(source, &source->items[i], node->field_name);
        TransformInsertedValue(node, value, &destination->values[new_index]);
    }
    return true;
}

static void DrawInsertContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    float x = bounds.x + CanvasSize(graph, 14.0f);
    float width = bounds.width - CanvasSize(graph, 28.0f);

    float output_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 43.0f);
    DrawInterfaceText(fonts.node_small, "New field", x, output_y, ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit),
                      COLOR_MUTED);
    Rectangle output_box = {x + CanvasSize(graph, 72.0f), output_y - CanvasSize(graph, 5.0f),
                            width - CanvasSize(graph, 72.0f), CanvasSize(graph, 27.0f)};
    if (DrawNodeTextBox(graph, node, output_box, node->output_field_name, sizeof(node->output_field_name), 0)) {
        MarkNodeDirty(graph, node->id);
    }

    float find_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 76.0f);
    float replace_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 111.0f);
    DrawInterfaceText(fonts.node_small, "Find", x, find_y + CanvasSize(graph, 7.0f),
                      ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit), COLOR_MUTED);
    DrawInterfaceText(fonts.node_small, "With", x, replace_y + CanvasSize(graph, 7.0f),
                      ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit), COLOR_MUTED);
    Rectangle find_box = {x + CanvasSize(graph, 50.0f), find_y, width - CanvasSize(graph, 50.0f),
                          CanvasSize(graph, 28.0f)};
    Rectangle replace_box = {find_box.x, replace_y, find_box.width, find_box.height};
    bool find_changed = DrawNodeTextBox(graph, node, find_box, node->parameter, sizeof(node->parameter), 1);
    bool replace_changed =
        DrawNodeTextBox(graph, node, replace_box, node->secondary_parameter, sizeof(node->secondary_parameter), 2);
    if (find_changed || replace_changed) {
        MarkNodeDirty(graph, node->id);
    }

    const char *operations[] = {"Text", "Filename", "Extension"};
    float operation_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 150.0f);
    for (int i = 0; i < 3; i++) {
        Rectangle button = {x + CanvasSize(graph, i * 88.0f), operation_y, CanvasSize(graph, 82.0f),
                            CanvasSize(graph, 25.0f)};
        if (DrawNodeOptionButton(graph, node, button, operations[i], node->insert_operation == (InsertOperation)i,
                                 body_font_size) &&
            node->insert_operation != (InsertOperation)i) {
            node->insert_operation = (InsertOperation)i;
            MarkNodeDirty(graph, node->id);
        }
    }
    const char *state_label = node->schema_error ? node->schema_error_message : node->is_dirty ? "NOT RUN" : "CURRENT";
    DrawInterfaceText(fonts.node_small, state_label, x, bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      ScaledFontSize(BODY_TEXT_SIZE * 0.82f, unit), NodeStateColor(node));
}

static bool MouseInEditAreaInsert(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle output_box = {bounds.x + CanvasSize(graph, 86.0f),
                            bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 38.0f),
                            bounds.width - CanvasSize(graph, 100.0f), CanvasSize(graph, 27.0f)};
    Rectangle find_box = {bounds.x + CanvasSize(graph, 64.0f), bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 76.0f),
                          bounds.width - CanvasSize(graph, 78.0f), CanvasSize(graph, 28.0f)};
    Rectangle replace_box = {find_box.x, bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 111.0f), find_box.width,
                             find_box.height};
    return CheckCollisionPointRec(mouse, output_box) || CheckCollisionPointRec(mouse, find_box) ||
           CheckCollisionPointRec(mouse, replace_box);
}

// ============================================================
// NODE_GET
// ============================================================

static void InitGet(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Get");
    node->bounds.height = 150;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Values", VALUE_NONE, PORT_DIR_OUTPUT, 112);
}

static bool CanAcceptGet(const Port *from) { return from->data_type == VALUE_RECORD; }

static bool FieldIsSelectableGet(ValueType type) {
    (void)type;
    return true;
}

static bool PropagateSchemaGet(Node *node, Port *input, Port *output, ValueType selected_type) {
    (void)node;
    (void)input;
    output->data_type = selected_type;
    memset(&output->schema, 0, sizeof(output->schema));
    return true;
}

static bool EvaluateGet(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)graph;
    int index = SchemaFieldIndex(&source->schema, node->field_name);
    if (index < 0) {
        return false;
    }
    for (int i = 0; i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        memset(&output->items[output->item_count], 0, sizeof(StreamItem));
        output->items[output->item_count++].values[0] = source->items[i].values[index];
    }
    return true;
}

static void DrawGetContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    const char *state_label = node->schema_error ? "SCHEMA ERROR" : node->is_dirty ? "NOT RUN" : "CURRENT";
    DrawInterfaceText(fonts.node_body, state_label, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + bounds.height - CanvasSize(graph, 21.0f), body_font_size, NodeStateColor(node));
}

// ============================================================
// Registry
// ============================================================

// NODE_LEGACY_NUMBER_FILTER has NULL init — it cannot be created interactively
// and is upgraded to NODE_MATCH by LoadGraph before being used.
static const NodeDef NODE_REGISTRY[] = {
    [NODE_DIRECTORY_LIST] =
        {
            .name = "Files",
            .init = InitFiles,
            .can_accept = NULL,
            .expected_input_type = VALUE_NONE,
            .is_schema_computing = false,
            .preferred_field_name = NULL,
            .field_is_selectable = NULL,
            .propagate_schema = NULL,
            .uses_field_selector = false,
            .field_selector_label = NULL,
            .field_selector_y_offset = 12.0f,
            .evaluate = EvaluateFiles,
            .draw_content = DrawFilesContent,
            .control_height = 110.0f,
            .mouse_in_edit_area = MouseInEditAreaFiles,
        },
    [NODE_MATCH] =
        {
            .name = "Match",
            .init = InitMatch,
            .can_accept = CanAcceptMatch,
            .expected_input_type = VALUE_NONE,
            .is_schema_computing = true,
            .preferred_field_name = "name",
            .field_is_selectable = FieldIsSelectableMatch,
            .propagate_schema = PropagateSchemaMatch,
            .uses_field_selector = true,
            .field_selector_label = "Field",
            .field_selector_y_offset = 12.0f,
            .evaluate = EvaluateMatch,
            .draw_content = DrawMatchContent,
            .control_height = 116.0f,
            .mouse_in_edit_area = MouseInEditAreaMatch,
        },
    [NODE_EXEC] =
        {
            .name = "Exec",
            .init = InitExec,
            .can_accept = CanAcceptExec,
            .expected_input_type = VALUE_STRING,
            .is_schema_computing = false,
            .preferred_field_name = NULL,
            .field_is_selectable = NULL,
            .propagate_schema = NULL,
            .uses_field_selector = false,
            .field_selector_label = NULL,
            .field_selector_y_offset = 12.0f,
            .evaluate = EvaluateExec,
            .draw_content = DrawExecContent,
            .control_height = 42.0f,
            .mouse_in_edit_area = MouseInEditAreaSimple,
        },
    [NODE_HTTP_REQUEST] =
        {
            .name = "HTTP Request",
            .init = InitHttp,
            .can_accept = NULL,
            .expected_input_type = VALUE_NONE,
            .is_schema_computing = false,
            .preferred_field_name = NULL,
            .field_is_selectable = NULL,
            .propagate_schema = NULL,
            .uses_field_selector = false,
            .field_selector_label = NULL,
            .field_selector_y_offset = 12.0f,
            .evaluate = EvaluateHttp,
            .draw_content = DrawHttpContent,
            .control_height = 42.0f,
            .mouse_in_edit_area = MouseInEditAreaSimple,
        },
    [NODE_INSERT] =
        {
            .name = "Insert",
            .init = InitInsert,
            .can_accept = CanAcceptInsert,
            .expected_input_type = VALUE_NONE,
            .is_schema_computing = true,
            .preferred_field_name = "path",
            .field_is_selectable = FieldIsSelectableInsert,
            .propagate_schema = PropagateSchemaInsert,
            .uses_field_selector = true,
            .field_selector_label = "From",
            .field_selector_y_offset = 10.0f,
            .evaluate = EvaluateInsert,
            .draw_content = DrawInsertContent,
            .control_height = 190.0f,
            .mouse_in_edit_area = MouseInEditAreaInsert,
        },
    [NODE_GET] =
        {
            .name = "Get",
            .init = InitGet,
            .can_accept = CanAcceptGet,
            .expected_input_type = VALUE_RECORD,
            .is_schema_computing = true,
            .preferred_field_name = "name",
            .field_is_selectable = FieldIsSelectableGet,
            .propagate_schema = PropagateSchemaGet,
            .uses_field_selector = true,
            .field_selector_label = "Field",
            .field_selector_y_offset = 12.0f,
            .evaluate = EvaluateGet,
            .draw_content = DrawGetContent,
            .control_height = 42.0f,
            .mouse_in_edit_area = NULL,
        },
    [NODE_LEGACY_NUMBER_FILTER] =
        {
            .name = NULL, // Not creatable; upgraded to NODE_MATCH on load.
        },
};

const NodeDef *GetNodeDef(NodeType type) {
    if ((int)type < 0 || (int)type >= (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0]))) {
        return NULL;
    }
    return &NODE_REGISTRY[type];
}

int NodeTypeFromName(const char *name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (int t = 0; t < (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0])); t++) {
        if (NODE_REGISTRY[t].name && TextIsEqual(NODE_REGISTRY[t].name, name)) {
            return t;
        }
    }
    return -1;
}
