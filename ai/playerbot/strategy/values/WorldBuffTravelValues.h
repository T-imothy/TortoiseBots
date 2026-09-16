#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include <ctime>

namespace ai
{
    class WorldBuffTravelStepValue : public ManualSetValue<uint8>
    {
    public:
        WorldBuffTravelStepValue(PlayerbotAI* ai)
            : ManualSetValue<uint8>(ai, 0, "world buff travel step") {}
    };
    // Shared by this AI's action instances, released with the AI incarnation.
    class WorldBuffSummonTimeValue : public ManualSetValue<time_t>
    {
    public:
        WorldBuffSummonTimeValue(PlayerbotAI* ai)
            : ManualSetValue<time_t>(ai, 0, "world buff summon time") {}
    };
}
