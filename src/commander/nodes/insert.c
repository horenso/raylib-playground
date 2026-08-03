#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <string.h>

// ============================================================
// Transformation helpers
// ============================================================

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
        size_t repl_len = strlen(replacement);
        if (repl_len > output_size - used - 1) {
            repl_len = output_size - used - 1;
        }
        memcpy(output + used, replacement, repl_len);
        used += repl_len;
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

// ============================================================
// Init
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

// ============================================================
// Connection / schema
// ============================================================

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

// ============================================================
// Evaluate
// ============================================================

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

// ============================================================
// Draw
// ============================================================

static void DrawInsertContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    float x = bounds.x + CanvasSize(graph, 14.0f);
    float width = bounds.width - CanvasSize(graph, 28.0f);

    float output_y = bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 43.0f);
    DrawInterfaceText(fonts.node_small, "New field", x, output_y, ScaledFontSize(BODY_TEXT_SIZE * 0.8f, unit),
                      COLOR_MUTED);
    Rectangle output_box = {
        x + CanvasSize(graph, 72.0f),
        output_y - CanvasSize(graph, 5.0f),
        width - CanvasSize(graph, 72.0f),
        CanvasSize(graph, 27.0f),
    };
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
    Rectangle replace_box = {x + CanvasSize(graph, 50.0f), replace_y, find_box.width, find_box.height};
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

// ============================================================
// Mouse hit-test
// ============================================================

static bool MouseInEditAreaInsert(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle output_box = {
        bounds.x + CanvasSize(graph, 86.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 38.0f),
        bounds.width - CanvasSize(graph, 100.0f),
        CanvasSize(graph, 27.0f),
    };
    Rectangle find_box = {
        bounds.x + CanvasSize(graph, 64.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 76.0f),
        bounds.width - CanvasSize(graph, 78.0f),
        CanvasSize(graph, 28.0f),
    };
    Rectangle replace_box = {find_box.x, bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 111.0f), find_box.width,
                             find_box.height};
    return CheckCollisionPointRec(mouse, output_box) || CheckCollisionPointRec(mouse, find_box) ||
           CheckCollisionPointRec(mouse, replace_box);
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kInsertNodeDef = {
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
};
