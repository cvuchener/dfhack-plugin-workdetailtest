Work detail test plugin for DFHack
==================================

Experimental work detail related functions.

Commands
--------

### `laborupdatetest`

Try updating labor state and compare with current state. Print changed labors and restore previous state.

Remote API
----------

See proto/workdetailtest.proto for message definitions.

### DF information/status

#### `workdetailtest::GetProcessInfo`

`dfproto::EmptyMessage` → `dfproto::workdetailtest::ProcessInfo`

Get information for finding the Dwarf Fortress process:

 - `pid`: Process ID of DF (if available)
 - `cookie_address`/`cookie_value`: address and value of a random 32 bits
   value, for checking the correct process memory is being read.

#### `workdetailtest::GetGameState`

`dfproto::EmptyMessage` → `dfproto::workdetailtest::GameState`

Get current game state:

 - `world_loaded`: address of the current loaded world, or 0 if no world is loaded.
 - `map_loaded`: address of the current loaded map, or 0 if no map is loaded.
 - `viewscreen`: current viewscreen if it is one of the watched viewscreen.

### Results

Most editing functions return `Result` messages. `success` is true if the
requested change was applied, otherwise it is false and `error` gives the
reason why.

### Unit functions

#### `workdetailtest::EditUnit`

`dfproto::workdetailtest::EditUnit` → `dfproto::workdetailtest::UnitResult`

Units are identified by their id using `UnitId`.

All fields in `UnitProperties` are optional. Only the given values are changed.

 - `nickname`: change the unit nickname.
 - `flags`: any number of `UnitFlagValue`, each one setting or resetting a flag.

Change the properties of a single unit. If the unit is not found, result `unit`
signals the failure. For each requested flag change a `Result` message is
returned. If a change fails, other may still be applied. Nickname changes never
fail.

#### `workdetailtest::EditUnits`

`dfproto::workdetailtest::EditUnits` → `dfproto::workdetailtest::UnitResults`

Call `workdetailtest::EditUnit` several times.

### Work detail functions

Work details don't have ids. They are instead identified through their index
and name in `WorkDetailId`. Both need to match. If the work detail is not
found, the error is returned in a `Result` message as the function output or
the `work_detail` field of `WorkDetailResult`.

All fields in `WorkDetailProperties` are optional. Only the given values are changed.

 - `name`: work detail display.
 - `mode`: "Everybody does this", "Only selected does this", or "Nobody does this".
 - `assignments`: list of units whose assignment is changed.
 - `labors`: list of labors to add or remove from the work detail.
 - `icon`: icon index (see values from enum `work_detail::icon`).
 - `no_modify`: disable editing the work detail from the in-game interface
   (this does not prevent modifications from this plugin).
 - `cannot_be_everybody`: disable the "Everybody does this` button in the
   in-game interface.

Changes that may fail have their corresponding field in `WorkDetailResult`.

#### `workdetailtest::EditWorkDetail`

`dfproto::workdetailtest::EditWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

Change properties from `changes` of a existing work detail identified by `id`.

#### `workdetailtest::AddWorkDetail`

`dfproto::workdetailtest::AddWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

Create a new work detail at index `position` or at the end, and set its
properties from `properties`.

#### `workdetailtest::RemoveWorkDetail`

`dfproto::workdetailtest::RemoveWorkDetail` → `dfproto::workdetailtest::Result`

Remove the work detail identified by `id`.

#### `workdetailtest::MoveWorkDetail`

`dfproto::workdetailtest::MoveWorkDetail` → `dfproto::workdetailtest::Result`

Move the work detail identified by `id` at index `new_position`.
