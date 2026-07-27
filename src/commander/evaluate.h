#pragma once

#include "types.h"

bool EvaluateNode(GraphContext *graph, Node *node, int depth);
void RunNode(GraphContext *graph, int node_id);
void RunGraph(GraphContext *graph);
void SeedGraph(GraphContext *graph);
