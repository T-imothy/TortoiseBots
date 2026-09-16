#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    class HireAction : public ChatCommandAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        HireAction(PlayerbotAI* ai) : ChatCommandAction(ai, "hire") {}
        virtual bool Execute(Event& event) override;
    };
}
