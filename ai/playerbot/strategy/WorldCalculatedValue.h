#pragma once
#include "Value.h"
#include "AiObjectContext.h"
#include "runtime/BotWorldActions.h"
#include <memory>
#include <type_traits>

namespace ai
{
// Explicit opt-in for copied decision facts that require the world phase.
// No live Player/Unit/Item pointer may be carried inside the result type.
template<class T, class Base = CalculatedValue<T>>
class WorldCalculatedValue : public Base
{
    static_assert(!std::is_pointer<T>::value, "World values must contain copied facts");
public:
    using Base::Base;
    T Get() override
    {
        if (!TortoiseBots::BotWorldActions::IsMapExecution())
            return Base::Get();
        if ((!this->lastCheckTime || this->Expired()) && pending.expired())
        {
            auto request = std::make_shared<int>(0);
            pending = request;
            std::weak_ptr<int> epoch = lifetime;
            std::string valueName = this->getName();
            if (auto* qualified = dynamic_cast<Qualified*>(this))
                if (!qualified->getQualifier().empty())
                    valueName += "::" + qualified->getQualifier();
            TortoiseBots::BotWorldActions::Instance().EnqueueContinuation(this->bot,
                "world value " + valueName, Event(),
                [epoch, request, valueName](PlayerbotAI& current)
                {
                    if (epoch.expired()) return;
                    // Resolve through the current AI; no value, Player or AI
                    // address is retained across the native phase boundary.
                    if (auto* value = current.GetAiObjectContext()->GetValue<T>(valueName))
                        value->Get();
                });
        }
        // Do not mark an admitted request as calculated. A rejected/discarded
        // continuation releases pending and remains due on the next map pass.
        return this->value;
    }
    void Reset() override
    {
        Base::Reset();
        lifetime = std::make_shared<int>(0);
        pending.reset();
    }
private:
    std::shared_ptr<int> lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> pending;
};
}
// End world calculated value implementation.
