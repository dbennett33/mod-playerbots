#ifndef _PLAYERBOT_RAIDSTRATEGYCONTEXT_H_
#define _PLAYERBOT_RAIDSTRATEGYCONTEXT_H_

#include "Strategy.h"
#include "raids/blackwinglair/RaidBwlStrategy.h"
#include "raids/naxxramas/RaidNaxxStrategy.h"
#include "raids/moltencore/RaidMoltenCoreStrategy.h"

class RaidStrategyContext : public NamedObjectContext<Strategy>
{
    public:
        RaidStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["naxx"] = &RaidStrategyContext::naxx;
            creators["bwl"] = &RaidStrategyContext::bwl;
            creators["molten core"] = &RaidStrategyContext::moltenCore;
        }
    private:
        static Strategy* naxx(PlayerbotAI* botAI) { return new RaidNaxxStrategy(botAI); }
        static Strategy* bwl(PlayerbotAI* botAI) { return new RaidBwlStrategy(botAI); }
        static Strategy* moltenCore(PlayerbotAI* botAI) { return new RaidMoltenCoreStrategy(botAI); }
};

#endif
