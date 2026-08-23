# MCP Support for Project IGI Editor

**Status:** Proposed implementation design
**Date:** 2026-08-23
**Target branch:** `feature/editor-mcp-support`

## Goal

Add a professional Model Context Protocol (MCP) server for Project IGI Editor that exposes the game-affecting level and asset operations already supported by the editor, while excluding editor-only preferences such as font size, panel layout, viewport cosmetics, and local UI state.

The server must support deterministic inspection and mutation of game data, safe persistence with backups and validation, MCP-compatible stdio transport, opt-in authenticated localhost Streamable HTTP, and evidence-backed unit, integration, security, build, and visual verification.

## Scope and non-goals

### In scope

- Level/project discovery, load, reload, validate, save, backup, restore, dry-run, and revision reporting.
- Task-tree and `LevelObject` inspection and mutation: create, duplicate, delete, rename, parent/child changes, type/parameter updates, and game-affecting transforms.
- Model identifiers, object type, scale, position, Euler orientation, building/prop/door/vehicle/train/spline/camera/terminal fields, and serialized task parameters.
- AI soldiers and related tasks: AI type, team, graph binding, patrol/script content, script compile/validation, weapon and ammunition child tasks, and authored animation/behavior fields.
- Objectives and mission/task data that are serialized into the game level.
- Navigation graph nodes, links, node position/radius/material/criteria, and graph persistence.
- Terrain edits and game-affecting lightmap/object-lightmap operations that have an existing safe persistence path.
- Game asset inspection and existing converter-backed operations where the result is consumed by the game (`DAT`, `MTP`, `RES`, `TEX`, `MEF`, `QSC`, `QVM`, `FNT`, terrain, and graph data).
- Read-only MCP resources for project manifest, level manifest, object snapshots, graph snapshots, validation reports, and operation history.

### Out of scope

- Font size, colors, panel layout, hotkeys, viewport/window dimensions, camera preferences, debug overlays, editor-only selection/hover state, and other settings with no effect on saved game data.
- Arbitrary shell execution, arbitrary filesystem access, arbitrary network requests, or direct SQL/process injection.
- Unvalidated raw QSC/QVM/binary replacement through an MCP argument. Raw file replacement, if needed later, must be a separate explicitly authorized feature with format validation.

## Architecture

The implementation has four layers with narrow interfaces:

1. **`McpJson`** — a small strict JSON value/parser/writer used for JSON-RPC and tool arguments. It supports objects, arrays, strings, finite numbers, booleans, and null; rejects duplicate keys, malformed UTF-8/escapes, excessive nesting, and oversized messages. It has no logging side effects.
2. **`McpProtocol`** — JSON-RPC 2.0 lifecycle, deterministic `tools/list`, `resources/list/read`, structured tool results, protocol errors, and capability negotiation. It is transport-neutral and writes no diagnostics to stdout.
3. **`GameDataService`** — the validated game-data application layer. It owns an allowlisted project session, maps stable task/object identifiers to existing `LevelObjects`, QSC/QVM, graph, terrain, and converter APIs, performs revision checks and atomic save transactions, and returns before/after records. It must not depend on GLUT or renderer UI state.
4. **Transports** — newline-delimited stdio for MCP clients and an opt-in localhost Streamable HTTP endpoint. Both call the same protocol/server object and therefore expose identical tools and schemas.

The first release is file-backed and deterministic. A GUI process is not required to be running. After a successful save, the editor can reload the level normally; the visual acceptance test launches the editor against the saved result. Any future live-editor bridge must use a queued main-thread command adapter rather than calling OpenGL/editor state from a network thread.

## Executables and invocation

Add a Win32 Release target named `igi_mcp`:

```powershell
.\igi_mcp.exe --stdio --project D:\IGI1
.\igi_mcp.exe --http --project D:\IGI1 --host 127.0.0.1 --port 0
```

Rules:

- `--stdio` is the default and is the recommended client integration.
- `--http` is explicit opt-in. It binds to `127.0.0.1` only; `0.0.0.0` is rejected.
- Port `0` selects an available local port and reports the endpoint and generated bearer token only on stderr.
- HTTP requires `Authorization: Bearer <token>`, validates `Origin` when present against the configured localhost origins, and returns safe JSON-RPC errors without filesystem paths, stack traces, or secrets.
- stdio stdout contains only one valid JSON-RPC message per line. Logs go to stderr.
- The project root and game root are canonicalized once at startup. Operations may address only the configured root and known level/asset subdirectories.

## MCP surface

Tool names are stable, sorted, and grouped by domain. Every mutating tool accepts `dry_run`, `expected_revision`, and `backup` where applicable. Mutations return a transaction id, changed paths, revision before/after, and concise before/after summaries.

### Session and validation tools

- `project_info`
- `project_list_levels`
- `level_open`
- `level_reload`
- `level_validate`
- `level_save`
- `level_backup`
- `level_restore`
- `level_undo`
- `level_redo`
- `operation_get`

### Task/object tools

- `task_list`
- `task_get`
- `task_create`
- `task_update`
- `task_delete`
- `task_duplicate`
- `task_reparent`
- `object_set_transform`
- `object_set_model`
- `object_set_type`
- `object_set_parameter`
- `object_get_schema`

Transforms use explicit native game units and radians/degrees as documented by the existing field schema; the response includes normalized position/orientation/scale and the exact serialized fields changed. A task id is never inferred from a natural-language query or array position.

### AI, weapon, and mission tools

- `ai_get`
- `ai_update`
- `ai_set_script`
- `ai_validate_script`
- `ai_compile_script`
- `ai_list_weapons`
- `ai_set_weapon_loadout`
- `pickup_create_or_update`
- `mission_objective_list`
- `mission_objective_update`

Scripts are parsed and compiled through the existing QSC/QVM pipeline. Compilation must validate the output before replacing the game file and must restore the previous file on failure.

### Graph and terrain tools

- `graph_list`
- `graph_get`
- `graph_node_create`
- `graph_node_update`
- `graph_node_delete`
- `graph_link_create`
- `graph_link_delete`
- `terrain_get_metadata`
- `terrain_apply_edit`
- `terrain_validate`
- `lightmap_get`
- `lightmap_rebuild_or_clear`

Graph updates reuse the existing graph serializer and adjacency-table regeneration. Terrain mutation is restricted to the existing supported HMP/LMP/CTR edit operations and must reject edits outside the loaded level bounds.

### Asset tools

- `asset_list`
- `asset_inspect`
- `asset_validate`
- `asset_convert`
- `asset_pack_or_update`

These call existing converter/parser seams rather than duplicating binary-format logic. The tool schema identifies the format and operation explicitly and never accepts an arbitrary command line.

### Resources

Expose read-only resources with stable URIs:

- `igi://project`
- `igi://levels`
- `igi://level/{level}/manifest`
- `igi://level/{level}/objects`
- `igi://level/{level}/graphs`
- `igi://level/{level}/validation`
- `igi://operation/{transaction_id}`

Resources contain structured data and redacted diagnostics. They do not expose arbitrary local files.

## Validation and transaction rules

- Validate all JSON argument types, required fields, enum values, finite numeric ranges, string lengths, collection sizes, and cross-references before mutation.
- Require a configured level and stable task/object id for all scoped mutations.
- Use a monotonic in-memory revision plus a content fingerprint of relevant source files. Reject stale `expected_revision` with a conflict error instead of overwriting another edit.
- Create a timestamped backup before the first write in a transaction. Write each changed file to a same-directory temporary file, flush it, then atomically replace the destination. Preserve the prior file if serialization, compile, or post-write validation fails.
- Reparse/reload the changed representation and validate it before reporting success.
- Keep an operation journal containing transaction id, operation name, level, changed paths relative to the configured root, and redacted result status. Do not log raw request payloads or secrets.
- Serialize mutations per project session. Read-only resources may run concurrently only when they observe an immutable snapshot.
- Never expose editor-only settings in `tools/list`; a contract test will reject forbidden tool names/fields.

## Error contract

Use JSON-RPC errors for protocol/argument failures and successful tool results with `isError: true` for domain failures that are useful to the model. Each domain error has a stable code such as `invalid_arguments`, `path_forbidden`, `level_not_open`, `stale_revision`, `unsupported_operation`, `validation_failed`, `compile_failed`, `backup_failed`, or `write_failed`. Messages are actionable but redact absolute paths, command lines, credentials, and raw provider/process output.

## Testing strategy

### Unit tests

- JSON parser/writer round trips, malformed input, duplicate keys, limits, escaping, and finite-number checks.
- JSON-RPC lifecycle, method-not-found, invalid params, notifications, deterministic tool ordering, and schema contract.
- Path allowlisting, level/id/type/range validation, stale revision detection, transaction journaling, and backup/atomic-write behavior.
- Task/object transforms, model/type/parameter updates, AI loadouts/scripts, graph edits, terrain bounds, and resource snapshots against synthetic fixtures.

### Integration tests

- Spawn `igi_mcp --stdio`, send initialize/initialized, list tools/resources, call read-only and mutating tools, and assert stdout contains only valid MCP messages.
- Run an end-to-end level edit, save, reopen/reparse, and compare the expected serialized fields and unchanged-byte regions.
- Compile a valid and invalid AI script and prove invalid output leaves the original QVM/QSC intact.
- Start HTTP on an ephemeral localhost port, reject missing/invalid bearer tokens and Origin values, perform initialize/list/call with the negotiated protocol headers, and reuse any returned session header as required by the selected MCP revision.
- Exercise concurrent read requests and serialized write conflict behavior.

### Existing regression/build tests

- Build `igi_mcp`, `igi_tests`, and `igi1ed` for Win32 Release.
- Run the MCP-focused tests, then the complete existing unit/parser suite, then the level/QVM verification suite with its documented bounds.
- Run sanitizer/static checks available for the selected toolchain and a secret/path scan of the final diff.

### Visual/runtime verification

- Launch the Win32 editor with a fixture or installed level from the isolated worktree.
- Use the MCP server to move and rotate a visible building, change its model id, edit an AI/weapon task, and save.
- Capture a before/after screenshot and confirm the editor renders the persisted result at the expected location/model; inspect the task/AI property view where applicable.
- Reload/reset and confirm backup/restore and undo/redo behavior visually and through serialized data.

## Commit sequence

Use small, independently testable commits:

1. `test: define MCP JSON and JSON-RPC contract`
2. `feat: add bounded MCP JSON/protocol core`
3. `test: define project session and transaction safety`
4. `feat: add file-backed game data service`
5. `feat: expose task and object game-data tools`
6. `feat: expose AI mission graph terrain and asset tools`
7. `feat: add stdio transport and integration harness`
8. `feat: add opt-in authenticated localhost HTTP transport`
9. `docs: document MCP setup and game-data tool surface`
10. `test: add full MCP build and visual verification coverage`

Each commit must pass its focused test command before moving to the next. No production claim is made until the full completion audit confirms every scoped tool, transport, security rule, documentation artifact, build target, and verification gate.
