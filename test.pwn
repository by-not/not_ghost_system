#include <a_samp>
#include <Pawn.RakNet>
#include <YSI_Coding/y_malloc>
#include "not_ghost_npc.inc"
#include "not_ghost_recorder.inc"

main() {}

public OnPlayerSpawn(playerid)
{
	new Ghost:g = Ghost_Create(280, 0.0, 0.0, 3.0, -1, "Ghost_Test", playerid);
	if(Ghost_IsValid(g))
	{
		Ghost_SetHealth(g, 100.0);
		Ghost_Rename(g, "Ghost_Renamed");
		Ghost_BroadcastDeath(g, INVALID_PLAYER_ID, 24);
		Ghost_SetColor(g, 0xFF0000FF);
		Ghost_ShowNameTag(g, true);
		Ghost_SpectateStart(playerid);
		Ghost_SpectatePlayer(playerid, g);
		Ghost_SpectateStop(playerid);
		Ghost_Destroy(g);
	}

	new GhostVehicle:gv = Ghost_CreateVehicle(411, 5.0, 5.0, 3.0, 0.0, 1, 1, .ownerid = playerid);
	if(Ghost_IsVehicleValid(gv))
	{
		Ghost_SpectateVehicle(playerid, gv);
		Ghost_DestroyVehicle(gv);
	}

	new Recording:rec = Recording_Start(playerid);
	if(Recording_IsValid(rec))
	{
		new SubPlayback:sp = Recording_PlayRecent(rec, playerid, 3000);
		Recording_StopRange(sp);
		Recording_Stop(rec);
		Recording_Play(rec);
		Recording_StopPlayback(rec);
		Recording_Destroy(rec);
	}
	return 1;
}
