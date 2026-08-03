#include "evaluate.h"
#include "graph.h"
#include "node_def.h"
#include "streams.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

// All per-type evaluation logic lives in node_def.c and is dispatched below
// through GetNodeDef(). The static helpers that were previously here have
// moved to node_def.c.

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

    const NodeDef *def = GetNodeDef(node->type);
    if (def && def->evaluate) {
        success = def->evaluate(graph, node, source_port, output);
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
    Node *filter = AddNode(graph, NODE_FILTER, (Vector2){360, 110});
    Node *insert = AddNode(graph, NODE_INSERT, (Vector2){675, 110});
    if (files && filter && insert) {
        AddLink(graph, files->output_port_ids[0], filter->input_port_ids[0]);
        AddLink(graph, filter->output_port_ids[0], insert->input_port_ids[0]);
        TextCopy(filter->field_name, "");
        TextCopy(filter->parameter, "");
        TextCopy(insert->field_name, "");
        TextCopy(insert->output_field_name, "");
        insert->insert_operation = INSERT_REPLACE_FILENAME;
        TextCopy(insert->parameter, "");
        TextCopy(insert->secondary_parameter, "");
        PropagateSchemas(graph);
    }
}
