/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunTriggers.h"
#include "RaidRunMgr.h"
#include "RaidRunState.h"

bool RaidRunRegenTrigger::IsActive()
{
    Player* master = botAI->GetMaster();
    if (!master)
        return false;

    RaidRunState const* state = sRaidRunMgr.GetState(master);
    return state && state->phase == RAID_RUN_REGEN && !bot->IsInCombat();
}
