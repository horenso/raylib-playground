#pragma once

#include "types.h"

bool EvaluateNode(GraphContext *graph, Node *node, int depth);
bool InitializeEvaluator(void);
void UpdateEvaluator(GraphContext *graph);
void ShutdownEvaluator(void);
bool EvaluatorIsBusy(void);
void RunNode(GraphContext *graph, int node_id);
void RunGraph(GraphContext *graph);
void SeedGraph(GraphContext *graph);
