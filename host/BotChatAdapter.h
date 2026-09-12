#pragma once
#include "ScriptObjects.h"
namespace TortoiseBots {
// Native command registration retains security, console/SOAP authorization,
// RBAC and normal dispatch. No pre-authorization interception is needed.
class BotChatAdapter final : public CommandScript
{
public:
    BotChatAdapter();
    std::vector<ChatCommand> GetCommands() const override;
};
}
