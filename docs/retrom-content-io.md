# Web content reads

The Web host registers a generic read-only WasmFS file tree using
`registerContentFile(absolutePath, numericId, sizeBytes)` before `startRunner`.
Registration, directory enumeration and stat do not read file bytes. Paths and
IDs are supplied by the host; the core has no HTTP, project-index, authentication,
cache or game-specific loading rules. Saves remain in the separate writable
overlay selected by the host.

`getContentReadSlot()` returns an address in shared Wasm memory. The host must
service this slot before calling `startRunner` and until the instance is disposed.
The slot contains sixteen atomic 32-bit words followed by a 256 KiB payload:

| Word | Meaning |
| --- | --- |
| 0 | State: idle 0, requested 1, successful 2, closed 4 |
| 1 | Content-read ABI version, currently 1 |
| 2 | Monotonically increasing request sequence |
| 4 | Returned byte count |
| 6 | Closed flag |
| 7 | Requested byte count, at most 256 KiB |
| 9 | Opaque registered file ID |
| 10, 11 | Unsigned offset, low and high 32 bits |

Other words are reserved. Native reads are serialized across threads and clamp
at EOF. After publishing a request the core notifies word 0 and waits up to 15
seconds. The host validates bounds, reads exactly the requested bytes, writes the
payload/count, changes requested to successful with compare-exchange and notifies.
The core copies the payload before returning the state to idle. On cancellation
or failure the host sets the closed flag/state and notifies, so waiting native
threads wake. The slot cannot reopen. Its memory prefix remains valid if the
shared Wasm memory grows. The host never has to execute JS on a paused core
thread to service a read.

GameMaker TXTR blobs and AUDO size headers/payloads load on first use. Lazy chunks
bypass the parser's per-chunk bulk read, including external audio groups. Other
parsed chunks keep the existing strategy. This does not guarantee that every
game reads only a fraction of its content: game code may request all of it.
Audio playback keeps the existing Web buffering behavior for each played file.

The checkpoint format remains `butterscotch-checkpoint-v2`. Deploy the new Web
core and a host implementing content-read version 1 together; the old checkpoint
ABI alone does not imply support for this file bridge. Tests use generated sparse
GameMaker chunks (`bash .github/rpg-runtime/test-lazy-data.sh`), never game names,
asset lists or commercial payloads.
