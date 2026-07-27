#pragma once

#include "types.h"

#include <stdbool.h>

bool SaveGraph(GraphContext *graph, const char *path);
bool LoadGraph(GraphContext *graph, const char *path);
