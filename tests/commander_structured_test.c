#include "evaluate.h"
#include "graph.h"
#include "streams.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void Touch(const char *path) {
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("test", file);
    fclose(file);
}

int main(void) {
    static GraphContext graph;
    SeedGraph(&graph);

    char directory[] = "/tmp/commander_structured_XXXXXX";
    assert(mkdtemp(directory));
    char jpg[MAX_PATH_LENGTH];
    char text[MAX_PATH_LENGTH];
    snprintf(jpg, sizeof(jpg), "%s/IMG_001.jpg", directory);
    snprintf(text, sizeof(text), "%s/notes.txt", directory);
    Touch(jpg);
    Touch(text);

    Node *files = &graph.nodes[0];
    Node *where = &graph.nodes[1];
    Node *insert = &graph.nodes[2];
    TextCopy(files->parameter, directory);
    MarkNodeDirty(&graph, files->id);
    assert(EvaluateNode(&graph, insert, 0));

    Port *file_rows = NodeOutputPort(&graph, files, 0);
    Port *filtered_rows = NodeOutputPort(&graph, where, 0);
    Port *derived_rows = NodeOutputPort(&graph, insert, 0);
    assert(file_rows->data_type == VALUE_RECORD);
    assert(file_rows->schema.field_count == 5);
    assert(file_rows->item_count == 2);
    assert(filtered_rows->item_count == 1);
    assert(derived_rows->schema.field_count == 6);
    assert(SchemaHasField(&derived_rows->schema, "destination", VALUE_PATH));
    int destination = SchemaFieldIndex(&derived_rows->schema, "destination");
    assert(destination >= 0);
    assert(strstr(derived_rows->items[0].values[destination].as.text, "holiday_001.jpg"));

    Node *get = AddNode(&graph, NODE_GET, (Vector2){1000, 110});
    assert(get);
    TextCopy(get->field_name, "destination");
    assert(AddLink(&graph, insert->output_port_ids[0], get->input_port_ids[0]));
    PropagateSchemas(&graph);
    assert(EvaluateNode(&graph, get, 0));
    Port *values = NodeOutputPort(&graph, get, 0);
    assert(values->data_type == VALUE_PATH);
    assert(values->item_count == 1);

    unlink(jpg);
    unlink(text);
    rmdir(directory);
    puts("structured commander test: ok");
    return 0;
}
