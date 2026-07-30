#include "evaluate.h"
#include "graph.h"
#include "streams.h"

#include "raylib.h"

#include <ctype.h>
#include <curl/curl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} CurlBuffer;

static size_t curl_write_cb(void *data, size_t size, size_t nmemb, void *userp) {
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
        return regexec(expression, text, 0, NULL, 0) == 0;
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

static long long NumericValue(const StreamValue *value) {
    if (!value) return 0;
    if (value->type == VALUE_INT || value->type == VALUE_DATETIME) return value->as.integer;
    if (value->type == VALUE_SIZE) return (long long)value->as.file_size;
    return 0;
}

static bool EvaluateNumberFilter(GraphContext *graph, Node *node, Port *source, Port *output) {
    char *end;
    long long threshold = strtoll(node->parameter, &end, 10);
    if (*node->parameter == '\0' || *end != '\0') {
        snprintf(graph->status, sizeof(graph->status), "Match Number: invalid threshold '%s'", node->parameter);
        graph->evaluation_error = true;
        return false;
    }
    for (int i = 0; output && i < source->item_count && output->item_count < MAX_ITEMS; i++) {
        const StreamValue *value = ItemFieldValue(source, &source->items[i], node->field_name);
        if (!value || !ValueTypeIsNumeric(value->type)) continue;
        long long v = NumericValue(value);
        bool matched = false;
        switch (node->number_filter_op) {
        case NUMBER_FILTER_EQ:  matched = v == threshold; break;
        case NUMBER_FILTER_NEQ: matched = v != threshold; break;
        case NUMBER_FILTER_LT:  matched = v <  threshold; break;
        case NUMBER_FILTER_LTE: matched = v <= threshold; break;
        case NUMBER_FILTER_GT:  matched = v >  threshold; break;
        case NUMBER_FILTER_GTE: matched = v >= threshold; break;
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
            size_t remaining = strlen(input);
            if (remaining > output_size - used - 1) {
                remaining = output_size - used - 1;
            }
            memcpy(output + used, input, remaining);
            used += remaining;
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

static bool EvaluateInsert(Node *node, Port *source, Port *output) {
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

static bool EvaluateGet(Node *node, Port *source, Port *output) {
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

bool EvaluateNode(GraphContext *graph, Node *node, int depth) {
    if (!node) {
        return false;
    }
    if (!node->is_dirty) {
        return !node->evaluation_failed;
    }
    if (node->evaluation_failed) {
        return false;
    }
    if (node->schema_error) {
        snprintf(graph->status, sizeof(graph->status), "%.48s: %.96s", node->title, node->schema_error_message);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        return false;
    }
    if (depth > MAX_NODES) {
        snprintf(graph->status, sizeof(graph->status), "Cannot evaluate %s: dependency cycle", node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        return false;
    }

    Port *source_port = InputSourcePort(graph, node, 0);
    Node *source_node = source_port ? FindNode(graph, source_port->node_id) : NULL;
    if (source_node && !EvaluateNode(graph, source_node, depth + 1)) {
        snprintf(graph->status, sizeof(graph->status), "Cannot update %s: upstream %s failed", node->title,
                 source_node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        return false;
    }
    if (node->input_count > 0 && !source_port) {
        snprintf(graph->status, sizeof(graph->status), "%s needs an input connection", node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        return false;
    }

    bool success = true;
    node->evaluation_failed = false;
    for (int i = 0; i < node->output_count; i++) {
        Port *port = NodeOutputPort(graph, node, i);
        if (port) {
            port->item_count = 0;
        }
    }
    Port *output = NodeOutputPort(graph, node, 0);

    if (node->type == NODE_DIRECTORY_LIST) {
        if (node->directory_recursive) {
            FilePathList entries = LoadDirectoryFilesEx(node->parameter, NULL, true);
            AppendDirectoryEntries(node, output, entries);
            UnloadDirectoryFiles(entries);
        } else {
            FilePathList entries = LoadDirectoryFiles(node->parameter);
            AppendDirectoryEntries(node, output, entries);
            UnloadDirectoryFiles(entries);
        }
    } else if (node->type == NODE_MATCH_STRING) {
        success = EvaluateWhere(graph, node, source_port, output);
    } else if (node->type == NODE_NUMBER_FILTER) {
        success = EvaluateNumberFilter(graph, node, source_port, output);
    } else if (node->type == NODE_INSERT) {
        success = EvaluateInsert(node, source_port, output);
    } else if (node->type == NODE_GET) {
        success = EvaluateGet(node, source_port, output);
    } else if (node->type == NODE_HTTP_REQUEST && node->parameter[0] != '\0') {
        CurlBuffer response = {0};
        CURL *curl = curl_easy_init();
        if (!curl) {
            TextCopy(graph->status, "HTTP error: curl_easy_init failed");
            graph->evaluation_error = true;
            success = false;
        } else {
            curl_easy_setopt(curl, CURLOPT_URL, node->parameter);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            CURLcode result = curl_easy_perform(curl);
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
        }
        free(response.buf);
    } else if (node->type == NODE_EXEC && node->parameter[0] != '\0') {
        Port *stderr_output = NodeOutputPort(graph, node, 1);
        char item_path[] = "/tmp/cdr_items_XXXXXX";
        int item_fd = mkstemp(item_path);
        FILE *items = item_fd >= 0 ? fdopen(item_fd, "w") : NULL;
        if (items) {
            for (int i = 0; source_port && i < source_port->item_count; i++) {
                fprintf(items, "%s\n", source_port->items[i].values[0].as.text);
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
            success = false;
        } else {
            close(stderr_fd);
            setenv("ITEMS", item_path, 1);
            char command[512];
            snprintf(command, sizeof(command), "(%s) 2> '%s'", node->parameter, stderr_path);
            FILE *process = popen(command, "r");
            if (!process) {
                TextCopy(graph->status, "Exec error: failed to run command");
                graph->evaluation_error = true;
                success = false;
            } else {
                ReadLines(process, output);
                int command_status = pclose(process);
                if (command_status == -1 || !WIFEXITED(command_status) || WEXITSTATUS(command_status) != 0) {
                    int exit_code =
                        command_status != -1 && WIFEXITED(command_status) ? WEXITSTATUS(command_status) : -1;
                    snprintf(graph->status, sizeof(graph->status), "Exec error: command exited with status %d",
                             exit_code);
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
        }
    }

    node->evaluation_failed = !success;
    if (success) {
        node->is_dirty = false;
        node->has_evaluated = true;
    }
    return success;
}

void RunNode(GraphContext *graph, int node_id) {
    Node *node = FindNode(graph, node_id);
    if (!node) {
        return;
    }
    graph->evaluation_error = false;
    MarkNodeDirty(graph, node_id);
    if (EvaluateNode(graph, node, 0) && !graph->evaluation_error) {
        snprintf(graph->status, sizeof(graph->status), "Updated %s and required upstream nodes", node->title);
    }
}

void RunGraph(GraphContext *graph) {
    graph->evaluation_error = false;
    PropagateSchemas(graph);
    for (int i = 0; i < graph->node_count; i++) {
        graph->nodes[i].is_dirty = true;
        graph->nodes[i].evaluation_failed = false;
    }
    for (int i = 0; i < graph->node_count; i++) {
        EvaluateNode(graph, &graph->nodes[i], 0);
    }
    if (!graph->evaluation_error) {
        int total = 0;
        for (int i = 0; i < graph->port_count; i++) {
            if (graph->ports[i].direction == PORT_DIR_OUTPUT) {
                total += graph->ports[i].item_count;
            }
        }
        snprintf(graph->status, sizeof(graph->status), "Graph evaluated - %d item%s total", total,
                 total == 1 ? "" : "s");
    }
}

void SeedGraph(GraphContext *graph) {
    memset(graph, 0, sizeof(*graph));
    graph->application_scale = 1.0f;
    graph->camera.offset = (Vector2){0, TOOLBAR_HEIGHT};
    graph->camera.target = (Vector2){-90, -55};
    graph->camera.zoom = 1.0f;
    graph->selected_node_id = -1;
    graph->active_port_id = -1;
    graph->dragging_node_id = -1;
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        graph->inspector_windows[i].port_id = -1;
        graph->inspector_windows[i].active = -1;
    }
    TextCopy(graph->status, "Ready - Files now emits typed rows; inspect an output to see its schema");

    Node *files = AddNode(graph, NODE_DIRECTORY_LIST, (Vector2){45, 110});
    Node *where = AddNode(graph, NODE_MATCH_STRING, (Vector2){360, 110});
    Node *insert = AddNode(graph, NODE_INSERT, (Vector2){675, 110});
    if (files && where && insert) {
        AddLink(graph, files->output_port_ids[0], where->input_port_ids[0]);
        AddLink(graph, where->output_port_ids[0], insert->input_port_ids[0]);
        TextCopy(where->field_name, "name");
        TextCopy(where->parameter, "\\.jpg$");
        TextCopy(insert->field_name, "path");
        TextCopy(insert->output_field_name, "destination");
        insert->insert_operation = INSERT_REPLACE_FILENAME;
        TextCopy(insert->parameter, "IMG_");
        TextCopy(insert->secondary_parameter, "holiday_");
        PropagateSchemas(graph);
    }
}
