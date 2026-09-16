#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    class SendMailAction : public ChatCommandAction
    {
    public:
        bool RequiresWorldOwner() const override { return true; }
        SendMailAction(PlayerbotAI* ai) : ChatCommandAction(ai, "sendmail") {}
        virtual bool Execute(Event& event) override;
    };
}