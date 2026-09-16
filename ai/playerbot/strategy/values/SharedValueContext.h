#pragma once
#include <memory>
#include <mutex>
#include "playerbot/strategy/AiObjectContext.h"

#include "PvpValues.h"
#include "QuestValues.h"
#include "TrainerValues.h"
#include "VendorValues.h"
#include "TravelValues.h"
#include "LootValues.h"
#include "MountValues.h"
#include "playerbot/PlayerbotAI.h"

namespace ai
{
    class SharedValueContext : public NamedObjectContext<UntypedValue>
    {
    public:
        SharedValueContext() : NamedObjectContext(true)
        {
            creators["bg masters"] = [](PlayerbotAI* ai) { return new BgMastersValue(ai); };

            creators["item drop map"] = [](PlayerbotAI* ai) { return new ItemDropMapValue(ai); };
            creators["drop map"] = [](PlayerbotAI* ai) { return new DropMapValue(ai); };
            creators["item drop list"] = [](PlayerbotAI* ai) { return new ItemDropListValue(ai); };
            creators["entry loot list"] = [](PlayerbotAI* ai) { return new EntryLootListValue(ai); };
            creators["loot chance"] = [](PlayerbotAI* ai) { return new LootChanceValue(ai); };

            creators["vendor map"] = [](PlayerbotAI* ai) { return new VendorMapValue(ai); };
            creators["item vendor list"] = [](PlayerbotAI* ai) { return new ItemVendorListValue(ai); };

            creators["entry quest relation"] = [](PlayerbotAI* ai) { return new EntryQuestRelationMapValue(ai); };

            creators["quest guidp map"] = [](PlayerbotAI* ai) { return new QuestGuidpMapValue(ai); };
            creators["quest givers"] = [](PlayerbotAI* ai) { return new QuestGiversValue(ai); };

            creators["trainable spell map"] = [](PlayerbotAI* ai) { return new TrainableSpellMapValue(ai); };



            creators["entry travel purpose"] = [](PlayerbotAI* ai) { return new EntryTravelPurposeMapValue(ai); };
            creators["entry guidps"] = [](PlayerbotAI* ai) { return new EntryGuidpsValue(ai); };

            creators["full mount list"] = [](PlayerbotAI* ai) { return new FullMountListValue(ai); };

            creators["global string"] = [](PlayerbotAI* ai) { return new StringManualSetValue(ai); };
        }
    };


    class SharedObjectContext
    {
    public:
        SharedObjectContext() : contextOwner(new SharedValueContext())
        {
            valueContexts.Add(contextOwner.get());
        }

        template<class T, class... Args>
        T ReadValue(const std::string& name, Args const&... args)
        {
            // Creation and calculation form one publication operation. Some
            // calculations read other global values, hence the recursive lock.
            std::lock_guard<std::recursive_mutex> guard(mutex);
            return GetValue<T>(name, args...)->Get();
        }

        template<class T>
        void WriteValue(const std::string& name, T value)
        {
            std::lock_guard<std::recursive_mutex> guard(mutex);
            GetValue<T>(name)->Set(value);
        }

        template<class T, class Param>
        void WriteValue(const std::string& name, Param const& param, T value)
        {
            std::lock_guard<std::recursive_mutex> guard(mutex);
            GetValue<T>(name, param)->Set(value);
        }

    private:
        UntypedValue* GetUntypedValue(const std::string& name)
        {
            // Shared values retain their AI-aware base pointer. Keep its
            // owner alive for the values' lifetime, not just their constructor.
            if (!valueOwner)
                valueOwner.reset(new PlayerbotAI());
            return valueContexts.GetObject(name, valueOwner.get());
        }

        template<class T>
        Value<T>* GetValue(const std::string& name)
        {
            return dynamic_cast<Value<T>*>(GetUntypedValue(name));
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, const std::string& param)
        {
            return GetValue<T>(name + "::" + param);
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, int32 param)
        {
            return GetValue<T>(name, std::to_string(param));
        }

        std::recursive_mutex mutex;
        // Destruction order: list, owned values, then their AI-aware owner.
        std::unique_ptr<PlayerbotAI> valueOwner;
        std::unique_ptr<SharedValueContext> contextOwner;
        NamedObjectContextList<UntypedValue> valueContexts;
    };
#define sSharedObjectContext MaNGOS::Singleton<SharedObjectContext>::Instance()
}