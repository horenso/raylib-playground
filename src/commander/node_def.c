// node_def.c – global NodeDef registry.
//
// Each NodeType maps to one NodeDef whose implementation lives in
// src/commander/nodes/<name>.c. Adding a new node type means:
//   1. Create src/commander/nodes/<name>.c with a `const NodeDef k<Name>NodeDef`.
//   2. Add an extern declaration and a registry entry below.

#include "node_def.h"

#include "raylib.h" // TextIsEqual

#include <stddef.h>

// Per-node definitions — implemented in nodes/*.c
extern const NodeDef kFilesNodeDef;
extern const NodeDef kFilterNodeDef;
extern const NodeDef kExecNodeDef;
extern const NodeDef kHttpNodeDef;
extern const NodeDef kInsertNodeDef;
extern const NodeDef kGetNodeDef;
extern const NodeDef kCsvNodeDef;
extern const NodeDef kStringifyNodeDef;
extern const NodeDef kSearchFilesNodeDef;

// The registry is indexed by NodeType enum value.
// NODE_LEGACY_NUMBER_FILTER has a NULL entry — it cannot be created
// interactively and is upgraded to NODE_FILTER by LoadGraph().
static const NodeDef *NODE_REGISTRY[] = {
    [NODE_DIRECTORY_LIST] = &kFilesNodeDef, [NODE_FILTER] = &kFilterNodeDef, [NODE_EXEC] = &kExecNodeDef,
    [NODE_HTTP_REQUEST] = &kHttpNodeDef,    [NODE_INSERT] = &kInsertNodeDef, [NODE_GET] = &kGetNodeDef,
    [NODE_LEGACY_NUMBER_FILTER] = NULL,     [NODE_CSV] = &kCsvNodeDef,       [NODE_STRINGIFY] = &kStringifyNodeDef,
    [NODE_SEARCH_FILES] = &kSearchFilesNodeDef,
};

const NodeDef *GetNodeDef(NodeType type) {
    int t = (int)type;
    if (t < 0 || t >= (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0]))) {
        return NULL;
    }
    return NODE_REGISTRY[t];
}

int NodeTypeFromName(const char *name) {
    if (!name || !name[0]) {
        return -1;
    }
    // Backward compatibility for graphs saved before Match was renamed.
    if (TextIsEqual(name, "Match")) {
        return NODE_FILTER;
    }
    for (int t = 0; t < (int)(sizeof(NODE_REGISTRY) / sizeof(NODE_REGISTRY[0])); t++) {
        if (NODE_REGISTRY[t] && NODE_REGISTRY[t]->name && TextIsEqual(NODE_REGISTRY[t]->name, name)) {
            return t;
        }
    }
    return -1;
}
