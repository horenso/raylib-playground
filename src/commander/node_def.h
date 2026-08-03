#pragma once

#include "types.h"

// Per-type dispatch table. Every NodeType has exactly one NodeDef entry that
// owns all type-specific behaviour: initialisation, connection rules, schema
// propagation, evaluation, rendering, and input handling.
typedef struct {
    // Identifier written to / read from save files (e.g. "Files", "Match").
    const char *name;

    // ---------- Lifecycle --------------------------------------------------

    // Populate title, default parameter values, bounds, and ports for a
    // freshly created node. NULL means the type cannot be created interactively
    // (e.g. NODE_LEGACY_NUMBER_FILTER).
    void (*init)(GraphContext *graph, Node *node);

    // ---------- Connection rules -------------------------------------------

    // Return true if the given upstream output port may feed this node's
    // input. Called from PortCanFeedNode(). NULL means no input is accepted.
    bool (*can_accept)(const Port *from);

    // ---------- Schema propagation ----------------------------------------

    // Port type that should be assigned to this node's input port(s) during
    // the schema-reset phase of PropagateSchemas() so the port chip can show
    // the expected type before a connection is made. VALUE_NONE means "any".
    ValueType expected_input_type;

    // True when this node's output schema depends on the upstream input and
    // therefore needs to participate in the iterative schema-computation pass
    // of PropagateSchemas().
    bool is_schema_computing;

    // When the node has no field_name configured yet and the input is a
    // record, PropagateSchemas() tries this field name first before falling
    // back to the first selectable field. NULL defaults to "name".
    const char *preferred_field_name;

    // Return true if a field of the given ValueType is a valid selection for
    // this node's field-selector dropdown. NULL means no field selector.
    bool (*field_is_selectable)(ValueType type);

    // Compute the output-port schema/type from the selected-field type.
    // 'input' and 'output' are already looked up and non-NULL. Return false
    // and write an error message into node->schema_error_message on failure.
    // NULL means the output schema is fixed (set once during init).
    bool (*propagate_schema)(Node *node, Port *input, Port *output, ValueType selected_type);

    // ---------- Field-selector UI -----------------------------------------

    // True if this node shows a field-selector dropdown button.
    bool uses_field_selector;

    // Label drawn to the left of the selector button (e.g. "Field", "From").
    const char *field_selector_label;

    // Y offset of the selector button from the top of the node, in logical
    // canvas units, measured from the top of the node body (not the header).
    // Feeds FieldSelectorYUnits(). Typical value: 12.0.
    float field_selector_y_offset;

    // ---------- Evaluation ------------------------------------------------

    // Run the node's logic. 'source' is the upstream output port (NULL for
    // source nodes that have no input). 'output' is the node's first output
    // port. Returns true on success.
    bool (*evaluate)(GraphContext *graph, Node *node, Port *source, Port *output);

    // ---------- Rendering -------------------------------------------------

    // Draw the body area between the header and the port section.
    void (*draw_content)(GraphContext *graph, Node *node);

    // Height in logical canvas units of the interactive-control hit-rectangle
    // used by MouseOverNodeControl(). Zero means use the default 42.0.
    float control_height;

    // ---------- Input handling --------------------------------------------

    // Return true if 'mouse' is over any text-editing control belonging to
    // this node. Called from MouseOverNodeTextBox() to set the I-beam cursor.
    // NULL means the node has no text-editing controls.
    bool (*mouse_in_edit_area)(GraphContext *graph, Node *node, Vector2 mouse);
} NodeDef;

// Return the NodeDef for a NodeType. Returns NULL for unknown types.
const NodeDef *GetNodeDef(NodeType type);

// Return the NodeType whose serialised name matches 'name', or -1 if not found.
int NodeTypeFromName(const char *name);
