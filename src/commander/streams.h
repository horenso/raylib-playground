#pragma once

#include "types.h"

const char *ValueTypeName(ValueType type);
bool ValueTypeIsText(ValueType type);
int SchemaFieldIndex(const RecordSchema *schema, const char *name);
bool SchemaHasField(const RecordSchema *schema, const char *name, ValueType type);
bool SchemaAddField(RecordSchema *schema, const char *name, ValueType type, bool derived);
const StreamValue *ItemFieldValue(const Port *port, const StreamItem *item, const char *field_name);
StreamValue *MutableItemFieldValue(Port *port, StreamItem *item, const char *field_name);
const char *ValueDisplayText(const StreamValue *value, char *buffer, int buffer_size);
void SetTextValue(StreamValue *value, ValueType type, const char *text);
