# Network bandwidth update — 0.2.1

The previous version sent complete raw JSON responses repeatedly: every two seconds for a peaceful yard and every 0.2 seconds when viewing a battle. Shops, descriptions, options and other unchanged fields were included each time. This update reduces the transmitted state while preserving the full reconstructed snapshot consumed by the existing HUD and board.

## Measured application data

These are 60-second offline tests of actual production simulation snapshots and the actual new codec. Totals include the first full state. Every reconstructed snapshot was compared independently to the original JSON; all 394 matched. They are application payload sizes, not Task Manager or live network throughput.

| Scenario | Updates | Previous full JSON, UTF-8 | New state payload | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Idle yard | 31 | 2,467,290 B | 11,239 B | 99.54% |
| Resource production and hatching | 31 | 2,540,082 B | 15,763 B | 99.38% |
| Invasion, five updates per second | 301 | 21,817,744 B | 125,766 B | 99.42% |
| Stationary Map Room | 31 | 8,875,424 B | 23,550 B | 99.73% |

The record is `Tools/OfflineValidation/network-transport-results.json`. It excludes RPC and packet headers, acknowledgement traffic, retransmissions, connection overhead, other applications and the server subsystem's small status wrapper. Native FString serialization can differ from the UTF-8 baseline. The test compression adapter uses system Zlib level 1 to exercise the same codec path; the built game uses Unreal Core's built-in compression. These results do not guarantee an identical percentage change in Windows Ethernet graphs or prove 100-player capacity.

## Transfer behavior

- Yards, Map Room responses and rankings use compact UTF-8 byte arrays. Compression is selected only when it makes a frame smaller.
- Recursive changes send only modified object fields and array entries. Field removal, nulls, array resize/reorder and type changes are represented explicitly. Complete frames are used for initial state, recovery or whenever a full compressed frame is smaller.
- Each connection owns its own raw baseline. Locally added artwork and catalog data never enter that baseline. Baseline sequence and identity checks prevent changes being applied to an unrelated state.
- There is one frame in flight per channel and one unacknowledged 8 KiB chunk per channel. Across the three channels, at most three such chunks are outstanding. A shared per-tick budget provides additional pacing.
- Ordinary waiting snapshots coalesce to the latest state. Required command, yard-switch and battle-start/end/result transitions remain ordered in a bounded queue. Queues have frame and character limits. Stalled or repeatedly invalid connections close rather than growing without limit.
- A rejected frame triggers a complete-state retry with a new sequence. The client applies only a complete validated result. Partial acknowledgements release chunk credit; the final acknowledgement commits the server baseline.
- The listen host's own player applies the original state locally, without compression or network acknowledgement overhead.

The default update rates remain two seconds for peaceful yards and 0.2 seconds for visible battles. Command responses remain reliable and request a fresh state. Server simulation, animations, bots, saves, original assets and game rules are unchanged. This is bandwidth optimization through lossless compression and change transfer, not encryption or an external service.

## Project settings

Under **Project Settings → Plugins → Backyard Monsters NXT → Network bandwidth**:

| Setting | Default | Purpose |
| --- | --- | --- |
| Compress Network State | Enabled | Use built-in lossless compression when beneficial. |
| Use Network Deltas | Enabled | Send changes from the client's acknowledged baseline. |
| Yard Snapshot Interval Seconds | 2.0 | Peaceful-yard refresh interval; commands still request updates. |
| Network Chunks Per Tick | 4 | Shared dispatch budget across the three channels; chunk credit also applies. |
| Network Ack Timeout Seconds | 30.0 | Maximum period without accepted transfer progress. |

The existing **Battle Snapshot Interval Seconds** remains 0.2 by default. The bandwidth improvements are enabled automatically; no optional script, backend or additional runtime library installation is needed.

## Inspecting actual payload use

Enter **`BYMNetStats`** in the normal Unreal console during play. On the host it writes per-remote-player totals to the game log: offered JSON bytes, state payload bytes sent, average payload Kbit/s since connection, frame counts and coalesced/unchanged updates. On a client it reports received state totals. These counters deliberately exclude protocol overhead and other programs. The host's local player uses no state RPC payload.

For a useful live comparison, let the same yard sit idle for a minute on the same remote client, then repeat an invasion. Compare the host's Ethernet graph and payload counters separately; a momentary Task Manager reading includes all traffic on that adapter.

## Updating and validation

Rebuild **both the host and all clients from this version**, using the same normal Unreal project/package workflow. The RPC layout changed, so old and new game packages must not be mixed. Preserve the host's existing `Saved` folder when updating an existing installation; it is not included in the distributable source archive and must not be shared with clients.

The completed codec/stream tests, controller integration checks and source integrity checks are recorded in `VALIDATION.md`. The broader simulation rerun was interrupted for the requested delivery deadline; its historical full-suite record is marked separately. Focused tests use actual production functions with explicit engine substitutes. A real UE 5.8 Windows compile/cook, live replication and a long-running multiplayer bandwidth test cannot be run in this environment.
