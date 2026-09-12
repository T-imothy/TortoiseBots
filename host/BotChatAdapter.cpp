#include "BotChatAdapter.h"
#include "../commands/BotCommands.h"
namespace TortoiseBots {
BotChatAdapter::BotChatAdapter() : CommandScript("tortoisebots_commands") {}

std::vector<ChatCommand> BotChatAdapter::GetCommands() const
{
    ChatCommand bot{};
    bot.Name = "bot";
    bot.SecurityLevel = SEC_PLAYER;
    bot.AllowConsole = true;
    bot.ModuleHandler = [](ChatHandler* handler, char* args)
    {
        return BotCommands::HandleChatCommand(handler, args ? args : "");
    };
    ChatCommand auction{};
    auction.Name = "ahbot";
    auction.SecurityLevel = SEC_ADMINISTRATOR;
    auction.AllowConsole = true;
    auction.ModuleHandler = [](ChatHandler* handler, char* args)
    {
        return BotCommands::HandleAuctionCommand(handler, args);
    };
    return {bot, auction};
}
}
