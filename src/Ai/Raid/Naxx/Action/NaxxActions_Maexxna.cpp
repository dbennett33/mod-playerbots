/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxActions.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "NaxxSpellIds.h"
#include "Playerbots.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr float WEB_WRAP_SCAN_RANGE = 100.0f;
// Wraps sit on the north/east ledges (Z ~298-308); floor is ~292. Do not mmap-path to wrap Z.
constexpr float WEB_WRAP_STAND_INSET = 5.0f;
constexpr float WEB_WRAP_ARRIVE_XY = 8.0f;

void CollectMaexxnaWebWraps(Player* bot, std::vector<Unit*>& out)
{
    if (!bot)
        return;

    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(bot, WEB_WRAP_SCAN_RANGE);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, WEB_WRAP_SCAN_RANGE);

    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!creature || !creature->IsAlive() || creature->GetEntry() != NaxxSpellIds::NpcWebWrap)
            continue;

        if (bot->IsFriendlyTo(creature))
            continue;

        out.push_back(creature);
    }

    std::sort(out.begin(), out.end(), [](Unit const* a, Unit const* b)
    {
        return a->GetGUID() < b->GetGUID();
    });
}

bool IsWebWrapped(Player* bot)
{
    if (!bot)
        return false;

    return bot->HasAura(NaxxSpellIds::WebWrapStun) || bot->HasAura(NaxxSpellIds::WebWrapSummon) ||
           bot->HasAura(NaxxSpellIds::WebWrapPacify);
}
}  // namespace

bool HasMaexxnaWebWrap(Player* bot)
{
    std::vector<Unit*> wraps;
    CollectMaexxnaWebWraps(bot, wraps);
    return !wraps.empty();
}

Unit* PickMaexxnaWebWrap(PlayerbotAI* botAI, Player* bot)
{
    if (!botAI || !bot)
        return nullptr;

    std::vector<Unit*> wraps;
    CollectMaexxnaWebWraps(bot, wraps);
    if (wraps.empty())
        return nullptr;

    if (wraps.size() == 1)
        return wraps[0];

    Group* group = bot->GetGroup();
    if (!group)
        return wraps[0];

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
        return wraps[0];

    std::sort(members.begin(), members.end(), [](Player const* a, Player const* b)
    {
        return a->GetGUID() < b->GetGUID();
    });

    auto const it = std::find(members.begin(), members.end(), bot);
    if (it == members.end())
        return wraps[0];

    uint32 const index = static_cast<uint32>(std::distance(members.begin(), it));
    return wraps[index % wraps.size()];
}

bool MaexxnaChooseTargetAction::isUseful()
{
    if (botAI->IsMainTank(bot) || IsWebWrapped(bot))
        return false;

    return PickMaexxnaWebWrap(botAI, bot) != nullptr;
}

bool MaexxnaChooseTargetAction::Execute(Event /*event*/)
{
    if (botAI->IsMainTank(bot) || IsWebWrapped(bot))
        return false;

    Unit* wrap = PickMaexxnaWebWrap(botAI, bot);
    if (!wrap)
        return false;

    float destX = wrap->GetPositionX();
    float destY = wrap->GetPositionY();
    float const dx = bot->GetPositionX() - destX;
    float const dy = bot->GetPositionY() - destY;
    float const len = std::hypot(dx, dy);
    if (len > 1.0f)
    {
        destX += dx / len * WEB_WRAP_STAND_INSET;
        destY += dy / len * WEB_WRAP_STAND_INSET;
    }

    if (bot->GetExactDist2d(destX, destY) > WEB_WRAP_ARRIVE_XY)
    {
        if (MoveTo(bot->GetMapId(), destX, destY, bot->GetPositionZ(), false, false, false, false,
                   MovementPriority::MOVEMENT_COMBAT))
            return true;
    }

    if (context->GetValue<Unit*>("current target")->Get() == wrap)
        return false;

    return Attack(wrap);
}
