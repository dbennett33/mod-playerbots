/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNTRIGGERS_H
#define PLAYERBOTS_RAIDRUNTRIGGERS_H

#include "Trigger.h"

class RaidRunRegenTrigger : public Trigger
{
public:
    RaidRunRegenTrigger(PlayerbotAI* botAI) : Trigger(botAI, "raid run regen") {}

    bool IsActive() override;
};

#endif
