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

`dfproto::workdetailtest::EditUnit` → `dfproto::workdetailtest::UnitResult`

### `workdetailtest::EditUnits`

`dfproto::workdetailtest::EditUnits` → `dfproto::workdetailtest::UnitResults`

### `workdetailtest::EditWorkDetail`

`dfproto::workdetailtest::EditWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

### `workdetailtest::AddWorkDetail`

`dfproto::workdetailtest::AddWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

### `workdetailtest::RemoveWorkDetail`

`dfproto::workdetailtest::RemoveWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

### `workdetailtest::MoveWorkDetail`

`dfproto::workdetailtest::MoveWorkDetail` → `dfproto::workdetailtest::WorkDetailResult`

