
#ifndef _PLAYERBOT_RAIDMOLTENCORESTRATEGY_H
#define _PLAYERBOT_RAIDMOLTENCORESTRATEGY_H

#include "Multiplier.h"
#include "AiObjectContext.h"
#include "Strategy.h"
#include "RaidMoltenCoreScripts.h"


class RaidMoltenCoreStrategy : public Strategy
{
public:
    RaidMoltenCoreStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    virtual std::string const getName() override { return "moltencore"; }
    virtual void InitTriggers(std::vector<TriggerNode*> &triggers) override;
    virtual void InitMultipliers(std::vector<Multiplier*> &multipliers) override;
};

  
#endif
