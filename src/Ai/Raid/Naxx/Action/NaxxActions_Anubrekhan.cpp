/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxActions.h"
#include "NaxxSpellIds.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "Spell.h"
#include <algorithm>
#include <vector>

namespace
{
constexpr float ANUB_MELEE_SPREAD_RADIUS = 9.0f;
constexpr float ANUB_RANGED_SPREAD_RADIUS = 24.0f;
constexpr float ANUB_LOCUST_SAFE_RADIUS = 10.0f;
constexpr float ANUB_SLOT_TOLERANCE = 3.5f;
// Creature spawn 3308.59,-3476.29 — east end of the room, facing the west entrance.
constexpr float ANUB_TANK_X = 3308.59f;
constexpr float ANUB_TANK_Y = -3476.29f;
constexpr float ANUB_TANK_TOLERANCE = 4.0f;

bool IsAnubLocustSwarm(Unit* boss)
{
    if (!boss)
        return false;

    if (NaxxSpellIds::HasAnyAura(
            boss, {NaxxSpellIds::LocustSwarm10, NaxxSpellIds::LocustSwarm10Alt, NaxxSpellIds::LocustSwarm25}))
        return true;

    if (Spell* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL))
    {
        if (NaxxSpellIds::MatchesAnySpellId(spell->GetSpellInfo(), {NaxxSpellIds::LocustSwarm10,
                NaxxSpellIds::LocustSwarm10Alt, NaxxSpellIds::LocustSwarm25}))
            return true;
    }

    if (Spell* spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        if (NaxxSpellIds::MatchesAnySpellId(spell->GetSpellInfo(), {NaxxSpellIds::LocustSwarm10,
                NaxxSpellIds::LocustSwarm10Alt, NaxxSpellIds::LocustSwarm25}))
            return true;
    }

    return false;
}

// Stable unique slot among alive non-main-tank members so Impale lines do not clip a stack.
bool GetAnubSpreadSlot(PlayerbotAI* botAI, Player* bot, uint32& index, uint32& count)
{
    Group* group = bot->GetGroup();
    if (!group)
    {
        index = 0;
        count = 1;
        return true;
    }

    std::vector<Player*> members;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        if (botAI->IsMainTank(member))
            continue;

        members.push_back(member);
    }

    if (members.empty())
        return false;

    std::sort(members.begin(), members.end(), [](Player const* a, Player const* b)
    {
        return a->GetGUID() < b->GetGUID();
    });

    auto const it = std::find(members.begin(), members.end(), bot);
    if (it == members.end())
        return false;

    index = static_cast<uint32>(std::distance(members.begin(), it));
    count = static_cast<uint32>(members.size());
    return true;
}
}  // namespace

bool AnubrekhanChooseTargetAction::Execute(Event /*event*/)
{
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    Unit* target = nullptr;
    Unit* target_boss = nullptr;
    std::vector<Unit*> target_guards;
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;
        if (botAI->EqualLowercaseName(unit->GetName(), "crypt guard"))
            target_guards.push_back(unit);

        if (botAI->EqualLowercaseName(unit->GetName(), "anub'rekhan"))
            target_boss = unit;
    }
    if (botAI->IsMainTank(bot))
        target = target_boss;
    else
    {
        if (target_guards.size() == 0)
            target = target_boss;
        else
        {
            if (botAI->IsAssistTank(bot))
            {
                for (Unit* t : target_guards)
                {
                    if (target == nullptr || (target->GetVictim() && target->GetVictim()->ToPlayer() &&
                                              botAI->IsTank(target->GetVictim()->ToPlayer())))
                        target = t;
                }
            }
            else
            {
                for (Unit* t : target_guards)
                {
                    if (target == nullptr || target->GetHealthPct() > t->GetHealthPct())
                        target = t;
                }
            }
        }
    }
    if (context->GetValue<Unit*>("current target")->Get() == target)
        return false;

    return Attack(target);
}

bool AnubrekhanPositionAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "anub'rekhan");
    if (!boss)
        return false;

    bool const locust = IsAnubLocustSwarm(boss);
    if (locust && botAI->IsMainTank(bot))
    {
        uint32 nearest = FindNearestWaypoint();
        uint32 next_point = (nearest + 1) % intervals;
        return MoveTo(bot->GetMapId(), waypoints[next_point].first, waypoints[next_point].second,
                      bot->GetPositionZ(), false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
    }

    // Hold Anub at spawn (east end) so the raid can spread in the open room toward the door.
    if (!locust && botAI->IsMainTank(bot))
    {
        if (boss->GetVictim() != bot)
            return false;

        float const dist = bot->GetExactDist2d(ANUB_TANK_X, ANUB_TANK_Y);
        if (dist <= ANUB_TANK_TOLERANCE)
            return false;

        float const step = std::min(10.0f, dist);
        float const moveX = bot->GetPositionX() + (ANUB_TANK_X - bot->GetPositionX()) / dist * step;
        float const moveY = bot->GetPositionY() + (ANUB_TANK_Y - bot->GetPositionY()) / dist * step;
        return MoveTo(bot->GetMapId(), moveX, moveY, bot->GetPositionZ(), false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }

    uint32 index = 0;
    uint32 count = 0;
    if (!GetAnubSpreadSlot(botAI, bot, index, count) || !count)
        return false;

    float const twoPi = 2.0f * static_cast<float>(M_PI);
    float angle = twoPi * static_cast<float>(index) / static_cast<float>(count);
    // Impale: spread in the open room (west of spawn), not through the east wall behind Anub.
    if (!locust)
    {
        float const west = static_cast<float>(M_PI);
        float const arc = static_cast<float>(M_PI);
        if (count == 1)
            angle = west;
        else
            angle = west - arc / 2.0f + arc * static_cast<float>(index) / static_cast<float>(count - 1);
    }
    float radius = ANUB_RANGED_SPREAD_RADIUS;
    if (locust)
        radius = ANUB_LOCUST_SAFE_RADIUS;
    else if (botAI->IsMelee(bot))
        radius = std::max(ANUB_MELEE_SPREAD_RADIUS, boss->GetCombatReach() + 2.5f);

    // Locust: stack loosely at room center while the tank kites. Impale: orbit the boss, melee included.
    float const originX = locust ? center_x : boss->GetPositionX();
    float const originY = locust ? center_y : boss->GetPositionY();
    float const destX = originX + cos(angle) * radius;
    float const destY = originY + sin(angle) * radius;
    if (bot->GetExactDist2d(destX, destY) <= ANUB_SLOT_TOLERANCE)
        return false;

    return MoveTo(bot->GetMapId(), destX, destY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
