#pragma once

#include "types.h"

void EvaluateNode(GraphContext *graph, Node *node, int depth);
void RunGraph(GraphContext *graph);
void SeedGraph(GraphContext *graph);
