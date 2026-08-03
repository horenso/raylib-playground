#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"
#include "streams.h"

#include "raylib.h"

#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <time.h>

// ============================================================
// Text-match helpers
// ============================================================

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
            char l = text[i + j], r = needle[j];
            if (!node->filter_case_sensitive) {
                l = (char)tolower((unsigned char)l);
                r = (char)tolower((unsigned char)r);
            }
            equal = l == r;
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

// ============================================================
// Numeric-match helpers
// ============================================================

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
        static const unsigned long long multipliers[] = {
            1ULL, 1024ULL, 1024ULL * 1024ULL, 1024ULL * 1024ULL * 1024ULL, 1024ULL * 1024ULL * 1024ULL * 1024ULL,
        };
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

// ============================================================
// Init
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

// ============================================================
// Connection / schema
// ============================================================

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

// ============================================================
// Evaluate
// ============================================================

static bool EvaluateMatch(GraphContext *graph, Node *node, Port *source, Port *output) {
    ValueType field_type = NodeSelectedFieldType(graph, node);
    return ValueTypeIsText(field_type) ? EvaluateWhere(graph, node, source, output)
                                       : EvaluateNumberFilter(graph, node, source, output);
}

// ============================================================
// Draw
// ============================================================

static void DrawMatchContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float unit = CanvasUnit(graph);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, unit);
    Port *output = NodeOutputPort(graph, node, 0);

    if (!InputSourcePort(graph, node, 0)) {
        int count = output ? output->item_count : 0;
        DrawInterfaceText(fonts.node_body,
                          TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                          bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                          body_font_size, NodeStateColor(node));
        return;
    }

    ValueType match_type = NodeSelectedFieldType(graph, node);
    bool text_match = match_type == VALUE_NONE || ValueTypeIsText(match_type);

    if (text_match) {
        float text_box_y = NODE_HEADER_HEIGHT + 48.0f;
        Rectangle text_box = {
            bounds.x + CanvasSize(graph, 14.0f),
            bounds.y + CanvasSize(graph, text_box_y),
            bounds.width - CanvasSize(graph, 28.0f),
            CanvasSize(graph, 30.0f),
        };
        if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
            MarkNodeDirty(graph, node->id);
            snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
        }

        float btn_y = text_box_y + 36.0f, btn_h = 24.0f, btn_w = 34.0f, gap = 6.0f, start_x = 14.0f;
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
            Rectangle btn = {
                bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                bounds.y + CanvasSize(graph, btn_y),
                CanvasSize(graph, btn_w),
                CanvasSize(graph, btn_h),
            };
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
                TextCopy(graph->status, "Filter and downstream nodes are dirty");
            }
        }

        const char *mode_label = node->filter_exclude ? "Exclude" : "Include";
        Rectangle mode_btn = {
            bounds.x + CanvasSize(graph, start_x + 3 * (btn_w + gap)),
            bounds.y + CanvasSize(graph, btn_y),
            CanvasSize(graph, 96.0f),
            CanvasSize(graph, btn_h),
        };
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
        // Numeric / datetime UI
        const char *numeric_op_labels[] = {"=", "!=", "<", "<=", ">", ">="};
        NumberFilterOp numeric_ops[] = {NUMBER_FILTER_EQ,  NUMBER_FILTER_NEQ, NUMBER_FILTER_LT,
                                        NUMBER_FILTER_LTE, NUMBER_FILTER_GT,  NUMBER_FILTER_GTE};
        const char *datetime_op_labels[] = {"<", ">="};
        NumberFilterOp datetime_ops[] = {NUMBER_FILTER_LT, NUMBER_FILTER_GTE};
        bool datetime_match = match_type == VALUE_DATETIME;
        const char **op_labels = datetime_match ? datetime_op_labels : numeric_op_labels;
        NumberFilterOp *ops = datetime_match ? datetime_ops : numeric_ops;
        int op_count = datetime_match ? 2 : 6;

        float btn_y = NODE_HEADER_HEIGHT + 48.0f, btn_h = 24.0f, btn_w = 33.0f, gap = 4.0f, start_x = 14.0f;
        Color active_bg = {85, 156, 228, 255};
        Color inactive_bg = {48, 55, 70, 255};
        for (int b = 0; b < op_count; b++) {
            Rectangle btn = {
                bounds.x + CanvasSize(graph, start_x + b * (btn_w + gap)),
                bounds.y + CanvasSize(graph, btn_y),
                CanvasSize(graph, btn_w),
                CanvasSize(graph, btn_h),
            };
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
        Rectangle text_box = {
            bounds.x + CanvasSize(graph, start_x),
            bounds.y + CanvasSize(graph, btn_y + btn_h + 8.0f),
            bounds.width - CanvasSize(graph, start_x * 2 + unit_width + unit_gap),
            CanvasSize(graph, 30.0f),
        };
        if (DrawNodeTextBox(graph, node, text_box, node->number_parameter, sizeof(node->number_parameter), 0)) {
            MarkNodeDirty(graph, node->id);
        }
    }

    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// Mouse hit-test
// ============================================================

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
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, text_box_y),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kMatchNodeDef = {
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
};
