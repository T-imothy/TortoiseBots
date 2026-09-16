#pragma once
#include "playerbot/PlayerbotAI.h"
#include "runtime/BotWorldActions.h"

#include "playerbot/strategy/Action.h"

namespace ai
{
    class SecurityCheckAction : public Action
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        SecurityCheckAction(PlayerbotAI* ai) : Action(ai, "security check") {}
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };
}
