#include "config.h"
#include "fonts.h"
#include "graph.h"
#include "node_def.h"
#include "nodes/helpers.h"
#include "render.h"

#include "raylib.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Curl helpers
// ============================================================

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
// Init
// ============================================================

static void InitHttp(GraphContext *graph, Node *node) {
    TextCopy(node->title, "HTTP Request");
    TextCopy(node->parameter, "https://");
    node->bounds.height = 164;
    AddPort(graph, node, "Lines", VALUE_STRING, PORT_DIR_OUTPUT, 112);
}

// ============================================================
// Evaluate
// ============================================================

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

// ============================================================
// Draw
// ============================================================

static void DrawHttpContent(GraphContext *graph, Node *node) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    float body_font_size = ScaledFontSize(BODY_TEXT_SIZE, CanvasUnit(graph));
    Port *output = NodeOutputPort(graph, node, 0);

    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    if (DrawNodeTextBox(graph, node, text_box, node->parameter, sizeof(node->parameter), 0)) {
        MarkNodeDirty(graph, node->id);
        snprintf(graph->status, sizeof(graph->status), "%s and downstream nodes are dirty", node->title);
    }
    int count = output ? output->item_count : 0;
    DrawInterfaceText(fonts.node_body, TextFormat("%s | %d item%s", NodeStateLabel(node), count, count == 1 ? "" : "s"),
                      bounds.x + CanvasSize(graph, 14.0f), bounds.y + bounds.height - CanvasSize(graph, 21.0f),
                      body_font_size, NodeStateColor(node));
}

// ============================================================
// Mouse hit-test
// ============================================================

static bool MouseInEditAreaHttp(GraphContext *graph, Node *node, Vector2 mouse) {
    Rectangle bounds = NodeScreenBounds(graph, node);
    Rectangle text_box = {
        bounds.x + CanvasSize(graph, 14.0f),
        bounds.y + CanvasSize(graph, NODE_HEADER_HEIGHT + 16.0f),
        bounds.width - CanvasSize(graph, 28.0f),
        CanvasSize(graph, 30.0f),
    };
    return CheckCollisionPointRec(mouse, text_box);
}

// ============================================================
// NodeDef
// ============================================================

const NodeDef kHttpNodeDef = {
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
    .mouse_in_edit_area = MouseInEditAreaHttp,
};
