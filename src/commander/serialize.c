#include "serialize.h"
#include "graph.h"
#include "node_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// File format (text):
//   camera <zoom> <target_x> <target_y>
//   node <id> <type> <x> <y> <w> <h> <legacy_state> <parameter_escaped> <case_sensitive> <whole_word> <use_regex>
//        <title_escaped> <exclude> <directory_entry_type> <directory_recursive>
//   filter_config <node_id> <number_escaped> <number_op> <size_unit> <field_escaped>
//   (legacy files may use match_config for the same data)
//   port <id> <node_id> <name> <data_type> <direction> <rel_x> <rel_y>
//   link <from_port_id> <to_port_id>
//   eof

static void escape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        if (src[i] == ' ') {
            dst[j++] = '\\';
            dst[j++] = '_';
        } else if (src[i] == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (src[i] == '\\') {
            dst[j++] = '\\';
            dst[j++] = '\\';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static void unescape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 1; i++) {
        if (src[i] == '\\' && src[i + 1]) {
            i++;
            if (src[i] == '_') {
                dst[j++] = ' ';
            } else if (src[i] == 'n') {
                dst[j++] = '\n';
            } else if (src[i] == '\\') {
                dst[j++] = '\\';
            } else {
                dst[j++] = '\\';
                dst[j++] = src[i];
            }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

bool SaveGraph(GraphContext *graph, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }

    fprintf(f, "camera %f %f %f\n", graph->camera.zoom, graph->camera.target.x, graph->camera.target.y);

    for (int i = 0; i < graph->node_count; i++) {
        Node *n = &graph->nodes[i];
        char esc[512];
        escape(n->parameter, esc, sizeof(esc));
        char title_esc[128];
        escape(n->title, title_esc, sizeof(title_esc));
        const NodeDef *def = GetNodeDef(n->type);
        const char *type_name = def && def->name ? def->name : "?";
        fprintf(f, "node %d %s %f %f %f %f %d %s %d %d %d %s %d %d %d\n", n->id, type_name, n->bounds.x, n->bounds.y,
                n->bounds.width, n->bounds.height, 0, esc, (int)n->filter_case_sensitive, (int)n->filter_whole_word,
                (int)n->filter_use_regex, title_esc, (int)n->filter_exclude, (int)n->directory_entry_type,
                (int)n->directory_recursive);
    }

    for (int i = 0; i < graph->node_count; i++) {
        Node *n = &graph->nodes[i];
        if (n->type != NODE_FILTER) {
            continue;
        }
        char number_esc[512];
        char field_esc[128];
        escape(n->number_parameter, number_esc, sizeof(number_esc));
        escape(n->field_name, field_esc, sizeof(field_esc));
        fprintf(f, "filter_config %d %s %d %d %s\n", n->id, number_esc, (int)n->number_filter_op, (int)n->file_size_unit,
                field_esc[0] ? field_esc : "-");
    }

    for (int i = 0; i < graph->port_count; i++) {
        Port *p = &graph->ports[i];
        char name_esc[64];
        escape(p->name, name_esc, sizeof(name_esc));
        fprintf(f, "port %d %d %s %d %d %f %f\n", p->id, p->node_id, name_esc, (int)p->data_type, (int)p->direction,
                p->relative_pos.x, p->relative_pos.y);
    }

    for (int i = 0; i < graph->link_count; i++) {
        fprintf(f, "link %d %d\n", graph->links[i].from_port_id, graph->links[i].to_port_id);
    }

    fprintf(f, "eof\n");
    fclose(f);
    return true;
}

bool LoadGraph(GraphContext *graph, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    memset(graph->nodes, 0, sizeof(graph->nodes));
    memset(graph->ports, 0, sizeof(graph->ports));
    memset(graph->links, 0, sizeof(graph->links));
    graph->node_count = 0;
    graph->port_count = 0;
    graph->link_count = 0;
    graph->selected_node_id = -1;
    graph->active_port_id = -1;
    graph->dragging_node_id = -1;
    for (int i = 0; i < MAX_INSPECTOR_WINDOWS; i++) {
        graph->inspector_windows[i].port_id = -1;
        graph->inspector_windows[i].active = -1;
    }
    graph->add_menu_open = false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char tag[16];
        if (sscanf(line, "%15s", tag) != 1) {
            continue;
        }

        if (strcmp(tag, "eof") == 0) {
            break;
        }

        if (strcmp(tag, "camera") == 0) {
            sscanf(line, "camera %f %f %f", &graph->camera.zoom, &graph->camera.target.x, &graph->camera.target.y);

        } else if (strcmp(tag, "node") == 0) {
            if (graph->node_count >= MAX_NODES) {
                continue;
            }
            Node *n = &graph->nodes[graph->node_count++];
            memset(n, 0, sizeof(*n));
            n->list_active = -1;
            n->editing_control = -1;
            int cs, ww, re, exclude = 0;
            int directory_entry_type = DIRECTORY_ENTRY_FILES;
            int directory_recursive = 0;
            char type_str[32] = {0};
            char esc_param[512] = {0};
            char esc_title[128] = {0};
            sscanf(line, "node %d %31s %f %f %f %f %*d %511s %d %d %d %127s %d %d %d", &n->id, type_str, &n->bounds.x,
                   &n->bounds.y, &n->bounds.width, &n->bounds.height, esc_param, &cs, &ww, &re, esc_title, &exclude,
                   &directory_entry_type, &directory_recursive);
            int type_int = NodeTypeFromName(type_str);
            if (type_int < 0) {
                // Backward compat: old files saved integer type codes
                char *endp;
                long val = strtol(type_str, &endp, 10);
                if (*endp == '\0' && val >= 0 && val <= NODE_CSV) {
                    type_int = (int)val;
                }
            }
            if (type_int < 0) {
                graph->node_count--;
                continue;
            }
            bool legacy_number = type_int == NODE_LEGACY_NUMBER_FILTER;
            n->type = legacy_number ? NODE_FILTER : (NodeType)type_int;
            TextCopy(n->number_parameter, "0");
            n->number_filter_op = NUMBER_FILTER_GTE;
            if (directory_entry_type < DIRECTORY_ENTRY_FILES || directory_entry_type > DIRECTORY_ENTRY_BOTH) {
                directory_entry_type = DIRECTORY_ENTRY_FILES;
            }
            n->directory_entry_type = (DirectoryEntryType)directory_entry_type;
            n->directory_recursive = (bool)directory_recursive;
            n->filter_case_sensitive = (bool)cs;
            n->filter_whole_word = (bool)ww;
            n->filter_use_regex = (bool)re;
            n->filter_exclude = (bool)exclude;
            n->is_dirty = true;
            unescape(esc_param, n->parameter, sizeof(n->parameter));
            unescape(esc_title, n->title, sizeof(n->title));
            if (n->type == NODE_FILTER) {
                n->bounds.height = 220.0f;
                TextCopy(n->title, "Filter");
                if (legacy_number) {
                    TextCopy(n->number_parameter, n->parameter);
                    TextCopy(n->parameter, "\\.c$");
                    n->filter_use_regex = true;
                    n->filter_exclude = false;
                }
            }

        } else if (strcmp(tag, "filter_config") == 0 || strcmp(tag, "match_config") == 0) {
            int node_id, op, unit;
            char number_esc[512] = {0};
            char field_esc[128] = {0};
            int parsed = sscanf(line, "%*s %d %511s %d %d %127s", &node_id, number_esc, &op, &unit, field_esc);
            if (parsed < 4) {
                continue;
            }
            Node *n = FindNode(graph, node_id);
            if (!n || n->type != NODE_FILTER) {
                continue;
            }
            unescape(number_esc, n->number_parameter, sizeof(n->number_parameter));
            if (op >= NUMBER_FILTER_EQ && op <= NUMBER_FILTER_GTE) {
                n->number_filter_op = (NumberFilterOp)op;
            }
            if (unit >= FILE_SIZE_BYTES && unit <= FILE_SIZE_TB) {
                n->file_size_unit = (FileSizeUnit)unit;
            }
            if (parsed >= 5 && !TextIsEqual(field_esc, "-")) {
                unescape(field_esc, n->field_name, sizeof(n->field_name));
            }

        } else if (strcmp(tag, "port") == 0) {
            if (graph->port_count >= MAX_PORTS) {
                continue;
            }
            Port *p = &graph->ports[graph->port_count++];
            memset(p, 0, sizeof(*p));
            int dtype, dir;
            char name_esc[64] = {0};
            sscanf(line, "port %d %d %63s %d %d %f %f", &p->id, &p->node_id, name_esc, &dtype, &dir, &p->relative_pos.x,
                   &p->relative_pos.y);
            p->data_type = (PortDataType)dtype;
            p->direction = (PortDirection)dir;
            p->schema_valid = p->direction == PORT_DIR_OUTPUT && p->data_type != VALUE_NONE;
            unescape(name_esc, p->name, sizeof(p->name));

            Node *n = NULL;
            for (int i = 0; i < graph->node_count; i++) {
                if (graph->nodes[i].id == p->node_id) {
                    n = &graph->nodes[i];
                    break;
                }
            }
            if (n) {
                if (p->direction == PORT_DIR_INPUT && n->input_count < 4) {
                    n->input_port_ids[n->input_count++] = p->id;
                } else if (p->direction == PORT_DIR_OUTPUT && n->output_count < 4) {
                    n->output_port_ids[n->output_count++] = p->id;
                }
            }

        } else if (strcmp(tag, "link") == 0) {
            if (graph->link_count >= MAX_LINKS) {
                continue;
            }
            Link *l = &graph->links[graph->link_count++];
            sscanf(line, "link %d %d", &l->from_port_id, &l->to_port_id);
        }
    }

    // Older graphs have only the Exec stdout port. Upgrade them in memory so
    // loading an existing graph exposes stderr without changing the file format.
    for (int i = 0; i < graph->node_count; i++) {
        Node *node = &graph->nodes[i];
        if (node->type == NODE_DIRECTORY_LIST && node->bounds.height < 220.0f) {
            node->bounds.height = 220.0f;
        }
        if (node->type != NODE_EXEC) {
            continue;
        }
        if (node->bounds.width < 320.0f) {
            node->bounds.width = 320.0f;
        }
        bool has_stderr = false;
        for (int output_index = 0; output_index < node->output_count; output_index++) {
            Port *port = FindPort(graph, node->output_port_ids[output_index]);
            if (port) {
                port->relative_pos.x = node->bounds.width;
                if (TextIsEqual(port->name, "Stderr")) {
                    has_stderr = true;
                }
            }
        }
        if (!has_stderr) {
            AddPort(graph, node, "Stderr", PORT_TYPE_STRING_LIST, PORT_DIR_OUTPUT, 148);
        }
    }

    PropagateSchemas(graph);
    fclose(f);
    return true;
}
