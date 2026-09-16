#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"

namespace ai
{
    class PetitionSignAction : public Action {
    public:
        bool RequiresWorldOwner() const override { return true; }
        PetitionSignAction(PlayerbotAI* ai) : Action(ai, "petition sign") {}
        virtual bool Execute(Event& event) override;
    };
}
