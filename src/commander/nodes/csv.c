#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char fields[MAX_FIELDS][MAX_PATH_LENGTH];
    int count;
    bool too_many_fields;
    bool field_too_long;
} CsvRecord;

// Read one RFC 4180-style record. Quoted fields may contain commas, escaped
// quotes, and newlines. Returns 1 for a record, 0 at EOF, and -1 for an
// unterminated quoted field.
static int ReadCsvRecord(FILE *file, CsvRecord *record) {
    memset(record, 0, sizeof(*record));
    int field = 0;
    int length = 0;
    bool any_input = false;
    bool quoted = false;

    for (;;) {
        int c = fgetc(file);
        if (c == EOF) {
            if (quoted) {
                return -1;
            }
            if (!any_input) {
                return 0;
            }
            record->count = field + 1;
            return 1;
        }
        any_input = true;

        if (quoted) {
            if (c == '"') {
                int next = fgetc(file);
                if (next == '"') {
                    c = '"';
                } else {
                    quoted = false;
                    if (next != EOF) {
                        ungetc(next, file);
                    }
                    continue;
                }
            }
        } else {
            if (c == '"' && length == 0) {
                quoted = true;
                continue;
            }
            if (c == ',') {
                field++;
                length = 0;
                if (field >= MAX_FIELDS) {
                    record->too_many_fields = true;
                }
                continue;
            }
            if (c == '\n' || c == '\r') {
                if (c == '\r') {
                    int next = fgetc(file);
                    if (next != '\n' && next != EOF) {
                        ungetc(next, file);
                    }
                }
                record->count = field + 1;
                return 1;
            }
        }

        if (field < MAX_FIELDS) {
            if (length < MAX_PATH_LENGTH - 1) {
                record->fields[field][length++] = (char)c;
                record->fields[field][length] = '\0';
            } else {
                record->field_too_long = true;
            }
        }
    }
}

static bool CsvTextEqualsIgnoreCase(const char *left, const char *right) {
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool ParseCsvBool(const char *text, bool *value) {
    if (CsvTextEqualsIgnoreCase(text, "true")) {
        if (value) {
            *value = true;
        }
        return true;
    }
    if (CsvTextEqualsIgnoreCase(text, "false")) {
        if (value) {
            *value = false;
        }
        return true;
    }
    return false;
}

static bool ParseCsvInt(const char *text, long long *value) {
    if (!text[0]) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }
    if (value) {
        *value = parsed;
    }
    return true;
}

static bool ParseCsvFloat(const char *text, double *value) {
    if (!text[0]) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(parsed)) {
        return false;
    }
    if (value) {
        *value = parsed;
    }
    return true;
}

static ValueType InferCsvValueType(const char *text) {
    if (ParseCsvBool(text, NULL)) {
        return VALUE_BOOL;
    }
    if (ParseCsvInt(text, NULL)) {
        return VALUE_INT;
    }
    if (ParseCsvFloat(text, NULL)) {
        return VALUE_FLOAT;
    }
    return VALUE_STRING;
}

static ValueType WeakenCsvType(ValueType current, ValueType observed) {
    if (current == VALUE_NONE) {
        return observed;
    }
    if (current == observed || current == VALUE_STRING) {
        return current;
    }
    if ((current == VALUE_INT && observed == VALUE_FLOAT) ||
        (current == VALUE_FLOAT && observed == VALUE_INT)) {
        return VALUE_FLOAT;
    }
    return VALUE_STRING;
}

static void RefreshCsvSchema(GraphContext *graph, Node *node, Port *output) {
    (void)graph;
    output->data_type = VALUE_RECORD;
    output->schema_valid = false;
    output->item_count = 0;
    memset(&output->schema, 0, sizeof(output->schema));

    if (!node->parameter[0]) {
        node->schema_error = true;
        TextCopy(node->schema_error_message, "Enter a CSV file path");
        return;
    }

    FILE *file = fopen(node->parameter, "rb");
    if (!file) {
        node->schema_error = true;
        TextCopy(node->schema_error_message, "Cannot open the CSV file");
        return;
    }

    CsvRecord header;
    int result = ReadCsvRecord(file, &header);
    if (result == 0) {
        node->schema_error = true;
        TextCopy(node->schema_error_message, "CSV file is empty");
        fclose(file);
        return;
    }
    if (result < 0) {
        node->schema_error = true;
        TextCopy(node->schema_error_message, "CSV header has an open quote");
        fclose(file);
        return;
    }
    if (header.too_many_fields) {
        node->schema_error = true;
        snprintf(node->schema_error_message, sizeof(node->schema_error_message), "CSV has more than %d columns",
                 MAX_FIELDS);
        fclose(file);
        return;
    }
    if (header.field_too_long) {
        node->schema_error = true;
        TextCopy(node->schema_error_message, "A CSV header is too long");
        fclose(file);
        return;
    }

    // Strip a UTF-8 BOM from the first header when present.
    unsigned char *first = (unsigned char *)header.fields[0];
    if (first[0] == 0xef && first[1] == 0xbb && first[2] == 0xbf) {
        memmove(header.fields[0], header.fields[0] + 3, strlen(header.fields[0] + 3) + 1);
    }

    for (int i = 0; i < header.count; i++) {
        if (!header.fields[i][0]) {
            node->schema_error = true;
            TextCopy(node->schema_error_message, "CSV headers cannot be empty");
            memset(&output->schema, 0, sizeof(output->schema));
            fclose(file);
            return;
        }
        if (strlen(header.fields[i]) >= MAX_FIELD_NAME) {
            node->schema_error = true;
            snprintf(node->schema_error_message, sizeof(node->schema_error_message),
                     "CSV headers must be shorter than %d characters", MAX_FIELD_NAME);
            memset(&output->schema, 0, sizeof(output->schema));
            fclose(file);
            return;
        }
        if (!SchemaAddField(&output->schema, header.fields[i], VALUE_NONE, false)) {
            node->schema_error = true;
            TextCopy(node->schema_error_message, "CSV headers must be unique");
            memset(&output->schema, 0, sizeof(output->schema));
            fclose(file);
            return;
        }
    }

    int row = 1;
    CsvRecord record;
    while ((result = ReadCsvRecord(file, &record)) > 0) {
        row++;
        if (record.too_many_fields || record.count != header.count) {
            node->schema_error = true;
            snprintf(node->schema_error_message, sizeof(node->schema_error_message),
                     "CSV row %d has %d columns; expected %d", row, record.count, header.count);
            memset(&output->schema, 0, sizeof(output->schema));
            fclose(file);
            return;
        }
        if (record.field_too_long) {
            node->schema_error = true;
            snprintf(node->schema_error_message, sizeof(node->schema_error_message), "CSV row %d has a long value",
                     row);
            memset(&output->schema, 0, sizeof(output->schema));
            fclose(file);
            return;
        }
        for (int i = 0; i < record.count; i++) {
            ValueType observed = InferCsvValueType(record.fields[i]);
            FieldSchema *field = &output->schema.fields[i];
            field->type = WeakenCsvType(field->type, observed);
        }
    }
    if (result < 0 || ferror(file)) {
        node->schema_error = true;
        snprintf(node->schema_error_message, sizeof(node->schema_error_message), "CSV row %d is malformed", row + 1);
        memset(&output->schema, 0, sizeof(output->schema));
        fclose(file);
        return;
    }
    fclose(file);

    // A header-only file has no evidence for a narrower type.
    for (int i = 0; i < output->schema.field_count; i++) {
        if (output->schema.fields[i].type == VALUE_NONE) {
            output->schema.fields[i].type = VALUE_STRING;
        }
    }
    output->schema_valid = true;
}

static void InitCsv(GraphContext *graph, Node *node) {
    TextCopy(node->title, "CSV");
    TextCopy(node->parameter, "data.csv");
    node->bounds.width = 280;
    node->bounds.height = 164;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_OUTPUT, 112);
}

static bool EvaluateCsv(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)source;
    FILE *file = fopen(node->parameter, "rb");
    if (!file) {
        snprintf(graph->status, sizeof(graph->status), "CSV: cannot open %.130s", node->parameter);
        graph->evaluation_error = true;
        return false;
    }

    CsvRecord record;
    int result = ReadCsvRecord(file, &record); // Skip the header.
    if (result <= 0) {
        TextCopy(graph->status, result == 0 ? "CSV file is empty" : "CSV header is malformed");
        graph->evaluation_error = true;
        fclose(file);
        return false;
    }
    int row = 1;
    while (result > 0 && output->item_count < MAX_ITEMS) {
        result = ReadCsvRecord(file, &record);
        if (result <= 0) {
            break;
        }
        row++;
        if (record.too_many_fields || record.count != output->schema.field_count) {
            snprintf(graph->status, sizeof(graph->status), "CSV row %d has %d columns; expected %d", row, record.count,
                     output->schema.field_count);
            graph->evaluation_error = true;
            output->item_count = 0;
            fclose(file);
            return false;
        }
        if (record.field_too_long) {
            snprintf(graph->status, sizeof(graph->status), "CSV row %d contains a value that is too long", row);
            graph->evaluation_error = true;
            output->item_count = 0;
            fclose(file);
            return false;
        }
        StreamItem *item = &output->items[output->item_count++];
        memset(item, 0, sizeof(*item));
        for (int field = 0; field < record.count; field++) {
            ValueType type = output->schema.fields[field].type;
            if (type == VALUE_BOOL) {
                item->values[field].type = VALUE_BOOL;
                if (!ParseCsvBool(record.fields[field], &item->values[field].as.boolean)) {
                    snprintf(graph->status, sizeof(graph->status), "CSV row %d no longer matches its schema", row);
                    graph->evaluation_error = true;
                    output->item_count = 0;
                    fclose(file);
                    return false;
                }
            } else if (type == VALUE_INT) {
                item->values[field].type = VALUE_INT;
                if (!ParseCsvInt(record.fields[field], &item->values[field].as.integer)) {
                    snprintf(graph->status, sizeof(graph->status), "CSV row %d no longer matches its schema", row);
                    graph->evaluation_error = true;
                    output->item_count = 0;
                    fclose(file);
                    return false;
                }
            } else if (type == VALUE_FLOAT) {
                item->values[field].type = VALUE_FLOAT;
                if (!ParseCsvFloat(record.fields[field], &item->values[field].as.floating)) {
                    snprintf(graph->status, sizeof(graph->status), "CSV row %d no longer matches its schema", row);
                    graph->evaluation_error = true;
                    output->item_count = 0;
                    fclose(file);
                    return false;
                }
            } else {
                SetTextValue(&item->values[field], VALUE_STRING, record.fields[field]);
            }
        }
    }

    bool success = result >= 0 && !ferror(file);
    if (!success) {
        snprintf(graph->status, sizeof(graph->status), "CSV row %d is malformed", row + 1);
        graph->evaluation_error = true;
        output->item_count = 0;
    }
    fclose(file);
    return success;
}

static void DrawCsvContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Port *output = NodeOutputPort(graph, node, 0);
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    if (DrawNodePathBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0, PATH_PICK_FILE)) {
        MarkNodeDirty(graph, node->id);
        TextCopy(graph->status, "CSV path changed - downstream schemas updated");
    }

    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d row%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

static bool MouseInEditAreaCsv(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f + 30.0f + 4.0f),
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

const NodeDef kCsvNodeDef = {
    .name = "CSV",
    .init = InitCsv,
    .can_accept = NULL,
    .expected_input_type = VALUE_NONE,
    .is_schema_computing = false,
    .preferred_field_name = NULL,
    .field_is_selectable = NULL,
    .propagate_schema = NULL,
    .refresh_source_schema = RefreshCsvSchema,
    .uses_field_selector = false,
    .field_selector_label = NULL,
    .field_selector_y_offset = 12.0f,
    .evaluate = EvaluateCsv,
    .draw_content = DrawCsvContent,
    .control_height = 42.0f,
    .mouse_in_edit_area = MouseInEditAreaCsv,
};
