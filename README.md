Work detail test plugin for DFHack
==================================

Experimental work detail related functions.

Commands
--------

### `laborupdatetest`

Try updating labor state and compare with current state. Print changed labors and restore previous state.

Remote API
----------

### `workdetailtest::GetProcessInfo`

`dfproto::EmptyMessage` → `dfproto::workdetailtest::ProcessInfo`
### `workdetailtest::GetGameState`

`dfproto::EmptyMessage` → `dfproto::workdetailtest::GameState`

### `workdetailtest::EditUnit`

`dfproto::workdetailtest::UnitProperties` → `dfproto::workdetailtest::Result`

### `workdetailtest::EditWorkDetail`

`dfproto::workdetailtest::WorkDetailProperties` → `dfproto::workdetailtest::WorkDetailResult`

### `workdetailtest::EditWorkDetails`

`dfproto::workdetailtest::WorkDetailChanges` → `dfproto::workdetailtest::WorkDetailResults`

