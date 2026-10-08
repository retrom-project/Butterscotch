# Retrom checkpoint variable identity

The browser checkpoint keeps the `BSCP` envelope and
`butterscotch-checkpoint-v2` ABI. Its JSON stores global and instance variables
as arrays of `{ "id": number, "value": value }` entries. The integer ID is the
identity used by compiled GameMaker bytecode; a variable name is only a lookup
binding. Several compiled variables can have an empty name, and variables
created at runtime do not necessarily have entries in the compiled VARI chunk.

`variableNames` captures the VM's complete name-to-ID map and `nextDynamicVarID`
captures its allocation cursor. Restore replaces the map created during cold
room initialization before restoring variables at their saved IDs. Subsequent
name lookups and allocations therefore address the same slots. Global and
instance scopes remain independent, and an explicitly undefined slot remains
present.

The existing bounded value and nesting checks apply to the variable values.
Bindings and variable slots also count against the checkpoint cell budget.
Restore checks integral IDs, duplicate slots, the allocation cursor, and the
required joystick channel selections. It reads only this current contract;
there is no historical-format reader or conversion path.

`tests/retrom_checkpoint_test.c` exercises multiple unnamed compiled IDs,
independent global and instance values, runtime-created names, a cold VM with
a different allocation order, allocation after restore, undefined slots, and
malformed or oversized variable collections. These deterministic fixtures do
not include game files or private save data. Product acceptance separately
verifies a new public save and its restoration in a fresh browser instance.
