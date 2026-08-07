#include "evaluate.h"
#include "graph.h"
#include "node_def.h"
#include "streams.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

typedef enum {
    EVALUATION_JOB_NONE,
    EVALUATION_JOB_NODE,
    EVALUATION_JOB_GRAPH,
} EvaluationJobKind;

typedef struct {
    thrd_t thread;
    mtx_t mutex;
    cnd_t wake;
    bool initialized;
    bool stop_requested;
    bool job_pending;
    bool job_running;
    bool result_ready;
    EvaluationJobKind job_kind;
    int node_id;
    GraphContext graph;
} Evaluator;

// The worker owns this snapshot while a job is running. The UI only touches it
// while holding mutex and only publishes results back from UpdateEvaluator().
static Evaluator evaluator;

static double MonotonicMilliseconds(void) {
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

bool EvaluateNode(GraphContext *graph, Node *node, int depth) {
    if (!node) {
        return false;
    }
    if (!node->is_dirty) {
        return !node->evaluation_failed;
    }

    node->is_running = true;
    if (node->evaluation_failed) {
        node->is_running = false;
        return false;
    }
    if (node->schema_error) {
        snprintf(graph->status, sizeof(graph->status), "%.48s: %.96s", node->title, node->schema_error_message);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        node->is_running = false;
        return false;
    }
    if (depth > MAX_NODES) {
        snprintf(graph->status, sizeof(graph->status), "Cannot evaluate %s: dependency cycle", node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        node->is_running = false;
        return false;
    }

    Port *source_port = InputSourcePort(graph, node, 0);
    Node *source_node = source_port ? FindNode(graph, source_port->node_id) : NULL;
    if (source_node && !EvaluateNode(graph, source_node, depth + 1)) {
        snprintf(graph->status, sizeof(graph->status), "Cannot update %s: upstream %s failed", node->title,
                 source_node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        node->is_running = false;
        return false;
    }
    if (node->input_count > 0 && !source_port) {
        snprintf(graph->status, sizeof(graph->status), "%s needs an input connection", node->title);
        graph->evaluation_error = true;
        node->evaluation_failed = true;
        node->is_running = false;
        return false;
    }

    double started = MonotonicMilliseconds();
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

    node->evaluation_time_ms = MonotonicMilliseconds() - started;
    node->has_evaluation_time = true;
    node->evaluation_failed = !success;
    node->is_running = false;
    if (success) {
        node->is_dirty = false;
        node->has_evaluated = true;
    }
    return success;
}

static void RunNodeSync(GraphContext *graph, int node_id) {
    Node *node = FindNode(graph, node_id);
    if (!node) {
        return;
    }
    graph->evaluation_error = false;
    if (EvaluateNode(graph, node, 0) && !graph->evaluation_error) {
        snprintf(graph->status, sizeof(graph->status), "Updated %s and required upstream nodes", node->title);
    }
}

static void RunGraphSync(GraphContext *graph) {
    graph->evaluation_error = false;
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

static int EvaluatorThread(void *unused) {
    (void)unused;
    for (;;) {
        mtx_lock(&evaluator.mutex);
        while (!evaluator.stop_requested && !evaluator.job_pending) {
            cnd_wait(&evaluator.wake, &evaluator.mutex);
        }
        if (evaluator.stop_requested) {
            mtx_unlock(&evaluator.mutex);
            return 0;
        }
        EvaluationJobKind job_kind = evaluator.job_kind;
        int node_id = evaluator.node_id;
        evaluator.job_pending = false;
        evaluator.job_running = true;
        mtx_unlock(&evaluator.mutex);

        if (job_kind == EVALUATION_JOB_NODE) {
            RunNodeSync(&evaluator.graph, node_id);
        } else if (job_kind == EVALUATION_JOB_GRAPH) {
            RunGraphSync(&evaluator.graph);
        }

        mtx_lock(&evaluator.mutex);
        evaluator.job_running = false;
        evaluator.result_ready = true;
        mtx_unlock(&evaluator.mutex);
    }
}

bool InitializeEvaluator(void) {
    if (evaluator.initialized) {
        return true;
    }
    memset(&evaluator, 0, sizeof(evaluator));
    if (mtx_init(&evaluator.mutex, mtx_plain) != thrd_success) {
        return false;
    }
    if (cnd_init(&evaluator.wake) != thrd_success) {
        mtx_destroy(&evaluator.mutex);
        return false;
    }
    if (thrd_create(&evaluator.thread, EvaluatorThread, NULL) != thrd_success) {
        cnd_destroy(&evaluator.wake);
        mtx_destroy(&evaluator.mutex);
        return false;
    }
    evaluator.initialized = true;
    return true;
}

static void MarkUpstreamRunning(GraphContext *graph, Node *node, int depth) {
    if (!node || !node->is_dirty || depth > MAX_NODES) {
        return;
    }
    node->is_running = true;
    Port *source = InputSourcePort(graph, node, 0);
    if (source) {
        MarkUpstreamRunning(graph, FindNode(graph, source->node_id), depth + 1);
    }
}

static bool BeginEvaluation(GraphContext *graph, EvaluationJobKind job_kind, int node_id) {
    if (!evaluator.initialized) {
        TextCopy(graph->status, "Background evaluator is unavailable");
        return false;
    }

    mtx_lock(&evaluator.mutex);
    if (evaluator.job_pending || evaluator.job_running || evaluator.result_ready) {
        mtx_unlock(&evaluator.mutex);
        TextCopy(graph->status, "An evaluation is already running");
        return false;
    }

    if (job_kind == EVALUATION_JOB_NODE) {
        Node *node = FindNode(graph, node_id);
        if (!node) {
            mtx_unlock(&evaluator.mutex);
            return false;
        }
        MarkNodeDirty(graph, node_id);
        MarkUpstreamRunning(graph, node, 0);
        snprintf(graph->status, sizeof(graph->status), "Running %s in background", node->title);
    } else {
        PropagateSchemas(graph);
        for (int i = 0; i < graph->node_count; i++) {
            graph->nodes[i].is_dirty = true;
            graph->nodes[i].evaluation_failed = false;
            graph->nodes[i].is_running = true;
        }
        TextCopy(graph->status, "Running graph in background");
    }

    memcpy(&evaluator.graph, graph, sizeof(evaluator.graph));
    evaluator.job_kind = job_kind;
    evaluator.node_id = node_id;
    evaluator.job_pending = true;
    cnd_signal(&evaluator.wake);
    mtx_unlock(&evaluator.mutex);
    return true;
}

static void MergeNodeResult(GraphContext *graph, const GraphContext *result, const Node *result_node) {
    Node *node = FindNode(graph, result_node->id);
    if (!node || node->revision != result_node->revision || node->type != result_node->type) {
        return;
    }

    node->is_dirty = result_node->is_dirty;
    node->evaluation_failed = result_node->evaluation_failed;
    node->has_evaluated = result_node->has_evaluated;
    node->has_evaluation_time = result_node->has_evaluation_time;
    node->evaluation_time_ms = result_node->evaluation_time_ms;

    for (int i = 0; i < result_node->output_count; i++) {
        Port *result_port = FindPort((GraphContext *)result, result_node->output_port_ids[i]);
        Port *port = FindPort(graph, result_node->output_port_ids[i]);
        if (!result_port || !port || port->node_id != node->id) {
            continue;
        }
        port->data_type = result_port->data_type;
        port->schema = result_port->schema;
        port->schema_valid = result_port->schema_valid;
        port->item_count = result_port->item_count;
        if (port->item_count > 0) {
            memcpy(port->items, result_port->items, (size_t)port->item_count * sizeof(port->items[0]));
        }
    }
}

void UpdateEvaluator(GraphContext *graph) {
    if (!evaluator.initialized) {
        return;
    }

    mtx_lock(&evaluator.mutex);
    if (!evaluator.result_ready) {
        mtx_unlock(&evaluator.mutex);
        return;
    }

    int stale_nodes = 0;
    for (int i = 0; i < evaluator.graph.node_count; i++) {
        Node *result_node = &evaluator.graph.nodes[i];
        Node *node = FindNode(graph, result_node->id);
        if (!node || node->revision != result_node->revision || node->type != result_node->type) {
            stale_nodes++;
            continue;
        }
        MergeNodeResult(graph, &evaluator.graph, result_node);
    }
    for (int i = 0; i < graph->node_count; i++) {
        graph->nodes[i].is_running = false;
    }

    graph->evaluation_error = evaluator.graph.evaluation_error;
    if (stale_nodes > 0) {
        snprintf(graph->status, sizeof(graph->status), "Evaluation finished; %d changed node%s kept dirty", stale_nodes,
                 stale_nodes == 1 ? "" : "s");
    } else {
        TextCopy(graph->status, evaluator.graph.status);
    }
    evaluator.result_ready = false;
    evaluator.job_kind = EVALUATION_JOB_NONE;
    mtx_unlock(&evaluator.mutex);
}

bool EvaluatorIsBusy(void) {
    if (!evaluator.initialized) {
        return false;
    }
    mtx_lock(&evaluator.mutex);
    bool busy = evaluator.job_pending || evaluator.job_running || evaluator.result_ready;
    mtx_unlock(&evaluator.mutex);
    return busy;
}

void ShutdownEvaluator(void) {
    if (!evaluator.initialized) {
        return;
    }
    mtx_lock(&evaluator.mutex);
    evaluator.stop_requested = true;
    cnd_signal(&evaluator.wake);
    mtx_unlock(&evaluator.mutex);
    thrd_join(evaluator.thread, NULL);
    cnd_destroy(&evaluator.wake);
    mtx_destroy(&evaluator.mutex);
    evaluator.initialized = false;
}

void RunNode(GraphContext *graph, int node_id) { BeginEvaluation(graph, EVALUATION_JOB_NODE, node_id); }

void RunGraph(GraphContext *graph) { BeginEvaluation(graph, EVALUATION_JOB_GRAPH, -1); }

void SeedGraph(GraphContext *graph) {
    memset(graph, 0, sizeof(*graph));
    graph->application_scale = 1.0f;
    graph->camera.offset = (Vector2){0, TOOLBAR_HEIGHT};
    graph->camera.target = (Vector2){-90, -55};
    graph->camera.zoom = 1.0f;
    graph->selected_node_id = -1;
    graph->active_port_id = -1;
    graph->dragging_node_id = -1;
    graph->resizing_node_id = -1;
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
