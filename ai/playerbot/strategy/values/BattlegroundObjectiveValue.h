#pragma once
#include "playerbot/strategy/Value.h"
#include "ObjectGuid.h"

namespace ai
{
    struct BattlegroundObjectiveState
    {
        ObjectGuid guid;
        uint64 mapGeneration = 0;
        uint32 selectedAt = 0;
    };

    // All BGTactics actions for one AI share the native value context.
    // The cached identity never extends a GameObject's lifetime.
    class BattlegroundObjectiveValue : public ManualSetValue<BattlegroundObjectiveState*>
    {
    public:
        BattlegroundObjectiveValue(PlayerbotAI* ai, std::string name = "battleground objective memory")
            : ManualSetValue<BattlegroundObjectiveState*>(ai, new BattlegroundObjectiveState, name) {}
        ~BattlegroundObjectiveValue() override { delete value; }
    };
}
