# not_ghost_system

> **Idioma:** [English](README.md) | Español

Sistema completo de **NPCs fantasma, vehículos fantasma y grabación/reproducción de escenas y killcams 100% client-side** para **SA-MP** y **open.mp** mediante **Pawn.RakNet** — **sin FCNPC** y **sin consumir slots reales del servidor**.

## Librerías incluidas

1. **`not_ghost_npc.inc`** — Motor base para crear y controlar jugadores fantasma (`Ghost:`) y vehículos fantasma (`GhostVehicle:`) mediante RPCs y paquetes crudos. Soporta visibilidad pública o privada por jugador (`ownerid`), espectador crudo y ocultación client-side de jugadores reales sin cambiar de virtual world.
2. **`not_ghost_recorder.inc`** — Construido sobre `not_ghost_npc.inc`. Graba escenas en vivo alrededor de un jugador foco (OnFoot, InCar, Passenger, Aim, Bullet, WeaponsUpdate, Unoccupied, TrailerSync) en memoria dinámica (`y_malloc`), exporta/importa archivos binarios `.rec` y reproduce de forma global (`Recording_Play`) o privada por rango (`Recording_PlayRange` / `Recording_PlayRecent` — ideal para killcams y cinemáticas).

## Requisitos

- `<a_samp>`
- **Pawn.RakNet** (`<Pawn.RakNet>`) de urShadow
- **YSI `y_malloc`** (`<YSI_Coding/y_malloc>`) de Y_Less *(requerido solo para `not_ghost_recorder.inc`)*

## Instalación

1. Copia `not_ghost_npc.inc` y `not_ghost_recorder.inc` en tu carpeta `pawno/include/` (o `qawno/include/`).
2. Crea la carpeta `scriptfiles/replays/` si vas a usar `Recording_SaveToFile` / `Recording_LoadFromFile`.
3. Inclúyelos en orden:

```pawn
#include <a_samp>
#include <Pawn.RakNet>
#include <YSI_Coding/y_malloc>

#include <not_ghost_npc>
#include <not_ghost_recorder>
```

## Uso rápido

### NPC fantasma client-side
```pawn
new Ghost:g = Ghost_Create(280, 1958.0, 1343.0, 15.3, -1, "Guardia_1");
Ghost_SetHealth(g, 100.0);
Ghost_ChatBubble(g, "Bienvenido!", 0xFFFFFFFF, 30.0, 5000);
```

### Killcam / Replay en vivo
```pawn
// Iniciar grabación centrada en playerid
new Recording:rec = Recording_Start(playerid);

// Mostrar los últimos 5 segundos en privado para victimid:
new SubPlayback:sp = Recording_PlayRecent(rec, victimid, 5000);

// O detener, guardar a disco y reproducir globalmente:
Recording_Stop(rec);
Recording_SaveToFile(rec, "clip_partida.rec");
Recording_Play(rec);
```

## Créditos

- Diseñado y desarrollado por **not**.
- Impulsado por **Pawn.RakNet** (urShadow) y **YSI `y_malloc`** (Y_Less).

## Licencia

Distribuido bajo la [Licencia MIT](LICENSE).
