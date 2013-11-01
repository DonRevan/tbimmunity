#include <sourcemod>
#include <tbimmunity>

#define PLUGIN_VERSION "1.0"

public Plugin:myinfo = 
{
	name = "Team Balance Immunity",
	author = "DonRevan",
	description = "Prevents certain players from being balanced.",
	version = PLUGIN_VERSION
};

public OnPluginStart()
{
	RegServerCmd("sm_balance", Command_Balance);
}

public Action:OnBalanceCheck(client)
{
	if(CheckCommandAccess(client, "tbimmunity", ADMFLAG_CUSTOM1, true))
	{
		return Plugin_Handled;
	}
	return Plugin_Continue;
}

public Action:Command_Balance(args)
{
	PrintToServer("Re-balancing teams...");
	CSS_BalanceTeams();
	return Plugin_Continue;
}