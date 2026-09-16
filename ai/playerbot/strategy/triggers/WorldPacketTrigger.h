#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class WorldPacketTrigger : public Trigger {
    public:
        WorldPacketTrigger(PlayerbotAI* ai, std::string command) : Trigger(ai, command) {}

        virtual void ExternalEvent(WorldPacket &packet, Player* owner = NULL) override
        {
            // Penqle's WorldPacket has a deleted copy operator=; copy-construct + move-assign instead.
            externalEvent = Event(getName(), packet, owner);
            triggered = true;
        }

        virtual Event Check() override
        {
            if (!triggered)
                return Event();

            return externalEvent;
        }

        virtual void Reset() override
        {
            triggered = false;
            externalEvent = Event();
        }
    };
}
