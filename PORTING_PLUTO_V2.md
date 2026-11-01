# Pluto v2.0 Remastered porting notes

Status: experimental. The earlier Windows x86 compatibility probe built successfully, but the latest selectable-APM and command-queue changes still need a fresh Windows CI build and live in-game testing. A successful build will not by itself prove Battle.net synchronization or good playing strength.

## Scope

- Keep the existing CoG 2026 branch and files unchanged.
- Use the official Pluto v2.0 md09b_1331000 binary and its separately generated binding table.
- Keep the existing StarCraft: Remastered 1.23.10.13515 x86 target and experimental custom-room launch modes.
- Do not add anti-cheat evasion or ranked-ladder support. Tests are for explicitly labelled custom games.

## Implemented on pluto-v2-porting

### Selectable command-rate ceiling

The v2 launchers now show four choices:

| Menu | Bridge packet ceiling | Minimum spacing |
|---|---:|---:|
| 300 | 300 packets/minute | 200 ms |
| 500 | 500 packets/minute | 120 ms |
| 1000 | 1000 packets/minute | 60 ms |
| Unlimited | No rolling-window cap | No bridge-imposed spacing |

The equivalent PowerShell argument is -APM 300, -APM 500, -APM 1000, or -APM 0. The default is 500. These limits count translated game-command packets; they are not the same metric as the in-game APM display. Even in Unlimited mode, the bridge sends at most one queued packet per game frame.

### Bounded command queue

- At most 64 pending command groups are retained.
- Unstarted tactical commands expire after 5 seconds, general commands after 8 seconds, and economy/production commands after 12 seconds.
- Exact duplicate tactical/selection groups that have not begun dispatch are coalesced. Build, training, morph, research, upgrade and cancellation commands are not coalesced as duplicates.
- When the queue is full, the oldest unstarted group with the lowest base priority is evicted; a new lower-priority group is rejected when all candidates are higher priority.
- A group already being dispatched is completed so another selection cannot be inserted between its selection and its associated actions.
- A selection and its following actions form one group. Long groups are chunked to eight packets, with the selection prefix repeated for the next chunk.

### Command priority

Higher priority is assigned to building placement, unit production, morphing, research, upgrades, return-cargo, resource-target right-click commands, and relevant production/cancellation packets. Attack and movement intent is lower priority. Ageing adds at most one tier to tactical orders. Economy, building and production remain a strictly higher scheduling tier regardless of age.

Priority assignment is based on verified translated Brood War command opcodes and, for right-clicks, the actual target unit type (mineral field, geyser, or refinery). The legacy 0x15 selected-units-action packet contains opaque fields, so the bridge does not guess its BWAPI command type from those bytes. This logic has unit tests, but its effect on real matches still needs to be evaluated with replays and bridge logs.

## Tests added

- APM limiter tests cover 300, 500, 1000 and Unlimited, including rolling-window boundaries.
- Scheduler tests cover build/train/return-cargo/resource-target-gather/opaque-action priorities, ageing, duplicate handling, selection grouping, and the eight-packet chunk limit.
- The Windows workflow builds the x86 bridge and runs CTest. The latest workflow result must be checked before distributing a new ZIP.

## Remaining validation

1. Confirm the latest Windows CI build and all CTest cases pass.
2. Test one offline 1v1 match and one explicitly labelled Battle.net custom Melee match at 500 APM.
3. Repeat with Unlimited and compare worker mining, mineral float, production uptime, command queue age, and the in-game APM display.
4. Inspect runtime-v2-multiplayer/bridge.log for SEND, QUEUE EXPIRED, QUEUE COALESCED, QUEUE EVICTED, and QUEUE REJECTED lines.
5. Validate Top vs Bottom / 1-v-many separately; the underlying model remains a 1v1 model.

## Official upstream references

- Pluto v2.0 release and model files: https://github.com/tscmoo/pluto/releases/tag/v2.0
- Pluto README: https://github.com/tscmoo/pluto
