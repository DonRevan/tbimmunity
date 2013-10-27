#if defined _tbimmunity_included
 #endinput
#endif
#define _tbimmunity_included

 /**
 * @brief Called when the game wants to know if a player can be balanced.
 *
 * @param client	Index of the client.
 * @return 			Anything but Plugin_Continue to tell the game to not balance the player.
 */
forward Action:OnBalanceCheck(client);

/**
 * @brief Asks the game to re-balance the teams.
 *
 */
native CSS_BalanceTeams();

public Extension:__ext_tbimmunity = 
{
	name = "Team Balance Immunity",
	file = "tbimmunity.ext",
#if defined NO_AUTOLOAD
	autoload = 0,
#else
	autoload = 1,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public __ext_tbimmunity_SetNTVOptional()
{
	MarkNativeAsOptional("CSS_BalanceTeams");
}
#endif
