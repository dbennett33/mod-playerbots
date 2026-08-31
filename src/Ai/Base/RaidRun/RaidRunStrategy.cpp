/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunStrategy.h"
#include "Playerbots.h"

void RaidRunRegenStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "raid run regen",
        {
            NextAction("raid run regen", 40.0f)
        }
    ));
}
