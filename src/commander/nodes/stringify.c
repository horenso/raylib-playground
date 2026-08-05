#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raygui.h"
#include "raylib.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool IsFieldNameStart(char c) { return isalpha((unsigned char)c) || c == '_'; }

static bool IsFieldNameCharacter(char c) { return isalnum((unsigned char)c) || c == '_'; }

static int FieldReferenceEnd(const char *text, int dollar) {
    int end = dollar + 1;
    if (!IsFieldNameStart(text[end])) {
        return dollar;
    }
    while (IsFieldNameCharacter(text[end])) {
        end++;
    }
    return end;
}

static int ReferencedFieldIndex(const RecordSchema *schema, const char *text, int dollar, int end) {
    int length = end - dollar - 1;
    if (!schema || length <= 0 || length >= MAX_FIELD_NAME) {
        return -1;
    }
    char field_name[MAX_FIELD_NAME];
    memcpy(field_name, text + dollar + 1, (size_t)length);
    field_name[length] = '\0';
    return SchemaFieldIndex(schema, field_name);
}

static void AppendBytes(char *output, size_t capacity, size_t *used, const char *text, size_t length) {
    if (*used >= capacity - 1) {
        return;
    }
    size_t available = capacity - *used - 1;
    if (length > available) {
        length = available;
    }
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = '\0';
}

static void ExpandTemplate(const Node *node, const Port *source, const StreamItem *item, char *output,
                           size_t capacity) {
    size_t used = 0;
    output[0] = '\0';
    for (int i = 0; node->parameter[i] && used < capacity - 1;) {
        if (node->parameter[i] != '$') {
            AppendBytes(output, capacity, &used, node->parameter + i, 1);
            i++;
            continue;
        }
        if (node->parameter[i + 1] == '$') {
            AppendBytes(output, capacity, &used, "$", 1);
            i += 2;
            continue;
        }
        int end = FieldReferenceEnd(node->parameter, i);
        int field_index = ReferencedFieldIndex(&source->schema, node->parameter, i, end);
        if (field_index < 0) {
            AppendBytes(output, capacity, &used, node->parameter + i, (size_t)(end > i ? end - i : 1));
            i = end > i ? end : i + 1;
            continue;
        }
        char formatted[64];
        const char *value = ValueDisplayText(&item->values[field_index], formatted, sizeof(formatted));
        AppendBytes(output, capacity, &used, value, strlen(value));
        i = end;
    }
}

static void InitStringify(GraphContext *graph, Node *node) {
    TextCopy(node->title, "Stringify");
    TextCopy(node->parameter, "My name is $name");
    node->bounds.width = 440;
    node->bounds.height = 184;
    AddPort(graph, node, "Rows", VALUE_RECORD, PORT_DIR_INPUT, 55);
    AddPort(graph, node, "Text", VALUE_STRING, PORT_DIR_OUTPUT, 148);
}

static bool CanAcceptStringify(const Port *from) { return from->data_type == VALUE_RECORD; }

static bool EvaluateStringify(GraphContext *graph, Node *node, Port *source, Port *output) {
    (void)graph;
    for (int i = 0; i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        char expanded[MAX_PATH_LENGTH];
        ExpandTemplate(node, source, &source->items[i], expanded, sizeof(expanded));
        AppendPrimitiveText(output, expanded, strlen(expanded));
    }
    return true;
}

static void DrawTemplateHighlights(GraphContext *graph, Node *node, Rectangle text_box) {
    Port *input = InputSourcePort(graph, node, 0);
    if (!input || input->data_type != VALUE_RECORD || !input->schema_valid) {
        return;
    }
    SetNodeGuiScale(CanvasUnit(graph));
    float text_size = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
    float spacing = (float)GuiGetStyle(DEFAULT, TEXT_SPACING);
    int border = GuiGetStyle(TEXTBOX, BORDER_WIDTH);
    int padding = GuiGetStyle(TEXTBOX, TEXT_PADDING);
    float available_width = text_box.width - 2.0f * (border + padding);
    if ((float)GuiGetTextWidth(node->parameter) > available_width) {
        return;
    }

    float text_x = text_box.x + border + padding;
    float text_y = text_box.y + FontTextCenterOffset(fonts.node_gui, text_box.height);
    BeginScissorMode((int)text_box.x + border, (int)text_box.y + border, (int)text_box.width - border * 2,
                     (int)text_box.height - border * 2);
    for (int i = 0; node->parameter[i];) {
        if (node->parameter[i] != '$') {
            i++;
            continue;
        }
        int end = FieldReferenceEnd(node->parameter, i);
        if (end == i || ReferencedFieldIndex(&input->schema, node->parameter, i, end) < 0) {
            i = end > i ? end : i + 1;
            continue;
        }
        char prefix[sizeof(node->parameter)];
        char reference[MAX_FIELD_NAME + 1];
        memcpy(prefix, node->parameter, (size_t)i);
        prefix[i] = '\0';
        int reference_length = end - i;
        memcpy(reference, node->parameter + i, (size_t)reference_length);
        reference[reference_length] = '\0';
        float x = text_x + (float)GuiGetTextWidth(prefix);
        DrawTextEx(fonts.node_gui, reference, (Vector2){x, text_y}, text_size, spacing, (Color){91, 207, 151, 255});
        i = end;
    }
    EndScissorMode();
}

static void DrawStringifyContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
    }
    DrawTemplateHighlights(graph, node, text_box);

    Port *output = NodeOutputPort(graph, node, 0);
    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      font_size, NodeStateColor(node));
}

static bool MouseInEditAreaStringify(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

const NodeDef kStringifyNodeDef = {
    .name = "Stringify",
    .init = InitStringify,
    .can_accept = CanAcceptStringify,
    .expected_input_type = VALUE_RECORD,
    .is_schema_computing = false,
    .preferred_field_name = NULL,
    .field_is_selectable = NULL,
    .propagate_schema = NULL,
    .uses_field_selector = false,
    .field_selector_label = NULL,
    .field_selector_y_offset = 12.0f,
    .evaluate = EvaluateStringify,
    .draw_content = DrawStringifyContent,
    .control_height = 42.0f,
    .mouse_in_edit_area = MouseInEditAreaStringify,
};
