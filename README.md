# not_ghost_system

> **Language:** English | [Español](README.es.md)

A complete **client-side Ghost NPC, Vehicle, and Scene/Killcam Replay** toolkit for **SA-MP** and **open.mp** powered by **Pawn.RakNet** — with **zero FCNPC dependency** and **zero server bot slots used**.

## Included Libraries

1. **`not_ghost_npc.inc`** — Core engine for creating and controlling 100% client-side ghost players (`Ghost:`) and ghost vehicles (`GhostVehicle:`) via raw RPCs and sync packets. Supports public or private per-player visibility (`ownerid`), raw spectate targeting, and client-side real player hiding without changing virtual worlds.
2. **`not_ghost_recorder.inc`** — Built on top of `not_ghost_npc.inc`. Records live scenes around a focus player (OnFoot, InCar, Passenger, Aim, Bullet, WeaponsUpdate, Unoccupied, TrailerSync) into dynamic `y_malloc` buffers, saves/loads binary `.rec` files, and replays either globally (`Recording_Play`) or privately for a single viewer (`Recording_PlayRange` / `Recording_PlayRecent` — ideal for killcams and cinematics).

## Requirements

- `<a_samp>`
- **Pawn.RakNet** (`<Pawn.RakNet>`) by urShadow
- **YSI `y_malloc`** (`<YSI_Coding/y_malloc>`) by Y_Less *(required only for `not_ghost_recorder.inc`)*

## Installation

1. Copy `not_ghost_npc.inc` and `not_ghost_recorder.inc` into your `pawno/include/` (or `qawno/include/`) folder.
2. Create `scriptfiles/replays/` if you plan to use `Recording_SaveToFile` / `Recording_LoadFromFile`.
3. Include them in order:

```pawn
#include <a_samp>
#include <Pawn.RakNet>
#include <YSI_Coding/y_malloc>

#include <not_ghost_npc>
#include <not_ghost_recorder>
```

## Quick Start

### Client-Side Ghost NPC
```pawn
new Ghost:g = Ghost_Create(280, 1958.0, 1343.0, 15.3, -1, "Guard_1");
Ghost_SetHealth(g, 100.0);
Ghost_ChatBubble(g, "Welcome!", 0xFFFFFFFF, 30.0, 5000);
```

### Live Killcam / Scene Replay
```pawn
// Start recording around playerid
new Recording:rec = Recording_Start(playerid);

// Later, replay the last 5 seconds privately for victimid:
new SubPlayback:sp = Recording_PlayRecent(rec, victimid, 5000);

// Or stop, save to disk, and replay globally:
Recording_Stop(rec);
Recording_SaveToFile(rec, "match_clip.rec");
Recording_Play(rec);
```

## Credits

- Designed and written by **not**.
- Powered by **Pawn.RakNet** (urShadow) and **YSI `y_malloc`** (Y_Less).

## License

Released under the [MIT License](LICENSE).
