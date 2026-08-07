#include "helpers.h"
#include "streams.h"

#include <stdlib.h>
#include <string.h>

void AppendPrimitiveText(Port *port, const char *text, size_t length) {
    if (!port || port->item_count >= MAX_ITEMS) {
        return;
    }
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        length--;
    }
    char copy[MAX_PATH_LENGTH];
    size_t copy_length = length < sizeof(copy) - 1 ? length : sizeof(copy) - 1;
    memcpy(copy, text, copy_length);
    copy[copy_length] = '\0';
    SetTextValue(&port->items[port->item_count++].values[0], port->data_type, copy);
}

void ReadLines(FILE *stream, Port *port) {
    char line[MAX_PATH_LENGTH];
    while (port && port->item_count < MAX_ITEMS && fgets(line, sizeof(line), stream)) {
        AppendPrimitiveText(port, line, strlen(line));
    }
}

bool NormalizeExistingPath(const char *path, char *normalized, size_t capacity) {
    if (!path || !path[0] || !normalized || capacity == 0) {
        return false;
    }
    char *resolved = realpath(path, NULL);
    if (!resolved) {
        return false;
    }
    size_t length = strlen(resolved);
    if (length >= capacity) {
        free(resolved);
        return false;
    }
    memcpy(normalized, resolved, length + 1);
    free(resolved);
    return true;
}

const char *NodeStateLabel(const Node *node) {
    return node->schema_error                      ? "SCHEMA ERROR"
           : node->evaluation_failed               ? "FAILED"
           : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
           : node->is_dirty                        ? "NOT RUN"
                                                   : "CURRENT";
}
