#include "evaluate.h"
#include "graph.h"
#include "raylib.h"
#include <regex.h>
#include <stdio.h>
#include <string.h>

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
    } else if (source && node->type == NODE_STRING_MATCH) {
        regex_t expression;
        int compile_result = regcomp(&expression, node->parameter, REG_EXTENDED | REG_NOSUB);
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
    } else if (source && node->type == NODE_INSPECT_VIEW) {
        node->item_count = source->item_count;
        for (int i = 0; i < source->item_count; i++) {
            TextCopy(node->items[i], source->items[i]);
        }
    }
    node->is_dirty = false;
}

void RunGraph(GraphContext *graph) {
    graph->evaluation_error = false;
    for (int i = 0; i < graph->node_count; i++) {
        EvaluateNode(graph, &graph->nodes[i], 0);
    }
    int total = 0;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].type == NODE_INSPECT_VIEW) {
            total += graph->nodes[i].item_count;
        }
    }
    if (!graph->evaluation_error) {
        snprintf(graph->status, sizeof(graph->status), "Graph evaluated - %d item%s visible in inspectors", total,
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
    TextCopy(graph->status, "Ready - drag an output port to a compatible input port");
    Node *directory = AddNode(graph, NODE_DIRECTORY_LIST, (Vector2){70, 120});
    Node *match = AddNode(graph, NODE_STRING_MATCH, (Vector2){410, 120});
    Node *inspect = AddNode(graph, NODE_INSPECT_VIEW, (Vector2){750, 70});
    if (directory && match && inspect) {
        AddLink(graph, directory->output_port_ids[0], match->input_port_ids[0]);
        AddLink(graph, match->output_port_ids[0], inspect->input_port_ids[0]);
    }
    RunGraph(graph);
}
