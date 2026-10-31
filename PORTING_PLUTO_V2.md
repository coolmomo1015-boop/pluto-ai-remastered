# Pluto v2.0 Remastered porting plan

Status: research branch only. This is not a working v2.0 bridge yet.

## Goals
- Preserve the existing CoG 2026 Battle.net custom-game bridge.
- Evaluate a separate v2.0 profile for the same StarCraft: Remastered 1.23.10.13515 x86 target.
- Keep Melee and the existing experimental Top vs Bottom Challenge behavior.
- Add a transparent 400-command/minute limiter for custom-game testing only.
- Do not implement anti-cheat evasion or concealment. All tests must be in explicitly labelled custom games.

## Verified upstream facts
- The currently supported bridge pins Pluto CoG 2026 by SHA-256:
  7e360b643c8c0156c03fe0cad9972a3058138ccfe22f921c5b4e0cd0aaf0abef
- The v2.0 release is md09b_1331000, 342M int8, and ships a new 32-bit BWAPI 4.4.0 DLL, 64-bit inference executable and weights.
- v2.0 requires Brood War 1.16.1 + BWAPI 4.4.0 in its upstream supported configuration. Remastered compatibility is not claimed upstream.
- Current Remastered adapter contains hard-coded relocation expectations generated from the CoG 2026 DLL, and the runtime explicitly checks the CoG 2026 DLL hash.
- Current bridge has experimental Battle.net Melee and Challenge code for the CoG 2026 build.

## Required engineering work
1. Independently inspect the v2.0 PE32 image and verify its architecture, exports, sections, relocation data and every absolute-memory reference used by the bot.
2. Determine whether the v2.0 binary still references the legacy 1.16.1 memory layout in the same way as CoG 2026.
3. Generate a new version-specific relocation table. Do not reuse the old binding table or merely remove the hash check.
4. Verify runtime ABI/export compatibility: gameInit, newAIModule, callback signatures, BWAPI data structures, command queue and all shared-memory/inference IPC.
5. Verify the 64-bit inference executable's expected launch arguments, environment variables, working directory and model/weight file hashes.
6. Build a separate v2.0 target profile; keep existing CoG 2026 profile unchanged.
7. Add tests for generated relocation operands, snapshot conversion, command conversion, session admission, and transparent APM limiter at 400 commands per minute.
8. Only then package and perform in-game Battle.net custom-room tests for Melee and Top vs Bottom.

## Important limitation
A build that compiles is not proof of runtime compatibility. The release remains experimental until a live Remastered custom game succeeds without memory validation errors, model startup errors or multiplayer synchronization failures.

## Exact official v2.0 artifact hashes
- pluto.dll: 603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc
- pluto/pluto_infer.exe: 3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc
- pluto/pluto_weights.bin: c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86

Official release: https://github.com/tscmoo/pluto/releases/tag/v2.0
