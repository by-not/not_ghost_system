/*
	=====================================================
	NOT_GHOST_NPC.INC - Client-Side Ghost NPCs & Vehicles
	=====================================================
	Author: not
	Version: 1.1
	Date: 2026
	License: MIT
	Credits / Dependencies: urShadow (Pawn.RakNet)

	DESCRIPTION:
	Creates and controls 100% client-side "Ghost" players (Ghost:) and
	vehicles (GhostVehicle:) in SA-MP / open.mp via raw Pawn.RakNet RPCs
	and sync packets -- with ZERO FCNPC dependency and ZERO bot slots used.
	Supports both public ghosts (visible to everyone) and private per-player
	ghosts (ownerid = playerid), automatic resync on connect, raw spectate
	targeting, and client-side real player hiding.

	MAIN FEATURES:
	- Full client-side NPCs & vehicles without using server player slots
	- Public (ownerid = -1) or private per-player visibility (ownerid = playerid)
	- OnFoot, InCar, Passenger, Aim, Bullet, and Unoccupied sync packet support
	- Raw spectate targeting for ghosts and ghost vehicles (ideal for killcams)
	- Client-side real player hiding without changing virtual worlds
	- Automatic virtual ID collision protection on OnPlayerConnect
	- Automatic cleanup of private ghosts/vehicles on OnPlayerDisconnect
	- Companion library: pair with not_ghost_recorder.inc for full scene replays

	BASIC CONFIGURATION:
	```pawn
	#include <a_samp>
	#include <Pawn.RakNet>          // Required

	#define MAX_GHOST_NPC       50  // Max simultaneous ghost players (default: 50)
	#define MAX_GHOST_VEHICLES  50  // Max simultaneous ghost vehicles (default: 50)
	#include <not_ghost_npc>
	```

	USAGE EXAMPLE:
	```pawn
	new Ghost:g = Ghost_Create(280, 1958.0, 1343.0, 15.3, -1, "Ghost_1");
	Ghost_SetHealth(g, 100.0);
	Ghost_ChatBubble(g, "Hello from client-side!", 0xFFFFFFFF, 30.0, 5000);
	Ghost_Destroy(g);
	```

	CALLBACKS:
	- GN_OnGhostKicked(Ghost:ghost, playerid) // Optional: if a real player claims a ghost's virtual ID

	COMPATIBILITY:
	- SA-MP 0.3.7 and open.mp (with Pawn.RakNet)
	- Standard ALS hooks on OnPlayerConnect and OnPlayerDisconnect

	=====================================================
*/

#if defined _not_ghost_npc_included
	#endinput
#endif
#define _not_ghost_npc_included

#include <a_samp>

#tryinclude <Pawn.RakNet>

#if !defined PAWNRAKNET_INC_
	#error "not_ghost_npc.inc requires Pawn.RakNet (Pawn.RakNet.inc not found)"
#endif

#if !defined IsValidVehicle
	native IsValidVehicle(vehicleid);
#endif

// ============================================================================
// Configuracion
// ============================================================================

#if !defined MAX_GHOST_NPC
	#define MAX_GHOST_NPC            (50)
#endif

#if !defined MAX_GHOST_VEHICLES
	#define MAX_GHOST_VEHICLES       (30)
#endif

#if !defined MAX_VEHICLES
	#define MAX_VEHICLES             (2000)
#endif

#if !defined GHOST_TEAM
	#define GHOST_TEAM               (255)
#endif
#if !defined GHOST_FIGHTING_STYLE
	#define GHOST_FIGHTING_STYLE     (4)
#endif
#if !defined GHOST_IS_NPC
	#define GHOST_IS_NPC             (0)
#endif
#if !defined GHOST_QUIT_REASON
	#define GHOST_QUIT_REASON        (1)
#endif
#if !defined GHOST_DEFAULT_COLOR
	#define GHOST_DEFAULT_COLOR      (0xFFFFFFFF)
#endif

#if !defined CHATBUBBLE_COLOR
	#define CHATBUBBLE_COLOR         (0xFFFFFFFF)
#endif
#if !defined CHATBUBBLE_DRAW_DIST
	#define CHATBUBBLE_DRAW_DIST     (30.0)
#endif
#if !defined CHATBUBBLE_EXPIRE_MS
	#define CHATBUBBLE_EXPIRE_MS     (6000)
#endif

#define INVALID_GHOST_ID             (Ghost:-1)
#define INVALID_GHOST_VEHICLE_ID     (GhostVehicle:-1)

// ============================================================================
// IDs de RPC y paquetes (SA-MP 0.3.7)
// ============================================================================

static const RPC_SERVER_JOIN           = 137;
static const RPC_SERVER_QUIT           = 138;
static const RPC_WORLD_PLAYER_ADD      = 32;
static const RPC_WORLD_PLAYER_REMOVE   = 163;
static const RPC_WORLD_VEHICLE_ADD     = 164;
static const RPC_WORLD_VEHICLE_REMOVE  = 165;
static const RPC_PLAYER_ENTER_VEHICLE  = 26;
static const RPC_PLAYER_EXIT_VEHICLE   = 154;
static const RPC_CHAT_BUBBLE           = 59;
static const RPC_TOGGLE_SPECTATING     = 124;
static const RPC_SPECTATE_PLAYER       = 126;
static const RPC_SPECTATE_VEHICLE      = 127;
static const RPC_APPLY_ANIMATION       = 86;
static const RPC_CLEAR_ANIMATIONS      = 87;
static stock const RPC_SET_PLAYER_HEALTH     = 14;
static const RPC_SET_VEHICLE_PARAMSEX  = 24;
static const RPC_SHOW_NAME_TAG         = 80;
static stock const RPC_SET_SPECIAL_ACTION    = 88;
static stock const RPC_SET_FIGHTING_STYLE    = 89;
static stock const RPC_SET_VEHICLE_VELOCITY  = 91;
static const RPC_SET_TIRE_STATUS       = 98;
static const RPC_UPDATE_VEH_DAMAGE     = 106;
static const RPC_SET_NUMBER_PLATE      = 123;
static stock const RPC_SET_VEHICLE_HEALTH    = 147;
static const RPC_ATTACH_TRAILER        = 148;
static const RPC_DETACH_TRAILER        = 149;
static stock const RPC_SET_VEHICLE_POS       = 159;
static stock const RPC_SET_VEHICLE_ZANGLE    = 160;
static const RPC_SET_VEHICLE_PARAMS    = 161;
static const RPC_DISABLE_VEH_COLLISION = 167;
static stock const RPC_SET_PLAYER_ARMOUR     = 66;
static stock const RPC_SET_ARMED_WEAPON      = 67;
static const RPC_LINK_VEHICLE_INTERIOR = 65;
static const RPC_REMOVE_VEHICLE_COMP   = 57;
static const RPC_SET_PLAYER_COLOR      = 72;

// Paquetes crudos (NO RPC) para vehiculos sin conductor y pasajeros
#if !defined UNOCCUPIED_SYNC
	static const UNOCCUPIED_SYNC = 209;
#endif
#if !defined PASSENGER_SYNC_PACKET
	static const PASSENGER_SYNC_PACKET = 211;
#endif

// ============================================================================
// Datos internos
// ============================================================================

enum e_GHOST_DATA
{
	bool:gGhostActive,
	gGhostVirtualId,
	gGhostName[MAX_PLAYER_NAME],
	gGhostSkin,
	gGhostColor,
	GhostVehicle:gGhostVehicle,
	bool:gGhostInVehicle,
	bool:gGhostIsPassenger,
	gGhostSeat,
	gGhostOwner              // PRIVADO: -1 = publico (todos lo ven), sino playerid dueno exclusivo
};
static GHOST_DATA[MAX_GHOST_NPC][e_GHOST_DATA];

enum e_GHOST_VEHICLE_DATA
{
	bool:gVehActive,
	gVehRealId,
	gVehModel,
	gVehColor1, gVehColor2,
	GhostVehicle:gVehAttachedTrailer,
	Float:gVehCacheX, Float:gVehCacheY, Float:gVehCacheZ,
	Float:gVehCacheVX, Float:gVehCacheVY, Float:gVehCacheVZ,
	Float:gVehCacheHealth,
	Float:gVehCacheAngle,
	// Playerid virtual registrado via ServerJoin exclusivamente para poder
	// reportar UNOCCUPIED_SYNC del vehiculo. El cliente descarta el paquete
	// si el id del "reportero" no esta registrado como conectado.
	gVehReporterVirtualId,
	gVehReporterName[MAX_PLAYER_NAME],
	gVehOwner                // PRIVADO: -1 = publico (todos lo ven), sino playerid dueno exclusivo
};
static GHOST_VEHICLE_DATA[MAX_GHOST_VEHICLES][e_GHOST_VEHICLE_DATA];

#define nGhost::%0(%1)     GHOST_DATA[%1][%0]
#define nGhostVeh::%0(%1)  GHOST_VEHICLE_DATA[%1][%0]

// Estado cacheado de sync por ghost. Ghost_SetHealth/Armour/ArmedWeapon/
// Teleport/Velocity modifican el campo correspondiente en esta copia y
// reenvian el paquete entero (los RPC "self" de SA-MP no hacen efecto sobre
// terceros, solo el re-envio de sync funciona en produccion).
static GHOST_ONFOOT_STATE[MAX_GHOST_NPC][PR_OnFootSync];
static GHOST_INCAR_STATE[MAX_GHOST_NPC][PR_InCarSync];
static GHOST_PASSENGER_STATE[MAX_GHOST_NPC][PR_PassengerSync];

// Campos de WorldPlayerAdd que no viajan en ningun paquete de sync:
// facing_angle, fighting_style y skill_levels. Se actualizan con un respawn
// (WorldPlayerRemove + WorldPlayerAdd) cuando cambian.
static Float:GHOST_FACING_ANGLE[MAX_GHOST_NPC];
static GHOST_FIGHTING_STYLE_CACHE[MAX_GHOST_NPC];
static GHOST_SKILL_LEVELS[MAX_GHOST_NPC][11];

static stock Int_CopyOnFoot(dest[PR_OnFootSync], const src[PR_OnFootSync])
{
	for(new PR_OnFootSync:i = PR_OnFootSync:0; i < PR_OnFootSync:sizeof(dest); i++) dest[i] = src[i];
}

static stock Int_CopyInCar(dest[PR_InCarSync], const src[PR_InCarSync])
{
	for(new PR_InCarSync:i = PR_InCarSync:0; i < PR_InCarSync:sizeof(dest); i++) dest[i] = src[i];
}

static stock Int_CopyPassenger(dest[PR_PassengerSync], const src[PR_PassengerSync])
{
	for(new PR_PassengerSync:i = PR_PassengerSync:0; i < PR_PassengerSync:sizeof(dest); i++) dest[i] = src[i];
}

// Nombres roleplay por defecto para autogenerar nick de ghosts. Se pueden
// reemplazar definiendo tus propios arrays con estos mismos nombres ANTES del
// #include si tu servidor exige otra convencion.
#if !defined GHOST_RPNAME_FIRSTNAMES
	static const g_GhostRPFirstNames[][] = {
		"Alex", "Carlos", "Daniel", "Eduardo", "Fernando", "Gabriel", "Hugo",
		"Ivan", "Javier", "Kevin", "Luis", "Marco", "Nestor", "Oscar", "Pablo",
		"Raul", "Sergio", "Tomas", "Victor", "Walter"
	};
	static const g_GhostRPLastNames[][] = {
		"Nava", "Rojas", "Mendez", "Torres", "Ramirez", "Castillo", "Flores",
		"Herrera", "Cruz", "Vega", "Salazar", "Reyes", "Guerrero", "Campos",
		"Aguilar", "Medina", "Ortiz", "Silva", "Pena", "Cordero"
	};
#endif

//      _    _ ______ _      _____  ______ _____   _____
//     | |  | |  ____| |    |  __ \|  ____|  __ \ / ____|
//     | |__| | |__  | |    | |__) | |__  | |__) | (___
//     |  __  |  __| | |    |  ___/|  __| |  _  / \___ \
//     | |  | | |____| |____| |    | |____| | \ \ ____) |
//     |_|  |_|______|______|_|    |______|_|  \_\_____/
//
//

static stock Int_FindFreeGhostSlot()
{
	for(new i = 0; i < MAX_GHOST_NPC; i++)
	{
		if(!nGhost::gGhostActive(i)) return i;
	}
	return -1;
}

static stock Int_FindFreeGhostVehSlot()
{
	for(new i = 0; i < MAX_GHOST_VEHICLES; i++)
	{
		if(!nGhostVeh::gVehActive(i)) return i;
	}
	return -1;
}

// Los ids virtuales (playerids falsos) son compartidos entre ghosts y
// reporteros de vehiculos, por lo que se chequea colision contra ambas listas.
static stock Int_FindFreeVirtualId()
{
	new maxp = GetMaxPlayers();
	for(new i = maxp - 1; i >= 0; i--)
	{
		if(IsPlayerConnected(i)) continue;

		new bool:taken = false;
		for(new s = 0; s < MAX_GHOST_NPC; s++)
		{
			if(nGhost::gGhostActive(s) && nGhost::gGhostVirtualId(s) == i) { taken = true; break; }
		}
		if(!taken)
		{
			for(new s = 0; s < MAX_GHOST_VEHICLES; s++)
			{
				if(nGhostVeh::gVehActive(s) && nGhostVeh::gVehReporterVirtualId(s) == i) { taken = true; break; }
			}
		}
		if(!taken) return i;
	}
	return -1;
}

static stock Int_FindFreeVehicleRealId()
{
	for(new i = MAX_VEHICLES - 1; i >= 1; i--)
	{
		if(IsValidVehicle(i)) continue;

		new bool:taken = false;
		for(new s = 0; s < MAX_GHOST_VEHICLES; s++)
		{
			if(nGhostVeh::gVehActive(s) && nGhostVeh::gVehRealId(s) == i) { taken = true; break; }
		}
		if(!taken) return i;
	}
	return -1;
}

static stock bool:Int_IsGhostNameTaken(const name[])
{
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(!IsPlayerConnected(i)) continue;

		new pname[MAX_PLAYER_NAME];
		GetPlayerName(i, pname, sizeof pname);
		if(!strcmp(pname, name, false)) return true;
	}
	return false;
}

// ============================================================================
// Envio de RPC / paquetes (uso interno)
// ============================================================================

// SCOPING / PRIVACIDAD: resuelve a quien se manda un paquete. Si se paso un
// target explicito (!= -1) ese gana; si no, cae al dueno del ghost/vehiculo
// (que puede ser -1 = broadcast a todos los conectados).
static stock Int_Target(explicitTarget, owner)
{
	return (explicitTarget != -1) ? explicitTarget : owner;
}

static stock Int_BroadcastRPC(BitStream:bs, rpcid, targetplayer = -1)
{
	if(targetplayer != -1)
	{
		if(IsPlayerConnected(targetplayer)) PR_SendRPC(bs, targetplayer, rpcid, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
		return;
	}
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(!IsPlayerConnected(i)) continue;
		PR_SendRPC(bs, i, rpcid, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
	}
}

static stock Int_BroadcastPacket(BitStream:bs, targetplayer = -1)
{
	if(targetplayer != -1)
	{
		if(IsPlayerConnected(targetplayer)) PR_SendPacket(bs, targetplayer, PR_HIGH_PRIORITY, PR_UNRELIABLE_SEQUENCED);
		return;
	}
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(!IsPlayerConnected(i)) continue;
		PR_SendPacket(bs, i, PR_HIGH_PRIORITY, PR_UNRELIABLE_SEQUENCED);
	}
}

static stock Int_SendServerJoin(virtualid, const name[], targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, virtualid,
		PR_UINT32, 0,
		PR_UINT8,  GHOST_IS_NPC,
		PR_UINT8,  strlen(name),
		PR_STRING, name
	);
	Int_BroadcastRPC(bs, RPC_SERVER_JOIN, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendServerQuit(virtualid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT8, GHOST_QUIT_REASON);
	Int_BroadcastRPC(bs, RPC_SERVER_QUIT, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendWorldPlayerAdd(virtualid, skinid, Float:x, Float:y, Float:z, Float:facingAngle, color, fightingStyle, const skill[11], targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, virtualid,
		PR_UINT8,  GHOST_TEAM,
		PR_UINT32, skinid,
		PR_FLOAT,  x,
		PR_FLOAT,  y,
		PR_FLOAT,  z,
		PR_FLOAT,  facingAngle,
		PR_UINT32, color,
		PR_UINT8,  fightingStyle,
		PR_UINT16, skill[0], PR_UINT16, skill[1], PR_UINT16, skill[2], PR_UINT16, skill[3],
		PR_UINT16, skill[4], PR_UINT16, skill[5], PR_UINT16, skill[6], PR_UINT16, skill[7],
		PR_UINT16, skill[8], PR_UINT16, skill[9], PR_UINT16, skill[10]
	);
	Int_BroadcastRPC(bs, RPC_WORLD_PLAYER_ADD, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendWorldPlayerRemove(virtualid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid);
	Int_BroadcastRPC(bs, RPC_WORLD_PLAYER_REMOVE, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendWorldVehicleAdd(vehicleid, modelid, Float:x, Float:y, Float:z, Float:health, color1, color2, Float:angle = 0.0, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, vehicleid,
		PR_UINT32, modelid,
		PR_FLOAT,  x,
		PR_FLOAT,  y,
		PR_FLOAT,  z,
		PR_FLOAT,  angle,
		PR_UINT8,  0,
		PR_UINT8,  0,
		PR_FLOAT,  health,
		PR_UINT8,  0,
		PR_UINT32, 0,
		PR_UINT32, 0,
		PR_UINT8,  0,
		PR_UINT8,  0,
		PR_UINT8,  0,
		PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0,
		PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0, PR_UINT8, 0,
		PR_UINT8,  3,
		PR_UINT32, color1,
		PR_UINT32, color2
	);
	Int_BroadcastRPC(bs, RPC_WORLD_VEHICLE_ADD, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendWorldVehicleRemove(vehicleid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid);
	Int_BroadcastRPC(bs, RPC_WORLD_VEHICLE_REMOVE, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendPlayerEnterVehicle(virtualid, vehicleid, bool:ispassenger = false, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT16, vehicleid, PR_UINT8, _:ispassenger);
	Int_BroadcastRPC(bs, RPC_PLAYER_ENTER_VEHICLE, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendPlayerExitVehicle(virtualid, vehicleid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT16, vehicleid);
	Int_BroadcastRPC(bs, RPC_PLAYER_EXIT_VEHICLE, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendApplyAnimation(virtualid, const lib[], const name[], Float:delta, bool:loop, bool:lockx, bool:locky, bool:freeze, time)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, virtualid,
		PR_UINT8,  strlen(lib),
		PR_STRING, lib,
		PR_UINT8,  strlen(name),
		PR_STRING, name,
		PR_FLOAT,  delta,
		PR_UINT8,  _:loop,
		PR_UINT8,  _:lockx,
		PR_UINT8,  _:locky,
		PR_UINT8,  _:freeze,
		PR_UINT32, time
	);
	Int_BroadcastRPC(bs, RPC_APPLY_ANIMATION);
	BS_Delete(bs);
}

static stock Int_SendClearAnimations(virtualid)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT8, 0);
	Int_BroadcastRPC(bs, RPC_CLEAR_ANIMATIONS);
	BS_Delete(bs);
}



static stock Int_SendDeathBroadcast(killerVirtualId, victimVirtualId, weaponid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, killerVirtualId, PR_UINT16, victimVirtualId, PR_UINT8, weaponid);
	Int_BroadcastRPC(bs, 55, targetplayer); // RPC_DeathMessage (55)
	BS_Delete(bs);
}

static stock Int_SendSetPlayerColor(virtualid, color, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT32, color);
	Int_BroadcastRPC(bs, RPC_SET_PLAYER_COLOR, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendShowNameTag(virtualid, bool:show, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, virtualid, PR_UINT8, _:show);
	Int_BroadcastRPC(bs, RPC_SHOW_NAME_TAG, targetplayer);
	BS_Delete(bs);
}

// --- Vehiculos (target = vehicleid, no virtualid) ---

// (Vehicle transform synced via packets)

static stock Int_SendSetVehicleParams(vehicleid, objective, doorslocked)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT8, objective, PR_UINT8, doorslocked);
	Int_BroadcastRPC(bs, RPC_SET_VEHICLE_PARAMS);
	BS_Delete(bs);
}

static stock Int_SendSetVehicleParamsEx(vehicleid, engine, lights, alarm, doors, bonnet, boot, objective)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, vehicleid,
		PR_UINT8, engine, PR_UINT8, lights, PR_UINT8, alarm, PR_UINT8, doors,
		PR_UINT8, bonnet, PR_UINT8, boot, PR_UINT8, objective
	);
	Int_BroadcastRPC(bs, RPC_SET_VEHICLE_PARAMSEX);
	BS_Delete(bs);
}

static stock Int_SendRemoveVehicleComponent(vehicleid, componentid)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT16, componentid);
	Int_BroadcastRPC(bs, RPC_REMOVE_VEHICLE_COMP);
	BS_Delete(bs);
}

static stock Int_SendUpdateVehicleDamage(vehicleid, panels, doors, lights, tires)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT32, panels, PR_UINT32, doors, PR_UINT8, lights, PR_UINT8, tires);
	Int_BroadcastRPC(bs, RPC_UPDATE_VEH_DAMAGE);
	BS_Delete(bs);
}

static stock Int_SendSetTireStatus(vehicleid, tirestatus)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT32, tirestatus);
	Int_BroadcastRPC(bs, RPC_SET_TIRE_STATUS);
	BS_Delete(bs);
}

static stock Int_SendSetNumberPlate(vehicleid, const plate[])
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT8, strlen(plate), PR_STRING, plate);
	Int_BroadcastRPC(bs, RPC_SET_NUMBER_PLATE);
	BS_Delete(bs);
}

static stock Int_SendDisableVehicleCollision(vehicleid, bool:disable)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT8, _:disable);
	Int_BroadcastRPC(bs, RPC_DISABLE_VEH_COLLISION);
	BS_Delete(bs);
}

static stock Int_SendLinkVehicleToInterior(vehicleid, interiorid)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT8, interiorid);
	Int_BroadcastRPC(bs, RPC_LINK_VEHICLE_INTERIOR);
	BS_Delete(bs);
}

static stock Int_SendAttachTrailer(vehicleid, trailerid, targetplayer = -1)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid, PR_UINT16, trailerid);
	Int_BroadcastRPC(bs, RPC_ATTACH_TRAILER, targetplayer);
	BS_Delete(bs);
}

static stock Int_SendDetachTrailer(vehicleid)
{
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, vehicleid);
	Int_BroadcastRPC(bs, RPC_DETACH_TRAILER);
	BS_Delete(bs);
}

//       _____ _    _  ____   _____ _______
//      / ____| |  | |/ __ \ / ____|__   __|
//     | |  __| |__| | |  | | (___    | |
//     | | |_ |  __  | |  | |\___ \   | |
//     | |__| | |  | | |__| |____) |  | |
//      \_____|_|  |_|\____/|_____/   |_|
//
//

/*
	Ghost_Create
	Crea un jugador fantasma (client-side) en la posicion indicada.

	Parametros:
		skinid - Skin del ghost
		Float:x, Float:y, Float:z - Posicion inicial
		color - Color del nombre/blip (-1 = blanco por defecto)
		name[] - Nombre del ghost. Si se deja vacio, se genera un
		         nombre roleplay aleatorio (Nombre_Apellido)

	Retorna:
		Ghost:id del ghost creado
		INVALID_GHOST_ID si no hay slots o playerids libres

	SCOPING / PRIVACIDAD:
		ownerid - Si != -1, el ghost solo se crea/sincroniza para ese
		          playerid (ghost privado, ej. repeticion individual).
		          Si == -1 (default), es publico y lo ve todo el server.

	Ejemplo:
		new Ghost:g = Ghost_Create(280, 1958.33, 1343.12, 15.36);
		new Ghost:priv = Ghost_Create(280, 1958.33, 1343.12, 15.36, .ownerid = playerid);
*/
stock Ghost:Ghost_Create(skinid, Float:x, Float:y, Float:z, color = -1, const name[] = "", ownerid = -1)
{
	new slot = Int_FindFreeGhostSlot();
	if(slot == -1) return INVALID_GHOST_ID;

	new virtualid = Int_FindFreeVirtualId();
	if(virtualid == -1) return INVALID_GHOST_ID;

	if(color == -1) color = GHOST_DEFAULT_COLOR;

	if(name[0] == EOS)
	{
		Ghost_GenerateRPName(nGhost::gGhostName(slot), MAX_PLAYER_NAME);
	}
	else
	{
		strmid(nGhost::gGhostName(slot), name, 0, MAX_PLAYER_NAME - 1);
	}

	nGhost::gGhostActive(slot) = true;
	nGhost::gGhostVirtualId(slot) = virtualid;
	nGhost::gGhostSkin(slot) = skinid;
	nGhost::gGhostColor(slot) = color;
	nGhost::gGhostVehicle(slot) = INVALID_GHOST_VEHICLE_ID;
	nGhost::gGhostInVehicle(slot) = false;
	nGhost::gGhostOwner(slot) = ownerid;

	GHOST_ONFOOT_STATE[slot][PR_position][0] = x;
	GHOST_ONFOOT_STATE[slot][PR_position][1] = y;
	GHOST_ONFOOT_STATE[slot][PR_position][2] = z;
	GHOST_ONFOOT_STATE[slot][PR_velocity][0] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_velocity][1] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_velocity][2] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][0] = 1.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][1] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][2] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][3] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_health] = 100;
	GHOST_ONFOOT_STATE[slot][PR_armour] = 0;
	GHOST_ONFOOT_STATE[slot][PR_weaponId] = 0;
	GHOST_ONFOOT_STATE[slot][PR_specialAction] = 0;
	GHOST_ONFOOT_STATE[slot][PR_keys] = 0;
	GHOST_ONFOOT_STATE[slot][PR_lrKey] = 0;
	GHOST_ONFOOT_STATE[slot][PR_udKey] = 0;

	GHOST_FACING_ANGLE[slot] = 0.0;
	GHOST_FIGHTING_STYLE_CACHE[slot] = GHOST_FIGHTING_STYLE;
	for(new i = 0; i < 11; i++) GHOST_SKILL_LEVELS[slot][i] = 0;

	Int_SendServerJoin(virtualid, nGhost::gGhostName(slot), ownerid);
	Int_SendWorldPlayerAdd(virtualid, skinid, x, y, z, GHOST_FACING_ANGLE[slot], color, GHOST_FIGHTING_STYLE_CACHE[slot], GHOST_SKILL_LEVELS[slot], ownerid);

	return Ghost:slot;
}

/*
	Ghost_IsValid
	Verifica si un ghost existe y esta activo.
*/
stock bool:Ghost_IsValid(Ghost:ghost)
{
	if(_:ghost < 0 || _:ghost >= MAX_GHOST_NPC) return false;
	return nGhost::gGhostActive(_:ghost);
}

/*
	Ghost_GetVirtualId
	Devuelve el playerid virtual (falso) que el ghost ocupa en el cliente.
	Util para armar RPCs/paquetes propios o para comparar contra killerid
	en callbacks que reportan un playerid.
*/
stock Ghost_GetVirtualId(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return -1;
	return nGhost::gGhostVirtualId(_:ghost);
}

/*
	Ghost_GetSkin
	Devuelve el skin con el que se creo el ghost.
*/
stock Ghost_GetSkin(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return -1;
	return nGhost::gGhostSkin(_:ghost);
}

/*
	Ghost_GetName
	Copia el nombre actual del ghost (el dado en Ghost_Create o el ultimo
	puesto con Ghost_Rename) al buffer name[].
*/
stock Ghost_GetName(Ghost:ghost, name[], size = MAX_PLAYER_NAME)
{
	if(!Ghost_IsValid(ghost)) { name[0] = EOS; return false; }
	strmid(name, nGhost::gGhostName(_:ghost), 0, (size - 1), size);
	return true;
}

/*
	Ghost_Destroy
	Destruye un ghost previamente creado. Si estaba subido a un vehiculo
	fantasma, lo baja antes. No destruye el vehiculo en si.

	Parametros:
		&Ghost:ghost - Ghost a destruir (se invalida por referencia)

	Retorna:
		true si se destruyo correctamente
		false si el ghost ya era invalido

	Ejemplo:
		Ghost_Destroy(g);
*/
stock Ghost_Destroy(&Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return false;

	new slot = _:ghost;
	new owner = nGhost::gGhostOwner(slot);          // SCOPING: mismo scope al destruir

	if(nGhost::gGhostInVehicle(slot))
	{
		Int_SendPlayerExitVehicle(nGhost::gGhostVirtualId(slot), Ghost_GetVehicleRealId(nGhost::gGhostVehicle(slot)), owner);
	}

	Int_SendWorldPlayerRemove(nGhost::gGhostVirtualId(slot), owner);
	Int_SendServerQuit(nGhost::gGhostVirtualId(slot), owner);

	nGhost::gGhostActive(slot) = false;
	nGhost::gGhostVirtualId(slot) = -1;
	nGhost::gGhostName(slot)[0] = EOS;
	nGhost::gGhostVehicle(slot) = INVALID_GHOST_VEHICLE_ID;
	nGhost::gGhostInVehicle(slot) = false;
	nGhost::gGhostIsPassenger(slot) = false;
	nGhost::gGhostSeat(slot) = 0;
	nGhost::gGhostOwner(slot) = -1;

	ghost = INVALID_GHOST_ID;
	return true;
}

/*
	Ghost_ApplyOnFoot
	Aplica un estado "a pie" al ghost enviando el paquete OnFootSync
	crudo a todos los clientes. Vos llenas el struct con los valores
	que quieras reproducir.

	Parametros:
		Ghost:ghost - Ghost destino
		onFoot[PR_OnFootSync] - Struct de Pawn.RakNet ya llenado
		targetplayer - Si != -1, envia solo a ese jugador

	Retorna:
		true si se envio, false si el ghost es invalido

	Ejemplo:
		new onFoot[PR_OnFootSync];
		onFoot[PR_position] = Float:{1958.0, 1343.0, 15.36};
		onFoot[PR_health] = 100;
		Ghost_ApplyOnFoot(g, onFoot);
*/
stock Ghost_ApplyOnFoot(Ghost:ghost, onFoot[PR_OnFootSync], targetplayer = -1)
{
	if(!Ghost_IsValid(ghost)) return false;

	Int_CopyOnFoot(GHOST_ONFOOT_STATE[_:ghost], onFoot);

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, 207, PR_UINT16, nGhost::gGhostVirtualId(_:ghost));
	BS_WriteOnFootSync(bs, onFoot, true);
	Int_BroadcastPacket(bs, Int_Target(targetplayer, nGhost::gGhostOwner(_:ghost)));   // SCOPING de privacidad
	BS_Delete(bs);
	return true;
}

/*
	Ghost_ApplyAim
	Aplica un estado de camara/apuntado (AimSync) al ghost.
	SCOPING: respeta el scoping del ghost (Ghost_Create .ownerid).
*/
stock Ghost_ApplyAim(Ghost:ghost, aim[PR_AimSync])
{
	if(!Ghost_IsValid(ghost)) return false;

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, 203, PR_UINT16, nGhost::gGhostVirtualId(_:ghost));
	BS_WriteAimSync(bs, aim);
	Int_BroadcastPacket(bs, nGhost::gGhostOwner(_:ghost));
	BS_Delete(bs);
	return true;
}

/*
	Ghost_ApplyBullet
	Aplica un evento de disparo (BulletSync) al ghost.
	SCOPING: respeta el scoping del ghost (Ghost_Create .ownerid).
*/
stock Ghost_ApplyBullet(Ghost:ghost, bullet[PR_BulletSync])
{
	if(!Ghost_IsValid(ghost)) return false;

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, 206, PR_UINT16, nGhost::gGhostVirtualId(_:ghost));
	BS_WriteBulletSync(bs, bullet);
	Int_BroadcastPacket(bs, nGhost::gGhostOwner(_:ghost));
	BS_Delete(bs);
	return true;
}

/*
	Ghost_ChatBubble
	Muestra un globo de chat flotando sobre el ghost, igual que el
	que se ve cuando un jugador real escribe en el chat.

	Parametros:
		Ghost:ghost - Ghost destino
		text[] - Texto a mostrar

	Ejemplo:
		Ghost_ChatBubble(g, "Hola, soy un fantasma");
*/
stock Ghost_ChatBubble(Ghost:ghost, const text[])
{
	if(!Ghost_IsValid(ghost)) return false;

	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, nGhost::gGhostVirtualId(_:ghost),
		PR_UINT32, CHATBUBBLE_COLOR,
		PR_FLOAT,  CHATBUBBLE_DRAW_DIST,
		PR_UINT32, CHATBUBBLE_EXPIRE_MS,
		PR_UINT8,  strlen(text),
		PR_STRING, text
	);
	Int_BroadcastRPC(bs, RPC_CHAT_BUBBLE, nGhost::gGhostOwner(_:ghost));   // SCOPING de privacidad
	BS_Delete(bs);
	return true;
}

//       _____ _____  ______ _____ _______    _______ _____ _   _  _____   _______       _____
//      / ____|  __ \|  ____/ ____|__   __|/\|__   __|_   _| \ | |/ ____| |__   __|/\   / ____|
//     | (___ | |__) | |__ | |       | |  /  \  | |    | | |  \| | |  __     | |  /  \ | |  __
//      \___ \|  ___/|  __|| |       | | / /\ \ | |    | | | . ` | | |_ |    | | / /\ \| | |_ |
//      ____) | |    | |___| |____   | |/ ____ \| |   _| |_| |\  | |__| |    | |/ ____ \ |__| |
//     |_____/|_|    |______\_____|  |_/_/    \_\_|  |_____|_| \_|\_____|    |_/_/    \_\_____|
//
//

/*
	Ghost_SpectateStart
	Activa el modo spectate (RPC 124, crudo) en un jugador REAL. A
	diferencia de la native TogglePlayerSpectating, el server nunca marca
	internamente al jugador como "spectating" - por eso Ghost_SpectateStop
	tampoco dispara ningun respawn automatico al apagarlo. Pensado
	especificamente para spectear ghosts, que la native no puede targetear
	(el server no los considera "conectados de verdad").

	Uso tipico:
		Ghost_SpectateStart(playerid);
		Ghost_SpectatePlayer(playerid, ghost);
		// ... tiempo despues ...
		Ghost_SpectateStop(playerid);
		SetPlayerPos(playerid, x, y, z, ...); // reposicionar a mano, no hay respawn automatico
*/
stock Ghost_SpectateStart(playerid)
{
	if(!IsPlayerConnected(playerid)) return false;
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT32, 1);
	PR_SendRPC(bs, playerid, RPC_TOGGLE_SPECTATING, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
	BS_Delete(bs);
	return true;
}

/*
	Ghost_SpectateStop
	Desactiva el modo activado con Ghost_SpectateStart. No reposiciona al
	jugador ni le devuelve nada - eso queda a cargo de quien llame.
*/
stock Ghost_SpectateStop(playerid)
{
	if(!IsPlayerConnected(playerid)) return false;
	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT32, 0);
	PR_SendRPC(bs, playerid, RPC_TOGGLE_SPECTATING, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
	BS_Delete(bs);
	return true;
}

/*
	Ghost_SpectatePlayer
	Apunta la camara de un jugador (ya en modo spectate via
	Ghost_SpectateStart) a un Ghost puntual.

	camtype: 1 = NORMAL (seguimiento suave, recomendado a pie),
	         2 = FIXED (camara fija), 3 = SIDE (lateral, tipica de vehiculos)
*/
stock Ghost_SpectatePlayer(playerid, Ghost:ghost, camtype = 1)
{
	if(!IsPlayerConnected(playerid)) return false;
	if(!Ghost_IsValid(ghost)) return false;

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, nGhost::gGhostVirtualId(_:ghost), PR_UINT8, camtype);
	PR_SendRPC(bs, playerid, RPC_SPECTATE_PLAYER, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
	BS_Delete(bs);
	return true;
}

/*
	Ghost_SpectateVehicle
	Analogo a Ghost_SpectatePlayer pero apuntando a un vehiculo fantasma.
	camtype default 3 (SIDE), la recomendada para vehiculos.
*/
forward bool:Ghost_IsVehicleValid(GhostVehicle:vehicle);

stock Ghost_SpectateVehicle(playerid, GhostVehicle:vehicle, camtype = 3)
{
	if(!IsPlayerConnected(playerid)) return false;
	if(!Ghost_IsVehicleValid(vehicle)) return false;

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT16, nGhostVeh::gVehRealId(_:vehicle), PR_UINT8, camtype);
	PR_SendRPC(bs, playerid, RPC_SPECTATE_VEHICLE, PR_HIGH_PRIORITY, PR_RELIABLE_ORDERED);
	BS_Delete(bs);
	return true;
}

//      ______ _   _ _______ _____ _____          _____    _______       _____
//     |  ____| \ | |__   __|_   _|  __ \   /\   |  __ \  |__   __|/\   / ____|
//     | |__  |  \| |  | |    | | | |  | | /  \  | |  | |    | |  /  \ | |  __
//     |  __| | . ` |  | |    | | | |  | |/ /\ \ | |  | |    | | / /\ \| | |_ |
//     | |____| |\  |  | |   _| |_| |__| / ____ \| |__| |    | |/ ____ \ |__| |
//     |______|_| \_|  |_|  |_____|_____/_/    \_\_____/     |_/_/    \_\_____|
//
//

/*
	Ghost_SetAnimation
	Aplica una animacion por nombre de libreria/animacion al ghost.
	Nota: usa el RPC de animacion (86) que puede no hacer efecto sobre
	terceros en todos los builds. Para reproduccion grabada, preferir
	Ghost_SetAnimationIndex (via sync, mas confiable).
*/
stock Ghost_SetAnimation(Ghost:ghost, const lib[], const name[], Float:delta, bool:loop, bool:lockx, bool:locky, bool:freeze, time)
{
	if(!Ghost_IsValid(ghost)) return false;
	Int_SendApplyAnimation(nGhost::gGhostVirtualId(_:ghost), lib, name, delta, loop, lockx, locky, freeze, time);
	return true;
}

/*
	Ghost_SetAnimationIndex
	Aplica una animacion por indice numerico (capturado de OnFootSync)
	al ghost. Usa el mecanismo de re-envio de sync, que es el mismo
	confirmado funcional para Ghost_SetHealth/Teleport. Solo aplica
	cuando el ghost esta a pie.
*/
stock Ghost_SetAnimationIndex(Ghost:ghost, animationId, animationFlags = 0)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostIsPassenger(slot) || nGhost::gGhostInVehicle(slot)) return false;
	GHOST_ONFOOT_STATE[slot][PR_animationId] = animationId;
	GHOST_ONFOOT_STATE[slot][PR_animationFlags] = animationFlags;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

stock Ghost_ClearAnimation(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	Int_SendClearAnimations(nGhost::gGhostVirtualId(slot));
	if(!nGhost::gGhostIsPassenger(slot) && !nGhost::gGhostInVehicle(slot))
		Ghost_SetAnimationIndex(ghost, 0, 0);
	return true;
}

/*
	Ghost_BroadcastDeath
	Muestra en la lista de muertes (esquina superior derecha) que el
	ghost murio. No mata ni destruye el ghost.

	Parametros:
		killerVirtualId - virtualid de otro ghost, o INVALID_PLAYER_ID si no aplica
		weaponid - arma que aparece en el anuncio de muerte
*/
stock Ghost_BroadcastDeath(Ghost:ghost, killerVirtualId = INVALID_PLAYER_ID, weaponid = 0)
{
	if(!Ghost_IsValid(ghost)) return false;
	Int_SendDeathBroadcast(killerVirtualId, nGhost::gGhostVirtualId(_:ghost), weaponid, nGhost::gGhostOwner(_:ghost));
	return true;
}

/*
	Ghost_Rename
	Cambia el nombre visible del ghost. Como RPC_SET_PLAYER_NAME no se
	propaga a terceros en SA-MP 0.3.7, se actualiza el cache y se
	realiza un respawn controlado (WorldPlayerRemove + WorldPlayerAdd)
	con el nombre nuevo, igual que Ghost_SetFightingStyle.
*/
stock Ghost_Rename(Ghost:ghost, const name[])
{
	if(!Ghost_IsValid(ghost)) return false;
	strmid(nGhost::gGhostName(_:ghost), name, 0, MAX_PLAYER_NAME - 1);
	new owner = nGhost::gGhostOwner(_:ghost);
	Int_SendWorldPlayerRemove(nGhost::gGhostVirtualId(_:ghost), owner);
	Int_SendServerQuit(nGhost::gGhostVirtualId(_:ghost), owner);
	Int_SendServerJoin(nGhost::gGhostVirtualId(_:ghost), nGhost::gGhostName(_:ghost), owner);
	Internal_GhostRespawn(_:ghost);
	return true;
}

/*
	Ghost_SetHealth / Ghost_SetArmour / Ghost_SetArmedWeapon
	Modifican el campo correspondiente del ultimo sync conocido del ghost
	y reenvian el paquete ENTERO. Distinguen tres estados: a pie,
	conductor y pasajero, usando el paquete correcto para cada caso.
*/
stock Ghost_SetHealth(Ghost:ghost, Float:health)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostIsPassenger(slot))
	{
		GHOST_PASSENGER_STATE[slot][PR_playerHealth] = floatround(health);
		return Ghost_ApplyPassenger(ghost, GHOST_PASSENGER_STATE[slot]);
	}
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_playerHealth] = floatround(health);
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_health] = floatround(health);
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

stock Ghost_SetArmour(Ghost:ghost, Float:armour)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostIsPassenger(slot))
	{
		GHOST_PASSENGER_STATE[slot][PR_playerArmour] = floatround(armour);
		return Ghost_ApplyPassenger(ghost, GHOST_PASSENGER_STATE[slot]);
	}
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_armour] = floatround(armour);
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_armour] = floatround(armour);
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

stock Ghost_SetArmedWeapon(Ghost:ghost, weaponid)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostIsPassenger(slot))
	{
		GHOST_PASSENGER_STATE[slot][PR_weaponId] = weaponid;
		return Ghost_ApplyPassenger(ghost, GHOST_PASSENGER_STATE[slot]);
	}
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_weaponId] = weaponid;
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_weaponId] = weaponid;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

/*
	Ghost_SetSpecialAction
	Via sync (campo specialAction de OnFootSync). Solo aplica a pie.
*/
stock Ghost_SetSpecialAction(Ghost:ghost, action)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostInVehicle(slot)) return false;
	GHOST_ONFOOT_STATE[slot][PR_specialAction] = action;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

// facing_angle, fighting_style y skill_levels no viajan en ningun sync -
// solo en WorldPlayerAdd. Se actualizan haciendo un "respawn" del ghost
// (WorldPlayerRemove + WorldPlayerAdd con los valores nuevos).
static stock Internal_GhostRespawn(slot)
{
	new virtualid = nGhost::gGhostVirtualId(slot);

	Int_SendWorldPlayerRemove(virtualid);
	Int_SendWorldPlayerAdd(
		virtualid, nGhost::gGhostSkin(slot),
		GHOST_ONFOOT_STATE[slot][PR_position][0], GHOST_ONFOOT_STATE[slot][PR_position][1], GHOST_ONFOOT_STATE[slot][PR_position][2],
		GHOST_FACING_ANGLE[slot], nGhost::gGhostColor(slot), GHOST_FIGHTING_STYLE_CACHE[slot], GHOST_SKILL_LEVELS[slot]
	);

	if(nGhost::gGhostIsPassenger(slot))
	{
		Int_SendPlayerEnterVehicle(virtualid, nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(slot)), true);
		Ghost_ApplyPassenger(Ghost:slot, GHOST_PASSENGER_STATE[slot]);
	}
	else if(nGhost::gGhostInVehicle(slot))
	{
		Int_SendPlayerEnterVehicle(virtualid, nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(slot)), false);
		Ghost_ApplyInCar(Ghost:slot, GHOST_INCAR_STATE[slot]);
	}
	else
	{
		Ghost_ApplyOnFoot(Ghost:slot, GHOST_ONFOOT_STATE[slot]);
	}
}

stock Ghost_SetFightingStyle(Ghost:ghost, style)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	GHOST_FIGHTING_STYLE_CACHE[slot] = style;
	Internal_GhostRespawn(slot);
	return true;
}

stock Ghost_SetSkillLevel(Ghost:ghost, skillid, level)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(skillid < 0 || skillid > 10) return false;
	new slot = _:ghost;
	GHOST_SKILL_LEVELS[slot][skillid] = level;
	Internal_GhostRespawn(slot);
	return true;
}

/*
	Ghost_SetColor
	Cambia el color de nombre/blip del ghost (RPC_SET_PLAYER_COLOR).
*/
stock Ghost_SetColor(Ghost:ghost, color)
{
	if(!Ghost_IsValid(ghost)) return false;
	nGhost::gGhostColor(_:ghost) = color;
	Int_SendSetPlayerColor(nGhost::gGhostVirtualId(_:ghost), color, nGhost::gGhostOwner(_:ghost));
	return true;
}

/*
	Ghost_ShowNameTag
	Muestra u oculta el nametag 3D sobre el ghost (RPC_SHOW_NAME_TAG).
*/
stock Ghost_ShowNameTag(Ghost:ghost, bool:show = true)
{
	if(!Ghost_IsValid(ghost)) return false;
	Int_SendShowNameTag(nGhost::gGhostVirtualId(_:ghost), show, nGhost::gGhostOwner(_:ghost));
	return true;
}

/*
	Ghost_Teleport / Ghost_SetFacingAngle / Ghost_SetVelocity
	Modifican posicion/velocidad/angulo via re-envio de sync (los RPCs
	de SetPlayerPos/SetPlayerFacingAngle/SetPlayerVelocity no hacen efecto
	sobre terceros). Si el ghost esta en vehiculo, mueven el vehiculo entero.

	Ghost_SetFacingAngle: construye el quaternion de rotacion en Z a partir
	del angulo usando seno/coseno de la mitad del angulo (angulo en grados).
*/
stock Ghost_Teleport(Ghost:ghost, Float:x, Float:y, Float:z)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_position][0] = x;
		GHOST_INCAR_STATE[slot][PR_position][1] = y;
		GHOST_INCAR_STATE[slot][PR_position][2] = z;
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_position][0] = x;
	GHOST_ONFOOT_STATE[slot][PR_position][1] = y;
	GHOST_ONFOOT_STATE[slot][PR_position][2] = z;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

stock Ghost_SetFacingAngle(Ghost:ghost, Float:angle)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	GHOST_FACING_ANGLE[slot] = angle;
	new Float:half = angle * 0.5;
	new Float:qw = floatcos(half, degrees);
	new Float:qz = floatsin(half, degrees);
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_quaternion][0] = qw;
		GHOST_INCAR_STATE[slot][PR_quaternion][1] = 0.0;
		GHOST_INCAR_STATE[slot][PR_quaternion][2] = 0.0;
		GHOST_INCAR_STATE[slot][PR_quaternion][3] = qz;
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_quaternion][0] = qw;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][1] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][2] = 0.0;
	GHOST_ONFOOT_STATE[slot][PR_quaternion][3] = qz;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

stock Ghost_SetVelocity(Ghost:ghost, Float:x, Float:y, Float:z)
{
	if(!Ghost_IsValid(ghost)) return false;
	new slot = _:ghost;
	if(nGhost::gGhostInVehicle(slot))
	{
		GHOST_INCAR_STATE[slot][PR_velocity][0] = x;
		GHOST_INCAR_STATE[slot][PR_velocity][1] = y;
		GHOST_INCAR_STATE[slot][PR_velocity][2] = z;
		return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[slot]);
	}
	GHOST_ONFOOT_STATE[slot][PR_velocity][0] = x;
	GHOST_ONFOOT_STATE[slot][PR_velocity][1] = y;
	GHOST_ONFOOT_STATE[slot][PR_velocity][2] = z;
	return Ghost_ApplyOnFoot(ghost, GHOST_ONFOOT_STATE[slot]);
}

/*
	Ghost_PutInVehicle / Ghost_RemoveFromVehicle
	Wrappers de Ghost_EnterVehicle y Ghost_ExitVehicle que ademas
	envian el paquete de sync correspondiente para actualizar el
	estado en todos los clientes.

	Parametros:
		seatid - 0 = conductor, 1 = adelante, 2/3 = atras
*/
stock Ghost_PutInVehicle(Ghost:ghost, GhostVehicle:vehicle, seatid = 0)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(!Ghost_IsVehicleValid(vehicle)) return false;

	// Exit current vehicle first, bajarlo primero
	if(nGhost::gGhostInVehicle(_:ghost)) Ghost_ExitVehicle(ghost);

	if(!Ghost_EnterVehicle(ghost, vehicle, seatid)) return false;

	// Forzar sync inmediato para que el cambio se refleje en clientes
	if(seatid != 0)
		return Ghost_ApplyPassenger(ghost, GHOST_PASSENGER_STATE[_:ghost]);
	return Ghost_ApplyInCar(ghost, GHOST_INCAR_STATE[_:ghost]);
}

stock Ghost_RemoveFromVehicle(Ghost:ghost)
{
	return Ghost_ExitVehicle(ghost);
}

//     __      ________ _    _ _____ _____ _    _ _      ____    _______       _____
//     \ \    / /  ____| |  | |_   _/ ____| |  | | |    / __ \  |__   __|/\   / ____|
//      \ \  / /| |__  | |__| | | || |    | |  | | |   | |  | |    | |  /  \ | |  __
//       \ \/ / |  __| |  __  | | || |    | |  | | |   | |  | |    | | / /\ \| | |_ |
//        \  /  | |____| |  | |_| || |____| |__| | |___| |__| |    | |/ ____ \ |__| |
//         \/   |______|_|  |_|_____\_____|\____/|______\____/     |_/_/    \_\_____|
//
//

/*
	Ghost_IsVehicleValid
	Valida si el vehiculo fantasma es valido.

	Parametros:
		GhostVehicle:vehicle - Vehiculo fantasma a validar

	Retorna:
		true si el vehiculo fantasma es valido, false en caso contrario
*/
stock bool:Ghost_IsVehicleValid(GhostVehicle:vehicle)
{
	if(_:vehicle < 0 || _:vehicle >= MAX_GHOST_VEHICLES) return false;
	return nGhostVeh::gVehActive(_:vehicle);
}

/*
	Ghost_CreateVehicle
	Crea un vehiculo fantasma (client-side, via WorldVehicleAdd) en
	la posicion indicada. Es independiente de cualquier ghost - se puede
	tener un vehiculo sin conductor, o subir un ghost despues con
	Ghost_EnterVehicle.

	Parametros:
		modelid - Modelo del vehiculo
		Float:x, Float:y, Float:z - Posicion inicial
		Float:health - Vida del vehiculo (por defecto 1000.0)
		color1, color2 - Colores del vehiculo
		Float:angle - Angulo Z inicial (por defecto 0.0)

	Retorna:
		GhostVehicle:id del vehiculo creado
		INVALID_GHOST_VEHICLE_ID si no hay slots o vehicleids libres

	SCOPING / PRIVACIDAD:
		ownerid - Si != -1, el vehiculo fantasma solo se crea/sincroniza
		          para ese playerid (privado, ej. repeticion individual).
		          Si == -1 (default), es publico y lo ve todo el server.

	Ejemplo:
		new GhostVehicle:v = Ghost_CreateVehicle(411, 1960.0, 1345.0, 15.36, .angle = 90.0);
		new GhostVehicle:priv = Ghost_CreateVehicle(411, 1960.0, 1345.0, 15.36, .ownerid = playerid);
*/
stock GhostVehicle:Ghost_CreateVehicle(modelid, Float:x, Float:y, Float:z, Float:health = 1000.0, color1 = 0, color2 = 0, Float:angle = 0.0, ownerid = -1)
{
	new slot = Int_FindFreeGhostVehSlot();
	if(slot == -1) return INVALID_GHOST_VEHICLE_ID;

	new realid = Int_FindFreeVehicleRealId();
	if(realid == -1) return INVALID_GHOST_VEHICLE_ID;

	// Reserva un playerid virtual para reportar UNOCCUPIED_SYNC de este vehiculo.
	// El cliente descarta el paquete si el id del reportero no esta registrado como conectado.
	new reporterid = Int_FindFreeVirtualId();
	if(reporterid == -1) return INVALID_GHOST_VEHICLE_ID;

	nGhostVeh::gVehActive(slot) = true;
	nGhostVeh::gVehRealId(slot) = realid;
	nGhostVeh::gVehModel(slot) = modelid;
	nGhostVeh::gVehColor1(slot) = color1;
	nGhostVeh::gVehColor2(slot) = color2;
	nGhostVeh::gVehAttachedTrailer(slot) = INVALID_GHOST_VEHICLE_ID;
	nGhostVeh::gVehCacheX(slot) = x;
	nGhostVeh::gVehCacheY(slot) = y;
	nGhostVeh::gVehCacheZ(slot) = z;
	nGhostVeh::gVehCacheVX(slot) = 0.0;
	nGhostVeh::gVehCacheVY(slot) = 0.0;
	nGhostVeh::gVehCacheVZ(slot) = 0.0;
	nGhostVeh::gVehCacheHealth(slot) = health;
	nGhostVeh::gVehCacheAngle(slot) = angle;
	nGhostVeh::gVehReporterVirtualId(slot) = reporterid;
	nGhostVeh::gVehOwner(slot) = ownerid;
	Ghost_GenerateRPName(nGhostVeh::gVehReporterName(slot), MAX_PLAYER_NAME);

	Int_SendWorldVehicleAdd(realid, modelid, x, y, z, health, color1, color2, angle, ownerid);
	// Solo ServerJoin (no WorldPlayerAdd): el reportero no necesita skin ni ser
	// visible en el mundo, solo que el cliente lo reconozca como "conectado".
	Int_SendServerJoin(reporterid, nGhostVeh::gVehReporterName(slot), ownerid);

	return GhostVehicle:slot;
}

/*
	Ghost_DestroyVehicle
	Destruye un vehiculo fantasma. Si habia ghosts subidos, los baja
	antes de destruirlo.

	Parametros:
		&GhostVehicle:vehicle - Vehiculo a destruir (se invalida por referencia)

	Ejemplo:
		Ghost_DestroyVehicle(v);
*/
stock Ghost_DestroyVehicle(&GhostVehicle:vehicle)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;

	new slot = _:vehicle;
	new owner = nGhostVeh::gVehOwner(slot);          // SCOPING: mismo scope al destruir

	for(new g = 0; g < MAX_GHOST_NPC; g++)
	{
		if(nGhost::gGhostActive(g) && nGhost::gGhostInVehicle(g) && _:nGhost::gGhostVehicle(g) == slot)
		{
			Int_SendPlayerExitVehicle(nGhost::gGhostVirtualId(g), nGhostVeh::gVehRealId(slot), nGhost::gGhostOwner(g));
			nGhost::gGhostInVehicle(g) = false;
			nGhost::gGhostIsPassenger(g) = false;
			nGhost::gGhostSeat(g) = 0;
			nGhost::gGhostVehicle(g) = INVALID_GHOST_VEHICLE_ID;
		}
	}

	// Si este vehiculo era el trailer de otro, limpia la referencia del cabezal
	for(new v = 0; v < MAX_GHOST_VEHICLES; v++)
	{
		if(nGhostVeh::gVehActive(v) && _:nGhostVeh::gVehAttachedTrailer(v) == slot)
			nGhostVeh::gVehAttachedTrailer(v) = INVALID_GHOST_VEHICLE_ID;
	}

	Int_SendWorldVehicleRemove(nGhostVeh::gVehRealId(slot), owner);
	Int_SendServerQuit(nGhostVeh::gVehReporterVirtualId(slot), owner);

	nGhostVeh::gVehActive(slot) = false;
	nGhostVeh::gVehRealId(slot) = -1;
	nGhostVeh::gVehModel(slot) = 0;
	nGhostVeh::gVehReporterVirtualId(slot) = -1;
	nGhostVeh::gVehOwner(slot) = -1;

	vehicle = INVALID_GHOST_VEHICLE_ID;
	return true;
}

/*
	Ghost_GetVehicleRealId
	Devuelve el vehicleid real (falso, reservado con Int_FindFreeVehicleRealId)
	que usa el vehiculo fantasma en el cliente.
*/
stock Ghost_GetVehicleRealId(GhostVehicle:vehicle)
{
	if(!Ghost_IsVehicleValid(vehicle)) return -1;
	return nGhostVeh::gVehRealId(_:vehicle);
}

/*
	Ghost_ApplyInCar
	Aplica un estado "en vehiculo" (InCarSync) al ghost. El ghost
	debe estar subido al vehiculo (Ghost_EnterVehicle) antes de
	llamar a esto.
*/
stock Ghost_ApplyInCar(Ghost:ghost, inCar[PR_InCarSync], targetplayer = -1)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(!nGhost::gGhostInVehicle(_:ghost)) return false;

	// El vehicleId del struct debe coincidir con el vehiculo real donde esta el ghost.
	inCar[PR_vehicleId] = nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(_:ghost));

	Int_CopyInCar(GHOST_INCAR_STATE[_:ghost], inCar);

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, 200, PR_UINT16, nGhost::gGhostVirtualId(_:ghost));
	BS_WriteInCarSync(bs, inCar, true);
	Int_BroadcastPacket(bs, Int_Target(targetplayer, nGhost::gGhostOwner(_:ghost)));   // SCOPING de privacidad
	BS_Delete(bs);
	return true;
}

/*
	Ghost_ApplyPassenger
	Aplica un estado "de pasajero" (PassengerSync) al ghost. Analogo a
	Ghost_ApplyInCar pero para el paquete PASSENGER_SYNC_PACKET (211).
	El ghost debe haber entrado como pasajero (Ghost_EnterVehicle con
	seatid > 0) antes de llamar a esto.
*/
stock Ghost_ApplyPassenger(Ghost:ghost, passenger[PR_PassengerSync], targetplayer = -1)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(!nGhost::gGhostIsPassenger(_:ghost)) return false;

	passenger[PR_vehicleId] = nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(_:ghost));
	passenger[PR_seatId] = nGhost::gGhostSeat(_:ghost);

	Int_CopyPassenger(GHOST_PASSENGER_STATE[_:ghost], passenger);

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, PASSENGER_SYNC_PACKET, PR_UINT16, nGhost::gGhostVirtualId(_:ghost));
	BS_WritePassengerSync(bs, passenger);
	Int_BroadcastPacket(bs, Int_Target(targetplayer, nGhost::gGhostOwner(_:ghost)));   // SCOPING de privacidad
	BS_Delete(bs);
	return true;
}

/*
	Ghost_EnterVehicle
	Sube (transicion visual) a un ghost a un vehiculo fantasma ya
	creado. Despues de esto, usa Ghost_ApplyInCar (conductor) o
	Ghost_ApplyPassenger (pasajero) para moverlo.

	Parametros:
		seatid - 0 = conductor (default), >0 = pasajero
		         (1 = adelante, 2/3 = atras). Con seatid > 0 el
		         ghost entra como pasajero y usa PassengerSync.

	Ejemplo:
		Ghost_EnterVehicle(g, v);       // conductor
		Ghost_EnterVehicle(g2, v, 1);   // pasajero, asiento delantero
*/
stock Ghost_EnterVehicle(Ghost:ghost, GhostVehicle:vehicle, seatid = 0)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	if(nGhost::gGhostInVehicle(_:ghost)) return false;

	new bool:isPassenger = (seatid != 0);
	Int_SendPlayerEnterVehicle(nGhost::gGhostVirtualId(_:ghost), nGhostVeh::gVehRealId(_:vehicle), isPassenger, nGhost::gGhostOwner(_:ghost));   // SCOPING de privacidad

	nGhost::gGhostVehicle(_:ghost) = vehicle;
	nGhost::gGhostInVehicle(_:ghost) = true;
	nGhost::gGhostIsPassenger(_:ghost) = isPassenger;
	nGhost::gGhostSeat(_:ghost) = seatid;

	new slot = _:ghost;
	if(isPassenger)
	{
		GHOST_PASSENGER_STATE[slot][PR_vehicleId] = nGhostVeh::gVehRealId(_:vehicle);
		GHOST_PASSENGER_STATE[slot][PR_seatId] = seatid;
		GHOST_PASSENGER_STATE[slot][PR_driveBy] = 0;
		GHOST_PASSENGER_STATE[slot][PR_weaponId] = 0;
		GHOST_PASSENGER_STATE[slot][PR_additionalKey] = 0;
		GHOST_PASSENGER_STATE[slot][PR_playerHealth] = 100;
		GHOST_PASSENGER_STATE[slot][PR_playerArmour] = 0;
		GHOST_PASSENGER_STATE[slot][PR_lrKey] = 0;
		GHOST_PASSENGER_STATE[slot][PR_udKey] = 0;
		GHOST_PASSENGER_STATE[slot][PR_keys] = 0;
		GHOST_PASSENGER_STATE[slot][PR_position][0] = 0.0;
		GHOST_PASSENGER_STATE[slot][PR_position][1] = 0.0;
		GHOST_PASSENGER_STATE[slot][PR_position][2] = 0.0;
		return true;
	}

	GHOST_INCAR_STATE[slot][PR_vehicleId] = nGhostVeh::gVehRealId(_:vehicle);
	GHOST_INCAR_STATE[slot][PR_quaternion][0] = 1.0;
	GHOST_INCAR_STATE[slot][PR_quaternion][1] = 0.0;
	GHOST_INCAR_STATE[slot][PR_quaternion][2] = 0.0;
	GHOST_INCAR_STATE[slot][PR_quaternion][3] = 0.0;
	GHOST_INCAR_STATE[slot][PR_velocity][0] = 0.0;
	GHOST_INCAR_STATE[slot][PR_velocity][1] = 0.0;
	GHOST_INCAR_STATE[slot][PR_velocity][2] = 0.0;
	GHOST_INCAR_STATE[slot][PR_playerHealth] = 100;
	GHOST_INCAR_STATE[slot][PR_armour] = 0;
	GHOST_INCAR_STATE[slot][PR_weaponId] = 0;
	GHOST_INCAR_STATE[slot][PR_keys] = 0;
	GHOST_INCAR_STATE[slot][PR_lrKey] = 0;
	GHOST_INCAR_STATE[slot][PR_udKey] = 0;
	GHOST_INCAR_STATE[slot][PR_vehicleHealth] = 1000.0;
	GHOST_INCAR_STATE[slot][PR_sirenState] = 0;

	return true;
}

/*
	Ghost_ExitVehicle
	Baja (transicion visual) a un ghost del vehiculo en el que
	estaba (conductor o pasajero). El vehiculo NO se destruye.
*/
stock Ghost_ExitVehicle(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return false;
	if(!nGhost::gGhostInVehicle(_:ghost)) return false;

	Int_SendPlayerExitVehicle(nGhost::gGhostVirtualId(_:ghost), Ghost_GetVehicleRealId(nGhost::gGhostVehicle(_:ghost)), nGhost::gGhostOwner(_:ghost));   // SCOPING de privacidad

	nGhost::gGhostInVehicle(_:ghost) = false;
	nGhost::gGhostIsPassenger(_:ghost) = false;
	nGhost::gGhostSeat(_:ghost) = 0;
	nGhost::gGhostVehicle(_:ghost) = INVALID_GHOST_VEHICLE_ID;
	return true;
}

stock bool:Ghost_IsInVehicle(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return false;
	return nGhost::gGhostInVehicle(_:ghost);
}

// Distingue conductor de pasajero.
stock bool:Ghost_IsPassenger(Ghost:ghost)
{
	if(!Ghost_IsValid(ghost)) return false;
	return nGhost::gGhostIsPassenger(_:ghost);
}

// Busca si algun ghost es el CONDUCTOR de este vehiculo (no pasajero).
static stock Ghost:Int_FindDriverOfVehicle(GhostVehicle:vehicle)
{
	for(new i = 0; i < MAX_GHOST_NPC; i++)
	{
		if(nGhost::gGhostActive(i) && nGhost::gGhostInVehicle(i) && !nGhost::gGhostIsPassenger(i)
			&& _:nGhost::gGhostVehicle(i) == _:vehicle)
			return Ghost:i;
	}
	return INVALID_GHOST_ID;
}

/*
	Ghost_ApplyUnoccupied
	Aplica fisica de "vehiculo sin conductor" (UNOCCUPIED_SYNC) a un
	GhostVehicle. Permite mover un vehiculo fantasma sin que ningun
	ghost lo este manejando.

	Parametros:
		vehicle - GhostVehicle a mover
		x, y, z - Posicion
		vx, vy, vz - Velocidad (default 0)
		health - Vida del vehiculo (default 1000)
		angle - Angulo Z, calculado automaticamente como roll/direction
		        (default 0). Ignorado si se pasan rollOverride/directionOverride.
		rollOverride, directionOverride - Vectores reales de rotacion del
		        paquete UNOCCUPIED_SYNC. Si directionOverride != {0,0,0},
		        se usan directamente (util para reproduccion grabada).
*/
stock Ghost_ApplyUnoccupied(GhostVehicle:vehicle, Float:x, Float:y, Float:z, Float:vx = 0.0, Float:vy = 0.0, Float:vz = 0.0, Float:health = 1000.0, Float:angle = 0.0, const Float:rollOverride[3] = {0.0, 0.0, 0.0}, const Float:directionOverride[3] = {0.0, 0.0, 0.0}, reporterVirtualId = INVALID_PLAYER_ID, const Float:turnOverride[3] = {0.0, 0.0, 0.0})
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;

	new slot = _:vehicle;
	nGhostVeh::gVehCacheX(slot) = x;
	nGhostVeh::gVehCacheY(slot) = y;
	nGhostVeh::gVehCacheZ(slot) = z;
	nGhostVeh::gVehCacheVX(slot) = vx;
	nGhostVeh::gVehCacheVY(slot) = vy;
	nGhostVeh::gVehCacheVZ(slot) = vz;
	nGhostVeh::gVehCacheHealth(slot) = health;

	new unoccupied[PR_UnoccupiedSync];
	unoccupied[PR_vehicleId] = nGhostVeh::gVehRealId(slot);
	unoccupied[PR_seatId] = 0;
	unoccupied[PR_position][0] = x;
	unoccupied[PR_position][1] = y;
	unoccupied[PR_position][2] = z;
	unoccupied[PR_velocity][0] = vx;
	unoccupied[PR_velocity][1] = vy;
	unoccupied[PR_velocity][2] = vz;
	unoccupied[PR_angularVelocity][0] = turnOverride[0];
	unoccupied[PR_angularVelocity][1] = turnOverride[1];
	unoccupied[PR_angularVelocity][2] = turnOverride[2];
	unoccupied[PR_vehicleHealth] = health;

	// direction {0,0,0} es un vector invalido (nunca es la orientacion real de
	// nada), asi que sirve como marca de "no vino override, calcular desde angle".
	if(directionOverride[0] != 0.0 || directionOverride[1] != 0.0 || directionOverride[2] != 0.0)
	{
		unoccupied[PR_roll][0] = rollOverride[0];
		unoccupied[PR_roll][1] = rollOverride[1];
		unoccupied[PR_roll][2] = rollOverride[2];
		unoccupied[PR_direction][0] = directionOverride[0];
		unoccupied[PR_direction][1] = directionOverride[1];
		unoccupied[PR_direction][2] = directionOverride[2];
		nGhostVeh::gVehCacheAngle(slot) = angle;
	}
	else
	{
		// direction = "adelante", roll = "derecha", calculados desde el angulo Z.
		unoccupied[PR_direction][0] = -floatsin(angle, degrees);
		unoccupied[PR_direction][1] = floatcos(angle, degrees);
		unoccupied[PR_direction][2] = 0.0;
		unoccupied[PR_roll][0] = floatcos(angle, degrees);
		unoccupied[PR_roll][1] = floatsin(angle, degrees);
		unoccupied[PR_roll][2] = 0.0;
		nGhostVeh::gVehCacheAngle(slot) = angle;
	}

	new BitStream:bs = BS_New();
	BS_WriteValue(bs, PR_UINT8, UNOCCUPIED_SYNC);
	// El servidor-cliente UNOCCUPIED_SYNC prepende el playerID del "reportero
	// de fisica" (uint16) antes del struct. El cliente descarta el paquete si
	// ese id no esta registrado como conectado. Si se pasa reporterVirtualId
	// (por ejemplo, el Ghost humano que interactua en la escena), el cliente
	// de SA-MP interpola la fisica suavemente. Si no, usa el reportero virtual.
	new reporter = (reporterVirtualId != INVALID_PLAYER_ID) ? reporterVirtualId : nGhostVeh::gVehReporterVirtualId(slot);
	BS_WriteValue(bs, PR_UINT16, reporter);
	BS_WriteUnoccupiedSync(bs, unoccupied);
	Int_BroadcastPacket(bs, nGhostVeh::gVehOwner(slot));   // SCOPING de privacidad
	BS_Delete(bs);
	return true;
}

/*
	Ghost_SetVehicleHealth / Ghost_SetVehicleVelocity / Ghost_SetVehiclePos /
	Ghost_SetVehicleZAngle
	Modifican propiedades del vehiculo via re-envio de sync. Si el vehiculo
	tiene un ghost conductor, usan InCarSync; si no, usan Ghost_ApplyUnoccupied.
*/
stock Ghost_SetVehicleHealth(GhostVehicle:vehicle, Float:health)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	new Ghost:driver = Int_FindDriverOfVehicle(vehicle);
	if(Ghost_IsValid(driver))
	{
		GHOST_INCAR_STATE[_:driver][PR_vehicleHealth] = health;
		return Ghost_ApplyInCar(driver, GHOST_INCAR_STATE[_:driver]);
	}
	new slot = _:vehicle;
	return Ghost_ApplyUnoccupied(vehicle, nGhostVeh::gVehCacheX(slot), nGhostVeh::gVehCacheY(slot), nGhostVeh::gVehCacheZ(slot),
		nGhostVeh::gVehCacheVX(slot), nGhostVeh::gVehCacheVY(slot), nGhostVeh::gVehCacheVZ(slot), health, nGhostVeh::gVehCacheAngle(slot));
}

stock Ghost_SetVehicleVelocity(GhostVehicle:vehicle, Float:x, Float:y, Float:z)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	new Ghost:driver = Int_FindDriverOfVehicle(vehicle);
	if(Ghost_IsValid(driver))
	{
		GHOST_INCAR_STATE[_:driver][PR_velocity][0] = x;
		GHOST_INCAR_STATE[_:driver][PR_velocity][1] = y;
		GHOST_INCAR_STATE[_:driver][PR_velocity][2] = z;
		return Ghost_ApplyInCar(driver, GHOST_INCAR_STATE[_:driver]);
	}
	new slot = _:vehicle;
	return Ghost_ApplyUnoccupied(vehicle, nGhostVeh::gVehCacheX(slot), nGhostVeh::gVehCacheY(slot), nGhostVeh::gVehCacheZ(slot),
		x, y, z, nGhostVeh::gVehCacheHealth(slot), nGhostVeh::gVehCacheAngle(slot));
}

stock Ghost_SetVehiclePos(GhostVehicle:vehicle, Float:x, Float:y, Float:z)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	new Ghost:driver = Int_FindDriverOfVehicle(vehicle);
	if(Ghost_IsValid(driver))
	{
		GHOST_INCAR_STATE[_:driver][PR_position][0] = x;
		GHOST_INCAR_STATE[_:driver][PR_position][1] = y;
		GHOST_INCAR_STATE[_:driver][PR_position][2] = z;
		return Ghost_ApplyInCar(driver, GHOST_INCAR_STATE[_:driver]);
	}
	new slot = _:vehicle;
	return Ghost_ApplyUnoccupied(vehicle, x, y, z,
		nGhostVeh::gVehCacheVX(slot), nGhostVeh::gVehCacheVY(slot), nGhostVeh::gVehCacheVZ(slot), nGhostVeh::gVehCacheHealth(slot), nGhostVeh::gVehCacheAngle(slot));
}

stock Ghost_SetVehicleZAngle(GhostVehicle:vehicle, Float:angle)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	new Ghost:driver = Int_FindDriverOfVehicle(vehicle);
	if(Ghost_IsValid(driver))
	{
		new Float:half = angle * 0.5;
		GHOST_INCAR_STATE[_:driver][PR_quaternion][0] = floatcos(half, degrees);
		GHOST_INCAR_STATE[_:driver][PR_quaternion][1] = 0.0;
		GHOST_INCAR_STATE[_:driver][PR_quaternion][2] = 0.0;
		GHOST_INCAR_STATE[_:driver][PR_quaternion][3] = floatsin(half, degrees);
		return Ghost_ApplyInCar(driver, GHOST_INCAR_STATE[_:driver]);
	}
	new BitStream:bs = BS_New();
	BS_WriteValue(bs,
		PR_UINT16, nGhostVeh::gVehRealId(_:vehicle),
		PR_FLOAT,  angle
	);
	Int_BroadcastRPC(bs, 160);
	BS_Delete(bs);

	new slot = _:vehicle;
	return Ghost_ApplyUnoccupied(vehicle, nGhostVeh::gVehCacheX(slot), nGhostVeh::gVehCacheY(slot), nGhostVeh::gVehCacheZ(slot),
		nGhostVeh::gVehCacheVX(slot), nGhostVeh::gVehCacheVY(slot), nGhostVeh::gVehCacheVZ(slot), nGhostVeh::gVehCacheHealth(slot), angle);
}

/*
	Ghost_SetVehicleParams / Ghost_SetVehicleParamsEx
	SetVehicleParams: objetivo de mision (radar) + puertas trabadas.
	SetVehicleParamsEx: motor, luces, alarma, puertas, capot, baul,
	objetivo - todo junto, igual que la nativa de a_samp.
*/
stock Ghost_SetVehicleParams(GhostVehicle:vehicle, objective, bool:doorslocked)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendSetVehicleParams(nGhostVeh::gVehRealId(_:vehicle), objective, _:doorslocked);
	return true;
}

stock Ghost_SetVehicleParamsEx(GhostVehicle:vehicle, bool:engine, bool:lights, bool:alarm, bool:doors, bool:bonnet, bool:boot, bool:objective)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendSetVehicleParamsEx(nGhostVeh::gVehRealId(_:vehicle), _:engine, _:lights, _:alarm, _:doors, _:bonnet, _:boot, _:objective);
	return true;
}

/*
	Ghost_RemoveVehicleComponent / Ghost_UpdateVehicleDamage / Ghost_SetTireStatus
	Dano visual y componentes (tuning). UpdateVehicleDamage usa el mismo
	formato de bitfields que GetVehicleDamageStatus de a_samp.
*/
stock Ghost_RemoveVehicleComponent(GhostVehicle:vehicle, componentid)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendRemoveVehicleComponent(nGhostVeh::gVehRealId(_:vehicle), componentid);
	return true;
}

stock Ghost_UpdateVehicleDamage(GhostVehicle:vehicle, panels, doors, lights, tires)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendUpdateVehicleDamage(nGhostVeh::gVehRealId(_:vehicle), panels, doors, lights, tires);
	return true;
}

stock Ghost_SetTireStatus(GhostVehicle:vehicle, tirestatus)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendSetTireStatus(nGhostVeh::gVehRealId(_:vehicle), tirestatus);
	return true;
}

/*
	Ghost_SetNumberPlate / Ghost_DisableVehicleCollisions / Ghost_LinkVehicleToInterior
*/
stock Ghost_SetNumberPlate(GhostVehicle:vehicle, const plate[])
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendSetNumberPlate(nGhostVeh::gVehRealId(_:vehicle), plate);
	return true;
}

stock Ghost_DisableVehicleCollisions(GhostVehicle:vehicle, bool:disable = true)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendDisableVehicleCollision(nGhostVeh::gVehRealId(_:vehicle), disable);
	return true;
}

stock Ghost_LinkVehicleToInterior(GhostVehicle:vehicle, interiorid)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	Int_SendLinkVehicleToInterior(nGhostVeh::gVehRealId(_:vehicle), interiorid);
	return true;
}

/*
	Ghost_AttachTrailer / Ghost_DetachTrailer
	Engancha/desengancha un vehiculo fantasma "trailer" a otro "cabezal".
	Una vez enganchado, el cliente hace que el trailer siga al cabezal
	automaticamente. Para mover el trailer suelto, usar
	Ghost_SetVehiclePos/Velocity sobre el.
*/
stock Ghost_AttachTrailer(GhostVehicle:vehicle, GhostVehicle:trailer)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	if(!Ghost_IsVehicleValid(trailer)) return false;
	nGhostVeh::gVehAttachedTrailer(_:vehicle) = trailer;
	Int_SendAttachTrailer(nGhostVeh::gVehRealId(_:vehicle), nGhostVeh::gVehRealId(_:trailer));
	return true;
}

stock Ghost_DetachTrailer(GhostVehicle:vehicle)
{
	if(!Ghost_IsVehicleValid(vehicle)) return false;
	nGhostVeh::gVehAttachedTrailer(_:vehicle) = INVALID_GHOST_VEHICLE_ID;
	Int_SendDetachTrailer(nGhostVeh::gVehRealId(_:vehicle));
	return true;
}

//      _    _ _______ _____ _       _____   _______       _____
//     | |  | |__   __|_   _| |     / ____| |__   __|/\   / ____|
//     | |  | |  | |    | | | |    | (___      | |  /  \ | |  __
//     | |  | |  | |    | | | |     \___ \     | | / /\ \| | |_ |
//     | |__| |  | |   _| |_| |____ ____) |    | |/ ____ \ |__| |
//      \____/   |_|  |_____|______|_____/     |_/_/    \_\_____|
//
//

/*
	Ghost_GenerateRPName
	Genera un nombre estilo roleplay (Nombre_Apellido) al azar, no
	usado por ningun jugador conectado. Pensado para servidores que
	exigen ese formato de nombre.

	Si tu servidor usa otro formato, define tus propios arrays
	g_GhostRPFirstNames / g_GhostRPLastNames (con GHOST_RPNAME_FIRSTNAMES
	definido) ANTES del #include de esta libreria.
*/
stock Ghost_GenerateRPName(name[], size = MAX_PLAYER_NAME)
{
	name[0] = EOS;
	new attempts = 0;
	do
	{
		format(name, size, "%s_%s",
			g_GhostRPFirstNames[random(sizeof g_GhostRPFirstNames)],
			g_GhostRPLastNames[random(sizeof g_GhostRPLastNames)]
		);
		attempts++;
	}
	while(Int_IsGhostNameTaken(name) && attempts < 20);
	return true;
}

//     __      _______  _____ _____ ____ _____ _      _____ _____          _____    _______       _____
//     \ \    / /_   _|/ ____|_   _|  _ \_   _| |    |_   _|  __ \   /\   |  __ \  |__   __|/\   / ____|
//      \ \  / /  | | | (___   | | | |_) || | | |      | | | |  | | /  \  | |  | |    | |  /  \ | |  __
//       \ \/ /   | |  \___ \  | | |  _ < | | | |      | | | |  | |/ /\ \ | |  | |    | | / /\ \| | |_ |
//        \  /   _| |_ ____) |_| |_| |_) || |_| |____ _| |_| |__| / ____ \| |__| |    | |/ ____ \ |__| |
//         \/   |_____|_____/|_____|____/_____|______|_____|_____/_/    \_\_____/     |_/_/    \_\_____|
//
//

// ============================================================================
// Ocultar/mostrar JUGADORES REALES (no ghosts) - mismo mecanismo RPC crudo
// (WorldPlayerRemove / WorldPlayerAdd) que ya usan los ghosts para
// aparecer/desaparecer, pero apuntado a un jugador real ya conectado.
//
// Es una funcionalidad GENERAL: sirve para
// cualquier caso donde haga falta limpiarle la pantalla a un jugador de
// otros jugadores reales sin pagar el costo de un SetPlayerVirtualWorld
// (que fuerza reconstruir todo el streaming del area para ese jugador).
// El server nunca deja de considerar a nadie streameado/conectado con
// normalidad - esto es 100% client-side, solo cambia lo que VE el viewer.
// ============================================================================

// [viewerid][targetid] = true si viewerid tiene a targetid oculto ahora mismo.
static g_RPlayerHidden[MAX_PLAYERS][MAX_PLAYERS char];

/*
	Ghost_HideRealPlayer
	Oculta al jugador REAL 'targetid' de la pantalla del jugador REAL
	'viewerid' (WorldPlayerRemove, RPC 163, dirigido solo a viewerid).
	No toca virtualworld, no dispara re-stream, no afecta a nadie mas.

	Ejemplo:
		Ghost_HideRealPlayer(viewerid, otherplayerid);
*/
stock bool:Ghost_HideRealPlayer(viewerid, targetid)
{
	if(!IsPlayerConnected(viewerid) || !IsPlayerConnected(targetid)) return false;
	if(viewerid == targetid) return false;
	if(g_RPlayerHidden[viewerid]{targetid}) return true;

	Int_SendWorldPlayerRemove(targetid, viewerid);
	g_RPlayerHidden[viewerid]{targetid} = 1;
	return true;
}

/*
	Ghost_ShowRealPlayer
	Revierte Ghost_HideRealPlayer: le manda a viewerid un WorldPlayerAdd
	(RPC 32) con el skin/posicion/angulo/color/estilo de pelea ACTUALES de
	targetid (leidos con natives reales), para que vuelva a aparecer.

	NOTA: los skill levels (habilidad por arma) no tienen native getter en
	a_samp, se reenvian en 0 - no afecta nada critico del gameplay, solo
	matices de animacion de pelea especificos por arma.
*/
stock bool:Ghost_ShowRealPlayer(viewerid, targetid)
{
	if(!IsPlayerConnected(viewerid) || !IsPlayerConnected(targetid)) return false;
	if(!g_RPlayerHidden[viewerid]{targetid}) return false;

	new Float:x, Float:y, Float:z, Float:angle;
	GetPlayerPos(targetid, x, y, z);
	GetPlayerFacingAngle(targetid, angle);

	new skill[11];
	for(new i = 0; i < 11; i++) skill[i] = 0;

	Int_SendWorldPlayerAdd(targetid, GetPlayerSkin(targetid), x, y, z, angle,
		GetPlayerColor(targetid), GetPlayerFightingStyle(targetid), skill, viewerid);

	g_RPlayerHidden[viewerid]{targetid} = 0;
	return true;
}

/*
	Ghost_IsRealPlayerHidden
	Verifica si targetid esta actualmente oculto en la pantalla de viewerid
	via Ghost_HideRealPlayer.
*/
stock bool:Ghost_IsRealPlayerHidden(viewerid, targetid)
{
	if(!IsPlayerConnected(viewerid) || !IsPlayerConnected(targetid)) return false;
	return (g_RPlayerHidden[viewerid]{targetid} != 0);
}

/*
	Ghost_HideAllRealPlayers / Ghost_ShowAllRealPlayers
	Wrappers sobre TODOS los jugadores conectados (menos 'exclude' en el
	caso de ocultar). Pensado para el arranque/fin de una escena privada
	(ej. una repeticion privada): ocultar el mundo real completo antes de reproducir
	los ghosts, y restaurarlo entero al terminar.

	Retornan la cantidad de jugadores afectados.

	Ejemplo:
		Ghost_HideAllRealPlayers(viewerid);   // arranca la escena privada
		...
		Ghost_ShowAllRealPlayers(viewerid);   // termino, restaurar todo
*/
stock Ghost_HideAllRealPlayers(viewerid, exclude = -1)
{
	if(!IsPlayerConnected(viewerid)) return 0;
	new count = 0;
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(i == viewerid || i == exclude) continue;
		if(!IsPlayerConnected(i)) continue;
		if(Ghost_HideRealPlayer(viewerid, i)) count++;
	}
	return count;
}

stock Ghost_ShowAllRealPlayers(viewerid)
{
	if(!IsPlayerConnected(viewerid)) return 0;
	new count = 0;
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		if(i == viewerid) continue;
		if(!IsPlayerConnected(i)) continue;
		if(g_RPlayerHidden[viewerid]{i})
		{
			Ghost_ShowRealPlayer(viewerid, i);
			count++;
		}
	}
	return count;
}

// ============================================================================
// Resync: sincroniza todos los ghosts/vehiculos a un jugador que se conecta
// despues de que ya existian (no recibio los WorldPlayerAdd/VehicleAdd originales).
// ============================================================================
static stock Internal_ResyncGhostsToPlayer(playerid)
{
	// 1) Vehiculos primero (deben existir antes de que le mandemos ATTACH_TRAILER
	//    o subamos un ghost a ellos)
	for(new v = 0; v < MAX_GHOST_VEHICLES; v++)
	{
		if(!nGhostVeh::gVehActive(v)) continue;
		// SCOPING: si el vehiculo es privado de otro jugador, no se resincroniza
		new vOwner = nGhostVeh::gVehOwner(v);
		if(vOwner != -1 && vOwner != playerid) continue;
		Int_SendWorldVehicleAdd(
			nGhostVeh::gVehRealId(v), nGhostVeh::gVehModel(v),
			nGhostVeh::gVehCacheX(v), nGhostVeh::gVehCacheY(v), nGhostVeh::gVehCacheZ(v),
			nGhostVeh::gVehCacheHealth(v), nGhostVeh::gVehColor1(v), nGhostVeh::gVehColor2(v),
			nGhostVeh::gVehCacheAngle(v), playerid
		);
		Int_SendServerJoin(nGhostVeh::gVehReporterVirtualId(v), nGhostVeh::gVehReporterName(v), playerid);
	}

	// 2) Enganches de trailer (recien ahora que ambos extremos existen para este jugador)
	for(new v = 0; v < MAX_GHOST_VEHICLES; v++)
	{
		if(!nGhostVeh::gVehActive(v)) continue;
		// PRIVACIDAD: mismo filtro que en el paso 1
		new vOwner2 = nGhostVeh::gVehOwner(v);
		if(vOwner2 != -1 && vOwner2 != playerid) continue;
		new GhostVehicle:trailer = nGhostVeh::gVehAttachedTrailer(v);
		if(trailer == INVALID_GHOST_VEHICLE_ID || !Ghost_IsVehicleValid(trailer)) continue;
		Int_SendAttachTrailer(nGhostVeh::gVehRealId(v), nGhostVeh::gVehRealId(_:trailer), playerid);
	}

	// 3) Jugadores fantasma: ServerJoin + WorldPlayerAdd + color + ultimo estado real
	for(new g = 0; g < MAX_GHOST_NPC; g++)
	{
		if(!nGhost::gGhostActive(g)) continue;
		// SCOPING: si el ghost es privado de otro jugador, no se resincroniza
		new gOwner = nGhost::gGhostOwner(g);
		if(gOwner != -1 && gOwner != playerid) continue;

		Int_SendServerJoin(nGhost::gGhostVirtualId(g), nGhost::gGhostName(g), playerid);
		Int_SendWorldPlayerAdd(
			nGhost::gGhostVirtualId(g), nGhost::gGhostSkin(g),
			GHOST_ONFOOT_STATE[g][PR_position][0], GHOST_ONFOOT_STATE[g][PR_position][1], GHOST_ONFOOT_STATE[g][PR_position][2],
			GHOST_FACING_ANGLE[g], nGhost::gGhostColor(g), GHOST_FIGHTING_STYLE_CACHE[g], GHOST_SKILL_LEVELS[g], playerid
		);
		Int_SendSetPlayerColor(nGhost::gGhostVirtualId(g), nGhost::gGhostColor(g), playerid);

		if(nGhost::gGhostIsPassenger(g))
		{
			Int_SendPlayerEnterVehicle(nGhost::gGhostVirtualId(g), nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(g)), true, playerid);
			Ghost_ApplyPassenger(Ghost:g, GHOST_PASSENGER_STATE[g], playerid);
		}
		else if(nGhost::gGhostInVehicle(g))
		{
			Int_SendPlayerEnterVehicle(nGhost::gGhostVirtualId(g), nGhostVeh::gVehRealId(_:nGhost::gGhostVehicle(g)), false, playerid);
			Ghost_ApplyInCar(Ghost:g, GHOST_INCAR_STATE[g], playerid);
		}
		else
		{
			Ghost_ApplyOnFoot(Ghost:g, GHOST_ONFOOT_STATE[g], playerid);
		}
	}
}

// ============================================================================
// Seguridad: liberar el playerid del ghost si un jugador real se conecta
// con ese mismo id (ALS - no pisa tu propio OnPlayerConnect)
// ============================================================================

#if defined GN_OnGhostKicked
	forward GN_OnGhostKicked(Ghost:ghost, playerid);
#endif

public OnPlayerConnect(playerid)
{
	for(new i = 0; i < MAX_GHOST_NPC; i++)
	{
		if(nGhost::gGhostActive(i) && nGhost::gGhostVirtualId(i) == playerid)
		{
			#if defined GN_OnGhostKicked
				GN_OnGhostKicked(Ghost:i, playerid);
			#endif

			new Ghost:g = Ghost:i;
			Ghost_Destroy(g);
		}
	}

	// Release any vehicle reporter virtual ID that collides with the connecting player
	for(new v = 0; v < MAX_GHOST_VEHICLES; v++)
	{
		if(nGhostVeh::gVehActive(v) && nGhostVeh::gVehReporterVirtualId(v) == playerid)
		{
			nGhostVeh::gVehReporterVirtualId(v) = -1;
		}
	}

	// Resync after cleanup: si el id fue robado a un ghost,
	// ese ghost ya no existe y no debe intentar resincronizarse a si mismo.
	Internal_ResyncGhostsToPlayer(playerid);

	#if defined NGN_OnPlayerConnect
		return NGN_OnPlayerConnect(playerid);
	#else
		return true;
	#endif
}
#if defined _ALS_OnPlayerConnect
	#undef OnPlayerConnect
#else
	#define _ALS_OnPlayerConnect
#endif
#define OnPlayerConnect NGN_OnPlayerConnect
#if defined NGN_OnPlayerConnect
	forward NGN_OnPlayerConnect(playerid);
#endif

// ============================================================================
// Seguridad: limpiar el estado de Ghost_HideRealPlayer/Ghost_ShowRealPlayer
// cuando cualquiera de los dos jugadores involucrados se desconecta (ALS -
// no pisa tu propio OnPlayerDisconnect).
// ============================================================================
public OnPlayerDisconnect(playerid, reason)
{
	for(new i = 0; i < MAX_PLAYERS; i++)
	{
		g_RPlayerHidden[playerid]{i} = 0;
		g_RPlayerHidden[i]{playerid} = 0;
	}

	// Destroy private ghosts and vehicles owned by the disconnecting player
	for(new i = 0; i < MAX_GHOST_NPC; i++)
	{
		if(nGhost::gGhostActive(i) && nGhost::gGhostOwner(i) == playerid)
		{
			new Ghost:g = Ghost:i;
			Ghost_Destroy(g);
		}
	}
	for(new v = 0; v < MAX_GHOST_VEHICLES; v++)
	{
		if(nGhostVeh::gVehActive(v) && nGhostVeh::gVehOwner(v) == playerid)
		{
			new GhostVehicle:gv = GhostVehicle:v;
			Ghost_DestroyVehicle(gv);
		}
	}

	#if defined NGN_OnPlayerDisconnect
		return NGN_OnPlayerDisconnect(playerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerDisconnect
	#undef OnPlayerDisconnect
#else
	#define _ALS_OnPlayerDisconnect
#endif
#define OnPlayerDisconnect NGN_OnPlayerDisconnect
#if defined NGN_OnPlayerDisconnect
	forward NGN_OnPlayerDisconnect(playerid, reason);
#endif

// # END :D
