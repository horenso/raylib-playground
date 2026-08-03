#include "streams.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

const char *ValueTypeName(ValueType type) {
    switch (type) {
    case VALUE_STRING:
        return "String";
    case VALUE_BOOL:
        return "Bool";
    case VALUE_INT:
        return "Int";
    case VALUE_FLOAT:
        return "Float";
    case VALUE_SIZE:
        return "Size";
    case VALUE_DATETIME:
        return "DateTime";
    case VALUE_RECORD:
        return "Record";
    default:
        return "Unresolved";
    }
}

bool ValueTypeIsText(ValueType type) { return type == VALUE_STRING; }

bool ValueTypeIsNumeric(ValueType type) {
    return type == VALUE_INT || type == VALUE_FLOAT || type == VALUE_SIZE || type == VALUE_DATETIME;
}

int SchemaFieldIndex(const RecordSchema *schema, const char *name) {
    if (!schema || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < schema->field_count; i++) {
        if (TextIsEqual(schema->fields[i].name, name)) {
            return i;
        }
    }
    return -1;
}

bool SchemaHasField(const RecordSchema *schema, const char *name, ValueType type) {
    int index = SchemaFieldIndex(schema, name);
    return index >= 0 && (type == VALUE_NONE || schema->fields[index].type == type);
}

bool SchemaAddField(RecordSchema *schema, const char *name, ValueType type, bool derived) {
    if (!schema || !name || !name[0] || schema->field_count >= MAX_FIELDS || SchemaFieldIndex(schema, name) >= 0) {
        return false;
    }
    FieldSchema *field = &schema->fields[schema->field_count++];
    TextCopy(field->name, name);
    field->type = type;
    field->derived = derived;
    return true;
}

const StreamValue *ItemFieldValue(const Port *port, const StreamItem *item, const char *field_name) {
    if (!port || !item) {
        return NULL;
    }
    if (port->data_type != VALUE_RECORD) {
        return !field_name || !field_name[0] || TextIsEqual(field_name, "Item") ? &item->values[0] : NULL;
    }
    int index = SchemaFieldIndex(&port->schema, field_name);
    return index >= 0 ? &item->values[index] : NULL;
}

StreamValue *MutableItemFieldValue(Port *port, StreamItem *item, const char *field_name) {
    return (StreamValue *)ItemFieldValue(port, item, field_name);
}

const char *ValueDisplayText(const StreamValue *value, char *buffer, int buffer_size) {
    if (!value) {
        return "";
    }
    switch (value->type) {
    case VALUE_STRING:
        return value->as.text;
    case VALUE_BOOL:
        return value->as.boolean ? "true" : "false";
    case VALUE_INT:
        snprintf(buffer, (size_t)buffer_size, "%lld", value->as.integer);
        return buffer;
    case VALUE_FLOAT:
        snprintf(buffer, (size_t)buffer_size, "%.15g", value->as.floating);
        return buffer;
    case VALUE_SIZE: {
        double size = (double)value->as.file_size;
        const char *unit = "B";
        if (size >= 1024.0 * 1024.0 * 1024.0) {
            size /= 1024.0 * 1024.0 * 1024.0;
            unit = "GB";
        } else if (size >= 1024.0 * 1024.0) {
            size /= 1024.0 * 1024.0;
            unit = "MB";
        } else if (size >= 1024.0) {
            size /= 1024.0;
            unit = "KB";
        }
        snprintf(buffer, (size_t)buffer_size, size >= 10.0 || unit[0] == 'B' ? "%.0f %s" : "%.1f %s", size, unit);
        return buffer;
    }
    case VALUE_DATETIME: {
        time_t timestamp = (time_t)value->as.datetime;
        struct tm local = {0};
        localtime_r(&timestamp, &local);
        strftime(buffer, (size_t)buffer_size, "%Y-%m-%d %H:%M", &local);
        return buffer;
    }
    default:
        return "";
    }
}

void SetTextValue(StreamValue *value, ValueType type, const char *text) {
    if (!value) {
        return;
    }
    memset(value, 0, sizeof(*value));
    value->type = type;
    TextCopy(value->as.text, text ? text : "");
}
