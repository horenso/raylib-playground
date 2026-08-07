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
// Init
// ============================================================

static void InitGet(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Get");
    node->bounds.height = 150;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Values", VALUE_NONE, PORT_DIR_OUTPUT, 112);
}

// ============================================================
// Connection / schema
// ============================================================

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

// ============================================================
// Evaluate
// ============================================================

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

// ============================================================
// Draw
// ============================================================

static void DrawGetContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    const char *state_label = NodeStateLabel(node);
    DrawInterfaceText(fonts.node_body, state_label, bounds.x + CanvasSize(graph, 14.0f),
                      bounds.y + bounds.height - CanvasSize(graph, 21.0f), body_font_size, NodeStateColor(node));
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kGetNodeDef = {
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
};
