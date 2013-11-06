/**
 * vim: set ts=4 :
 * =============================================================================
 * Team Balance Immunity
 * Copyright (C) 2013 DonRevan.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"

/* We've to use the old JIT because the new masm doesn't support custom output positions(or I overlooked it). */
#include <jit/jit_helpers.h>
#include <jit/x86/x86_macros.h>

TBExtension g_Extension;
IGameConfig *g_pGameConf = NULL;
IForward *g_pForward = NULL;
ISDKTools *g_pSDKTools = NULL;

// Cached CGameRules::BalanceTeams() address.
static void *g_pBalanceTeamAddr;
static tPlayerByIndex PlayerByIndex;

cell_t NCSS_BalanceTeams(IPluginContext *pContext, const cell_t *params);
const sp_nativeinfo_t TBNatives[] = 
{
	{"CSS_BalanceTeams",	NCSS_BalanceTeams},
	{NULL,					NULL},
};

SMEXT_LINK(&g_Extension);

// native CSS_BalanceTeams()
cell_t NCSS_BalanceTeams(IPluginContext *pContext, const cell_t *params)
{
	CGameRules *gamerules;

	if(!g_pSDKTools)
	{
		return pContext->ThrowNativeError("Failed to lookup ISDKTools interface.");
	}
	if(g_pSDKTools && g_pSDKTools->GetInterfaceVersion() < 2)
	{
		return pContext->ThrowNativeError("SDKTools is outdated. BalanceTeams is disabled.");
	}

	 gamerules = (CGameRules *)g_pSDKTools->GetGameRules();
	 if(!gamerules)
	 {
		 return pContext->ThrowNativeError("Failed to get a CGameRules pointer from SDKTools.");
	 }

#ifdef _DEBUG
	 g_pSM->LogMessage(myself, "Calling BalanceTeams()");
#endif

	 /* Call original function. */
#if defined(PLATFORM_WINDOWS)
	__asm
	{
		mov ecx, gamerules
		call g_pBalanceTeamAddr
	}
#else
	typedef void (* BalanceTeams_t)(void *thisptr);
	BalanceTeams_t fn = (BalanceTeams_t)g_pBalanceTeamAddr;
	fn(gamerules);
#endif

	return 0;
}

bool TBExtension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	int offset;

	if(!gameconfs->LoadGameConfigFile("tbimmunity", &g_pGameConf, error, maxlength))
	{
		g_pSM->Format(error, maxlength, "Failed to load game config \"tbimmunity\": %s", error);
		return false;
	}

	if(!g_pGameConf->GetMemSig("BalanceTeams", &g_pBalanceTeamAddr))
	{
		g_pSM->Format(error, maxlength, "Couldn't find \"BalanceTeams\" function.");
		return false;
	}

	// linux: 0x104 | win: 0x17A
	if(!g_pGameConf->GetOffset("PlayerByIndex", &offset))
	{
		g_pSM->Format(error, maxlength, "Couldn't read \"PlayerByIndex\" offset from gamedata.");
		return false;
	}

	memset(&m_oldBytes, 0, sizeof(mempatch_t));
	PlayerByIndex = TBExtension::PatchBalanceFunction(g_pBalanceTeamAddr, offset, &m_oldBytes);

	sharesys->AddDependency(myself, "sdktools.ext", true, true);
	sharesys->AddNatives(myself, TBNatives);

	g_pForward = g_pForwards->CreateForward("OnBalanceCheck", ET_Event, 1, NULL, Param_Cell);

	return true;
}

void TBExtension::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
}

bool TBExtension::QueryRunning(char *error, size_t maxlength)
{
	SM_CHECK_IFACE(SDKTOOLS, g_pSDKTools);
	if(g_pSDKTools && g_pSDKTools->GetInterfaceVersion() < 2)
	{
		g_pSM->LogError(myself, "SDKTools is outdated. BalanceTeams native disabled.");
	}
	
	return true;
}

void TBExtension::SDK_OnUnload()
{
	g_pForwards->ReleaseForward(g_pForward);

	/* Apply backup patch. */
	memcpy(m_oldBytes.address, m_oldBytes.bytes, 5);
}

CBasePlayer *hkUTIL_PlayerByIndex(int entindex)
{
	CBasePlayer *pPlayer = PlayerByIndex(entindex);

	if(pPlayer && g_pForward->GetFunctionCount() > 0)
	{
		cell_t res;

		g_pForward->PushCell(entindex);
		g_pForward->Execute(&res);

		if (res > Pl_Continue)
		{
#ifdef _DEBUG
			g_pSM->LogMessage(myself, "Skipped call to UTIL_PlayerByIndex(%d)", entindex);
#endif
			return NULL;
		}
	}

	return pPlayer;
}

tPlayerByIndex TBExtension::PatchBalanceFunction(void *addr, int offset, mempatch_t *patch)
{
	/**
	 * What we want:
	 *	Immunity for certain players. We call into SourcePawn to see whether the specified
	 *	player needs team balance immunity by using a so called scripting forward.
	 *	We don't want to intercept the balancing process(we just want certain players to be ignored by it).
	 *
	 * How does the original routine look like:
	 *	The original function looks roughly like this:
	 *
	 *	void CCSGameRules::BalanceTeams()				| Located at 002E3E90
	 *	{
	 *		... code ...
	 *		int idx = 1;
	 *		while(true)
	 *		{
	 *			player = UTIL_PlayerByIndex(idx);		| Located at 002E3F94 (our target)
	 *			if(!player)
	 *				continue;
	 *
	 *			... code ...
	 *
	 *			if(gpGlobals->maxClients < ++idx)
	 *				break;
	 *		}
	 *		... code ...
	 *	}
	 *
	 * How do we proceed:
	 *	We'll do something I call function address replacing.
	 *	Basicly we'll just replace that UTIL_PlayerByIndex call to a function
	 *	specially tailored for this case of use.
	 */
	JitWriter writer;
	jitoffs_t offs_call, offs_orig;
	tPlayerByIndex orig_function;

	writer.outbase = (jitcode_t)addr;
	writer.set_outputpos(offset);

#ifdef _DEBUG
	g_pSM->LogMessage(myself, "Patching function at 0x%p (outpos: %p)", addr, writer.outptr);
#endif

	/* Create a patch to restore the function to its original state later. */
	patch->address = writer.outptr;
	memcpy(patch->bytes, writer.outptr, 5);

	/* Get the original function address so we can call it later. */
	offs_orig = *reinterpret_cast<jitoffs_t*>(writer.outptr + 1);
	/* We need to use the next instruction address(value of EIP)
	 * in order to properly calculate the address of the original function. */
	orig_function = reinterpret_cast<tPlayerByIndex>((writer.outptr + 5) + offs_orig);

	/* Ensure we can patch the bytes in. */
	SourceHook::SetMemAccess(writer.outptr, 5, SH_MEM_READ | SH_MEM_WRITE | SH_MEM_EXEC);

	/* Overwrite with our custom function. */
	offs_call = IA32_Call_Imm32(&writer, 0);
	IA32_Write_Jump32_Abs(&writer, offs_call, (void*)hkUTIL_PlayerByIndex);

#ifdef _DEBUG
	g_pSM->LogMessage(myself, "Patched BalanceTeams - Original UTIL_PlayerByIndex address is: %p", orig_function);
#endif
	return orig_function;
}