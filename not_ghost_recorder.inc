/*
	=====================================================
	NOT_GHOST_RECORDER.INC - Scene & Killcam Replay System
	=====================================================
	Author: not
	Version: 1.1
	Date: 2026
	License: MIT
	Credits / Dependencies: urShadow (Pawn.RakNet), Y_Less (YSI y_malloc)

	DESCRIPTION:
	Full scene recording and playback library for SA-MP / open.mp built on top
	of not_ghost_npc.inc. Captures streamed players (OnFoot, InCar, Passenger,
	Aim, Bullet, WeaponsUpdate) and vehicles (Unoccupied, TrailerSync) around
	a focus player using dynamic heap buffers (y_malloc), exports/loads binary
	.rec files, and supports both global scene replay and private per-player
	range replay (ideal for killcams, cinematics, and admin review).

	MAIN FEATURES:
	- Dynamic memory allocation via YSI y_malloc (grows on demand)
	- Zero IPacket overhead when no recording is active (active capture counter)
	- Binary .rec file export and import (Recording_SaveToFile / LoadFromFile)
	- Global scene playback (Recording_Play) and private range/killcam playback
	  (Recording_PlayRange / Recording_PlayRecent)
	- Automatic cleanup on Recording_Destroy and OnPlayerDisconnect
	- Auto-stopping playback timers when idle (no wasted background timers)

	BASIC CONFIGURATION:
	```pawn
	#include <a_samp>
	#include <Pawn.RakNet>
	#include <YSI_Coding/y_malloc>
	#include <not_ghost_npc>

	#define MAX_RECORDINGS             10   // Max simultaneous recordings (default: 10)
	#define MAX_TRACKS_PER_RECORDING   50   // Max tracks per recording (default: 50)
	#define PLAYBACK_TICK_INTERVAL     25   // Playback timer interval in ms (default: 25)
	#define RECORDING_SAVE_FOLDER "replays/" // Folder inside scriptfiles/
	#include <not_ghost_recorder>
	```

	USAGE EXAMPLE:
	```pawn
	// Start recording around playerid
	new Recording:rec = Recording_Start(playerid);
	// ... gameplay happens ...
	// Show a private 5-second killcam replay to victimid:
	new SubPlayback:sp = Recording_PlayRecent(rec, victimid, 5000);
	// Or stop, save, and replay globally:
	Recording_Stop(rec);
	Recording_SaveToFile(rec, "clip1.rec");
	Recording_Play(rec);
	```

	COMPATIBILITY:
	- SA-MP 0.3.7 and open.mp
	- Requires <a_samp>, <Pawn.RakNet>, <YSI_Coding/y_malloc>, and <not_ghost_npc>
	- Standard ALS hooks on StreamIn/Out, OnTrailerUpdate, and OnPlayerDisconnect

	=====================================================
*/

#if defined _not_ghost_recorder_included
	#endinput
#endif
#define _not_ghost_recorder_included

#include <a_samp>

#tryinclude <not_ghost_npc>
#include <YSI_Coding/y_malloc>

#if !defined _not_ghost_npc_included
	#error "not_ghost_recorder.inc requires not_ghost_npc.inc (include not_ghost_npc.inc first)"
#endif

// ============================================================================
// Configuracion
// ============================================================================

#if !defined MAX_RECORDINGS
	#define MAX_RECORDINGS              (10)
#endif

#if !defined MAX_TRACKS_PER_RECORDING
	#define MAX_TRACKS_PER_RECORDING    (50)
#endif

#if !defined MAX_FRAMES_PER_TRACK
	#define MAX_FRAMES_PER_TRACK        (10000)
#endif

#if !defined MAX_AIM_EVENTS_PER_TRACK
	#define MAX_AIM_EVENTS_PER_TRACK    (1000)
#endif

#if !defined MAX_BULLET_EVENTS_PER_TRACK
	#define MAX_BULLET_EVENTS_PER_TRACK (1000)
#endif

#if !defined MAX_VEH_FRAMES_PER_TRACK
	#define MAX_VEH_FRAMES_PER_TRACK    (2000)
#endif

#if !defined MAX_WEAPON_EVENTS_PER_TRACK
	#define MAX_WEAPON_EVENTS_PER_TRACK (100)
#endif

#if !defined MAX_TRAILER_EVENTS_PER_TRACK
	#define MAX_TRAILER_EVENTS_PER_TRACK (20)
#endif

#if !defined PLAYBACK_TICK_INTERVAL
	#define PLAYBACK_TICK_INTERVAL      (25)
#endif

// Carpeta (relativa a scriptfiles/) donde Recording_SaveToFile /
// Recording_LoadFromFile guardan y leen los .rec. Para cambiarla, define
// RECORDING_SAVE_FOLDER ANTES del #include <not_ghost_recorder>.
// IMPORTANTE: la carpeta debe existir ya en el disco antes de usar SaveToFile.
#if !defined RECORDING_SAVE_FOLDER
	#define RECORDING_SAVE_FOLDER       "replays/"
#endif

// Capacidades iniciales de los buffers dinamicos por track. Aumentalas si
// tus escenas suelen tener muchos frames desde el principio y queres evitar
// los primeros reallocs; bajalas si priorizas memoria sobre velocidad.
#if !defined RECORDING_INITIAL_FRAME_CAP
	#define RECORDING_INITIAL_FRAME_CAP        (64)
#endif
#if !defined RECORDING_INITIAL_AIM_CAP
	#define RECORDING_INITIAL_AIM_CAP          (16)
#endif
#if !defined RECORDING_INITIAL_BULLET_CAP
	#define RECORDING_INITIAL_BULLET_CAP       (8)
#endif
#if !defined RECORDING_INITIAL_VEH_FRAME_CAP
	#define RECORDING_INITIAL_VEH_FRAME_CAP    (32)
#endif
#if !defined RECORDING_INITIAL_WEAPON_CAP
	#define RECORDING_INITIAL_WEAPON_CAP       (8)
#endif
#if !defined RECORDING_INITIAL_TRAILER_CAP
	#define RECORDING_INITIAL_TRAILER_CAP      (4)
#endif

// Cantidad de campos de cada enum (debe coincidir con los enums de abajo).
// Se usan para calcular cuantas celdas leer/escribir con fblockread/fblockwrite.
#define FRAME_FIELD_COUNT               (34)
#define AIM_FIELD_COUNT                 (12)
#define BULLET_FIELD_COUNT              (13)
#define VEH_FRAME_FIELD_COUNT           (17)
#define WEAPON_FIELD_COUNT              (3)
#define TRAILER_FIELD_COUNT             (2)

#define RECORDING_FILE_MAGIC            (0x4E475231) // "NGR1"
#define RECORDING_FILE_VERSION          (5)

#define INVALID_RECORDING_ID            (Recording:-1)

#define TRACK_PLAYER                    (0)
#define TRACK_VEHICLE                   (1)

#define FRAME_TYPE_FOOT                 (0)
#define FRAME_TYPE_CAR                  (1)
#define FRAME_TYPE_PASSENGER            (2)

// IDs de packet (SA-MP 0.3.7 via Pawn.RakNet)
#if !defined ONFOOT_SYNC
	static const ONFOOT_SYNC = 207;
#endif
#if !defined DRIVER_SYNC
	static const DRIVER_SYNC = 200;
#endif
#if !defined AIM_SYNC_PACKET
	static const AIM_SYNC_PACKET = 203;
#endif
#if !defined BULLET_SYNC_PACKET
	static const BULLET_SYNC_PACKET = 206;
#endif
#if !defined UNOCCUPIED_SYNC
	static const UNOCCUPIED_SYNC = 209;
#endif
#if !defined TRAILER_SYNC
	static const TRAILER_SYNC = 210;
#endif
#if !defined PASSENGER_SYNC_PACKET
	static const PASSENGER_SYNC_PACKET = 211;
#endif
#if !defined WEAPONS_UPDATE_PACKET
	static const WEAPONS_UPDATE_PACKET = 204;
#endif

// ============================================================================
// Datos internos
// ============================================================================

enum e_RECORDING_DATA
{
	bool:recSlotUsed,
	bool:recCapturing,
	recFocusPlayer,
	bool:recIncludeEmptyVehicles,
	recStartTick,
	recDuration,
	recTrackCount,
	bool:recIsPlaying,
	recPlayStartTick
};
static RECORDING_DATA[MAX_RECORDINGS][e_RECORDING_DATA];

enum e_TRACK_DATA
{
	bool:trkActive,
	trkType,                       // TRACK_PLAYER / TRACK_VEHICLE
	bool:trkIsFocus,
	trkRealId,
	trkSkin,
	trkModel,
	trkStart,                      // ms relativos al inicio de la Recording
	trkEnd,                        // ms relativos, -1 mientras sigue abierto
	Float:trkSpawnX, Float:trkSpawnY, Float:trkSpawnZ,
	Float:trkSpawnAngle,
	trkFrameCount,
	trkAimCount,
	trkBulletCount,
	bool:trkIsPassenger,
	trkSeatId,
	trkVehFrameCount,
	trkWeaponCount,
	trkTrailerCount,
	// Estado de reproduccion (no se exporta a fichero)
	bool:trkPlaySpawned,
	Ghost:trkPlayGhost,
	GhostVehicle:trkPlayVehicle,
	trkPlayFrameIdx,
	trkPlayAimIdx,
	trkPlayBulletIdx,
	trkPlayVehFrameIdx,
	trkPlayWeaponIdx,
	trkPlayTrailerIdx
};
static TRACK_DATA[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING][e_TRACK_DATA];

// Timeline unificado de un track de JUGADOR. Alterna frames FOOT/CAR/PASSENGER
// a medida que el jugador se mueve, sube o baja de vehiculos.
enum e_FRAME_DATA
{
	frTick,                        // ms relativos al INICIO DEL TRACK
	frFrameType,                   // FRAME_TYPE_FOOT / FRAME_TYPE_CAR / FRAME_TYPE_PASSENGER
	Float:frX, Float:frY, Float:frZ,
	Float:frVX, Float:frVY, Float:frVZ,
	Float:frQW, Float:frQX, Float:frQY, Float:frQZ,
	frHealth,
	frArmor,
	frWeapon,
	frSpecialAction,               // solo FOOT
	frKeys,
	frLRKey,
	frUDKey,
	Float:frVehHealth,             // solo CAR
	frSiren,                       // solo CAR
	frVehTrackRef,                 // CAR/PASSENGER: indice del TRACK_VEHICLE asociado (-1 si no)
	frSeat,                        // solo PASSENGER: asiento (1/2/3)
	// Parametros completos de sync para combate fluido (C-Bug), surf y vehiculos
	frAnimId,                      // solo FOOT (PR_animationId)
	frAnimFlags,                   // solo FOOT (PR_animationFlags)
	frAdditionalKey,               // FOOT / CAR / PASSENGER (PR_additionalKey)
	frSurfingVeh,                  // solo FOOT (PR_surfingVehicleId)
	Float:frSurfingX,              // solo FOOT (PR_surfingOffsets[0])
	Float:frSurfingY,              // solo FOOT (PR_surfingOffsets[1])
	Float:frSurfingZ,              // solo FOOT (PR_surfingOffsets[2])
	frLandingGear,                 // solo CAR (PR_landingGearState)
	frTrailerId,                   // solo CAR (PR_trailerId)
	Float:frTrainSpeed,            // solo CAR (PR_trainSpeed / inclinacion / hydra)
	frDriveBy                      // solo PASSENGER (PR_driveBy)
};
static Alloc:FRAME_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static FRAME_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

// Timeline de un track de VEHICULO sin conductor (UNOCCUPIED_SYNC) o trailer (TRAILER_SYNC).
// roll[3] y direction[3] son los vectores de orientacion del paquete UNOCCUPIED_SYNC.
// Para frames que vienen de TRAILER_SYNC (que usa quaternion) se almacenan {0,0,0}
// en ambos - Ghost_ApplyUnoccupied los trata como "sin dato" y no toca la rotacion.
enum e_VEH_FRAME_DATA
{
	vfTick,
	Float:vfX, Float:vfY, Float:vfZ,
	Float:vfVX, Float:vfVY, Float:vfVZ,
	Float:vfHealth,
	Float:vfRollX, Float:vfRollY, Float:vfRollZ,
	Float:vfDirX, Float:vfDirY, Float:vfDirZ,
	Float:vfTurnX, Float:vfTurnY, Float:vfTurnZ
};
static Alloc:VEH_FRAME_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static VEH_FRAME_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

// Eventos discretos de cambio de arma/municion (WEAPONS_UPDATE) de un track de jugador.
enum e_WEAPON_EVENT
{
	weTick,
	weWeaponId,
	weAmmo
};
static Alloc:WEAPON_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static WEAPON_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

// Eventos de enganche/desenganche de un track de VEHICULO que actua como trailer.
// teTowTrackRef = -1 significa "desenganchado en este momento".
enum e_TRAILER_EVENT
{
	teTick,
	teTowTrackRef
};
static Alloc:TRAILER_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static TRAILER_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

enum e_AIM_EVENT
{
	aimTick,
	aimCamMode,
	Float:aimFrontX, Float:aimFrontY, Float:aimFrontZ,
	Float:aimPosX, Float:aimPosY, Float:aimPosZ,
	Float:aimZ,
	aimCamZoom,
	aimWeaponState,
	aimAspectRatio
};
static Alloc:AIM_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static AIM_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

enum e_BULLET_EVENT
{
	bulTick,
	bulHitType,
	bulHitId,
	Float:bulOriginX, Float:bulOriginY, Float:bulOriginZ,
	Float:bulHitX, Float:bulHitY, Float:bulHitZ,
	Float:bulOffX, Float:bulOffY, Float:bulOffZ,
	bulWeapon
};
static Alloc:BULLET_BUF[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];
static BULLET_CAP[MAX_RECORDINGS][MAX_TRACKS_PER_RECORDING];

#define nRec::%0(%1)          RECORDING_DATA[%1][%0]
#define nTrack::%0(%1,%2)     TRACK_DATA[%1][%2][%0]

// Macros de acceso a buffers dinamicos (mget/mset via y_malloc).
// Para leer se usa nXxx:: y para escribir nXxxSet::.
#define nFrame::%0(%1,%2,%3)         Internal_FrameGet(%1,%2,%3,_:(%0))
#define nFrameSet::%0(%1,%2,%3,%4)   Internal_FrameSet(%1,%2,%3,_:(%0),(%4))
#define nAimEv::%0(%1,%2,%3)         Internal_AimGet(%1,%2,%3,_:(%0))
#define nAimEvSet::%0(%1,%2,%3,%4)   Internal_AimSet(%1,%2,%3,_:(%0),(%4))
#define nBulletEv::%0(%1,%2,%3)         Internal_BulletGet(%1,%2,%3,_:(%0))
#define nBulletEvSet::%0(%1,%2,%3,%4)   Internal_BulletSet(%1,%2,%3,_:(%0),(%4))
#define nVehFrame::%0(%1,%2,%3)         Internal_VehFrameGet(%1,%2,%3,_:(%0))
#define nVehFrameSet::%0(%1,%2,%3,%4)   Internal_VehFrameSet(%1,%2,%3,_:(%0),(%4))
#define nWeaponEv::%0(%1,%2,%3)         Internal_WeaponGet(%1,%2,%3,_:(%0))
#define nWeaponEvSet::%0(%1,%2,%3,%4)   Internal_WeaponSet(%1,%2,%3,_:(%0),(%4))
#define nTrailerEv::%0(%1,%2,%3)         Internal_TrailerGet(%1,%2,%3,_:(%0))
#define nTrailerEvSet::%0(%1,%2,%3,%4)   Internal_TrailerSet(%1,%2,%3,_:(%0),(%4))

// Tamano de chunk para copias en bloque de buffers (ver Internal_EnsureCapacity).
// Se copia en tandas para no exceder el stack de PAWN en tracks grandes.
#define COPY_CHUNK_CELLS (256)

// Asegura que 'buf' tenga lugar para 'needed' elementos. Si hace falta,
// duplica la capacidad (arrancando en 'initialCap' si no existia) y copia
// los datos viejos en bloque. 'hardCeiling' es el techo absoluto (los MAX_*
// de configuracion). Devuelve false si se supera el techo o si malloc() falla.
static stock bool:Internal_EnsureCapacity(&Alloc:buf, &cap, needed, fieldCount, initialCap, hardCeiling)
{
	if(needed <= cap) return true;
	if(needed > hardCeiling) return false;

	new newCap = (cap < 1) ? initialCap : cap;
	while(newCap < needed) newCap *= 2;
	if(newCap > hardCeiling) newCap = hardCeiling;

	new Alloc:newBuf = malloc(newCap * fieldCount);
	if(newBuf == NO_ALLOC) return false;

	if(_:buf != 0 && cap > 0)
	{
		new tmp[COPY_CHUNK_CELLS];
		new totalCells = cap * fieldCount;
		new copied = 0;
		while(copied < totalCells)
		{
			new chunk = totalCells - copied;
			if(chunk > COPY_CHUNK_CELLS) chunk = COPY_CHUNK_CELLS;
			Malloc_GetA(tmp, chunk, buf, copied);
			Malloc_SetA(newBuf, copied, tmp, chunk);
			copied += chunk;
		}
		free(buf);
	}

	buf = newBuf;
	cap = newCap;
	return true;
}

// Libera los buffers dinamicos de un track. CRITICO llamarla desde
// Recording_Destroy: a diferencia de arrays fijos, esta memoria no se
// devuelve sola y se pierde hasta reiniciar el gamemode si no se libera.
static stock Internal_FreeTrackBuffers(recslot, trackindex)
{
	if(_:FRAME_BUF[recslot][trackindex] != 0)      { free(FRAME_BUF[recslot][trackindex]); FRAME_BUF[recslot][trackindex] = Alloc:0; }
	if(_:AIM_BUF[recslot][trackindex] != 0)        { free(AIM_BUF[recslot][trackindex]); AIM_BUF[recslot][trackindex] = Alloc:0; }
	if(_:BULLET_BUF[recslot][trackindex] != 0)     { free(BULLET_BUF[recslot][trackindex]); BULLET_BUF[recslot][trackindex] = Alloc:0; }
	if(_:VEH_FRAME_BUF[recslot][trackindex] != 0)  { free(VEH_FRAME_BUF[recslot][trackindex]); VEH_FRAME_BUF[recslot][trackindex] = Alloc:0; }
	if(_:WEAPON_BUF[recslot][trackindex] != 0)     { free(WEAPON_BUF[recslot][trackindex]); WEAPON_BUF[recslot][trackindex] = Alloc:0; }
	if(_:TRAILER_BUF[recslot][trackindex] != 0)    { free(TRAILER_BUF[recslot][trackindex]); TRAILER_BUF[recslot][trackindex] = Alloc:0; }
	FRAME_CAP[recslot][trackindex] = 0;
	AIM_CAP[recslot][trackindex] = 0;
	BULLET_CAP[recslot][trackindex] = 0;
	VEH_FRAME_CAP[recslot][trackindex] = 0;
	WEAPON_CAP[recslot][trackindex] = 0;
	TRAILER_CAP[recslot][trackindex] = 0;
}

static stock Internal_FrameGet(recslot, trackindex, idx, field)
{
	return mget(FRAME_BUF[recslot][trackindex], idx * FRAME_FIELD_COUNT + field);
}
static stock Internal_FrameSet(recslot, trackindex, idx, field, value)
{
	mset(FRAME_BUF[recslot][trackindex], idx * FRAME_FIELD_COUNT + field, value);
}
static stock Internal_AimGet(recslot, trackindex, idx, field)
{
	return mget(AIM_BUF[recslot][trackindex], idx * AIM_FIELD_COUNT + field);
}
static stock Internal_AimSet(recslot, trackindex, idx, field, value)
{
	mset(AIM_BUF[recslot][trackindex], idx * AIM_FIELD_COUNT + field, value);
}
static stock Internal_BulletGet(recslot, trackindex, idx, field)
{
	return mget(BULLET_BUF[recslot][trackindex], idx * BULLET_FIELD_COUNT + field);
}
static stock Internal_BulletSet(recslot, trackindex, idx, field, value)
{
	mset(BULLET_BUF[recslot][trackindex], idx * BULLET_FIELD_COUNT + field, value);
}
static stock Internal_VehFrameGet(recslot, trackindex, idx, field)
{
	return mget(VEH_FRAME_BUF[recslot][trackindex], idx * VEH_FRAME_FIELD_COUNT + field);
}
static stock Internal_VehFrameSet(recslot, trackindex, idx, field, value)
{
	mset(VEH_FRAME_BUF[recslot][trackindex], idx * VEH_FRAME_FIELD_COUNT + field, value);
}
static stock Internal_WeaponGet(recslot, trackindex, idx, field)
{
	return mget(WEAPON_BUF[recslot][trackindex], idx * WEAPON_FIELD_COUNT + field);
}
static stock Internal_WeaponSet(recslot, trackindex, idx, field, value)
{
	mset(WEAPON_BUF[recslot][trackindex], idx * WEAPON_FIELD_COUNT + field, value);
}
static stock Internal_TrailerGet(recslot, trackindex, idx, field)
{
	return mget(TRAILER_BUF[recslot][trackindex], idx * TRAILER_FIELD_COUNT + field);
}
static stock Internal_TrailerSet(recslot, trackindex, idx, field, value)
{
	mset(TRAILER_BUF[recslot][trackindex], idx * TRAILER_FIELD_COUNT + field, value);
}

//      _    _ ______ _      _____  ______ _____   _____
//     | |  | |  ____| |    |  __ \|  ____|  __ \ / ____|
//     | |__| | |__  | |    | |__) | |__  | |__) | (___
//     |  __  |  __| | |    |  ___/|  __| |  _  / \___ \
//     | |  | | |____| |____| |    | |____| | \ \ ____) |
//     |_|  |_|______|______|_|    |______|_|  \_\_____/
//
//

static bool:g_PlaybackTimerRunning = false;
static g_PlaybackTimerId = -1;
static g_ActiveCaptureCount = 0;

static stock Internal_FindFreeRecordingSlot()
{
	for(new i = 0; i < MAX_RECORDINGS; i++)
	{
		if(!nRec::recSlotUsed(i)) return i;
	}
	return -1;
}

// Abre un track nuevo dentro de una Recording. Una misma entidad puede
// entrar y salir del streaming varias veces generando tracks distintos.
static stock Internal_OpenTrack(recslot, type, realid, bool:isFocus, skin = -1, model = -1, Float:spawnX = 0.0, Float:spawnY = 0.0, Float:spawnZ = 0.0, Float:spawnAngle = 0.0, bool:isPassenger = false, seatId = 0)
{
	new count = nRec::recTrackCount(recslot);
	if(count >= MAX_TRACKS_PER_RECORDING) return -1;

	// Evita abrir dos tracks activos para la misma entidad a la vez
	for(new t = 0; t < count; t++)
	{
		if(nTrack::trkActive(recslot, t) && nTrack::trkType(recslot, t) == type
			&& nTrack::trkRealId(recslot, t) == realid && nTrack::trkEnd(recslot, t) == -1)
			return -1;
	}

	new t = count;
	nTrack::trkActive(recslot, t) = true;
	nTrack::trkType(recslot, t) = type;
	nTrack::trkIsFocus(recslot, t) = isFocus;
	nTrack::trkRealId(recslot, t) = realid;
	nTrack::trkSkin(recslot, t) = skin;
	nTrack::trkModel(recslot, t) = model;
	nTrack::trkStart(recslot, t) = GetTickCount() - nRec::recStartTick(recslot);
	nTrack::trkEnd(recslot, t) = -1;
	nTrack::trkSpawnX(recslot, t) = spawnX;
	nTrack::trkSpawnY(recslot, t) = spawnY;
	nTrack::trkSpawnZ(recslot, t) = spawnZ;
	nTrack::trkSpawnAngle(recslot, t) = spawnAngle;
	nTrack::trkFrameCount(recslot, t) = 0;
	nTrack::trkAimCount(recslot, t) = 0;
	nTrack::trkBulletCount(recslot, t) = 0;
	nTrack::trkIsPassenger(recslot, t) = isPassenger;
	nTrack::trkSeatId(recslot, t) = seatId;
	nTrack::trkVehFrameCount(recslot, t) = 0;
	nTrack::trkWeaponCount(recslot, t) = 0;
	nTrack::trkTrailerCount(recslot, t) = 0;
	nTrack::trkPlaySpawned(recslot, t) = false;
	nTrack::trkPlayGhost(recslot, t) = INVALID_GHOST_ID;
	nTrack::trkPlayVehicle(recslot, t) = INVALID_GHOST_VEHICLE_ID;
	nTrack::trkPlayFrameIdx(recslot, t) = 0;
	nTrack::trkPlayAimIdx(recslot, t) = 0;
	nTrack::trkPlayBulletIdx(recslot, t) = 0;
	nTrack::trkPlayVehFrameIdx(recslot, t) = 0;
	nTrack::trkPlayWeaponIdx(recslot, t) = 0;
	nTrack::trkPlayTrailerIdx(recslot, t) = 0;

	nRec::recTrackCount(recslot) = count + 1;
	return t;
}

static stock bool:Internal_CloseTrack(recslot, trackindex)
{
	if(trackindex < 0 || trackindex >= nRec::recTrackCount(recslot)) return false;
	if(!nTrack::trkActive(recslot, trackindex)) return false;
	if(nTrack::trkEnd(recslot, trackindex) != -1) return false;

	nTrack::trkEnd(recslot, trackindex) = GetTickCount() - nRec::recStartTick(recslot);
	return true;
}

//      _____  ______ _____ ____  _____  _____ _____ _   _  _____
//     |  __ \|  ____/ ____/ __ \|  __ \|  __ \_   _| \ | |/ ____|
//     | |__) | |__ | |   | |  | | |__) | |  | || | |  \| | |  __
//     |  _  /|  __|| |   | |  | |  _  /| |  | || | | . ` | | |_ |
//     | | \ \| |___| |___| |__| | | \ \| |__| || |_| |\  | |__| |
//     |_|  \_\______\_____\____/|_|  \_\_____/_____|_| \_|\_____|
//
//

/*
	Recording_IsValid
	Verifica si una Recording existe (fue creada y no destruida todavia).
*/
stock bool:Recording_IsValid(Recording:rec)
{
	if(_:rec < 0 || _:rec >= MAX_RECORDINGS) return false;
	return nRec::recSlotUsed(_:rec);
}

/*
	Recording_Start
	Arranca una nueva sesion de grabacion centrada en focusplayerid.
	Abre automaticamente el track del jugador foco y hace un barrido
	inicial de todas las entidades que ya estan en su streaming.

	Parametros:
		focusplayerid - Jugador sobre el que se centra la grabacion
		bool:includeEmptyVehicles - Si true, graba vehiculos sin conductor

	Retorna:
		Recording:id de la sesion creada
		INVALID_RECORDING_ID si no hay slots libres o el jugador es invalido

	Ejemplo:
		new Recording:rec = Recording_Start(playerid);
*/
stock Recording:Recording_Start(focusplayerid, bool:includeEmptyVehicles = true)
{
	if(!IsPlayerConnected(focusplayerid)) return INVALID_RECORDING_ID;

	new slot = Internal_FindFreeRecordingSlot();
	if(slot == -1) return INVALID_RECORDING_ID;

	nRec::recSlotUsed(slot) = true;
	nRec::recCapturing(slot) = true;
	g_ActiveCaptureCount++;
	nRec::recFocusPlayer(slot) = focusplayerid;
	nRec::recIncludeEmptyVehicles(slot) = includeEmptyVehicles;
	nRec::recStartTick(slot) = GetTickCount();
	nRec::recDuration(slot) = 0;
	nRec::recTrackCount(slot) = 0;

	new Float:fx, Float:fy, Float:fz;
	GetPlayerPos(focusplayerid, fx, fy, fz);
	Internal_OpenTrack(slot, TRACK_PLAYER, focusplayerid, true, GetPlayerSkin(focusplayerid), -1, fx, fy, fz);

	// Barrido inicial: entidades que ya estaban streameadas al momento de
	// Recording_Start nunca disparan OnPlayerStreamIn/OnVehicleStreamIn.
	for(new pid = 0; pid < MAX_PLAYERS; pid++)
	{
		if(!IsPlayerConnected(pid)) continue;
		if(pid == focusplayerid) continue;
		if(!IsPlayerStreamedIn(pid, focusplayerid)) continue;

		new Float:px, Float:py, Float:pz;
		GetPlayerPos(pid, px, py, pz);
		Internal_OpenTrack(slot, TRACK_PLAYER, pid, false, GetPlayerSkin(pid), -1, px, py, pz);
	}

	for(new vid = 1; vid <= MAX_VEHICLES; vid++)
	{
		if(!IsVehicleStreamedIn(vid, focusplayerid)) continue;
		if(!includeEmptyVehicles && !Internal_IsVehicleOccupied(vid)) continue;

		new Float:vx, Float:vy, Float:vz, Float:vAngle;
		GetVehiclePos(vid, vx, vy, vz);
		GetVehicleZAngle(vid, vAngle);
		Internal_OpenTrack(slot, TRACK_VEHICLE, vid, false, -1, GetVehicleModel(vid), vx, vy, vz, vAngle);
	}

	return Recording:slot;
}

/*
	Recording_Stop
	Detiene la captura de una sesion (cierra todos los tracks abiertos y
	fija la duracion final). La Recording sigue existiendo y puede reproducirse
	o exportarse. Usar Recording_Destroy cuando ya no se necesite.

	Parametros:
		&Recording:rec - Sesion a detener

	Retorna:
		true si se detuvo correctamente
		false si la Recording es invalida o ya estaba detenida

	Ejemplo:
		Recording_Stop(rec);
*/
stock bool:Recording_Stop(&Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;

	new slot = _:rec;
	if(!nRec::recCapturing(slot)) return false;

	nRec::recDuration(slot) = GetTickCount() - nRec::recStartTick(slot);

	for(new t = 0, count = nRec::recTrackCount(slot); t < count; t++)
	{
		if(nTrack::trkActive(slot, t) && nTrack::trkEnd(slot, t) == -1)
		{
			Internal_CloseTrack(slot, t);
		}
	}

	nRec::recCapturing(slot) = false;
	if(g_ActiveCaptureCount > 0) g_ActiveCaptureCount--;
	return true;
}

/*
	Recording_Destroy
	Libera por completo el slot de una Recording, incluyendo todos los
	buffers dinamicos de tracks. Si todavia estaba capturando o reproduciendo,
	los detiene antes. Usar SIEMPRE despues de exportar a fichero o de que
	ya no se necesite la Recording, para evitar memory leaks.

	Parametros:
		&Recording:rec - Sesion a destruir (se invalida por referencia)

	Ejemplo:
		Recording_Destroy(rec);
*/
forward Internal_StopSubPlaybacksForRec(recslot);

stock bool:Recording_Destroy(&Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;

	new slot = _:rec;

	if(nRec::recCapturing(slot))
	{
		if(g_ActiveCaptureCount > 0) g_ActiveCaptureCount--;
		nRec::recDuration(slot) = GetTickCount() - nRec::recStartTick(slot);
		for(new t = 0, count = nRec::recTrackCount(slot); t < count; t++)
		{
			if(nTrack::trkActive(slot, t) && nTrack::trkEnd(slot, t) == -1)
			{
				Internal_CloseTrack(slot, t);
			}
		}
	}

	if(nRec::recIsPlaying(slot))
	{
		Internal_StopPlaybackBySlot(slot);
	}

	// Prevent Use-After-Free: stop any active SubPlaybacks referencing this recording
	Internal_StopSubPlaybacksForRec(slot);

	for(new t = 0; t < MAX_TRACKS_PER_RECORDING; t++)
	{
		Internal_FreeTrackBuffers(slot, t);
		nTrack::trkActive(slot, t) = false;
		nTrack::trkType(slot, t) = 0;
		nTrack::trkIsFocus(slot, t) = false;
		nTrack::trkRealId(slot, t) = -1;
		nTrack::trkStart(slot, t) = 0;
		nTrack::trkEnd(slot, t) = -1;
		nTrack::trkFrameCount(slot, t) = 0;
		nTrack::trkAimCount(slot, t) = 0;
		nTrack::trkBulletCount(slot, t) = 0;
		nTrack::trkIsPassenger(slot, t) = false;
		nTrack::trkSeatId(slot, t) = 0;
		nTrack::trkVehFrameCount(slot, t) = 0;
		nTrack::trkWeaponCount(slot, t) = 0;
		nTrack::trkTrailerCount(slot, t) = 0;
		nTrack::trkPlaySpawned(slot, t) = false;
		nTrack::trkPlayGhost(slot, t) = INVALID_GHOST_ID;
		nTrack::trkPlayVehicle(slot, t) = INVALID_GHOST_VEHICLE_ID;
		nTrack::trkPlayFrameIdx(slot, t) = 0;
		nTrack::trkPlayAimIdx(slot, t) = 0;
		nTrack::trkPlayBulletIdx(slot, t) = 0;
		nTrack::trkPlayVehFrameIdx(slot, t) = 0;
		nTrack::trkPlayWeaponIdx(slot, t) = 0;
		nTrack::trkPlayTrailerIdx(slot, t) = 0;
	}

	nRec::recSlotUsed(slot) = false;
	nRec::recCapturing(slot) = false;
	nRec::recFocusPlayer(slot) = -1;
	nRec::recIncludeEmptyVehicles(slot) = false;
	nRec::recTrackCount(slot) = 0;
	nRec::recDuration(slot) = 0;
	nRec::recIsPlaying(slot) = false;

	rec = INVALID_RECORDING_ID;
	return true;
}

/*
	Recording_IsCapturing
	Verifica si una Recording todavia esta capturando (entre Start y Stop).
*/
stock bool:Recording_IsCapturing(Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;
	return nRec::recCapturing(_:rec);
}

/*
	Recording_GetFocusPlayer
	Devuelve el playerid sobre el que se centro Recording_Start.
*/
stock Recording_GetFocusPlayer(Recording:rec)
{
	if(!Recording_IsValid(rec)) return -1;
	return nRec::recFocusPlayer(_:rec);
}

/*
	Recording_IncludesEmptyVehicles
	Devuelve el valor de includeEmptyVehicles pasado en Recording_Start.
*/
stock bool:Recording_IncludesEmptyVehicles(Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;
	return nRec::recIncludeEmptyVehicles(_:rec);
}

/*
	Recording_GetDuration
	Duracion de la sesion en ms. Mientras captura, devuelve el tiempo
	transcurrido hasta ahora; despues de Stop, la duracion final fija.
*/
stock Recording_GetDuration(Recording:rec)
{
	if(!Recording_IsValid(rec)) return 0;
	if(nRec::recCapturing(_:rec)) return GetTickCount() - nRec::recStartTick(_:rec);
	return nRec::recDuration(_:rec);
}

/*
	Recording_GetTrackCount
	Cantidad de tracks (entidades) que tiene la sesion hasta el momento.
*/
stock Recording_GetTrackCount(Recording:rec)
{
	if(!Recording_IsValid(rec)) return 0;
	return nRec::recTrackCount(_:rec);
}

/*
	Recording_GetTrackInfo
	Devuelve los datos basicos de un track por indice (0-based).
*/
stock bool:Recording_GetTrackInfo(Recording:rec, trackindex, &type, &bool:isFocus, &realid, &skin, &model, &tStart, &tEnd, &frameCount)
{
	if(!Recording_IsValid(rec)) return false;
	new slot = _:rec;
	if(trackindex < 0 || trackindex >= nRec::recTrackCount(slot)) return false;

	type = nTrack::trkType(slot, trackindex);
	isFocus = nTrack::trkIsFocus(slot, trackindex);
	realid = nTrack::trkRealId(slot, trackindex);
	skin = nTrack::trkSkin(slot, trackindex);
	model = nTrack::trkModel(slot, trackindex);
	tStart = nTrack::trkStart(slot, trackindex);
	tEnd = nTrack::trkEnd(slot, trackindex);
	frameCount = nTrack::trkFrameCount(slot, trackindex);
	return true;
}

/*
	Recording_GetFrame
	Vuelca el frame `frameindex` del track `trackindex` en un struct
	onFoot[PR_OnFootSync] o inCar[PR_InCarSync] segun el tipo de frame.

	Retorna:
		FRAME_TYPE_FOOT / FRAME_TYPE_CAR / FRAME_TYPE_PASSENGER, o -1 si no existe.
*/
stock Recording_GetFrame(Recording:rec, trackindex, frameindex, onFoot[PR_OnFootSync], inCar[PR_InCarSync], &tick, &vehTrackRef, &seat = 0)
{
	if(!Recording_IsValid(rec)) return -1;
	new slot = _:rec;
	if(trackindex < 0 || trackindex >= nRec::recTrackCount(slot)) return -1;
	if(frameindex < 0 || frameindex >= nTrack::trkFrameCount(slot, trackindex)) return -1;

	tick = nFrame::frTick(slot, trackindex, frameindex);
	vehTrackRef = nFrame::frVehTrackRef(slot, trackindex, frameindex);
	seat = nFrame::frSeat(slot, trackindex, frameindex);

	new type = nFrame::frFrameType(slot, trackindex, frameindex);
	if(type == FRAME_TYPE_FOOT)
	{
		onFoot[PR_position][0] = Float:nFrame::frX(slot, trackindex, frameindex);
		onFoot[PR_position][1] = Float:nFrame::frY(slot, trackindex, frameindex);
		onFoot[PR_position][2] = Float:nFrame::frZ(slot, trackindex, frameindex);
		onFoot[PR_velocity][0] = Float:nFrame::frVX(slot, trackindex, frameindex);
		onFoot[PR_velocity][1] = Float:nFrame::frVY(slot, trackindex, frameindex);
		onFoot[PR_velocity][2] = Float:nFrame::frVZ(slot, trackindex, frameindex);
		onFoot[PR_quaternion][0] = Float:nFrame::frQW(slot, trackindex, frameindex);
		onFoot[PR_quaternion][1] = Float:nFrame::frQX(slot, trackindex, frameindex);
		onFoot[PR_quaternion][2] = Float:nFrame::frQY(slot, trackindex, frameindex);
		onFoot[PR_quaternion][3] = Float:nFrame::frQZ(slot, trackindex, frameindex);
		onFoot[PR_health] = nFrame::frHealth(slot, trackindex, frameindex);
		onFoot[PR_armour] = nFrame::frArmor(slot, trackindex, frameindex);
		onFoot[PR_weaponId] = nFrame::frWeapon(slot, trackindex, frameindex);
		onFoot[PR_specialAction] = nFrame::frSpecialAction(slot, trackindex, frameindex);
		onFoot[PR_keys] = nFrame::frKeys(slot, trackindex, frameindex);
		onFoot[PR_lrKey] = nFrame::frLRKey(slot, trackindex, frameindex);
		onFoot[PR_udKey] = nFrame::frUDKey(slot, trackindex, frameindex);
		onFoot[PR_animationId] = nFrame::frAnimId(slot, trackindex, frameindex);
		onFoot[PR_animationFlags] = nFrame::frAnimFlags(slot, trackindex, frameindex);
		onFoot[PR_additionalKey] = nFrame::frAdditionalKey(slot, trackindex, frameindex);
		onFoot[PR_surfingVehicleId] = nFrame::frSurfingVeh(slot, trackindex, frameindex);
		onFoot[PR_surfingOffsets][0] = Float:nFrame::frSurfingX(slot, trackindex, frameindex);
		onFoot[PR_surfingOffsets][1] = Float:nFrame::frSurfingY(slot, trackindex, frameindex);
		onFoot[PR_surfingOffsets][2] = Float:nFrame::frSurfingZ(slot, trackindex, frameindex);
	}
	else if(type == FRAME_TYPE_CAR)
	{
		inCar[PR_position][0] = Float:nFrame::frX(slot, trackindex, frameindex);
		inCar[PR_position][1] = Float:nFrame::frY(slot, trackindex, frameindex);
		inCar[PR_position][2] = Float:nFrame::frZ(slot, trackindex, frameindex);
		inCar[PR_velocity][0] = Float:nFrame::frVX(slot, trackindex, frameindex);
		inCar[PR_velocity][1] = Float:nFrame::frVY(slot, trackindex, frameindex);
		inCar[PR_velocity][2] = Float:nFrame::frVZ(slot, trackindex, frameindex);
		inCar[PR_quaternion][0] = Float:nFrame::frQW(slot, trackindex, frameindex);
		inCar[PR_quaternion][1] = Float:nFrame::frQX(slot, trackindex, frameindex);
		inCar[PR_quaternion][2] = Float:nFrame::frQY(slot, trackindex, frameindex);
		inCar[PR_quaternion][3] = Float:nFrame::frQZ(slot, trackindex, frameindex);
		inCar[PR_playerHealth] = nFrame::frHealth(slot, trackindex, frameindex);
		inCar[PR_armour] = nFrame::frArmor(slot, trackindex, frameindex);
		inCar[PR_weaponId] = nFrame::frWeapon(slot, trackindex, frameindex);
		inCar[PR_keys] = nFrame::frKeys(slot, trackindex, frameindex);
		inCar[PR_lrKey] = nFrame::frLRKey(slot, trackindex, frameindex);
		inCar[PR_udKey] = nFrame::frUDKey(slot, trackindex, frameindex);
		inCar[PR_vehicleHealth] = Float:nFrame::frVehHealth(slot, trackindex, frameindex);
		inCar[PR_sirenState] = nFrame::frSiren(slot, trackindex, frameindex);
		inCar[PR_additionalKey] = nFrame::frAdditionalKey(slot, trackindex, frameindex);
		inCar[PR_landingGearState] = nFrame::frLandingGear(slot, trackindex, frameindex);
		inCar[PR_trailerId] = nFrame::frTrailerId(slot, trackindex, frameindex);
		inCar[PR_trainSpeed] = Float:nFrame::frTrainSpeed(slot, trackindex, frameindex);
	}
	else // FRAME_TYPE_PASSENGER: reusamos inCar como contenedor
	{
		inCar[PR_position][0] = Float:nFrame::frX(slot, trackindex, frameindex);
		inCar[PR_position][1] = Float:nFrame::frY(slot, trackindex, frameindex);
		inCar[PR_position][2] = Float:nFrame::frZ(slot, trackindex, frameindex);
		inCar[PR_playerHealth] = nFrame::frHealth(slot, trackindex, frameindex);
		inCar[PR_armour] = nFrame::frArmor(slot, trackindex, frameindex);
		inCar[PR_weaponId] = nFrame::frWeapon(slot, trackindex, frameindex);
		inCar[PR_keys] = nFrame::frKeys(slot, trackindex, frameindex);
		inCar[PR_lrKey] = nFrame::frLRKey(slot, trackindex, frameindex);
		inCar[PR_udKey] = nFrame::frUDKey(slot, trackindex, frameindex);
		inCar[PR_additionalKey] = nFrame::frAdditionalKey(slot, trackindex, frameindex);
	}
	return type;
}

/*
	Recording_GetVehFrame
	Vuelca el frame de UNOCCUPIED_SYNC/TRAILER_SYNC de un track TRACK_VEHICLE.
	roll/direction son los vectores de orientacion del paquete (si el frame vino
	de TRAILER_SYNC, salen en {0,0,0}).
*/
stock bool:Recording_GetVehFrame(Recording:rec, trackindex, frameindex, &Float:x, &Float:y, &Float:z, &Float:vx, &Float:vy, &Float:vz, &Float:health, &tick, Float:roll[3], Float:direction[3], Float:turn[3] = {0.0, 0.0, 0.0})
{
	if(!Recording_IsValid(rec)) return false;
	new slot = _:rec;
	if(trackindex < 0 || trackindex >= nRec::recTrackCount(slot)) return false;
	if(frameindex < 0 || frameindex >= nTrack::trkVehFrameCount(slot, trackindex)) return false;

	tick = nVehFrame::vfTick(slot, trackindex, frameindex);
	x = Float:nVehFrame::vfX(slot, trackindex, frameindex);
	y = Float:nVehFrame::vfY(slot, trackindex, frameindex);
	z = Float:nVehFrame::vfZ(slot, trackindex, frameindex);
	vx = Float:nVehFrame::vfVX(slot, trackindex, frameindex);
	vy = Float:nVehFrame::vfVY(slot, trackindex, frameindex);
	vz = Float:nVehFrame::vfVZ(slot, trackindex, frameindex);
	health = Float:nVehFrame::vfHealth(slot, trackindex, frameindex);
	roll[0] = Float:nVehFrame::vfRollX(slot, trackindex, frameindex);
	roll[1] = Float:nVehFrame::vfRollY(slot, trackindex, frameindex);
	roll[2] = Float:nVehFrame::vfRollZ(slot, trackindex, frameindex);
	direction[0] = Float:nVehFrame::vfDirX(slot, trackindex, frameindex);
	direction[1] = Float:nVehFrame::vfDirY(slot, trackindex, frameindex);
	direction[2] = Float:nVehFrame::vfDirZ(slot, trackindex, frameindex);
	turn[0] = Float:nVehFrame::vfTurnX(slot, trackindex, frameindex);
	turn[1] = Float:nVehFrame::vfTurnY(slot, trackindex, frameindex);
	turn[2] = Float:nVehFrame::vfTurnZ(slot, trackindex, frameindex);
	return true;
}

//       _____          _____ _______ _    _ _____  ______
//      / ____|   /\   |  __ \__   __| |  | |  __ \|  ____|
//     | |       /  \  | |__) | | |  | |  | | |__) | |__
//     | |      / /\ \ |  ___/  | |  | |  | |  _  /|  __|
//     | |____ / ____ \| |      | |  | |__| | | \ \| |____
//      \_____/_/    \_\_|      |_|   \____/|_|  \_\______|
//
//

// Busca el track activo y abierto (trkEnd == -1) del tipo y realId dados.
static stock Internal_FindOpenTrackByRealId(recslot, type, realid)
{
	for(new t = 0, count = nRec::recTrackCount(recslot); t < count; t++)
	{
		if(nTrack::trkActive(recslot, t) && nTrack::trkType(recslot, t) == type
			&& nTrack::trkRealId(recslot, t) == realid && nTrack::trkEnd(recslot, t) == -1)
			return t;
	}
	return -1;
}

static stock /* bool: */Internal_IsVehicleOccupied(vehicleid)
{
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(!IsPlayerConnected(i)) continue;
		if(GetPlayerVehicleID(i) == vehicleid) return true;
	}
	return false;
}

static stock Internal_PushFootFrame(recslot, trackindex, const onFoot[PR_OnFootSync])
{
	new frame = nTrack::trkFrameCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(FRAME_BUF[recslot][trackindex], FRAME_CAP[recslot][trackindex], frame + 1, FRAME_FIELD_COUNT, RECORDING_INITIAL_FRAME_CAP, MAX_FRAMES_PER_TRACK))
	{
		Internal_CloseTrack(recslot, trackindex);
		return;
	}

	nFrameSet::frTick(recslot, trackindex, frame, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nFrameSet::frFrameType(recslot, trackindex, frame, FRAME_TYPE_FOOT);

	nFrameSet::frX(recslot, trackindex, frame, _:onFoot[PR_position][0]);
	nFrameSet::frY(recslot, trackindex, frame, _:onFoot[PR_position][1]);
	nFrameSet::frZ(recslot, trackindex, frame, _:onFoot[PR_position][2]);
	nFrameSet::frVX(recslot, trackindex, frame, _:onFoot[PR_velocity][0]);
	nFrameSet::frVY(recslot, trackindex, frame, _:onFoot[PR_velocity][1]);
	nFrameSet::frVZ(recslot, trackindex, frame, _:onFoot[PR_velocity][2]);
	nFrameSet::frQW(recslot, trackindex, frame, _:onFoot[PR_quaternion][0]);
	nFrameSet::frQX(recslot, trackindex, frame, _:onFoot[PR_quaternion][1]);
	nFrameSet::frQY(recslot, trackindex, frame, _:onFoot[PR_quaternion][2]);
	nFrameSet::frQZ(recslot, trackindex, frame, _:onFoot[PR_quaternion][3]);
	nFrameSet::frHealth(recslot, trackindex, frame, onFoot[PR_health]);
	nFrameSet::frArmor(recslot, trackindex, frame, onFoot[PR_armour]);
	nFrameSet::frWeapon(recslot, trackindex, frame, onFoot[PR_weaponId]);
	nFrameSet::frSpecialAction(recslot, trackindex, frame, onFoot[PR_specialAction]);
	nFrameSet::frKeys(recslot, trackindex, frame, onFoot[PR_keys]);
	nFrameSet::frLRKey(recslot, trackindex, frame, onFoot[PR_lrKey]);
	nFrameSet::frUDKey(recslot, trackindex, frame, onFoot[PR_udKey]);
	nFrameSet::frVehHealth(recslot, trackindex, frame, 0);
	nFrameSet::frSiren(recslot, trackindex, frame, 0);
	nFrameSet::frVehTrackRef(recslot, trackindex, frame, -1);
	nFrameSet::frSeat(recslot, trackindex, frame, 0);
	nFrameSet::frAnimId(recslot, trackindex, frame, onFoot[PR_animationId]);
	nFrameSet::frAnimFlags(recslot, trackindex, frame, onFoot[PR_animationFlags]);
	nFrameSet::frAdditionalKey(recslot, trackindex, frame, onFoot[PR_additionalKey]);
	nFrameSet::frSurfingVeh(recslot, trackindex, frame, onFoot[PR_surfingVehicleId]);
	nFrameSet::frSurfingX(recslot, trackindex, frame, _:onFoot[PR_surfingOffsets][0]);
	nFrameSet::frSurfingY(recslot, trackindex, frame, _:onFoot[PR_surfingOffsets][1]);
	nFrameSet::frSurfingZ(recslot, trackindex, frame, _:onFoot[PR_surfingOffsets][2]);
	nFrameSet::frLandingGear(recslot, trackindex, frame, 0);
	nFrameSet::frTrailerId(recslot, trackindex, frame, 0);
	nFrameSet::frTrainSpeed(recslot, trackindex, frame, 0);
	nFrameSet::frDriveBy(recslot, trackindex, frame, 0);

	nTrack::trkFrameCount(recslot, trackindex) = frame + 1;
}

static stock Internal_PushCarFrame(recslot, trackindex, const inCar[PR_InCarSync], vehTrackRef)
{
	new frame = nTrack::trkFrameCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(FRAME_BUF[recslot][trackindex], FRAME_CAP[recslot][trackindex], frame + 1, FRAME_FIELD_COUNT, RECORDING_INITIAL_FRAME_CAP, MAX_FRAMES_PER_TRACK))
	{
		Internal_CloseTrack(recslot, trackindex);
		return;
	}

	nFrameSet::frTick(recslot, trackindex, frame, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nFrameSet::frFrameType(recslot, trackindex, frame, FRAME_TYPE_CAR);

	nFrameSet::frX(recslot, trackindex, frame, _:inCar[PR_position][0]);
	nFrameSet::frY(recslot, trackindex, frame, _:inCar[PR_position][1]);
	nFrameSet::frZ(recslot, trackindex, frame, _:inCar[PR_position][2]);
	nFrameSet::frVX(recslot, trackindex, frame, _:inCar[PR_velocity][0]);
	nFrameSet::frVY(recslot, trackindex, frame, _:inCar[PR_velocity][1]);
	nFrameSet::frVZ(recslot, trackindex, frame, _:inCar[PR_velocity][2]);
	nFrameSet::frQW(recslot, trackindex, frame, _:inCar[PR_quaternion][0]);
	nFrameSet::frQX(recslot, trackindex, frame, _:inCar[PR_quaternion][1]);
	nFrameSet::frQY(recslot, trackindex, frame, _:inCar[PR_quaternion][2]);
	nFrameSet::frQZ(recslot, trackindex, frame, _:inCar[PR_quaternion][3]);
	nFrameSet::frHealth(recslot, trackindex, frame, inCar[PR_playerHealth]);
	nFrameSet::frArmor(recslot, trackindex, frame, inCar[PR_armour]);
	nFrameSet::frWeapon(recslot, trackindex, frame, inCar[PR_weaponId]);
	nFrameSet::frSpecialAction(recslot, trackindex, frame, 0);
	nFrameSet::frKeys(recslot, trackindex, frame, inCar[PR_keys]);
	nFrameSet::frLRKey(recslot, trackindex, frame, inCar[PR_lrKey]);
	nFrameSet::frUDKey(recslot, trackindex, frame, inCar[PR_udKey]);
	nFrameSet::frVehHealth(recslot, trackindex, frame, _:inCar[PR_vehicleHealth]);
	nFrameSet::frSiren(recslot, trackindex, frame, inCar[PR_sirenState]);
	nFrameSet::frVehTrackRef(recslot, trackindex, frame, vehTrackRef);
	nFrameSet::frSeat(recslot, trackindex, frame, 0);
	nFrameSet::frAnimId(recslot, trackindex, frame, 0);
	nFrameSet::frAnimFlags(recslot, trackindex, frame, 0);
	nFrameSet::frAdditionalKey(recslot, trackindex, frame, inCar[PR_additionalKey]);
	nFrameSet::frSurfingVeh(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingX(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingY(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingZ(recslot, trackindex, frame, 0);
	nFrameSet::frLandingGear(recslot, trackindex, frame, inCar[PR_landingGearState]);
	nFrameSet::frTrailerId(recslot, trackindex, frame, inCar[PR_trailerId]);
	nFrameSet::frTrainSpeed(recslot, trackindex, frame, _:inCar[PR_trainSpeed]);
	nFrameSet::frDriveBy(recslot, trackindex, frame, 0);

	nTrack::trkFrameCount(recslot, trackindex) = frame + 1;
}

static stock Internal_PushAimEvent(recslot, trackindex, const aim[PR_AimSync])
{
	new idx = nTrack::trkAimCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(AIM_BUF[recslot][trackindex], AIM_CAP[recslot][trackindex], idx + 1, AIM_FIELD_COUNT, RECORDING_INITIAL_AIM_CAP, MAX_AIM_EVENTS_PER_TRACK)) return;

	nAimEvSet::aimTick(recslot, trackindex, idx, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nAimEvSet::aimCamMode(recslot, trackindex, idx, aim[PR_camMode]);
	nAimEvSet::aimFrontX(recslot, trackindex, idx, _:aim[PR_camFrontVec][0]);
	nAimEvSet::aimFrontY(recslot, trackindex, idx, _:aim[PR_camFrontVec][1]);
	nAimEvSet::aimFrontZ(recslot, trackindex, idx, _:aim[PR_camFrontVec][2]);
	nAimEvSet::aimPosX(recslot, trackindex, idx, _:aim[PR_camPos][0]);
	nAimEvSet::aimPosY(recslot, trackindex, idx, _:aim[PR_camPos][1]);
	nAimEvSet::aimPosZ(recslot, trackindex, idx, _:aim[PR_camPos][2]);
	nAimEvSet::aimZ(recslot, trackindex, idx, _:aim[PR_aimZ]);
	nAimEvSet::aimCamZoom(recslot, trackindex, idx, aim[PR_camZoom]);
	nAimEvSet::aimWeaponState(recslot, trackindex, idx, aim[PR_weaponState]);
	nAimEvSet::aimAspectRatio(recslot, trackindex, idx, aim[PR_aspectRatio]);

	nTrack::trkAimCount(recslot, trackindex) = idx + 1;
}

static stock Internal_PushBulletEvent(recslot, trackindex, const bullet[PR_BulletSync])
{
	new idx = nTrack::trkBulletCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(BULLET_BUF[recslot][trackindex], BULLET_CAP[recslot][trackindex], idx + 1, BULLET_FIELD_COUNT, RECORDING_INITIAL_BULLET_CAP, MAX_BULLET_EVENTS_PER_TRACK)) return;

	nBulletEvSet::bulTick(recslot, trackindex, idx, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nBulletEvSet::bulHitType(recslot, trackindex, idx, bullet[PR_hitType]);
	nBulletEvSet::bulHitId(recslot, trackindex, idx, bullet[PR_hitId]);
	nBulletEvSet::bulOriginX(recslot, trackindex, idx, _:bullet[PR_origin][0]);
	nBulletEvSet::bulOriginY(recslot, trackindex, idx, _:bullet[PR_origin][1]);
	nBulletEvSet::bulOriginZ(recslot, trackindex, idx, _:bullet[PR_origin][2]);
	nBulletEvSet::bulHitX(recslot, trackindex, idx, _:bullet[PR_hitPos][0]);
	nBulletEvSet::bulHitY(recslot, trackindex, idx, _:bullet[PR_hitPos][1]);
	nBulletEvSet::bulHitZ(recslot, trackindex, idx, _:bullet[PR_hitPos][2]);
	nBulletEvSet::bulOffX(recslot, trackindex, idx, _:bullet[PR_offsets][0]);
	nBulletEvSet::bulOffY(recslot, trackindex, idx, _:bullet[PR_offsets][1]);
	nBulletEvSet::bulOffZ(recslot, trackindex, idx, _:bullet[PR_offsets][2]);
	nBulletEvSet::bulWeapon(recslot, trackindex, idx, bullet[PR_weaponId]);

	nTrack::trkBulletCount(recslot, trackindex) = idx + 1;
}

// Frame PASSENGER dentro del timeline unificado del jugador. No trae posicion/
// velocidad propia (el cliente interpola al pasajero desde el vehiculo).
static stock Internal_PushPassengerFrame(recslot, trackindex, const passenger[PR_PassengerSync], vehTrackRef)
{
	new frame = nTrack::trkFrameCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(FRAME_BUF[recslot][trackindex], FRAME_CAP[recslot][trackindex], frame + 1, FRAME_FIELD_COUNT, RECORDING_INITIAL_FRAME_CAP, MAX_FRAMES_PER_TRACK))
	{
		Internal_CloseTrack(recslot, trackindex);
		return;
	}

	nFrameSet::frTick(recslot, trackindex, frame, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nFrameSet::frFrameType(recslot, trackindex, frame, FRAME_TYPE_PASSENGER);

	nFrameSet::frX(recslot, trackindex, frame, _:passenger[PR_position][0]);
	nFrameSet::frY(recslot, trackindex, frame, _:passenger[PR_position][1]);
	nFrameSet::frZ(recslot, trackindex, frame, _:passenger[PR_position][2]);
	nFrameSet::frVX(recslot, trackindex, frame, 0);
	nFrameSet::frVY(recslot, trackindex, frame, 0);
	nFrameSet::frVZ(recslot, trackindex, frame, 0);
	nFrameSet::frQW(recslot, trackindex, frame, 0);
	nFrameSet::frQX(recslot, trackindex, frame, 0);
	nFrameSet::frQY(recslot, trackindex, frame, 0);
	nFrameSet::frQZ(recslot, trackindex, frame, 0);
	nFrameSet::frHealth(recslot, trackindex, frame, passenger[PR_playerHealth]);
	nFrameSet::frArmor(recslot, trackindex, frame, passenger[PR_playerArmour]);
	nFrameSet::frWeapon(recslot, trackindex, frame, passenger[PR_weaponId]);
	nFrameSet::frSpecialAction(recslot, trackindex, frame, 0);
	nFrameSet::frKeys(recslot, trackindex, frame, passenger[PR_keys]);
	nFrameSet::frLRKey(recslot, trackindex, frame, passenger[PR_lrKey]);
	nFrameSet::frUDKey(recslot, trackindex, frame, passenger[PR_udKey]);
	nFrameSet::frVehHealth(recslot, trackindex, frame, 0);
	nFrameSet::frSiren(recslot, trackindex, frame, 0);
	nFrameSet::frVehTrackRef(recslot, trackindex, frame, vehTrackRef);
	nFrameSet::frSeat(recslot, trackindex, frame, passenger[PR_seatId]);
	nFrameSet::frAnimId(recslot, trackindex, frame, 0);
	nFrameSet::frAnimFlags(recslot, trackindex, frame, 0);
	nFrameSet::frAdditionalKey(recslot, trackindex, frame, passenger[PR_additionalKey]);
	nFrameSet::frSurfingVeh(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingX(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingY(recslot, trackindex, frame, 0);
	nFrameSet::frSurfingZ(recslot, trackindex, frame, 0);
	nFrameSet::frLandingGear(recslot, trackindex, frame, 0);
	nFrameSet::frTrailerId(recslot, trackindex, frame, 0);
	nFrameSet::frTrainSpeed(recslot, trackindex, frame, 0);
	nFrameSet::frDriveBy(recslot, trackindex, frame, passenger[PR_driveBy]);

	nTrack::trkFrameCount(recslot, trackindex) = frame + 1;
}

// Frame de UNOCCUPIED_SYNC (vehiculo sin conductor) o TRAILER_SYNC (trailer).
// Para frames de TRAILER_SYNC, rollx/rolly/rollz y directionx/y/z son {0,0,0}.
static stock Internal_PushVehFrame(recslot, trackindex, Float:x, Float:y, Float:z, Float:vx, Float:vy, Float:vz, Float:health, Float:rollx, Float:rolly, Float:rollz, Float:directionx, Float:directiony, Float:directionz, Float:turnx = 0.0, Float:turny = 0.0, Float:turnz = 0.0)
{
	new frame = nTrack::trkVehFrameCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(VEH_FRAME_BUF[recslot][trackindex], VEH_FRAME_CAP[recslot][trackindex], frame + 1, VEH_FRAME_FIELD_COUNT, RECORDING_INITIAL_VEH_FRAME_CAP, MAX_VEH_FRAMES_PER_TRACK)) return;

	nVehFrameSet::vfTick(recslot, trackindex, frame, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nVehFrameSet::vfX(recslot, trackindex, frame, _:x);
	nVehFrameSet::vfY(recslot, trackindex, frame, _:y);
	nVehFrameSet::vfZ(recslot, trackindex, frame, _:z);
	nVehFrameSet::vfVX(recslot, trackindex, frame, _:vx);
	nVehFrameSet::vfVY(recslot, trackindex, frame, _:vy);
	nVehFrameSet::vfVZ(recslot, trackindex, frame, _:vz);
	nVehFrameSet::vfHealth(recslot, trackindex, frame, _:health);
	nVehFrameSet::vfRollX(recslot, trackindex, frame, _:rollx);
	nVehFrameSet::vfRollY(recslot, trackindex, frame, _:rolly);
	nVehFrameSet::vfRollZ(recslot, trackindex, frame, _:rollz);
	nVehFrameSet::vfDirX(recslot, trackindex, frame, _:directionx);
	nVehFrameSet::vfDirY(recslot, trackindex, frame, _:directiony);
	nVehFrameSet::vfDirZ(recslot, trackindex, frame, _:directionz);
	nVehFrameSet::vfTurnX(recslot, trackindex, frame, _:turnx);
	nVehFrameSet::vfTurnY(recslot, trackindex, frame, _:turny);
	nVehFrameSet::vfTurnZ(recslot, trackindex, frame, _:turnz);

	nTrack::trkVehFrameCount(recslot, trackindex) = frame + 1;
}

// Evento discreto de WEAPONS_UPDATE (cambio de arma o municion) de un track de jugador.
static stock Internal_PushWeaponEvent(recslot, trackindex, weaponid, ammo)
{
	new idx = nTrack::trkWeaponCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(WEAPON_BUF[recslot][trackindex], WEAPON_CAP[recslot][trackindex], idx + 1, WEAPON_FIELD_COUNT, RECORDING_INITIAL_WEAPON_CAP, MAX_WEAPON_EVENTS_PER_TRACK)) return;

	nWeaponEvSet::weTick(recslot, trackindex, idx, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nWeaponEvSet::weWeaponId(recslot, trackindex, idx, weaponid);
	nWeaponEvSet::weAmmo(recslot, trackindex, idx, ammo);

	nTrack::trkWeaponCount(recslot, trackindex) = idx + 1;
}

// Evento de enganche/desenganche (OnTrailerUpdate) sobre el track del trailer.
// towTrackRef = -1 significa "se desengancho en este momento".
static stock Internal_PushTrailerEvent(recslot, trackindex, towTrackRef)
{
	new idx = nTrack::trkTrailerCount(recslot, trackindex);
	if(!Internal_EnsureCapacity(TRAILER_BUF[recslot][trackindex], TRAILER_CAP[recslot][trackindex], idx + 1, TRAILER_FIELD_COUNT, RECORDING_INITIAL_TRAILER_CAP, MAX_TRAILER_EVENTS_PER_TRACK)) return;

	nTrailerEvSet::teTick(recslot, trackindex, idx, (GetTickCount() - nRec::recStartTick(recslot)) - nTrack::trkStart(recslot, trackindex));
	nTrailerEvSet::teTowTrackRef(recslot, trackindex, idx, towTrackRef);

	nTrack::trkTrailerCount(recslot, trackindex) = idx + 1;
}

public OnPlayerStreamIn(playerid, forplayerid)
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;
		if(nRec::recFocusPlayer(r) != forplayerid) continue;
		if(playerid == forplayerid) continue;

		new Float:px, Float:py, Float:pz;
		GetPlayerPos(playerid, px, py, pz);
		Internal_OpenTrack(r, TRACK_PLAYER, playerid, false, GetPlayerSkin(playerid), -1, px, py, pz);
	}

	#if defined NGR_OnPlayerStreamIn
		return NGR_OnPlayerStreamIn(playerid, forplayerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerStreamIn
	#undef OnPlayerStreamIn
#else
	#define _ALS_OnPlayerStreamIn
#endif
#define OnPlayerStreamIn NGR_OnPlayerStreamIn
#if defined NGR_OnPlayerStreamIn
	forward NGR_OnPlayerStreamIn(playerid, forplayerid);
#endif

public OnPlayerStreamOut(playerid, forplayerid)
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;
		if(nRec::recFocusPlayer(r) != forplayerid) continue;
		if(playerid == forplayerid) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t != -1) Internal_CloseTrack(r, t);
	}

	#if defined NGR_OnPlayerStreamOut
		return NGR_OnPlayerStreamOut(playerid, forplayerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerStreamOut
	#undef OnPlayerStreamOut
#else
	#define _ALS_OnPlayerStreamOut
#endif
#define OnPlayerStreamOut NGR_OnPlayerStreamOut
#if defined NGR_OnPlayerStreamOut
	forward NGR_OnPlayerStreamOut(playerid, forplayerid);
#endif

public OnVehicleStreamIn(vehicleid, forplayerid)
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;
		if(nRec::recFocusPlayer(r) != forplayerid) continue;

		if(!nRec::recIncludeEmptyVehicles(r) && !Internal_IsVehicleOccupied(vehicleid)) continue;

		new Float:vx, Float:vy, Float:vz, Float:vAngle;
		GetVehiclePos(vehicleid, vx, vy, vz);
		GetVehicleZAngle(vehicleid, vAngle);
		Internal_OpenTrack(r, TRACK_VEHICLE, vehicleid, false, -1, GetVehicleModel(vehicleid), vx, vy, vz, vAngle);
	}

	#if defined NGR_OnVehicleStreamIn
		return NGR_OnVehicleStreamIn(vehicleid, forplayerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnVehicleStreamIn
	#undef OnVehicleStreamIn
#else
	#define _ALS_OnVehicleStreamIn
#endif
#define OnVehicleStreamIn NGR_OnVehicleStreamIn
#if defined NGR_OnVehicleStreamIn
	forward NGR_OnVehicleStreamIn(vehicleid, forplayerid);
#endif

public OnVehicleStreamOut(vehicleid, forplayerid)
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;
		if(nRec::recFocusPlayer(r) != forplayerid) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, vehicleid);
		if(t != -1) Internal_CloseTrack(r, t);
	}

	#if defined NGR_OnVehicleStreamOut
		return NGR_OnVehicleStreamOut(vehicleid, forplayerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnVehicleStreamOut
	#undef OnVehicleStreamOut
#else
	#define _ALS_OnVehicleStreamOut
#endif
#define OnVehicleStreamOut NGR_OnVehicleStreamOut
#if defined NGR_OnVehicleStreamOut
	forward NGR_OnVehicleStreamOut(vehicleid, forplayerid);
#endif

// Enganche/desenganche de trailer. Abre track del trailer on-demand y registra
// el evento para reproducirlo correctamente.
public OnTrailerUpdate(playerid, vehicleid)
{
	if(g_ActiveCaptureCount > 0)
	{
		new towveh = GetPlayerVehicleID(playerid);
		new trailerid = (towveh != 0) ? GetVehicleTrailer(towveh) : 0;

		for(new r = 0; r < MAX_RECORDINGS; r++)
		{
			if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;
			if(nRec::recFocusPlayer(r) != playerid) continue;
			if(towveh == 0) continue;

			new towTrackRef = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, towveh);
			if(towTrackRef == -1 && (nRec::recIncludeEmptyVehicles(r) || Internal_IsVehicleOccupied(towveh)))
			{
				new Float:tvx, Float:tvy, Float:tvz, Float:tvAngle;
				GetVehiclePos(towveh, tvx, tvy, tvz);
				GetVehicleZAngle(towveh, tvAngle);
				towTrackRef = Internal_OpenTrack(r, TRACK_VEHICLE, towveh, false, -1, GetVehicleModel(towveh), tvx, tvy, tvz, tvAngle);
			}

			if(trailerid == 0)
		{
				// Desenganche: marca el evento en el track del trailer si ya tenia
				// al menos un enganche previo registrado.
				for(new t = 0, count = nRec::recTrackCount(r); t < count; t++)
			{
					if(nTrack::trkActive(r, t) && nTrack::trkType(r, t) == TRACK_VEHICLE && nTrack::trkEnd(r, t) == -1)
				{
						if(nTrack::trkTrailerCount(r, t) > 0)
					{
							new lastIdx = nTrack::trkTrailerCount(r, t) - 1;
							if(nTrailerEv::teTowTrackRef(r, t, lastIdx) != -1)
								Internal_PushTrailerEvent(r, t, -1);
					}
				}
			}
			}
			else
			{
				new trailerTrackRef = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, trailerid);
				if(trailerTrackRef == -1)
				{
					new Float:tx, Float:ty, Float:tz, Float:tAngle;
					GetVehiclePos(trailerid, tx, ty, tz);
					GetVehicleZAngle(trailerid, tAngle);
					trailerTrackRef = Internal_OpenTrack(r, TRACK_VEHICLE, trailerid, false, -1, GetVehicleModel(trailerid), tx, ty, tz, tAngle);
				}
				if(trailerTrackRef != -1) Internal_PushTrailerEvent(r, trailerTrackRef, towTrackRef);
			}
		}
	}

	#if defined NGR_OnTrailerUpdate
		return NGR_OnTrailerUpdate(playerid, vehicleid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnTrailerUpdate
	#undef OnTrailerUpdate
#else
	#define _ALS_OnTrailerUpdate
#endif
#define OnTrailerUpdate NGR_OnTrailerUpdate
#if defined NGR_OnTrailerUpdate
	forward NGR_OnTrailerUpdate(playerid, vehicleid);
#endif

// ============================================================================
// Hooks de sync packets
// ============================================================================

IPacket:ONFOOT_SYNC(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	new onFoot[PR_OnFootSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadOnFootSync(bs, onFoot);

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		Internal_PushFootFrame(r, t, onFoot);
	}
	return 1;
}

IPacket:DRIVER_SYNC(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	new inCar[PR_InCarSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadInCarSync(bs, inCar);

	new vehid = GetPlayerVehicleID(playerid);

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		new vehTrackRef = -1;
		if(vehid != 0)
		{
			vehTrackRef = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, vehid);

			// Abre el track del vehiculo si el jugador ya estaba arriba al
			// iniciar la Recording (nunca disparo OnVehicleStreamIn).
			if(vehTrackRef == -1 && (nRec::recIncludeEmptyVehicles(r) || Internal_IsVehicleOccupied(vehid)))
			{
				new Float:vx, Float:vy, Float:vz, Float:vAngle;
				GetVehiclePos(vehid, vx, vy, vz);
				GetVehicleZAngle(vehid, vAngle);
				vehTrackRef = Internal_OpenTrack(r, TRACK_VEHICLE, vehid, false, -1, GetVehicleModel(vehid), vx, vy, vz, vAngle);
			}
		}
		Internal_PushCarFrame(r, t, inCar, vehTrackRef);
	}
	return 1;
}

IPacket:AIM_SYNC_PACKET(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	new aim[PR_AimSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadAimSync(bs, aim);

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		Internal_PushAimEvent(r, t, aim);
	}
	return 1;
}

IPacket:BULLET_SYNC_PACKET(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	new bullet[PR_BulletSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadBulletSync(bs, bullet);

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		Internal_PushBulletEvent(r, t, bullet);
	}
	return 1;
}

// UNOCCUPIED_SYNC: fisica de un vehiculo sin conductor.
// Campos: PR_vehicleId, PR_seatId, PR_roll[3], PR_direction[3],
//         PR_position[3], PR_velocity[3], PR_angularVelocity[3], PR_vehicleHealth.
IPacket:UNOCCUPIED_SYNC(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	#pragma unused playerid
	new unoccupied[PR_UnoccupiedSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadUnoccupiedSync(bs, unoccupied);

	new vehicleid = unoccupied[PR_vehicleId];

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, vehicleid);
		if(t == -1)
		{
			if(!nRec::recIncludeEmptyVehicles(r)) continue;
			if(!IsVehicleStreamedIn(vehicleid, nRec::recFocusPlayer(r))) continue;
			new Float:vx, Float:vy, Float:vz, Float:vAngle;
			GetVehiclePos(vehicleid, vx, vy, vz);
			GetVehicleZAngle(vehicleid, vAngle);
			t = Internal_OpenTrack(r, TRACK_VEHICLE, vehicleid, false, -1, GetVehicleModel(vehicleid), vx, vy, vz, vAngle);
			if(t == -1) continue;
		}

		Internal_PushVehFrame(r, t,
			unoccupied[PR_position][0], unoccupied[PR_position][1], unoccupied[PR_position][2],
			unoccupied[PR_velocity][0], unoccupied[PR_velocity][1], unoccupied[PR_velocity][2],
			Float:unoccupied[PR_vehicleHealth],
			unoccupied[PR_roll][0], unoccupied[PR_roll][1], unoccupied[PR_roll][2],
			unoccupied[PR_direction][0], unoccupied[PR_direction][1], unoccupied[PR_direction][2],
			unoccupied[PR_angularVelocity][0], unoccupied[PR_angularVelocity][1], unoccupied[PR_angularVelocity][2]
		);
	}
	return 1;
}

// TRAILER_SYNC: fisica de un trailer (enganchado o suelto).
// Campos: PR_trailerId, PR_position[3], PR_quaternion[4], PR_velocity[3], PR_angularVelocity[3].
// No trae roll/direction (usa quaternion), por eso se almacenan {0,0,0} en ambos.
IPacket:TRAILER_SYNC(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	#pragma unused playerid
	new trailer[PR_TrailerSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadTrailerSync(bs, trailer);

	new trailerid = trailer[PR_trailerId];

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, trailerid);
		if(t == -1)
		{
			if(!nRec::recIncludeEmptyVehicles(r)) continue;
			new Float:vx, Float:vy, Float:vz, Float:vAngle;
			GetVehiclePos(trailerid, vx, vy, vz);
			GetVehicleZAngle(trailerid, vAngle);
			t = Internal_OpenTrack(r, TRACK_VEHICLE, trailerid, false, -1, GetVehicleModel(trailerid), vx, vy, vz, vAngle);
			if(t == -1) continue;
		}

		// TRAILER_SYNC no trae roll/direction, se pasan {0,0,0}.
		// Ghost_ApplyUnoccupied los interpreta como "sin dato, no tocar rotacion".
		Internal_PushVehFrame(r, t,
			trailer[PR_position][0], trailer[PR_position][1], trailer[PR_position][2],
			trailer[PR_velocity][0], trailer[PR_velocity][1], trailer[PR_velocity][2],
			1000.0, // TRAILER_SYNC no trae vida, se asume completa
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0,
			trailer[PR_angularVelocity][0], trailer[PR_angularVelocity][1], trailer[PR_angularVelocity][2]
		);
	}
	return 1;
}

// PASSENGER_SYNC: estado (no posicion) de un pasajero.
IPacket:PASSENGER_SYNC_PACKET(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;
	new passenger[PR_PassengerSync];
	BS_IgnoreBits(bs, 8);
	BS_ReadPassengerSync(bs, passenger);

	new vehid = passenger[PR_vehicleId];
	new seat = passenger[PR_seatId];

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		nTrack::trkIsPassenger(r, t) = true;
		nTrack::trkSeatId(r, t) = seat;

		new vehTrackRef = Internal_FindOpenTrackByRealId(r, TRACK_VEHICLE, vehid);
		if(vehTrackRef == -1 && (nRec::recIncludeEmptyVehicles(r) || Internal_IsVehicleOccupied(vehid)))
		{
			new Float:vx, Float:vy, Float:vz, Float:vAngle;
			GetVehiclePos(vehid, vx, vy, vz);
			GetVehicleZAngle(vehid, vAngle);
			vehTrackRef = Internal_OpenTrack(r, TRACK_VEHICLE, vehid, false, -1, GetVehicleModel(vehid), vx, vy, vz, vAngle);
		}

		Internal_PushPassengerFrame(r, t, passenger, vehTrackRef);
	}
	return 1;
}

// WEAPONS_UPDATE: cambio de arma/municion de un slot.
IPacket:WEAPONS_UPDATE_PACKET(playerid, BitStream:bs)
{
	if(g_ActiveCaptureCount <= 0) return 1;

	// SA-MP 0.3.7 WeaponsUpdate (204): uint8 pktId, uint16 targetPlayer, uint16 targetActor, then {uint8 slot, uint8 weaponId, uint16 ammo}*
	BS_IgnoreBits(bs, 8 + 16 + 16);

	new count = 0, slotId, weaponid[13], ammo[13];
	new unreadBits;
	BS_GetNumberOfUnreadBits(bs, unreadBits);
	while(unreadBits >= 32 && count < 13)
	{
		BS_ReadValue(bs, PR_UINT8, slotId, PR_UINT8, weaponid[count], PR_UINT16, ammo[count]);
		#pragma unused slotId
		count++;
		unreadBits -= 32;
	}
	if(count == 0) return 1;

	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recCapturing(r)) continue;

		new t = Internal_FindOpenTrackByRealId(r, TRACK_PLAYER, playerid);
		if(t == -1) continue;

		for(new i = 0; i < count; i++)
		{
			Internal_PushWeaponEvent(r, t, weaponid[i], ammo[i]);
		}
	}
	return 1;
}

//      _____  _           __     ______          _____ _  __
//     |  __ \| |        /\\ \   / /  _ \   /\   / ____| |/ /
//     | |__) | |       /  \\ \_/ /| |_) | /  \ | |    | ' /
//     |  ___/| |      / /\ \\   / |  _ < / /\ \| |    |  <
//     | |    | |____ / ____ \| |  | |_) / ____ \ |____| . \
//     |_|    |______/_/    \_\_|  |____/_/    \_\_____|_|\_\
//
//

static stock Internal_SpawnTrackGhost(recslot, trackindex)
{
	new bool:ok = false;
	if(nTrack::trkType(recslot, trackindex) == TRACK_PLAYER)
	{
		new Ghost:g = Ghost_Create(
			nTrack::trkSkin(recslot, trackindex),
			nTrack::trkSpawnX(recslot, trackindex), nTrack::trkSpawnY(recslot, trackindex), nTrack::trkSpawnZ(recslot, trackindex)
		);
		nTrack::trkPlayGhost(recslot, trackindex) = g;
		ok = Ghost_IsValid(g);
	}
	else // TRACK_VEHICLE
	{
		new GhostVehicle:v = Ghost_CreateVehicle(
			nTrack::trkModel(recslot, trackindex),
			nTrack::trkSpawnX(recslot, trackindex), nTrack::trkSpawnY(recslot, trackindex), nTrack::trkSpawnZ(recslot, trackindex),
			.angle = nTrack::trkSpawnAngle(recslot, trackindex)
		);
		nTrack::trkPlayVehicle(recslot, trackindex) = v;
		ok = Ghost_IsVehicleValid(v);
	}
	nTrack::trkPlaySpawned(recslot, trackindex) = ok;
}

static stock Internal_DespawnTrackGhost(recslot, trackindex)
{
	if(nTrack::trkType(recslot, trackindex) == TRACK_PLAYER)
	{
		new Ghost:g = nTrack::trkPlayGhost(recslot, trackindex);
		if(Ghost_IsValid(g)) Ghost_Destroy(g);
		nTrack::trkPlayGhost(recslot, trackindex) = INVALID_GHOST_ID;
	}
	else
	{
		new GhostVehicle:v = nTrack::trkPlayVehicle(recslot, trackindex);
		if(Ghost_IsVehicleValid(v)) Ghost_DestroyVehicle(v);
		nTrack::trkPlayVehicle(recslot, trackindex) = INVALID_GHOST_VEHICLE_ID;
	}
	nTrack::trkPlaySpawned(recslot, trackindex) = false;
}

// Aplica el frame `frameidx` al ghost del track, resolviendo entrar/salir de
// vehiculo segun haga falta usando el track de vehiculo referenciado.
static stock Internal_ApplyTrackFrame(recslot, trackindex, frameidx)
{
	new Ghost:g = nTrack::trkPlayGhost(recslot, trackindex);
	if(!Ghost_IsValid(g)) return;

	new onFoot[PR_OnFootSync], inCar[PR_InCarSync], tick, vehTrackRef, seat;
	new type = Recording_GetFrame(Recording:recslot, trackindex, frameidx, onFoot, inCar, tick, vehTrackRef, seat);

	if(type == FRAME_TYPE_FOOT)
	{
		if(Ghost_IsInVehicle(g)) Ghost_ExitVehicle(g);
		Ghost_ApplyOnFoot(g, onFoot);
	}
	else if(type == FRAME_TYPE_CAR)
	{
		if(!Ghost_IsInVehicle(g) && vehTrackRef != -1 && vehTrackRef < MAX_TRACKS_PER_RECORDING && nTrack::trkPlaySpawned(recslot, vehTrackRef))
		{
			new GhostVehicle:v = nTrack::trkPlayVehicle(recslot, vehTrackRef);
			if(Ghost_IsVehicleValid(v)) Ghost_EnterVehicle(g, v);
		}
		if(Ghost_IsInVehicle(g)) Ghost_ApplyInCar(g, inCar);
	}
	else // FRAME_TYPE_PASSENGER
	{
		if(!Ghost_IsInVehicle(g) && vehTrackRef != -1 && vehTrackRef < MAX_TRACKS_PER_RECORDING && nTrack::trkPlaySpawned(recslot, vehTrackRef))
		{
			new GhostVehicle:v = nTrack::trkPlayVehicle(recslot, vehTrackRef);
			if(Ghost_IsVehicleValid(v)) Ghost_EnterVehicle(g, v, seat);
		}
		if(Ghost_IsPassenger(g))
		{
			new passenger[PR_PassengerSync];
			passenger[PR_driveBy] = nFrame::frDriveBy(recslot, trackindex, frameidx);
			passenger[PR_weaponId] = inCar[PR_weaponId];
			passenger[PR_additionalKey] = inCar[PR_additionalKey];
			passenger[PR_playerHealth] = inCar[PR_playerHealth];
			passenger[PR_playerArmour] = inCar[PR_armour];
			passenger[PR_lrKey] = inCar[PR_lrKey];
			passenger[PR_udKey] = inCar[PR_udKey];
			passenger[PR_keys] = inCar[PR_keys];
			passenger[PR_position][0] = Float:nFrame::frX(recslot, trackindex, frameidx);
			passenger[PR_position][1] = Float:nFrame::frY(recslot, trackindex, frameidx);
			passenger[PR_position][2] = Float:nFrame::frZ(recslot, trackindex, frameidx);
			Ghost_ApplyPassenger(g, passenger);
		}
	}
}

// Obtiene el virtualid del Ghost de jugador mas adecuado para actuar como reportero de fisica.
// Si hay un Ghost valido spawneado en la grabacion, devuelve su virtualid; si no, INVALID_PLAYER_ID.
static stock Internal_FindActiveGhostVid(recslot)
{
	for(new t = 0, count = nRec::recTrackCount(recslot); t < count; t++)
	{
		if(nTrack::trkActive(recslot, t) && nTrack::trkType(recslot, t) == TRACK_PLAYER && nTrack::trkPlaySpawned(recslot, t))
		{
			new Ghost:g = nTrack::trkPlayGhost(recslot, t);
			if(Ghost_IsValid(g))
			{
				new vid = Ghost_GetVirtualId(g);
				if(vid != INVALID_PLAYER_ID) return vid;
			}
		}
	}
	return INVALID_PLAYER_ID;
}

static stock Internal_UpdateTrackPlayback(recslot, trackindex, elapsed)
{
	new tStart = nTrack::trkStart(recslot, trackindex);
	new tEnd = nTrack::trkEnd(recslot, trackindex);

	if(!nTrack::trkPlaySpawned(recslot, trackindex))
	{
		if(elapsed < tStart) return;
		Internal_SpawnTrackGhost(recslot, trackindex);
	}

	new trackElapsed = elapsed - tStart;

	new Ghost:g = nTrack::trkPlayGhost(recslot, trackindex);
	if(nTrack::trkType(recslot, trackindex) == TRACK_PLAYER)
	{
		new frameCount = nTrack::trkFrameCount(recslot, trackindex);
		new fi = nTrack::trkPlayFrameIdx(recslot, trackindex);
		new aimCount = (Ghost_IsValid(g)) ? nTrack::trkAimCount(recslot, trackindex) : 0;
		new ai = nTrack::trkPlayAimIdx(recslot, trackindex);
		new bulletCount = (Ghost_IsValid(g)) ? nTrack::trkBulletCount(recslot, trackindex) : 0;
		new bi = nTrack::trkPlayBulletIdx(recslot, trackindex);

		// Despacho en orden cronologico estricto (Aim -> Bullet -> Frame en el mismo tick)
		// para evitar que el ped cancele animaciones o se desincronice en combate rapido / C-Bug
		while((fi < frameCount && nFrame::frTick(recslot, trackindex, fi) <= trackElapsed) ||
		      (ai < aimCount && nAimEv::aimTick(recslot, trackindex, ai) <= trackElapsed) ||
		      (bi < bulletCount && nBulletEv::bulTick(recslot, trackindex, bi) <= trackElapsed))
		{
			new tFrame = 0x7FFFFFFF;
			if(fi < frameCount && nFrame::frTick(recslot, trackindex, fi) <= trackElapsed)
			{
				tFrame = nFrame::frTick(recslot, trackindex, fi);
			}
			new tAim = 0x7FFFFFFF;
			if(ai < aimCount && nAimEv::aimTick(recslot, trackindex, ai) <= trackElapsed)
			{
				tAim = nAimEv::aimTick(recslot, trackindex, ai);
			}
			new tBul = 0x7FFFFFFF;
			if(bi < bulletCount && nBulletEv::bulTick(recslot, trackindex, bi) <= trackElapsed)
			{
				tBul = nBulletEv::bulTick(recslot, trackindex, bi);
			}

			if(tAim <= tFrame && tAim <= tBul)
			{
				new aim[PR_AimSync];
				aim[PR_camMode] = nAimEv::aimCamMode(recslot, trackindex, ai);
				aim[PR_camFrontVec][0] = Float:nAimEv::aimFrontX(recslot, trackindex, ai);
				aim[PR_camFrontVec][1] = Float:nAimEv::aimFrontY(recslot, trackindex, ai);
				aim[PR_camFrontVec][2] = Float:nAimEv::aimFrontZ(recslot, trackindex, ai);
				aim[PR_camPos][0] = Float:nAimEv::aimPosX(recslot, trackindex, ai);
				aim[PR_camPos][1] = Float:nAimEv::aimPosY(recslot, trackindex, ai);
				aim[PR_camPos][2] = Float:nAimEv::aimPosZ(recslot, trackindex, ai);
				aim[PR_aimZ] = Float:nAimEv::aimZ(recslot, trackindex, ai);
				aim[PR_camZoom] = nAimEv::aimCamZoom(recslot, trackindex, ai);
				aim[PR_weaponState] = nAimEv::aimWeaponState(recslot, trackindex, ai);
				aim[PR_aspectRatio] = nAimEv::aimAspectRatio(recslot, trackindex, ai);
				Ghost_ApplyAim(g, aim);
				ai++;
			}
			else if(tBul <= tFrame && tBul <= tAim)
			{
				new bullet[PR_BulletSync];
				bullet[PR_hitType] = nBulletEv::bulHitType(recslot, trackindex, bi);
				bullet[PR_hitId] = nBulletEv::bulHitId(recslot, trackindex, bi);
				bullet[PR_origin][0] = Float:nBulletEv::bulOriginX(recslot, trackindex, bi);
				bullet[PR_origin][1] = Float:nBulletEv::bulOriginY(recslot, trackindex, bi);
				bullet[PR_origin][2] = Float:nBulletEv::bulOriginZ(recslot, trackindex, bi);
				bullet[PR_hitPos][0] = Float:nBulletEv::bulHitX(recslot, trackindex, bi);
				bullet[PR_hitPos][1] = Float:nBulletEv::bulHitY(recslot, trackindex, bi);
				bullet[PR_hitPos][2] = Float:nBulletEv::bulHitZ(recslot, trackindex, bi);
				bullet[PR_offsets][0] = Float:nBulletEv::bulOffX(recslot, trackindex, bi);
				bullet[PR_offsets][1] = Float:nBulletEv::bulOffY(recslot, trackindex, bi);
				bullet[PR_offsets][2] = Float:nBulletEv::bulOffZ(recslot, trackindex, bi);
				bullet[PR_weaponId] = nBulletEv::bulWeapon(recslot, trackindex, bi);
				Ghost_ApplyBullet(g, bullet);
				bi++;
			}
			else
			{
				Internal_ApplyTrackFrame(recslot, trackindex, fi);
				fi++;
			}
		}
		nTrack::trkPlayFrameIdx(recslot, trackindex) = fi;
		nTrack::trkPlayAimIdx(recslot, trackindex) = ai;
		nTrack::trkPlayBulletIdx(recslot, trackindex) = bi;
	}
	else
	{
		new frameCount = nTrack::trkFrameCount(recslot, trackindex);
		new fi = nTrack::trkPlayFrameIdx(recslot, trackindex);
		while(fi < frameCount && nFrame::frTick(recslot, trackindex, fi) <= trackElapsed)
		{
			Internal_ApplyTrackFrame(recslot, trackindex, fi);
			fi++;
		}
		nTrack::trkPlayFrameIdx(recslot, trackindex) = fi;
	}

	if(nTrack::trkType(recslot, trackindex) == TRACK_VEHICLE)
	{
		new GhostVehicle:v = nTrack::trkPlayVehicle(recslot, trackindex);
		if(Ghost_IsVehicleValid(v))
		{
			new vehFrameCount = nTrack::trkVehFrameCount(recslot, trackindex);
			new vfi = nTrack::trkPlayVehFrameIdx(recslot, trackindex);
			new Float:vx, Float:vy, Float:vz, Float:vvx, Float:vvy, Float:vvz, Float:vhealth, vtick;
			new Float:vroll[3], Float:vdir[3];
			new Float:vturn[3];
			new bool:hasVehFrame = false;
			new Float:applyX, Float:applyY, Float:applyZ, Float:applyVX, Float:applyVY, Float:applyVZ, Float:applyHealth;
			new Float:applyRoll[3], Float:applyDir[3], Float:applyTurn[3];
			while(vfi < vehFrameCount && Recording_GetVehFrame(Recording:recslot, trackindex, vfi, vx, vy, vz, vvx, vvy, vvz, vhealth, vtick, vroll, vdir, vturn) && vtick <= trackElapsed)
			{
				hasVehFrame = true;
				applyX = vx; applyY = vy; applyZ = vz;
				applyVX = vvx; applyVY = vvy; applyVZ = vvz;
				applyHealth = vhealth;
				applyRoll[0] = vroll[0]; applyRoll[1] = vroll[1]; applyRoll[2] = vroll[2];
				applyDir[0] = vdir[0]; applyDir[1] = vdir[1]; applyDir[2] = vdir[2];
				applyTurn[0] = vturn[0]; applyTurn[1] = vturn[1]; applyTurn[2] = vturn[2];
				vfi++;
			}
			if(hasVehFrame)
			{
				new repId = Internal_FindActiveGhostVid(recslot);
				Ghost_ApplyUnoccupied(v, applyX, applyY, applyZ, applyVX, applyVY, applyVZ, applyHealth, .rollOverride = applyRoll, .directionOverride = applyDir, .reporterVirtualId = repId, .turnOverride = applyTurn);
			}
			nTrack::trkPlayVehFrameIdx(recslot, trackindex) = vfi;

			new trailerCount = nTrack::trkTrailerCount(recslot, trackindex);
			new ti = nTrack::trkPlayTrailerIdx(recslot, trackindex);
			while(ti < trailerCount && nTrailerEv::teTick(recslot, trackindex, ti) <= trackElapsed)
			{
				new towTrackRef = nTrailerEv::teTowTrackRef(recslot, trackindex, ti);
				if(towTrackRef == -1)
				{
					// Desenganche: busca el cabezal en el ultimo evento de enganche.
					if(ti > 0)
					{
						new lastTow = nTrailerEv::teTowTrackRef(recslot, trackindex, ti - 1);
						if(lastTow >= 0 && lastTow < MAX_TRACKS_PER_RECORDING && nTrack::trkPlaySpawned(recslot, lastTow))
						{
							new GhostVehicle:towVeh = nTrack::trkPlayVehicle(recslot, lastTow);
							if(Ghost_IsVehicleValid(towVeh)) Ghost_DetachTrailer(towVeh);
						}
					}
				}
				else if(towTrackRef >= 0 && towTrackRef < MAX_TRACKS_PER_RECORDING && nTrack::trkPlaySpawned(recslot, towTrackRef))
				{
					new GhostVehicle:tow = nTrack::trkPlayVehicle(recslot, towTrackRef);
					if(Ghost_IsVehicleValid(tow)) Ghost_AttachTrailer(tow, v);
				}
				ti++;
			}
			nTrack::trkPlayTrailerIdx(recslot, trackindex) = ti;
		}
	}

	if(tEnd != -1 && elapsed >= tEnd && nTrack::trkPlaySpawned(recslot, trackindex))
	{
		Internal_DespawnTrackGhost(recslot, trackindex);
	}
}

static stock Internal_StopPlaybackBySlot(slot)
{
	for(new t = 0; t < MAX_TRACKS_PER_RECORDING; t++)
	{
		if(nTrack::trkPlaySpawned(slot, t))
		{
			Internal_DespawnTrackGhost(slot, t);
		}
	}
	nRec::recIsPlaying(slot) = false;

	new bool:anyPlaying = false;
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(nRec::recSlotUsed(r) && nRec::recIsPlaying(r)) { anyPlaying = true; break; }
	}
	if(!anyPlaying && g_PlaybackTimerRunning)
	{
		if(g_PlaybackTimerId != -1) KillTimer(g_PlaybackTimerId);
		g_PlaybackTimerId = -1;
		g_PlaybackTimerRunning = false;
	}
}

forward Internal_PlaybackTick();
public Internal_PlaybackTick()
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(!nRec::recSlotUsed(r) || !nRec::recIsPlaying(r)) continue;

		new elapsed = GetTickCount() - nRec::recPlayStartTick(r);
		new bool:finished = (elapsed >= nRec::recDuration(r));
		if(finished) elapsed = nRec::recDuration(r);

		for(new t = 0, count = nRec::recTrackCount(r); t < count; t++)
		{
			if(nTrack::trkActive(r, t)) Internal_UpdateTrackPlayback(r, t, elapsed);
		}

		if(finished) Internal_StopPlaybackBySlot(r);
	}
	return 1;
}

/*
	Recording_Play
	Arranca la reproduccion de una Recording ya detenida. Crea ghosts
	(jugadores y vehiculos) en el momento exacto en que cada track entra
	en escena y los destruye cuando terminan. Soporta multiples
	reproduciones simultaneas (comparten un unico timer interno).

	Parametros:
		&Recording:rec - Sesion a reproducir (debe estar detenida)

	Retorna:
		true si arranco la reproduccion
		false si es invalida, sigue capturando o ya se esta reproduciendo

	Ejemplo:
		Recording_Play(rec);
*/
stock bool:Recording_Play(Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;
	if(Recording_IsCapturing(rec)) return false;

	new slot = _:rec;
	if(nRec::recIsPlaying(slot)) return false;

	for(new t = 0, count = nRec::recTrackCount(slot); t < count; t++)
	{
		nTrack::trkPlaySpawned(slot, t) = false;
		nTrack::trkPlayGhost(slot, t) = INVALID_GHOST_ID;
		nTrack::trkPlayVehicle(slot, t) = INVALID_GHOST_VEHICLE_ID;
		nTrack::trkPlayFrameIdx(slot, t) = 0;
		nTrack::trkPlayAimIdx(slot, t) = 0;
		nTrack::trkPlayBulletIdx(slot, t) = 0;
		nTrack::trkPlayVehFrameIdx(slot, t) = 0;
		nTrack::trkPlayWeaponIdx(slot, t) = 0;
		nTrack::trkPlayTrailerIdx(slot, t) = 0;
	}

	nRec::recPlayStartTick(slot) = GetTickCount();
	nRec::recIsPlaying(slot) = true;

	if(!g_PlaybackTimerRunning)
	{
		g_PlaybackTimerId = SetTimer("Internal_PlaybackTick", PLAYBACK_TICK_INTERVAL, true);
		g_PlaybackTimerRunning = true;
	}
	return true;
}

/*
	Recording_StopPlayback
	Corta la reproduccion antes de tiempo y destruye todos los ghosts
	activos en ese momento. La Recording NO se destruye y puede volver
	a reproducirse con Recording_Play.
*/
stock bool:Recording_StopPlayback(Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;
	if(!nRec::recIsPlaying(_:rec)) return false;

	Internal_StopPlaybackBySlot(_:rec);
	return true;
}

stock bool:Recording_IsPlaying(Recording:rec)
{
	if(!Recording_IsValid(rec)) return false;
	return nRec::recIsPlaying(_:rec);
}

//      ________   _______   ____  _____ _______
//     |  ____\ \ / /  __ \ / __ \|  __ \__   __|
//     | |__   \ V /| |__) | |  | | |__) | | |
//     |  __|   > < |  ___/| |  | |  _  /  | |
//     | |____ / . \| |    | |__| | | \ \  | |
//     |______/_/ \_\_|     \____/|_|  \_\ |_|
//
//

// Antepone RECORDING_SAVE_FOLDER al nombre de fichero.
static stock Internal_BuildSavePath(dest[], len, const filename[])
{
	dest[0] = EOS;
	format(dest, len, "%s%s", RECORDING_SAVE_FOLDER, filename);
}

// Vuelca 'elementCount' elementos (de 'fieldCount' celdas cada uno) desde un
// buffer malloc'd al fichero, en tandas de COPY_CHUNK_CELLS.
static stock Internal_WriteBufferBlock(File:file, Alloc:buf, fieldCount, elementCount)
{
	new totalCells = elementCount * fieldCount;
	if(totalCells <= 0) return;

	new tmp[COPY_CHUNK_CELLS];
	new written = 0;
	while(written < totalCells)
	{
		new chunk = totalCells - written;
		if(chunk > COPY_CHUNK_CELLS) chunk = COPY_CHUNK_CELLS;
		Malloc_GetA(tmp, chunk, buf, written);
		fblockwrite(file, tmp, chunk);
		written += chunk;
	}
}

/*
	Recording_SaveToFile
	Exporta una Recording ya detenida a un fichero binario.

	Parametros:
		&Recording:rec - Sesion a exportar (debe estar detenida)
		filename[] - Solo el nombre del fichero (ej. "escena1.rec"),
		             sin carpeta. Se guarda dentro de RECORDING_SAVE_FOLDER.

	Retorna:
		true si se guardo correctamente

	Ejemplo:
		Recording_SaveToFile(rec, "pelea_2026_08_26.rec");
*/
stock bool:Recording_SaveToFile(Recording:rec, const filename[])
{
	if(!Recording_IsValid(rec)) return false;
	if(Recording_IsCapturing(rec)) return false;

	new slot = _:rec;

	new path[128];
	Internal_BuildSavePath(path, sizeof path, filename);

	new File:file = fopen(path, io_write);
	if(!file) return false;

	new header[8];
	header[0] = RECORDING_FILE_MAGIC;
	header[1] = RECORDING_FILE_VERSION;
	header[2] = nRec::recFocusPlayer(slot);
	header[3] = _:nRec::recIncludeEmptyVehicles(slot);
	header[4] = nRec::recDuration(slot);
	header[5] = nRec::recTrackCount(slot);
	header[6] = 0;
	header[7] = 0;
	fblockwrite(file, header, sizeof header);

	for(new t = 0, count = nRec::recTrackCount(slot); t < count; t++)
	{
		new trkHeader[15];
		trkHeader[0] = nTrack::trkType(slot, t);
		trkHeader[1] = _:nTrack::trkIsFocus(slot, t);
		trkHeader[2] = nTrack::trkRealId(slot, t);
		trkHeader[3] = nTrack::trkSkin(slot, t);
		trkHeader[4] = nTrack::trkModel(slot, t);
		trkHeader[5] = nTrack::trkStart(slot, t);
		trkHeader[6] = nTrack::trkEnd(slot, t);
		trkHeader[7] = nTrack::trkFrameCount(slot, t);
		trkHeader[8] = nTrack::trkAimCount(slot, t);
		trkHeader[9] = nTrack::trkBulletCount(slot, t);
		trkHeader[10] = _:nTrack::trkIsPassenger(slot, t);
		trkHeader[11] = nTrack::trkSeatId(slot, t);
		trkHeader[12] = nTrack::trkVehFrameCount(slot, t);
		trkHeader[13] = nTrack::trkWeaponCount(slot, t);
		trkHeader[14] = nTrack::trkTrailerCount(slot, t);
		fblockwrite(file, trkHeader, sizeof trkHeader);

		new Float:spawn[4];
		spawn[0] = nTrack::trkSpawnX(slot, t);
		spawn[1] = nTrack::trkSpawnY(slot, t);
		spawn[2] = nTrack::trkSpawnZ(slot, t);
		spawn[3] = nTrack::trkSpawnAngle(slot, t);
		fblockwrite(file, _:spawn, sizeof spawn);

		Internal_WriteBufferBlock(file, FRAME_BUF[slot][t], FRAME_FIELD_COUNT, trkHeader[7]);
		Internal_WriteBufferBlock(file, AIM_BUF[slot][t], AIM_FIELD_COUNT, trkHeader[8]);
		Internal_WriteBufferBlock(file, BULLET_BUF[slot][t], BULLET_FIELD_COUNT, trkHeader[9]);
		Internal_WriteBufferBlock(file, VEH_FRAME_BUF[slot][t], VEH_FRAME_FIELD_COUNT, trkHeader[12]);
		Internal_WriteBufferBlock(file, WEAPON_BUF[slot][t], WEAPON_FIELD_COUNT, trkHeader[13]);
		Internal_WriteBufferBlock(file, TRAILER_BUF[slot][t], TRAILER_FIELD_COUNT, trkHeader[14]);
	}

	fclose(file);
	return true;
}

// Lee 'savedCount' elementos de un buffer malloc'd desde fichero, en tandas.
// Si el fichero tiene mas elementos de los que soporta este build, salta los
// que sobran con fseek para no desalinear el resto del fichero.
static stock Internal_ReadBufferBlock(File:file, &Alloc:buf, &cap, fieldCount, savedCount, initialCap, hardCeiling)
{
	new toRead = (savedCount > hardCeiling) ? hardCeiling : savedCount;

	if(toRead > 0)
	{
		if(!Internal_EnsureCapacity(buf, cap, toRead, fieldCount, initialCap, hardCeiling))
		{
			toRead = 0;
		}
		else
		{
			new totalCells = toRead * fieldCount;
			new tmp[COPY_CHUNK_CELLS];
			new readCells = 0;
			while(readCells < totalCells)
			{
				new chunk = totalCells - readCells;
				if(chunk > COPY_CHUNK_CELLS) chunk = COPY_CHUNK_CELLS;
				fblockread(file, tmp, chunk);
				Malloc_SetA(buf, readCells, tmp, chunk);
				readCells += chunk;
			}
		}
	}

	if(savedCount > toRead)
	{
		fseek(file, (savedCount - toRead) * fieldCount * 4, seek_current);
	}
	return toRead;
}

/*
	Recording_LoadFromFile
	Carga una Recording previamente guardada con Recording_SaveToFile.
	Queda detenida (no capturando) y lista para Recording_Play o para
	leerla con Recording_GetTrackInfo/Recording_GetFrame.

	Parametros:
		filename[] - Solo el nombre del fichero (ej. "escena1.rec"),
		             sin carpeta. Se busca dentro de RECORDING_SAVE_FOLDER.

	Retorna:
		Recording:id de la sesion cargada
		INVALID_RECORDING_ID si el fichero no existe, no es del formato
		correcto, o no hay slots libres

	Ejemplo:
		new Recording:rec = Recording_LoadFromFile("pelea_2026_08_26.rec");
		if(Recording_IsValid(rec)) Recording_Play(rec);
*/
stock Recording:Recording_LoadFromFile(const filename[])
{
	new path[128];
	Internal_BuildSavePath(path, sizeof path, filename);

	new File:file = fopen(path, io_read);
	if(!file) return INVALID_RECORDING_ID;

	new header[8];
	if(fblockread(file, header, sizeof header) != sizeof header) { fclose(file); return INVALID_RECORDING_ID; }
	if(header[0] != RECORDING_FILE_MAGIC) { fclose(file); return INVALID_RECORDING_ID; }
	if(header[1] != RECORDING_FILE_VERSION) { fclose(file); return INVALID_RECORDING_ID; }

	new slot = Internal_FindFreeRecordingSlot();
	if(slot == -1) { fclose(file); return INVALID_RECORDING_ID; }

	nRec::recSlotUsed(slot) = true;
	nRec::recCapturing(slot) = false;
	nRec::recFocusPlayer(slot) = header[2];
	nRec::recIncludeEmptyVehicles(slot) = bool:header[3];
	nRec::recStartTick(slot) = 0;
	nRec::recDuration(slot) = header[4];
	nRec::recIsPlaying(slot) = false;

	new savedTrackCount = header[5];
	new loadTrackCount = (savedTrackCount > MAX_TRACKS_PER_RECORDING) ? MAX_TRACKS_PER_RECORDING : savedTrackCount;

	for(new t = 0; t < loadTrackCount; t++)
	{
		new trkHeader[15];
		if(fblockread(file, trkHeader, sizeof trkHeader) != sizeof trkHeader) { loadTrackCount = t; break; }

		new Float:spawn[4];
		fblockread(file, _:spawn, sizeof spawn);

		nTrack::trkActive(slot, t) = true;
		nTrack::trkType(slot, t) = trkHeader[0];
		nTrack::trkIsFocus(slot, t) = bool:trkHeader[1];
		nTrack::trkRealId(slot, t) = trkHeader[2];
		nTrack::trkSkin(slot, t) = trkHeader[3];
		nTrack::trkModel(slot, t) = trkHeader[4];
		nTrack::trkStart(slot, t) = trkHeader[5];
		nTrack::trkEnd(slot, t) = trkHeader[6];
		nTrack::trkSpawnX(slot, t) = spawn[0];
		nTrack::trkSpawnY(slot, t) = spawn[1];
		nTrack::trkSpawnZ(slot, t) = spawn[2];
		nTrack::trkSpawnAngle(slot, t) = spawn[3];
		nTrack::trkIsPassenger(slot, t) = bool:trkHeader[10];
		nTrack::trkSeatId(slot, t) = trkHeader[11];

		nTrack::trkFrameCount(slot, t) = Internal_ReadBufferBlock(file, FRAME_BUF[slot][t], FRAME_CAP[slot][t], FRAME_FIELD_COUNT, trkHeader[7], RECORDING_INITIAL_FRAME_CAP, MAX_FRAMES_PER_TRACK);
		nTrack::trkAimCount(slot, t) = Internal_ReadBufferBlock(file, AIM_BUF[slot][t], AIM_CAP[slot][t], AIM_FIELD_COUNT, trkHeader[8], RECORDING_INITIAL_AIM_CAP, MAX_AIM_EVENTS_PER_TRACK);
		nTrack::trkBulletCount(slot, t) = Internal_ReadBufferBlock(file, BULLET_BUF[slot][t], BULLET_CAP[slot][t], BULLET_FIELD_COUNT, trkHeader[9], RECORDING_INITIAL_BULLET_CAP, MAX_BULLET_EVENTS_PER_TRACK);
		nTrack::trkVehFrameCount(slot, t) = Internal_ReadBufferBlock(file, VEH_FRAME_BUF[slot][t], VEH_FRAME_CAP[slot][t], VEH_FRAME_FIELD_COUNT, trkHeader[12], RECORDING_INITIAL_VEH_FRAME_CAP, MAX_VEH_FRAMES_PER_TRACK);
		nTrack::trkWeaponCount(slot, t) = Internal_ReadBufferBlock(file, WEAPON_BUF[slot][t], WEAPON_CAP[slot][t], WEAPON_FIELD_COUNT, trkHeader[13], RECORDING_INITIAL_WEAPON_CAP, MAX_WEAPON_EVENTS_PER_TRACK);
		nTrack::trkTrailerCount(slot, t) = Internal_ReadBufferBlock(file, TRAILER_BUF[slot][t], TRAILER_CAP[slot][t], TRAILER_FIELD_COUNT, trkHeader[14], RECORDING_INITIAL_TRAILER_CAP, MAX_TRAILER_EVENTS_PER_TRACK);

		nTrack::trkPlaySpawned(slot, t) = false;
		nTrack::trkPlayGhost(slot, t) = INVALID_GHOST_ID;
		nTrack::trkPlayVehicle(slot, t) = INVALID_GHOST_VEHICLE_ID;
		nTrack::trkPlayFrameIdx(slot, t) = 0;
		nTrack::trkPlayAimIdx(slot, t) = 0;
		nTrack::trkPlayBulletIdx(slot, t) = 0;
		nTrack::trkPlayVehFrameIdx(slot, t) = 0;
		nTrack::trkPlayWeaponIdx(slot, t) = 0;
		nTrack::trkPlayTrailerIdx(slot, t) = 0;
	}

	nRec::recTrackCount(slot) = loadTrackCount;

	fclose(file);
	return Recording:slot;
}

/**
 * [--- REPRODUCCION POR RANGO / SUB-PLAYBACK ---]
 */

// ============================================================================
// REPRODUCCION POR RANGO / SUB-PLAYBACK
// ============================================================================
//  Reproduccion de un SUB-RANGO de una Recording, scopeada a un UNICO
//  jugador (via Ghost_Create/.../ownerid), sin interferir con la captura
//  en curso: solo LEE frames ya escritos en los buffers de la Recording
//  (no los modifica), asi que funciona aunque la Recording siga capturando.
//
//  Incluye TODOS los tracks (jugadores Y vehiculos) que se solapan con la
//  ventana de tiempo pedida (por ejemplo en repeticiones de muerte o cinematicas),
//  no solo el foco - asi se mantiene el entorno completo (ej. otros tiradores
//  o vehiculos en escena).
//
//  Es un sistema PARALELO e independiente de Recording_Play/StopPlayback:
//  no comparte estado ni timer con la reproduccion de round completo.
//
//  FUNCIONES API NATIVAS:
//  - Recording_PlayRange(rec, tStart, tEnd, targetplayer)   -> SubPlayback:id
//  - Recording_PlayRecent(rec, targetplayer, msBefore=5000) -> SubPlayback:id
//  - Recording_StopRange(SubPlayback:sp)                    -> bool
//  - Recording_GetRangeGhost(SubPlayback:sp, realplayerid)  -> Ghost:id
//
//  CONFIGURACION (define antes del #include, junto con las demas):
//    #define MAX_SUBPLAYBACKS       (20)  // Sesiones simultaneas (default: 20)
//    #define MAX_SUBPLAYBACK_TRACKS (20)  // Entidades por sesion (default: 20)
// ============================================================================

#if !defined MAX_SUBPLAYBACKS
	#define MAX_SUBPLAYBACKS            (20)
#endif
#if !defined MAX_SUBPLAYBACK_TRACKS
	#define MAX_SUBPLAYBACK_TRACKS      (20)
#endif

#define INVALID_SUBPLAYBACK_ID (SubPlayback:-1)

// ----------------------------------------------------------------------
//  Datos internos
// ----------------------------------------------------------------------

// Una sesion de reproduccion de rango: que Recording, que ventana de tiempo [spAbsStart,
// spAbsEnd] (ms, relativos al inicio de la Recording original) y para que
// jugador (spViewer) se esta reproduciendo.
enum e_SUBPLAYBACK_DATA
{
	bool:spActive,
	spRecSlot,               // slot (_:Recording) de la Recording fuente
	spViewer,                // playerid que ve esta sesion (scope del ghost)
	spAbsStart,              // ms, inicio de la ventana dentro de la Recording
	spAbsEnd,                // ms, fin de la ventana dentro de la Recording
	spPlayStartTick,         // GetTickCount() de arranque de ESTA sesion
	spSubTrackCount          // cuantos tracks de la Recording entraron en la ventana
};
static SUBPLAYBACK_DATA[MAX_SUBPLAYBACKS][e_SUBPLAYBACK_DATA];

// Un "sub-track" = un track de la Recording original que se solapa con la
// ventana pedida. Cada uno tiene su propio ghost/vehiculo fantasma y sus
// propios indices de reproduccion (independientes de Recording_Play).
enum e_SUB_TRACK
{
	spTrackRef,              // indice del track dentro de la Recording original
	bool:spSpawned,
	Ghost:spGhost,
	GhostVehicle:spVehicle,
	spFrameIdx,
	spAimIdx,
	spBulletIdx,
	spVehFrameIdx
};
static SUB_TRACK_DATA[MAX_SUBPLAYBACKS][MAX_SUBPLAYBACK_TRACKS][e_SUB_TRACK];

#define nSP::%0(%1)      SUBPLAYBACK_DATA[%1][%0]
#define nSPT::%0(%1,%2)  SUB_TRACK_DATA[%1][%2][%0]

static bool:g_SubPlaybackTimerRunning = false;
static g_SubPlaybackTimerId = -1;

static stock Internal_FindFreeSPSlot()
{
	for(new i = 0; i < MAX_SUBPLAYBACKS; i++)
	{
		if(!nSP::spActive(i)) return i;
	}
	return -1;
}

// Traduce un indice de track de la Recording ORIGINAL (frVehTrackRef) al
// indice de sub-track DENTRO de esta sesion (spTrackRef).
static stock Internal_FindSPSubByTrackRef(spslot, trackRef)
{
	for(new s = 0, n = nSP::spSubTrackCount(spslot); s < n; s++)
	{
		if(nSPT::spTrackRef(spslot, s) == trackRef) return s;
	}
	return -1;
}

static stock bool:Internal_IsStaleTrackInWindow(recslot, trackindex, tStart, tEnd, dur)
{
	new myType = nTrack::trkType(recslot, trackindex);
	new myRealId = nTrack::trkRealId(recslot, trackindex);
	new myStart = nTrack::trkStart(recslot, trackindex);

	new count = nRec::recTrackCount(recslot);
	for(new t2 = 0; t2 < count; t2++)
	{
		if(t2 == trackindex) continue;
		if(nTrack::trkType(recslot, t2) != myType) continue;
		if(nTrack::trkRealId(recslot, t2) != myRealId) continue;

		new tEndTrack2 = nTrack::trkEnd(recslot, t2);
		if(tEndTrack2 == -1) tEndTrack2 = dur;
		if(tEndTrack2 < tStart || nTrack::trkStart(recslot, t2) > tEnd) continue;

		if(nTrack::trkStart(recslot, t2) > myStart) return true;
	}
	return false;
}

/*
	Recording_PlayRange
	Reproduce el tramo [tStart, tEnd] (ms, relativos al inicio de la
	Recording) SOLO para targetplayer (ghosts privados via .ownerid).
	Incluye todo track (jugador o vehiculo) cuya ventana de vida se
	solape con el rango pedido - asi se ve el entorno completo.

	No requiere que la Recording este detenida: puede seguir capturando
	en paralelo (solo se leen frames ya escritos, nunca se modifican).

	Parametros:
		Recording:rec  - Recording fuente (puede seguir capturando)
		tStart, tEnd   - Ventana en ms, relativa al inicio de la Recording
		targetplayer   - Unico jugador que vera esta reproduccion

	Retorna:
		SubPlayback:id de la sesion creada
		INVALID_SUBPLAYBACK_ID si la Recording es invalida, no hay slots
		libres, el jugador no esta conectado, o la ventana es invalida
*/
stock SubPlayback:Recording_PlayRange(Recording:rec, tStart, tEnd, targetplayer)
{
	if(!Recording_IsValid(rec)) return INVALID_SUBPLAYBACK_ID;
	if(!IsPlayerConnected(targetplayer)) return INVALID_SUBPLAYBACK_ID;

	new recslot = _:rec;
	new spslot = Internal_FindFreeSPSlot();
	if(spslot == -1) return INVALID_SUBPLAYBACK_ID;

	if(tStart < 0) tStart = 0;
	new dur = Recording_GetDuration(rec);
	if(tEnd > dur) tEnd = dur;
	if(tEnd <= tStart) return INVALID_SUBPLAYBACK_ID;

	nSP::spActive(spslot) = true;
	nSP::spRecSlot(spslot) = recslot;
	nSP::spViewer(spslot) = targetplayer;
	nSP::spAbsStart(spslot) = tStart;
	nSP::spAbsEnd(spslot) = tEnd;
	nSP::spPlayStartTick(spslot) = GetTickCount();
	nSP::spSubTrackCount(spslot) = 0;

	new count = nRec::recTrackCount(recslot);
	for(new t = 0; t < count && nSP::spSubTrackCount(spslot) < MAX_SUBPLAYBACK_TRACKS; t++)
	{
		new tStartTrack = nTrack::trkStart(recslot, t);
		new tEndTrack = nTrack::trkEnd(recslot, t);
		if(tEndTrack == -1) tEndTrack = dur;

		if(tEndTrack < tStart || tStartTrack > tEnd) continue;
		if(Internal_IsStaleTrackInWindow(recslot, t, tStart, tEnd, dur)) continue;

		new sub = nSP::spSubTrackCount(spslot);
		nSPT::spTrackRef(spslot, sub) = t;
		nSPT::spSpawned(spslot, sub) = false;
		nSPT::spGhost(spslot, sub) = INVALID_GHOST_ID;
		nSPT::spVehicle(spslot, sub) = INVALID_GHOST_VEHICLE_ID;

		new wantElapsed = tStart - tStartTrack;

		new frameCount = nTrack::trkFrameCount(recslot, t);
		new fi = 0;
		while(fi < frameCount && nFrame::frTick(recslot, t, fi) < wantElapsed) fi++;
		nSPT::spFrameIdx(spslot, sub) = fi;

		new aimCount = nTrack::trkAimCount(recslot, t);
		new ai = 0;
		while(ai < aimCount && nAimEv::aimTick(recslot, t, ai) < wantElapsed) ai++;
		nSPT::spAimIdx(spslot, sub) = ai;

		new bulletCount = nTrack::trkBulletCount(recslot, t);
		new bi = 0;
		while(bi < bulletCount && nBulletEv::bulTick(recslot, t, bi) < wantElapsed) bi++;
		nSPT::spBulletIdx(spslot, sub) = bi;

		new vehFrameCount = nTrack::trkVehFrameCount(recslot, t);
		new vfi = 0;
		while(vfi < vehFrameCount && nVehFrame::vfTick(recslot, t, vfi) < wantElapsed) vfi++;
		nSPT::spVehFrameIdx(spslot, sub) = vfi;

		nSP::spSubTrackCount(spslot) = sub + 1;
	}

	if(!g_SubPlaybackTimerRunning)
	{
		g_SubPlaybackTimerId = SetTimer("Internal_SubPlaybackTick", PLAYBACK_TICK_INTERVAL, true);
		g_SubPlaybackTimerRunning = true;
	}
	return SubPlayback:spslot;
}

/*
	Recording_PlayRecent
	Atajo sobre Recording_PlayRange: reproduce los ultimos 'msBefore' ms
	de una Recording que sigue en curso, solo para targetplayer.
*/
stock SubPlayback:Recording_PlayRecent(Recording:rec, targetplayer, msBefore = 5000)
{
	new dur = Recording_GetDuration(rec);
	new tStart = dur - msBefore;
	if(tStart < 0) tStart = 0;
	return Recording_PlayRange(rec, tStart, dur, targetplayer);
}

/*
	Recording_StopRange
	Corta una reproduccion por rango antes de tiempo y destruye todos los
	ghosts que tuviera activos en ese momento. No afecta a la Recording fuente.
*/
stock bool:Recording_StopRange(SubPlayback:sp)
{
	if(_:sp < 0 || _:sp >= MAX_SUBPLAYBACKS) return false;
	if(!nSP::spActive(_:sp)) return false;
	Internal_StopSPBySlot(_:sp);
	return true;
}

static stock Internal_SpawnSPGhost(spslot, sub)
{
	new recslot = nSP::spRecSlot(spslot);
	new t = nSPT::spTrackRef(spslot, sub);
	new viewer = nSP::spViewer(spslot);

	if(nTrack::trkType(recslot, t) == TRACK_PLAYER)
	{
		new Ghost:g = Ghost_Create(
			nTrack::trkSkin(recslot, t),
			nTrack::trkSpawnX(recslot, t), nTrack::trkSpawnY(recslot, t), nTrack::trkSpawnZ(recslot, t),
			-1, "", viewer
		);
		nSPT::spGhost(spslot, sub) = g;
		nSPT::spSpawned(spslot, sub) = Ghost_IsValid(g);
	}
	else // TRACK_VEHICLE
	{
		new GhostVehicle:v = Ghost_CreateVehicle(
			nTrack::trkModel(recslot, t),
			nTrack::trkSpawnX(recslot, t), nTrack::trkSpawnY(recslot, t), nTrack::trkSpawnZ(recslot, t),
			.angle = nTrack::trkSpawnAngle(recslot, t), .ownerid = viewer
		);
		nSPT::spVehicle(spslot, sub) = v;
		nSPT::spSpawned(spslot, sub) = Ghost_IsVehicleValid(v);
	}
}

static stock Internal_DespawnSPGhost(spslot, sub)
{
	new recslot = nSP::spRecSlot(spslot);
	new t = nSPT::spTrackRef(spslot, sub);

	if(nTrack::trkType(recslot, t) == TRACK_PLAYER)
	{
		new Ghost:g = nSPT::spGhost(spslot, sub);
		if(Ghost_IsValid(g)) Ghost_Destroy(g);
		nSPT::spGhost(spslot, sub) = INVALID_GHOST_ID;
	}
	else
	{
		new GhostVehicle:v = nSPT::spVehicle(spslot, sub);
		if(Ghost_IsVehicleValid(v)) Ghost_DestroyVehicle(v);
		nSPT::spVehicle(spslot, sub) = INVALID_GHOST_VEHICLE_ID;
	}
	nSPT::spSpawned(spslot, sub) = false;
}

static stock Internal_ApplySPFrame(spslot, sub, frameidx)
{
	new recslot = nSP::spRecSlot(spslot);
	new t = nSPT::spTrackRef(spslot, sub);
	new Ghost:g = nSPT::spGhost(spslot, sub);
	if(!Ghost_IsValid(g)) return;

	new onFoot[PR_OnFootSync], inCar[PR_InCarSync], tick, vehTrackRef, seat;
	new ftype = Recording_GetFrame(Recording:recslot, t, frameidx, onFoot, inCar, tick, vehTrackRef, seat);

	if(ftype == FRAME_TYPE_FOOT)
	{
		if(Ghost_IsInVehicle(g)) Ghost_ExitVehicle(g);
		Ghost_ApplyOnFoot(g, onFoot);
	}
	else if(ftype == FRAME_TYPE_CAR)
	{
		new vehSub = Internal_FindSPSubByTrackRef(spslot, vehTrackRef);
		if(vehSub != -1)
		{
			new GhostVehicle:v = nSPT::spVehicle(spslot, vehSub);
			if(Ghost_IsVehicleValid(v)) Ghost_EnterVehicle(g, v, seat);
		}
		Ghost_ApplyInCar(g, inCar);
	}
	else if(ftype == FRAME_TYPE_PASSENGER)
	{
		new vehSub = Internal_FindSPSubByTrackRef(spslot, vehTrackRef);
		if(vehSub != -1)
		{
			new GhostVehicle:v = nSPT::spVehicle(spslot, vehSub);
			if(Ghost_IsVehicleValid(v)) Ghost_EnterVehicle(g, v, seat);
		}
		if(Ghost_IsPassenger(g))
		{
			new passenger[PR_PassengerSync];
			passenger[PR_driveBy] = nFrame::frDriveBy(recslot, t, frameidx);
			passenger[PR_weaponId] = inCar[PR_weaponId];
			passenger[PR_additionalKey] = inCar[PR_additionalKey];
			passenger[PR_playerHealth] = inCar[PR_playerHealth];
			passenger[PR_playerArmour] = inCar[PR_armour];
			passenger[PR_lrKey] = inCar[PR_lrKey];
			passenger[PR_udKey] = inCar[PR_udKey];
			passenger[PR_keys] = inCar[PR_keys];
			passenger[PR_position][0] = Float:nFrame::frX(recslot, t, frameidx);
			passenger[PR_position][1] = Float:nFrame::frY(recslot, t, frameidx);
			passenger[PR_position][2] = Float:nFrame::frZ(recslot, t, frameidx);
			Ghost_ApplyPassenger(g, passenger);
		}
	}
}

static stock Internal_StopSPBySlot(spslot)
{
	for(new s = 0, n = nSP::spSubTrackCount(spslot); s < n; s++)
	{
		if(nSPT::spSpawned(spslot, s)) Internal_DespawnSPGhost(spslot, s);
	}
	nSP::spActive(spslot) = false;

	new bool:anySub = false;
	for(new k = 0; k < MAX_SUBPLAYBACKS; k++)
	{
		if(nSP::spActive(k)) { anySub = true; break; }
	}
	if(!anySub && g_SubPlaybackTimerRunning)
	{
		if(g_SubPlaybackTimerId != -1) KillTimer(g_SubPlaybackTimerId);
		g_SubPlaybackTimerId = -1;
		g_SubPlaybackTimerRunning = false;
	}
}

// Obtiene el virtualid del Ghost de jugador mas adecuado en SubPlayback.
static stock Internal_FindActiveSPGhostVid(k)
{
	new recslot = nSP::spRecSlot(k);
	for(new s = 0, n = nSP::spSubTrackCount(k); s < n; s++)
	{
		new t = nSPT::spTrackRef(k, s);
		if(nTrack::trkType(recslot, t) == TRACK_PLAYER && nSPT::spSpawned(k, s))
		{
			new Ghost:g = nSPT::spGhost(k, s);
			if(Ghost_IsValid(g))
			{
				new vid = Ghost_GetVirtualId(g);
				if(vid != INVALID_PLAYER_ID) return vid;
			}
		}
	}
	return INVALID_PLAYER_ID;
}

forward Internal_SubPlaybackTick();
public Internal_SubPlaybackTick()
{
	for(new k = 0; k < MAX_SUBPLAYBACKS; k++)
	{
		if(!nSP::spActive(k)) continue;

		new recslot = nSP::spRecSlot(k);
		new elapsed = nSP::spAbsStart(k) + (GetTickCount() - nSP::spPlayStartTick(k));
		new bool:finished = (elapsed >= nSP::spAbsEnd(k));
		if(finished) elapsed = nSP::spAbsEnd(k);

		for(new s = 0, n = nSP::spSubTrackCount(k); s < n; s++)
		{
			new t = nSPT::spTrackRef(k, s);
			new tStartTrack = nTrack::trkStart(recslot, t);

			if(!nSPT::spSpawned(k, s))
			{
				if(elapsed < tStartTrack) continue;
				Internal_SpawnSPGhost(k, s);
				if(!nSPT::spSpawned(k, s)) continue;
			}

			new trackElapsed = elapsed - tStartTrack;

			new Ghost:g = nSPT::spGhost(k, s);
			if(nTrack::trkType(recslot, t) == TRACK_PLAYER)
			{
				new frameCount = nTrack::trkFrameCount(recslot, t);
				new fi = nSPT::spFrameIdx(k, s);
				new aimCount = (Ghost_IsValid(g)) ? nTrack::trkAimCount(recslot, t) : 0;
				new ai = nSPT::spAimIdx(k, s);
				new bulletCount = (Ghost_IsValid(g)) ? nTrack::trkBulletCount(recslot, t) : 0;
				new bi = nSPT::spBulletIdx(k, s);

				// Despacho en orden cronologico estricto (Aim -> Bullet -> Frame en el mismo tick)
				while((fi < frameCount && nFrame::frTick(recslot, t, fi) <= trackElapsed) ||
				      (ai < aimCount && nAimEv::aimTick(recslot, t, ai) <= trackElapsed) ||
				      (bi < bulletCount && nBulletEv::bulTick(recslot, t, bi) <= trackElapsed))
				{
					new tFrame = 0x7FFFFFFF;
					if(fi < frameCount && nFrame::frTick(recslot, t, fi) <= trackElapsed)
					{
						tFrame = nFrame::frTick(recslot, t, fi);
					}
					new tAim = 0x7FFFFFFF;
					if(ai < aimCount && nAimEv::aimTick(recslot, t, ai) <= trackElapsed)
					{
						tAim = nAimEv::aimTick(recslot, t, ai);
					}
					new tBul = 0x7FFFFFFF;
					if(bi < bulletCount && nBulletEv::bulTick(recslot, t, bi) <= trackElapsed)
					{
						tBul = nBulletEv::bulTick(recslot, t, bi);
					}

					if(tAim <= tFrame && tAim <= tBul)
					{
						new aim[PR_AimSync];
						aim[PR_camMode] = nAimEv::aimCamMode(recslot, t, ai);
						aim[PR_camFrontVec][0] = Float:nAimEv::aimFrontX(recslot, t, ai);
						aim[PR_camFrontVec][1] = Float:nAimEv::aimFrontY(recslot, t, ai);
						aim[PR_camFrontVec][2] = Float:nAimEv::aimFrontZ(recslot, t, ai);
						aim[PR_camPos][0] = Float:nAimEv::aimPosX(recslot, t, ai);
						aim[PR_camPos][1] = Float:nAimEv::aimPosY(recslot, t, ai);
						aim[PR_camPos][2] = Float:nAimEv::aimPosZ(recslot, t, ai);
						aim[PR_aimZ] = Float:nAimEv::aimZ(recslot, t, ai);
						aim[PR_camZoom] = nAimEv::aimCamZoom(recslot, t, ai);
						aim[PR_weaponState] = nAimEv::aimWeaponState(recslot, t, ai);
						aim[PR_aspectRatio] = nAimEv::aimAspectRatio(recslot, t, ai);
						Ghost_ApplyAim(g, aim);
						ai++;
					}
					else if(tBul <= tFrame && tBul <= tAim)
					{
						new bullet[PR_BulletSync];
						bullet[PR_hitType] = nBulletEv::bulHitType(recslot, t, bi);
						bullet[PR_hitId] = nBulletEv::bulHitId(recslot, t, bi);
						bullet[PR_origin][0] = Float:nBulletEv::bulOriginX(recslot, t, bi);
						bullet[PR_origin][1] = Float:nBulletEv::bulOriginY(recslot, t, bi);
						bullet[PR_origin][2] = Float:nBulletEv::bulOriginZ(recslot, t, bi);
						bullet[PR_hitPos][0] = Float:nBulletEv::bulHitX(recslot, t, bi);
						bullet[PR_hitPos][1] = Float:nBulletEv::bulHitY(recslot, t, bi);
						bullet[PR_hitPos][2] = Float:nBulletEv::bulHitZ(recslot, t, bi);
						bullet[PR_offsets][0] = Float:nBulletEv::bulOffX(recslot, t, bi);
						bullet[PR_offsets][1] = Float:nBulletEv::bulOffY(recslot, t, bi);
						bullet[PR_offsets][2] = Float:nBulletEv::bulOffZ(recslot, t, bi);
						bullet[PR_weaponId] = nBulletEv::bulWeapon(recslot, t, bi);
						Ghost_ApplyBullet(g, bullet);
						bi++;
					}
					else
					{
						Internal_ApplySPFrame(k, s, fi);
						fi++;
					}
				}
				nSPT::spFrameIdx(k, s) = fi;
				nSPT::spAimIdx(k, s) = ai;
				nSPT::spBulletIdx(k, s) = bi;
			}
			else
			{
				new frameCount = nTrack::trkFrameCount(recslot, t);
				new fi = nSPT::spFrameIdx(k, s);
				while(fi < frameCount && nFrame::frTick(recslot, t, fi) <= trackElapsed)
				{
					Internal_ApplySPFrame(k, s, fi);
					fi++;
				}
				nSPT::spFrameIdx(k, s) = fi;
			}

			// --- Fisica de vehiculo sin conductor ---
			if(nTrack::trkType(recslot, t) == TRACK_VEHICLE)
			{
				new GhostVehicle:v = nSPT::spVehicle(k, s);
				if(Ghost_IsVehicleValid(v))
				{
					new vehFrameCount = nTrack::trkVehFrameCount(recslot, t);
					new vfi = nSPT::spVehFrameIdx(k, s);
					new Float:vx, Float:vy, Float:vz, Float:vvx, Float:vvy, Float:vvz, Float:vhealth, vtick;
					new Float:vroll[3], Float:vdir[3];
					new Float:vturn[3];
					new bool:hasVehFrame = false;
					new Float:applyX, Float:applyY, Float:applyZ, Float:applyVX, Float:applyVY, Float:applyVZ, Float:applyHealth;
					new Float:applyRoll[3], Float:applyDir[3], Float:applyTurn[3];
					while(vfi < vehFrameCount && Recording_GetVehFrame(Recording:recslot, t, vfi, vx, vy, vz, vvx, vvy, vvz, vhealth, vtick, vroll, vdir, vturn) && vtick <= trackElapsed)
					{
						hasVehFrame = true;
						applyX = vx; applyY = vy; applyZ = vz;
						applyVX = vvx; applyVY = vvy; applyVZ = vvz;
						applyHealth = vhealth;
						applyRoll[0] = vroll[0]; applyRoll[1] = vroll[1]; applyRoll[2] = vroll[2];
						applyDir[0] = vdir[0]; applyDir[1] = vdir[1]; applyDir[2] = vdir[2];
						applyTurn[0] = vturn[0]; applyTurn[1] = vturn[1]; applyTurn[2] = vturn[2];
						vfi++;
					}
					if(hasVehFrame)
					{
						new repId = Internal_FindActiveSPGhostVid(k);
						Ghost_ApplyUnoccupied(v, applyX, applyY, applyZ, applyVX, applyVY, applyVZ, applyHealth, .rollOverride = applyRoll, .directionOverride = applyDir, .reporterVirtualId = repId, .turnOverride = applyTurn);
					}
					nSPT::spVehFrameIdx(k, s) = vfi;
				}
			}

			// Track termino dentro de la ventana -> despawnear su ghost
			new tEndTrack = nTrack::trkEnd(recslot, t);
			if(tEndTrack != -1 && elapsed >= tEndTrack && nSPT::spSpawned(k, s))
			{
				Internal_DespawnSPGhost(k, s);
			}
		}

		if(finished) Internal_StopSPBySlot(k);
	}
	return 1;
}

/*
	Recording_GetRangeGhost
	Devuelve el Ghost: de una entidad dentro de un SubPlayback en curso.
	- Si realplayerid == INVALID_PLAYER_ID, devuelve el ghost del foco original.
	- Si realplayerid != INVALID_PLAYER_ID, busca el ghost cuyo jugador real
	  era realplayerid (por ejemplo, el asesino en un replay de combate).

	Puede devolver INVALID_GHOST_ID si el sub-track todavia no fue spawneado
	(el spawn es progresivo, tick a tick, dentro de Internal_SubPlaybackTick).
*/
stock Ghost:Recording_GetRangeGhost(SubPlayback:sp, realplayerid = INVALID_PLAYER_ID)
{
	if(_:sp < 0 || _:sp >= MAX_SUBPLAYBACKS) return INVALID_GHOST_ID;
	if(!nSP::spActive(_:sp)) return INVALID_GHOST_ID;

	new spslot = _:sp;
	new recslot = nSP::spRecSlot(spslot);

	for(new s = 0, n = nSP::spSubTrackCount(spslot); s < n; s++)
	{
		new t = nSPT::spTrackRef(spslot, s);
		if(nTrack::trkType(recslot, t) == TRACK_PLAYER)
		{
			if(realplayerid == INVALID_PLAYER_ID)
			{
				if(nTrack::trkIsFocus(recslot, t))
				{
					if(!nSPT::spSpawned(spslot, s)) return INVALID_GHOST_ID;
					return nSPT::spGhost(spslot, s);
				}
			}
			else
			{
				if(nTrack::trkRealId(recslot, t) == realplayerid)
				{
					if(!nSPT::spSpawned(spslot, s)) return INVALID_GHOST_ID;
					return nSPT::spGhost(spslot, s);
				}
			}
		}
	}
	return INVALID_GHOST_ID;
}

// # END :D


public Internal_StopSubPlaybacksForRec(recslot)
{
	for(new k = 0; k < MAX_SUBPLAYBACKS; k++)
	{
		if(nSP::spActive(k) && nSP::spRecSlot(k) == recslot)
		{
			Recording_StopRange(SubPlayback:k);
		}
	}
	return 1;
}

//       _____ _      ______          _   _ _    _ _____
//      / ____| |    |  ____|   /\   | \ | | |  | |  __ \
//     | |    | |    | |__     /  \  |  \| | |  | | |__) |
//     | |    | |    |  __|   / /\ \ | . ` | |  | |  ___/
//     | |____| |____| |____ / ____ \| |\  | |__| | |
//      \_____|______|______/_/    \_\_| \_|\____/|_|
//
//

public OnPlayerDisconnect(playerid, reason)
{
	for(new r = 0; r < MAX_RECORDINGS; r++)
	{
		if(nRec::recSlotUsed(r) && nRec::recCapturing(r) && nRec::recFocusPlayer(r) == playerid)
		{
			new Recording:rHandle = Recording:r;
			Recording_Stop(rHandle);
		}
	}

	for(new k = 0; k < MAX_SUBPLAYBACKS; k++)
	{
		if(nSP::spActive(k) && nSP::spViewer(k) == playerid)
		{
			Recording_StopRange(SubPlayback:k);
		}
	}

	#if defined NGR_OnPlayerDisconnect
		return NGR_OnPlayerDisconnect(playerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerDisconnect
	#undef OnPlayerDisconnect
#else
	#define _ALS_OnPlayerDisconnect
#endif
#define OnPlayerDisconnect NGR_OnPlayerDisconnect
#if defined NGR_OnPlayerDisconnect
	forward NGR_OnPlayerDisconnect(playerid, reason);
#endif
