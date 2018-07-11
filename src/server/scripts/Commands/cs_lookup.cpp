/*
 * Copyright (C) 
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
Name: lookup_commandscript
%Complete: 100
Comment: All lookup related commands
Category: commandscripts
EndScriptData */

#include "AccountMgr.h"
#include "Chat.h"
#include "GameEventMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"

class lookup_commandscript : public CommandScript
{
public:
    lookup_commandscript() : CommandScript("lookup_commandscript") { }

    std::vector<ChatCommand> GetCommands() const
    {
        static std::vector<ChatCommand> lookupPlayerCommandTable =
        {
            { "ip",             SEC_GAMEMASTER,     true,  &HandleLookupPlayerIpCommand,        "" },
            { "account",        SEC_GAMEMASTER,     true,  &HandleLookupPlayerAccountCommand,   "" },
            { "email",          SEC_GAMEMASTER,     true,  &HandleLookupPlayerEmailCommand,     "" }
        };

        static std::vector<ChatCommand> lookupSpellCommandTable =
        {
            { "id",             SEC_ADMINISTRATOR,  true,  &HandleLookupSpellIdCommand,         "" },
            { "",               SEC_ADMINISTRATOR,  true,  &HandleLookupSpellCommand,           "" }
        };

        static std::vector<ChatCommand> lookupCommandTable =
        {
            { "area",           SEC_GAMEMASTER,      true,  &HandleLookupAreaCommand,            "" },
            { "creature",       SEC_ADMINISTRATOR,  true,  &HandleLookupCreatureCommand,        "" },
            { "event",          SEC_GAMEMASTER,     true,  &HandleLookupEventCommand,           "" },
            { "faction",        SEC_ADMINISTRATOR,  true,  &HandleLookupFactionCommand,         "" },
            { "item",           SEC_ADMINISTRATOR,  true,  &HandleLookupItemCommand,            "" },
            { "itemset",        SEC_ADMINISTRATOR,  true,  &HandleLookupItemSetCommand,         "" },
            { "object",         SEC_ADMINISTRATOR,  true,  &HandleLookupObjectCommand,          "" },
            { "quest",          SEC_ADMINISTRATOR,  true,  &HandleLookupQuestCommand,           "" },
            { "player",         SEC_GAMEMASTER,     true,  NULL,                                "", lookupPlayerCommandTable },
            { "skill",          SEC_ADMINISTRATOR,  true,  &HandleLookupSkillCommand,           "" },
            { "spell",          SEC_ADMINISTRATOR,  true,  NULL,                                "", lookupSpellCommandTable },
            { "taxinode",       SEC_ADMINISTRATOR,  true,  &HandleLookupTaxiNodeCommand,        "" },
            { "tele",           SEC_GAMEMASTER,      true,  &HandleLookupTeleCommand,            "" },
            { "title",          SEC_GAMEMASTER,     true,  &HandleLookupTitleCommand,           "" },
            { "map",            SEC_ADMINISTRATOR,  true,  &HandleLookupMapCommand,             "" }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "lookup",         SEC_ADMINISTRATOR,  true,  NULL,                                "", lookupCommandTable }
        };
        return commandTable;
    }

    static bool HandleLookupAreaCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        // Search in AreaTable.dbc
        for (uint32 i = 0; i < sAreaTableStore.GetNumRows(); ++i)
        {
            AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(i);
            if (areaEntry)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = areaEntry->area_name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = areaEntry->area_name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send area in "id - [name]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << areaEntry->ID << " - |cffffffff|Harea:" << areaEntry->ID << "|h[" << name << ' ' << localeNames[locale]<< "]|h|r";
                    else
                        ss << areaEntry->ID << " - " << name << ' ' << localeNames[locale];

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOAREAFOUND);

        return true;
    }

    static bool HandleLookupCreatureCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
        for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
        {
            uint32 id = itr->second.Entry;

            std::string name = itr->second.Name;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CHAT, id, id, name.c_str());
                else
                    handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CONSOLE, id, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOCREATUREFOUND);

        return true;
    }

    static bool HandleLookupEventCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        for (uint32 id = 0; id < events.size(); ++id)
        {
            GameEventData const& eventData = events[id];

            std::string descr = eventData.description;
            if (descr.empty())
                continue;

            if (Utf8FitTo(descr, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                char const* active = activeEvents.find(id) != activeEvents.end() ? handler->GetTrinityString(LANG_ACTIVE) : "";

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT, id, id, eventData.description.c_str(), active);
                else
                    handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE, id, eventData.description.c_str(), active);

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_NOEVENTFOUND);

        return true;
    }

    static bool HandleLookupFactionCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // Can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower (wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(id);
            if (factionEntry)
            {
                FactionState const* factionState = target ? target->GetReputationMgr().GetState(factionEntry) : NULL;

                int locale = handler->GetSessionDbcLocale();
                std::string name = factionEntry->name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = factionEntry->name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send faction in "id - [faction] rank reputation [visible] [at war] [own team] [unknown] [invisible] [inactive]" format
                    // or              "id - [faction] [no reputation]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << id << " - |cffffffff|Hfaction:" << id << "|h[" << name << ' ' << localeNames[locale] << "]|h|r";
                    else
                        ss << id << " - " << name << ' ' << localeNames[locale];

                    if (factionState) // and then target != NULL also
                    {
                        uint32 index = target->GetReputationMgr().GetReputationRankStrIndex(factionEntry);
                        std::string rankName = handler->GetTrinityString(index);

                        ss << ' ' << rankName << "|h|r (" << target->GetReputationMgr().GetReputation(factionEntry) << ')';

                        if (factionState->Flags & FACTION_FLAG_VISIBLE)
                            ss << handler->GetTrinityString(LANG_FACTION_VISIBLE);
                        if (factionState->Flags & FACTION_FLAG_AT_WAR)
                            ss << handler->GetTrinityString(LANG_FACTION_ATWAR);
                        if (factionState->Flags & FACTION_FLAG_PEACE_FORCED)
                            ss << handler->GetTrinityString(LANG_FACTION_PEACE_FORCED);
                        if (factionState->Flags & FACTION_FLAG_HIDDEN)
                            ss << handler->GetTrinityString(LANG_FACTION_HIDDEN);
                        if (factionState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
                            ss << handler->GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
                        if (factionState->Flags & FACTION_FLAG_INACTIVE)
                            ss << handler->GetTrinityString(LANG_FACTION_INACTIVE);
                    }
                    else
                        ss << handler->GetTrinityString(LANG_FACTION_NOREPUTATION);

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_FACTION_NOTFOUND);
        return true;
    }

    static bool HandleLookupItemCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in `item_template`
        ItemTemplateContainer const* its = sObjectMgr->GetItemTemplateStore();
        for (ItemTemplateContainer::const_iterator itr = its->begin(); itr != its->end(); ++itr)
        {
            std::string name = itr->second.Name1;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_ITEM_LIST_CHAT, itr->second.ItemId, itr->second.ItemId, name.c_str());
                else
                    handler->PSendSysMessage(LANG_ITEM_LIST_CONSOLE, itr->second.ItemId, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOITEMFOUND);

        return true;
    }

    static bool HandleLookupItemSetCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in ItemSet.dbc
        for (uint32 id = 0; id < sItemSetStore.GetNumRows(); id++)
        {
            ItemSetEntry const* set = sItemSetStore.LookupEntry(id);
            if (set)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = set->name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = set->name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send item set in "id - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_ITEMSET_LIST_CHAT, id, id, name.c_str(), localeNames[locale]);
                    else
                        handler->PSendSysMessage(LANG_ITEMSET_LIST_CONSOLE, id, name.c_str(), localeNames[locale]);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOITEMSETFOUND);

        return true;
    }

    static bool HandleLookupObjectCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        GameObjectTemplateContainer const* gotc = sObjectMgr->GetGameObjectTemplates();
        for (GameObjectTemplateContainer::const_iterator itr = gotc->begin(); itr != gotc->end(); ++itr)
        {
            std::string name = itr->second.name;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CHAT, itr->second.entry, itr->second.entry, name.c_str());
                else
                    handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CONSOLE, itr->second.entry, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOGAMEOBJECTFOUND);

        return true;
    }

    static bool HandleLookupQuestCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        ObjectMgr::QuestMap const& qTemplates = sObjectMgr->GetQuestTemplates();
        for (ObjectMgr::QuestMap::const_iterator iter = qTemplates.begin(); iter != qTemplates.end(); ++iter)
        {
            Quest* qInfo = iter->second;

            std::string title = qInfo->GetTitle();
            if (title.empty())
                continue;

            if (Utf8FitTo(title, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                char const* statusStr = "";

                if (target)
                {
                    QuestStatus status = target->GetQuestStatus(qInfo->GetQuestId());

                    switch (status)
                    {
                        case QUEST_STATUS_COMPLETE:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_COMPLETE);
                            break;
                        case QUEST_STATUS_INCOMPLETE:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_ACTIVE);
                            break;
                        case QUEST_STATUS_REWARDED:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_REWARDED);
                            break;
                        default:
                            break;
                    }
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_QUEST_LIST_CHAT, qInfo->GetQuestId(), qInfo->GetQuestId(), qInfo->GetQuestLevel(), title.c_str(), statusStr);
                else
                    handler->PSendSysMessage(LANG_QUEST_LIST_CONSOLE, qInfo->GetQuestId(), title.c_str(), statusStr);

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOQUESTFOUND);

        return true;
    }

    static bool HandleLookupSkillCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL in console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in SkillLine.dbc
        for (uint32 id = 0; id < sSkillLineStore.GetNumRows(); id++)
        {
            SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(id);
            if (skillInfo)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = skillInfo->name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = skillInfo->name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    char valStr[50] = "";
                    char const* knownStr = "";
                    if (target && target->HasSkill(id))
                    {
                        knownStr = handler->GetTrinityString(LANG_KNOWN);
                        uint32 curValue = target->GetPureSkillValue(id);
                        uint32 maxValue  = target->GetPureMaxSkillValue(id);
                        uint32 permValue = target->GetSkillPermBonusValue(id);
                        uint32 tempValue = target->GetSkillTempBonusValue(id);

                        char const* valFormat = handler->GetTrinityString(LANG_SKILL_VALUES);
                        snprintf(valStr, 50, valFormat, curValue, maxValue, permValue, tempValue);
                    }

                    // send skill in "id - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_SKILL_LIST_CHAT, id, id, name.c_str(), localeNames[locale], knownStr, valStr);
                    else
                        handler->PSendSysMessage(LANG_SKILL_LIST_CONSOLE, id, name.c_str(), localeNames[locale], knownStr, valStr);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOSKILLFOUND);

        return true;
    }

    static bool HandleLookupSpellCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in Spell.dbc
        for (uint32 id = 0; id < sSpellMgr->GetSpellInfoStoreSize(); id++)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
            if (spellInfo)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = spellInfo->SpellName[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = spellInfo->SpellName[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    bool known = target && target->HasSpell(id);
                    bool learn = (spellInfo->Effects[0].Effect == SPELL_EFFECT_LEARN_SPELL);

                    SpellInfo const* learnSpellInfo = sSpellMgr->GetSpellInfo(spellInfo->Effects[0].TriggerSpell);

                    uint32 talentCost = GetTalentSpellCost(id);

                    bool talent = (talentCost > 0);
                    bool passive = spellInfo->IsPassive();
                    bool active = target && target->HasAura(id);

                    // unit32 used to prevent interpreting uint8 as char at output
                    // find rank of learned spell for learning spell, or talent rank
                    uint32 rank = talentCost ? talentCost : learn && learnSpellInfo ? learnSpellInfo->GetRank() : spellInfo->GetRank();

                    // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << id << " - |cffffffff|Hspell:" << id << "|h[" << name;
                    else
                        ss << id << " - " << name;

                    // include rank in link name
                    if (rank)
                        ss << handler->GetTrinityString(LANG_SPELL_RANK) << rank;

                    if (handler->GetSession())
                        ss << ' ' << localeNames[locale] << "]|h|r";
                    else
                        ss << ' ' << localeNames[locale];

                    if (talent)
                        ss << handler->GetTrinityString(LANG_TALENT);
                    if (passive)
                        ss << handler->GetTrinityString(LANG_PASSIVE);
                    if (learn)
                        ss << handler->GetTrinityString(LANG_LEARN);
                    if (known)
                        ss << handler->GetTrinityString(LANG_KNOWN);
                    if (active)
                        ss << handler->GetTrinityString(LANG_ACTIVE);

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);

        return true;
    }

    static bool HandleLookupSpellIdCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        uint32 id = atoi((char*)args);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = 1;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
        if (spellInfo)
        {
            int locale = handler->GetSessionDbcLocale();
            std::string name = spellInfo->SpellName[locale];
            if (name.empty())
            {
                handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);
                return true;
            }

            if (locale < TOTAL_LOCALES)
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                bool known = target && target->HasSpell(id);
                bool learn = (spellInfo->Effects[0].Effect == SPELL_EFFECT_LEARN_SPELL);

                SpellInfo const* learnSpellInfo = sSpellMgr->GetSpellInfo(spellInfo->Effects[0].TriggerSpell);

                uint32 talentCost = GetTalentSpellCost(id);

                bool talent = (talentCost > 0);
                bool passive = spellInfo->IsPassive();
                bool active = target && target->HasAura(id);

                // unit32 used to prevent interpreting uint8 as char at output
                // find rank of learned spell for learning spell, or talent rank
                uint32 rank = talentCost ? talentCost : learn && learnSpellInfo ? learnSpellInfo->GetRank() : spellInfo->GetRank();

                // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
                std::ostringstream ss;
                if (handler->GetSession())
                    ss << id << " - |cffffffff|Hspell:" << id << "|h[" << name;
                else
                    ss << id << " - " << name;

                // include rank in link name
                if (rank)
                    ss << handler->GetTrinityString(LANG_SPELL_RANK) << rank;

                if (handler->GetSession())
                    ss << ' ' << localeNames[locale] << "]|h|r";
                else
                    ss << ' ' << localeNames[locale];

                if (talent)
                    ss << handler->GetTrinityString(LANG_TALENT);
                if (passive)
                    ss << handler->GetTrinityString(LANG_PASSIVE);
                if (learn)
                    ss << handler->GetTrinityString(LANG_LEARN);
                if (known)
                    ss << handler->GetTrinityString(LANG_KNOWN);
                if (active)
                    ss << handler->GetTrinityString(LANG_ACTIVE);

                handler->SendSysMessage(ss.str().c_str());

                if (!found)
                    found = true;
            }
        }
        
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);

        return true;
    }

    static bool HandleLookupTaxiNodeCommand(ChatHandler* handler, const char * args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in TaxiNodes.dbc
        for (uint32 id = 0; id < sTaxiNodesStore.GetNumRows(); id++)
        {
            TaxiNodesEntry const* nodeEntry = sTaxiNodesStore.LookupEntry(id);
            if (nodeEntry)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = nodeEntry->name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = nodeEntry->name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send taxinode in "id - [name] (Map:m X:x Y:y Z:z)" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_TAXINODE_ENTRY_LIST_CHAT, id, id, name.c_str(), localeNames[locale],
                            nodeEntry->map_id, nodeEntry->x, nodeEntry->y, nodeEntry->z);
                    else
                        handler->PSendSysMessage(LANG_TAXINODE_ENTRY_LIST_CONSOLE, id, name.c_str(), localeNames[locale],
                            nodeEntry->map_id, nodeEntry->x, nodeEntry->y, nodeEntry->z);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOTAXINODEFOUND);

        return true;
    }

    // Find tele in game_tele order by name
    static bool HandleLookupTeleCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
        {
            handler->SendSysMessage(LANG_COMMAND_TELE_PARAMETER);
            handler->SetSentErrorMessage(true);
            return false;
        }

        char const* str = strtok((char*)args, " ");
        if (!str)
            return false;

        std::string namePart = str;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        std::ostringstream reply;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);
        bool limitReached = false;

        GameTeleContainer const & teleMap = sObjectMgr->GetGameTeleMap();
        for (GameTeleContainer::const_iterator itr = teleMap.begin(); itr != teleMap.end(); ++itr)
        {
            GameTele const* tele = &itr->second;

            if (tele->wnameLow.find(wNamePart) == std::wstring::npos)
                continue;

            if (maxResults && count++ == maxResults)
            {
                limitReached = true;
                break;
            }

            if (handler->GetSession())
                reply << "  |cffffffff|Htele:" << itr->first << "|h[" << tele->name << "]|h|r\n";
            else
                reply << "  " << itr->first << ' ' << tele->name << "\n";
        }

        if (reply.str().empty())
            handler->SendSysMessage(LANG_COMMAND_TELE_NOLOCATION);
        else
            handler->PSendSysMessage(LANG_COMMAND_TELE_LOCATION, reply.str().c_str());

        if (limitReached)
            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);

        return true;
    }

    static bool HandleLookupTitleCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL in console call
        Player* target = handler->getSelectedPlayer();

        // title name have single string arg for player name
        char const* targetName = target ? target->GetName().c_str() : "NAME";

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        uint32 counter = 0;                                     // Counter for figure out that we found smth.
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in CharTitles.dbc
        for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
            if (titleInfo)
            {
                int locale = handler->GetSessionDbcLocale();
                std::string name = titleInfo->nameMale[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = titleInfo->nameMale[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && counter == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    char const* knownStr = target && target->HasTitle(titleInfo) ? handler->GetTrinityString(LANG_KNOWN) : "";

                    char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->bit_index
                        ? handler->GetTrinityString(LANG_ACTIVE)
                        : "";

                    char titleNameStr[80];
                    snprintf(titleNameStr, 80, name.c_str(), targetName);

                    // send title in "id (idx:idx) - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->bit_index, id, titleNameStr, localeNames[locale], knownStr, activeStr);
                    else
                        handler->PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->bit_index, titleNameStr, localeNames[locale], knownStr, activeStr);

                    ++counter;
                }
            }
        }
        if (counter == 0)  // if counter == 0 then we found nth
            handler->SendSysMessage(LANG_COMMAND_NOTITLEFOUND);

        return true;
    }

    static bool HandleLookupMapCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        uint32 counter = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);
        uint8 locale = handler->GetSession() ? handler->GetSession()->GetSessionDbcLocale() : sWorld->GetDefaultDbcLocale();

        // search in Map.dbc
        for (uint32 id = 0; id < sMapStore.GetNumRows(); id++)
        {
            if (MapEntry const* mapInfo = sMapStore.LookupEntry(id))
            {
                std::string name = mapInfo->name[locale];
                if (name.empty())
                    continue;

                if (Utf8FitTo(name, wNamePart) && locale < TOTAL_LOCALES)
                {
                    if (maxResults && counter == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    std::ostringstream ss;
                    ss << id << " - [" << name << ']';

                    if (mapInfo->IsContinent())
                        ss << handler->GetTrinityString(LANG_CONTINENT);

                    handler->SendSysMessage(ss.str().c_str());

                    ++counter;
                }
            }
        }

        if (!counter)
            handler->SendSysMessage(LANG_COMMAND_NOMAPFOUND);

        return true;
    }

    static bool HandleLookupPlayerIpCommand(ChatHandler* handler, char const* args)
    {
        std::string ip;
        int32 limit;
        char* limitStr;

        Player* target = handler->getSelectedPlayer();
        if (!*args)
        {
            // NULL only if used from console
            if (!target || target == handler->GetSession()->GetPlayer())
                return false;

            ip = target->GetSession()->GetRemoteAddress();
            limit = -1;
        }
        else
        {
            ip = strtok((char*)args, " ");
            limitStr = strtok(NULL, " ");
            limit = limitStr ? atoi(limitStr) : -1;
        }

        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_IP);
        stmt->setString(0, ip);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    static bool HandleLookupPlayerAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string account = strtok((char*)args, " ");
        char* limitStr = strtok(NULL, " ");
        int32 limit = limitStr ? atoi(limitStr) : -1;

        if (!AccountMgr::normalizeString
            (account))
            return false;

        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_NAME);
        stmt->setString(0, account);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    static bool HandleLookupPlayerEmailCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string email = strtok((char*)args, " ");
        char* limitStr = strtok(NULL, " ");
        int32 limit = limitStr ? atoi(limitStr) : -1;

        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_EMAIL);
        stmt->setString(0, email);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    static bool LookupPlayerSearchCommand(PreparedQueryResult result, int32 limit, ChatHandler* handler)
    {
        if (!result)
        {
            handler->PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        int32 counter = 0;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

		const char* name_races[RACE_DRAENEI] = {"Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome", "Troll", "", "Blood Elf", "Draenei"};
		const char* name_classes[CLASS_DRUID] = {"Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Death Knight", "Shaman", "Mage", "Warlock", "", "Druid"};

        do
        {
            if (maxResults && count++ == maxResults)
            {
                handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                return true;
            }

            Field* fields           = result->Fetch();
            uint32 accountId        = fields[0].GetUInt32();
            std::string accountName = fields[1].GetString();

            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_GUID_NAME_BY_ACC);
            stmt->setUInt32(0, accountId);
            PreparedQueryResult result2 = CharacterDatabase.Query(stmt);

            if (result2)
            {
                handler->PSendSysMessage(LANG_LOOKUP_PLAYER_ACCOUNT, accountName.c_str(), accountId);

                do
                {
                    Field* characterFields  = result2->Fetch();
                    uint32 guid             = characterFields[0].GetUInt32();
                    std::string name        = characterFields[1].GetString();
					uint8 plevel = 0, prace = 0, pclass = 0;
					bool online = (ObjectAccessor::FindPlayerInOrOutOfWorld(MAKE_NEW_GUID(guid, 0, HIGHGUID_PLAYER)) != NULL);

					if (const GlobalPlayerData* gpd = sWorld->GetGlobalPlayerData(guid))
					{
						plevel = gpd->level;
						prace = gpd->race;
						pclass = gpd->playerClass;
					}

					if (plevel > 0 && prace > 0 && prace <= RACE_DRAENEI && pclass > 0 && pclass <= CLASS_DRUID)
						handler->PSendSysMessage("  %s (GUID %u) - %s - %s - %u%s", name.c_str(), guid, name_races[prace-1], name_classes[pclass-1], plevel, (online ? " - online" : ""));
					else
						handler->PSendSysMessage(LANG_LOOKUP_PLAYER_CHARACTER, name.c_str(), guid);
                    ++counter;
                }
                while (result2->NextRow() && (limit == -1 || counter < limit));
            }
        }
        while (result->NextRow());

        if (counter == 0) // empty accounts only
        {
            handler->PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        return true;
    }

};

void AddSC_lookup_commandscript()
{
    new lookup_commandscript();
}