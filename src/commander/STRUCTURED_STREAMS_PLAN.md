# Commander Structured Streams Plan

## Goal

Move Commander from string-list connections to Nushell-inspired structured
streams. Nodes should be able to inspect or transform one field while preserving
the complete row, so users do not need adapter nodes just to filter a filename
and later recover its path.

The motivating workflow is:

```text
Files
  -> Where(name matches ".jpg")
  -> Insert(destination from path using replacement)
  -> Move(source=path, destination=destination)
```

Only `Move` has filesystem side effects. Every preceding node is safe to run and
inspect.

## Core decisions

### Edges carry streams of typed values

Replace types such as `STRING_LIST` with an item type and an associated schema.
Every normal connection is a stream, so collection cardinality does not need to
be encoded in names such as `STRING_LIST`.

Initial primitive value types:

- `String`
- `Path`
- `Bool`
- `Int`
- `FileSize`
- `DateTime`
- `FileKind`
- `Record`

Do not use `Vec2<String>` or positional pairs for filesystem operations. Source
and destination must be named fields.

### Record compatibility is structural

A consumer specifies required fields or field capabilities, not an exact record
layout. A node requiring `{name: String}` accepts a record containing
`{path: Path, name: String, type: FileKind, size: FileSize}`.

Extra fields never make a connection incompatible.

### Field-aware nodes preserve rows

Filtering, sorting, and updating a field must retain the complete input record.
Extracting or discarding fields is always explicit through `Get` or `Select`.

For primitive streams, field-aware nodes expose a synthetic `Item` field. This
allows one `Where` node to work with both `Stream<String>` and record streams.

### Side-effect nodes consume explicit data

Filesystem actions do not contain reusable matching or replacement logic. A
`Move` node consumes records and lets the user choose a source `Path` field and
a destination `Path` field.

## Target node behavior

### Files

`Files` becomes a source of records:

```text
{
    path: Path,
    name: String,
    type: FileKind,
    size: FileSize,
    modified: DateTime,
}
```

Its current type and recursion controls remain source options.

### Where

Replaces the current string-specific Filter node over time.

```text
Input:  Stream<R>
Output: Stream<R>
```

Options:

- Field selector
- Operation appropriate for the selected field
- Comparison value
- Case-sensitive, whole-word, regex, and include/exclude options where relevant

The output schema is identical to the input schema.

### Update

Transforms an existing field while preserving all other fields.

```text
Input:  Stream<R>
Output: Stream<R>
```

The selected input field and output field are the same.

### Insert

Derives a new field from an existing field.

```text
Input:  Stream<R>
Output: Stream<R + {new_field: T}>
```

Initial operations should be constrained UI operations rather than a text
expression language:

- String replacement
- Path filename replacement
- Path extension replacement
- Path parent/join operations
- Basic formatting

For the rename workflow, `Insert` reads `path` and writes `destination`.

### Get and Select

These are deliberate projection nodes:

- `Get(field)` converts `Stream<Record>` into a stream of that field's values.
- `Select(fields...)` retains a subset of record fields.

Neither is required for ordinary filtering, sorting, or transformation.

### Move

`Move` is the filesystem side-effect node used for renames and moves.

Options:

- Source field selector, requiring `Path`
- Destination field selector, requiring `Path`
- Collision policy, defaulting to `Fail`
- Missing-source policy, defaulting to `Fail`

Before changing anything, it preflights the complete input:

- Missing sources
- Duplicate sources
- Duplicate destinations
- Existing destination conflicts
- Identical source and destination
- Missing or invalid parent directories
- Rename cycles such as `a -> b` and `b -> a`

If preflight fails, no filesystem changes are made. Cycles require temporary
staging paths. The node should have danger styling and report planned,
successful, skipped, and failed counts.

## Type and schema representation

Introduce a runtime value representation separate from the serialized graph:

```c
typedef enum {
    VALUE_STRING,
    VALUE_PATH,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_FILE_SIZE,
    VALUE_DATETIME,
    VALUE_FILE_KIND,
    VALUE_RECORD,
} ValueType;
```

A record schema contains ordered, named fields:

```c
typedef struct {
    char name[MAX_FIELD_NAME];
    ValueType type;
} FieldSchema;

typedef struct {
    FieldSchema fields[MAX_FIELDS];
    int field_count;
} RecordSchema;
```

Runtime records store values corresponding to the schema. Use bounded storage
initially to match Commander's existing fixed-capacity design. Avoid allocating
one independent heap object per cell.

Port schemas should be derived from node configuration and upstream schemas.
Runtime output values are not serialized; only node configuration and links are.

## Connection and configuration rules

Connections have three useful states:

- **Compatible:** all configured requirements are satisfied.
- **Needs configuration:** the upstream schema has usable fields, but the node
  has not selected one yet.
- **Incompatible:** no upstream field can satisfy the requirement.

After connecting a record stream, field dropdowns are populated from the
upstream schema and filtered by capability. For example, `Move` only lists
`Path` fields.

If an upstream schema changes:

1. Recompute downstream schemas.
2. Keep still-valid field selections.
3. Mark invalid nodes as configuration errors.
4. Mark the node and all downstream consumers dirty.
5. Never execute a node with an unresolved schema error.

Port chips should show the broad item type, such as `Rows` or `Path`. Hover and
the inspector should expose the full schema.

## Evaluation rules

Keep the current safe execution behavior:

- Editing a node marks it and downstream nodes dirty.
- A node play button evaluates dirty upstream dependencies and that node only.
- It never evaluates downstream consumers.
- The toolbar Run button explicitly evaluates the complete graph.
- Loading or creating a graph never evaluates it automatically.

Schema propagation is pure and should happen without executing nodes.

## Inspector changes

The inspector becomes schema-aware:

- Primitive streams use the current one-column list.
- Record streams render a table with field-name headers.
- `Path`, `FileSize`, `DateTime`, and `FileKind` receive type-aware formatting.
- Changed/derived fields can be visually distinguished.
- Large values are truncated in cells but available in a detail view.

For a planned move, the important preview is:

```text
path                         destination
/photos/IMG_001.jpg          /photos/holiday_001.jpg
/photos/IMG_002.jpg          /photos/holiday_002.jpg
```

## Serialization

Extend node serialization with:

- Selected input field names
- Selected output/new field names
- Operation kind
- Operation-specific options
- Collision and error policies

Serialize field names rather than numeric field indexes so column ordering can
change safely.

Older graphs must continue to load. Legacy string-list ports can initially map
to `Stream<String>`. Existing Filter nodes can either remain as a legacy node or
be upgraded in memory to `Where(field=Item)`.

## Staged implementation

### Phase 1: Value and schema foundation

1. Separate stream cardinality from item type.
2. Add primitive `Value` types and record schemas.
3. Add structural compatibility checks.
4. Add schema propagation independent of evaluation.
5. Preserve support for legacy `Stream<String>` nodes.

### Phase 2: Structured Files output

1. Make Files produce file records.
2. Populate path, name, type, size, and modified fields.
3. Upgrade the inspector to render record tables.
4. Retain the current Files type and depth options.

### Phase 3: Row-preserving tools

1. Add `Where` with a schema-driven field selector.
2. Add `Insert` with string and path replacement operations.
3. Add `Update`.
4. Add `Get` and `Select` only after row-preserving operations work.

### Phase 4: Safe filesystem action

1. Add `Move` with source/destination field selectors.
2. Implement complete preflight validation.
3. Implement temporary staging for rename cycles.
4. Add success and error outputs.
5. Add danger styling and clear execution summaries.

### Phase 5: Generalization

Add `Sort`, `Unique`, conversions, nested field paths, and richer schemas only
after real workflows demonstrate the need.

## Testing

Add tests for:

- Structural schema compatibility and extra-field acceptance
- Schema propagation through `Where`, `Update`, and `Insert`
- Invalid field selections after upstream schema changes
- Primitive `Item` field behavior
- Legacy graph loading
- Files output for files, folders, both, and recursive traversal
- Move preflight conflicts and all-or-nothing behavior
- Duplicate destinations
- Missing sources
- Rename cycles and temporary staging
- Dirty propagation and upstream-only node execution
- No evaluation during graph load or startup

Use temporary directories for filesystem tests and verify both final paths and
absence of partial mutations after failure.

## Non-goals for the first version

- A Nushell-compatible expression language
- Closures or function-valued ports
- Arbitrary nested records
- Dynamic unbounded schemas
- Implicit positional zipping of independent streams
- Hidden source paths stored only as metadata
- Automatic coercion between `String` and `Path`

## Open questions

- Should `Files` initially output records directly, or expose a compatibility
  mode for existing string-based graphs?
- Should `Update` and `Insert` be separate nodes or one node with an explicit
  overwrite/new-field mode?
- Should filesystem actions be included in toolbar Run, or require an additional
  confirmation policy?
- Should schema errors prevent saving, or only prevent evaluation?
- Which file metadata fields are worth collecting eagerly versus on demand?
