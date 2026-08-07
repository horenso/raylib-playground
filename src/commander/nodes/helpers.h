#pragma once
// Shared helpers used by multiple node implementations.
// Include this from any file inside src/commander/nodes/.

#include "types.h"

#include <stddef.h>
#include <stdio.h>

// Append one text item to a primitive output port, stripping trailing newlines.
void AppendPrimitiveText(Port *port, const char *text, size_t length);

// Read lines from a stdio stream into a primitive output port.
void ReadLines(FILE *stream, Port *port);

// Resolve an existing path to an absolute canonical path without redundant
// separators, ".", or ".." components.
bool NormalizeExistingPath(const char *path, char *normalized, size_t capacity);

// Return a short label reflecting the current evaluation state of a node.
const char *NodeStateLabel(const Node *node);
