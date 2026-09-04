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
#include <cmath>
#include <vector>

namespace
{
constexpr float ANUB_MELEE_SPREAD_RADIUS = 9.0f;
constexpr float ANUB_RANGED_SPREAD_RADIUS = 24.0f;
constexpr float ANUB_SLOT_TOLERANCE = 3.5f;
// East spawn door (home ~3331). West entrance is 3202.67,-3475.94 — do not drag him there.
constexpr float ANUB_TANK_X = 3308.59f;
constexpr float ANUB_TANK_Y = -3476.29f;
constexpr float ANUB_TANK_TOLERANCE = 4.0f;
constexpr float ANUB_LOCUST_KITE_X = 3308.59f;
constexpr float ANUB_LOCUST_KITE_Y = -3476.29f;
constexpr float ANUB_LOCUST_NORTH_Y = -3448.0f;
constexpr float ANUB_LOCUST_SOUTH_Y = -3504.0f;
constexpr float ANUB_LOCUST_ARRIVE = 8.0f;
// Mid-room (instance circle centre). Locust: raid holds here, ~spell range west of Anub.
constexpr float ANUB_ROOM_CENTER_X = 3273.38f;
constexpr float ANUB_ROOM_CENTER_Y = -3475.88f;
constexpr float ANUB_LOCUST_RANGE = 30.0f;
constexpr float ANUB_LOCUST_FAN = 12.0f;

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
        float destX = ANUB_LOCUST_KITE_X;
        float destY = ANUB_LOCUST_KITE_Y;
        if (bot->GetExactDist2d(destX, destY) <= ANUB_LOCUST_ARRIVE)
            destY = (bot->GetPositionY() < ANUB_LOCUST_KITE_Y) ? ANUB_LOCUST_NORTH_Y : ANUB_LOCUST_SOUTH_Y;

        return MoveTo(bot->GetMapId(), destX, destY, bot->GetPositionZ(), false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }

    // Hold Anub at the east spawn door. Walking him to the entrance wipes positioning.
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

    if (locust)
    {
        // Mid-room, west of Anub, at spell range so ranged keep casting while he N/S jukes.
        float const dx = ANUB_ROOM_CENTER_X - boss->GetPositionX();
        float const dy = ANUB_ROOM_CENTER_Y - boss->GetPositionY();
        float const len = std::hypot(dx, dy);
        float destX;
        float destY;
        if (len < 1.0f)
        {
            destX = boss->GetPositionX() - ANUB_LOCUST_RANGE;
            destY = boss->GetPositionY();
        }
        else
        {
            float const dist = std::min(ANUB_LOCUST_RANGE, len);
            destX = boss->GetPositionX() + dx / len * dist;
            destY = boss->GetPositionY() + dy / len * dist;
            if (count > 1)
            {
                float const offset = ANUB_LOCUST_FAN * (static_cast<float>(index) / static_cast<float>(count - 1) - 0.5f);
                destX += (-dy / len) * offset;
                destY += (dx / len) * offset;
            }
        }
        if (bot->GetExactDist2d(destX, destY) <= ANUB_SLOT_TOLERANCE)
            return false;

        return MoveTo(bot->GetMapId(), destX, destY, bot->GetPositionZ(), false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
    }

    float angle;
    // Impale: spread west into the room. Tank is at the east spawn door; east is the wall.
    float const west = static_cast<float>(M_PI);
    float const arc = static_cast<float>(M_PI);
    if (count == 1)
        angle = west;
    else
        angle = west - arc / 2.0f + arc * static_cast<float>(index) / static_cast<float>(count - 1);

    float radius = ANUB_RANGED_SPREAD_RADIUS;
    if (botAI->IsMelee(bot))
        radius = std::max(ANUB_MELEE_SPREAD_RADIUS, boss->GetCombatReach() + 2.5f);

    float const destX = boss->GetPositionX() + cos(angle) * radius;
    float const destY = boss->GetPositionY() + sin(angle) * radius;
    if (bot->GetExactDist2d(destX, destY) <= ANUB_SLOT_TOLERANCE)
        return false;

    return MoveTo(bot->GetMapId(), destX, destY, bot->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
