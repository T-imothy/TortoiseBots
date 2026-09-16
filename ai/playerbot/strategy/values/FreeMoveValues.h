#pragma once
#include "playerbot/strategy/WorldCalculatedValue.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class FreeMoveCenterValue : public WorldCalculatedValue<GuidPosition>
	{
	public:
        FreeMoveCenterValue(PlayerbotAI* ai) : WorldCalculatedValue<GuidPosition>(ai, "free move center", 5) {};
        virtual GuidPosition Calculate() override;
    };

    class FreeMoveRangeValue : public FloatCalculatedValue
    {
    public:
        FreeMoveRangeValue(PlayerbotAI* ai) : FloatCalculatedValue(ai, "free move range", 2) {};
        virtual float Calculate() override;
    };

    class CanFreeMoveValue : public BoolCalculatedValue, public Qualified
    {
    public:
        CanFreeMoveValue(PlayerbotAI* ai, std::string name = "can free move", int checkInterval = 2) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};
        static bool CanFreeMoveTo(PlayerbotAI* ai, WorldPosition dest);
        static bool CanFreeTarget(PlayerbotAI* ai, WorldPosition dest);
        static bool CanFreeAttack(PlayerbotAI* ai, WorldPosition dest);

        bool Calculate() override;
    private:
        //Actual caculation functions used.
        static bool CanFreeMove(PlayerbotAI* ai, WorldPosition dest, float range);
    };
};
