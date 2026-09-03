/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxActions.h"
#include "Playerbots.h"

bool HeiganDanceAction::Prepare()
{
    return helper.UpdateBossAI();
}

bool HeiganDanceMeleeAction::Execute(Event /*event*/)
{
    if (!Prepare())
        return false;

    if (!helper.IsFastDance() && botAI->IsMainTank(bot) && !AI_VALUE2(bool, "has aggro", "boss target"))
        return false;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    helper.GetSafePosition(x, y, z);
    float const radius = botAI->IsMainTank(bot) ? 0.5f : 2.0f;
    return MoveInside(bot->GetMapId(), x, y, z, radius, MovementPriority::MOVEMENT_COMBAT);
}

bool HeiganDanceRangedAction::Execute(Event /*event*/)
{
    if (!Prepare())
        return false;

    if (!helper.IsFastDance())
    {
        if (MoveTo(bot->GetMapId(), HeiganBossHelper::PlatformX, HeiganBossHelper::PlatformY,
                   HeiganBossHelper::PlatformZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT))
            return true;

        return MoveInside(bot->GetMapId(), HeiganBossHelper::PlatformX, HeiganBossHelper::PlatformY,
                          HeiganBossHelper::PlatformZ, 2.0f, MovementPriority::MOVEMENT_COMBAT);
    }

    botAI->InterruptSpell();
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    helper.GetSafePosition(x, y, z);
    return MoveInside(bot->GetMapId(), x, y, z, 1.0f, MovementPriority::MOVEMENT_COMBAT);
}
