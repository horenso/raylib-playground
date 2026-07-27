#include "evaluate.h"
#include "graph.h"
#include "raylib.h"
#include <ctype.h>
#include <curl/curl.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} CurlBuffer;

static size_t curl_write_cb(void *data, size_t size, size_t nmemb, void *userp) {
    size_t bytes = size * nmemb;
    CurlBuffer *b = userp;
    if (b->len + bytes + 1 > b->cap) {
        size_t new_cap = b->cap == 0 ? 65536 : b->cap * 2;
        while (new_cap < b->len + bytes + 1) new_cap *= 2;
        char *tmp = realloc(b->buf, new_cap);
        if (!tmp) return 0;
        b->buf = tmp;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, data, bytes);
    b->len += bytes;
    b->buf[b->len] = '\0';
    return bytes;
}

void EvaluateNode(GraphContext *graph, Node *node, int depth) {
    if (!node || !node->is_dirty || depth > MAX_NODES) {
        return;
    }

    Node *source = InputSource(graph, node, 0);
    if (source) {
        EvaluateNode(graph, source, depth + 1);
    }
    node->item_count = 0;

    if (node->type == NODE_DIRECTORY_LIST) {
        FilePathList files = LoadDirectoryFiles(node->parameter);
        for (unsigned int i = 0; i < files.count && node->item_count < MAX_ITEMS; i++) {
            if (!DirectoryExists(files.paths[i])) {
                TextCopy(node->items[node->item_count++], files.paths[i]);
            }
        }
        UnloadDirectoryFiles(files);
    } else if (source && node->type == NODE_STRING_FILTER) {
        if (node->filter_use_regex) {
            int flags = REG_EXTENDED | REG_NOSUB;
            if (!node->filter_case_sensitive) {
                flags |= REG_ICASE;
            }
            regex_t expression;
            int compile_result = regcomp(&expression, node->parameter, flags);
            if (compile_result != 0) {
                char error[96] = {0};
                regerror(compile_result, &expression, error, sizeof(error));
                snprintf(graph->status, sizeof(graph->status), "Regex error: %s", error);
                graph->evaluation_error = true;
            } else {
                for (int i = 0; i < source->item_count && node->item_count < MAX_ITEMS; i++) {
                    if (regexec(&expression, source->items[i], 0, NULL, 0) == 0) {
                        TextCopy(node->items[node->item_count++], source->items[i]);
                    }
                }
                regfree(&expression);
            }
        } else {
            for (int i = 0; i < source->item_count && node->item_count < MAX_ITEMS; i++) {
                const char *haystack = source->items[i];
                const char *needle = node->parameter;
                bool matched = false;
                // search for needle in haystack respecting case sensitivity
                int hlen = (int)strlen(haystack);
                int nlen = (int)strlen(needle);
                if (nlen == 0) {
                    matched = true;
                } else {
                    for (int j = 0; j <= hlen - nlen && !matched; j++) {
                        bool equal = true;
                        for (int k = 0; k < nlen && equal; k++) {
                            char hc = haystack[j + k];
                            char nc = needle[k];
                            if (!node->filter_case_sensitive) {
                                hc = (char)tolower((unsigned char)hc);
                                nc = (char)tolower((unsigned char)nc);
                            }
                            if (hc != nc) {
                                equal = false;
                            }
                        }
                        if (equal) {
                            if (node->filter_whole_word) {
                                bool left_ok = (j == 0) || !isalnum((unsigned char)haystack[j - 1]);
                                bool right_ok = (j + nlen >= hlen) || !isalnum((unsigned char)haystack[j + nlen]);
                                if (left_ok && right_ok) {
                                    matched = true;
                                }
                            } else {
                                matched = true;
                            }
                        }
                    }
                }
                if (matched) {
                    TextCopy(node->items[node->item_count++], source->items[i]);
                }
            }
        }
    } else if (node->type == NODE_HTTP_REQUEST && node->parameter[0] != '\0') {
        CurlBuffer response = {0};
        CURL *curl = curl_easy_init();
        if (!curl) {
            snprintf(graph->status, sizeof(graph->status), "HTTP error: curl_easy_init failed");
            graph->evaluation_error = true;
        } else {
            curl_easy_setopt(curl, CURLOPT_URL, node->parameter);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                snprintf(graph->status, sizeof(graph->status), "HTTP error: %s", curl_easy_strerror(res));
                graph->evaluation_error = true;
            } else {
                char *line = response.buf;
                char *end = response.buf + response.len;
                while (line < end && node->item_count < MAX_ITEMS) {
                    char *nl = memchr(line, '\n', (size_t)(end - line));
                    size_t line_len = nl ? (size_t)(nl - line) : (size_t)(end - line);
                    if (line_len > 0 && line[line_len - 1] == '\r') line_len--;
                    size_t copy_len = line_len < MAX_PATH_LENGTH - 1 ? line_len : MAX_PATH_LENGTH - 1;
                    memcpy(node->items[node->item_count], line, copy_len);
                    node->items[node->item_count][copy_len] = '\0';
                    node->item_count++;
                    line = nl ? nl + 1 : end;
                }
            }
            curl_easy_cleanup(curl);
        }
        free(response.buf);
    } else if (source && node->type == NODE_BASH_EXEC && node->parameter[0] != '\0') {
        // Write input items to a tmpfile and expose path as $ITEMS for the command
        char tmppath[] = "/tmp/cdr_items_XXXXXX";
        int fd = mkstemp(tmppath);
        if (fd >= 0) {
            FILE *tmp = fdopen(fd, "w");
            if (tmp) {
                for (int i = 0; i < source->item_count; i++) {
                    fprintf(tmp, "%s\n", source->items[i]);
                }
                fclose(tmp);
            } else {
                close(fd);
            }
        }
        setenv("ITEMS", tmppath, 1);
        FILE *proc = popen(node->parameter, "r");
        if (!proc) {
            snprintf(graph->status, sizeof(graph->status), "Bash error: failed to run command");
            graph->evaluation_error = true;
        } else {
            char line[MAX_PATH_LENGTH];
            while (fgets(line, sizeof(line), proc) && node->item_count < MAX_ITEMS) {
                int len = (int)strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                }
                TextCopy(node->items[node->item_count++], line);
            }
            pclose(proc);
        }
        remove(tmppath);
    }
    node->is_dirty = false;
}

void RunGraph(GraphContext *graph) {
    graph->evaluation_error = false;
    for (int i = 0; i < graph->node_count; i++) {
        EvaluateNode(graph, &graph->nodes[i], 0);
    }
    if (!graph->evaluation_error) {
        int total = 0;
        for (int i = 0; i < graph->node_count; i++) {
            total += graph->nodes[i].item_count;
        }
        snprintf(graph->status, sizeof(graph->status), "Graph evaluated - %d item%s total", total,
                 total == 1 ? "" : "s");
    }
}

void SeedGraph(GraphContext *graph) {
    memset(graph, 0, sizeof(*graph));
    graph->camera.offset = (Vector2){0, TOOLBAR_HEIGHT};
    graph->camera.target = (Vector2){-90, -55};
    graph->camera.zoom = 1.0f;
    graph->selected_node_id = -1;
    graph->active_port_id = -1;
    graph->dragging_node_id = -1;
    graph->inspected_port_id = -1;
    graph->inspect_active = -1;
    TextCopy(graph->status, "Ready - drag an output port to a compatible input port");
    Node *directory = AddNode(graph, NODE_DIRECTORY_LIST, (Vector2){70, 120});
    Node *match = AddNode(graph, NODE_STRING_FILTER, (Vector2){410, 120});
    Node *bash = AddNode(graph, NODE_BASH_EXEC, (Vector2){750, 120});
    if (directory && match && bash) {
        AddLink(graph, directory->output_port_ids[0], match->input_port_ids[0]);
        AddLink(graph, match->output_port_ids[0], bash->input_port_ids[0]);
        TextCopy(bash->parameter, "sort $ITEMS");
    }
    RunGraph(graph);
}
