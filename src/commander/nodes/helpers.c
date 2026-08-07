#include "helpers.h"
#include "streams.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static bool WalkDirectory(const char *directory, bool recursive, DirectoryEntryVisitor visitor, void *context,
                          bool *stopped) {
    DIR *dir = opendir(directory);
    if (!dir) {
        return false;
    }

    bool success = true;
    struct dirent *entry = NULL;
    while (!*stopped && (entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char path[MAX_PATH_LENGTH];
        int length = snprintf(path, sizeof(path), "%s%s%s", directory,
                              directory[0] && directory[strlen(directory) - 1] == '/' ? "" : "/", entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            continue;
        }
        if (!visitor(path, context)) {
            *stopped = true;
            break;
        }
        if (!recursive) {
            continue;
        }
        struct stat info;
        if (lstat(path, &info) == 0 && S_ISDIR(info.st_mode) && !WalkDirectory(path, true, visitor, context, stopped)) {
            success = false;
        }
    }
    closedir(dir);
    return success;
}

bool VisitDirectoryEntries(const char *directory, bool recursive, DirectoryEntryVisitor visitor, void *context) {
    if (!directory || !directory[0] || !visitor) {
        return false;
    }
    bool stopped = false;
    return WalkDirectory(directory, recursive, visitor, context, &stopped);
}

const char *PathFileName(const char *path) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path ? path : "";
}

void CopyPathDirectory(const char *path, char *directory, size_t capacity) {
    if (!directory || capacity == 0) {
        return;
    }
    directory[0] = '\0';
    if (!path) {
        return;
    }
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return;
    }
    size_t length = slash == path ? 1 : (size_t)(slash - path);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(directory, path, length);
    directory[length] = '\0';
}

const char *NodeStateLabel(const Node *node) {
    return node->is_running                        ? "RUNNING"
           : node->schema_error                    ? "SCHEMA ERROR"
           : node->evaluation_failed               ? "FAILED"
           : node->is_dirty && node->has_evaluated ? "DIRTY | cached"
           : node->is_dirty                        ? "NOT RUN"
                                                   : "CURRENT";
}
