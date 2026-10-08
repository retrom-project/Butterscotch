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

The allocation cursor may reach `INT32_MAX`, representing an exhausted ID
space. Existing name lookups still succeed, while allocating another name
fails explicitly before incrementing the cursor. Compiled IDs must also leave
room for a representable cursor when creating the VM.

The existing bounded value and nesting checks apply to the variable values.
Bindings and variable slots also count against the checkpoint cell budget.
Restore checks integral IDs, duplicate slots, the allocation cursor, and the
required joystick channel selections. It reads only this current contract;
there is no historical-format reader or conversion path.

The helper-level tests in `tests/retrom_checkpoint_test.c` exercise multiple unnamed compiled IDs,
independent global and instance values, runtime-created names, a cold VM with
a different allocation order, allocation after restore, undefined slots, and
malformed or oversized variable collections. The public-entry regression uses
`VM_create`, `Runner_create`, and `Runner_restoreStateJson` with two synthetic
rooms, a legal object, and room creation bytecode that calls
`variable_global_set`. It covers reset and room initialization, a different
allocation order, a transition to the saved room, instance reconstruction,
and owned strings and nested arrays. A malformed value in the second instance
tests partial-state teardown and successful retry through the public entry
point. The native gate accepts `CFLAGS` and `LDFLAGS` for AddressSanitizer and
UndefinedBehaviorSanitizer checks, including leak detection. These fixtures
do not include game files or private save data. Product acceptance separately
verifies a new public save and its restoration in a fresh browser instance.
