/*
 * Copyright (C) 
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

#include "AccountMgr.h"
#include "Chat.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "GameEventMgr.h"
#include "GossipDef.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "PoolMgr.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "Transport.h"
#include "UpdateMask.h"
#include "Util.h"
#include "Vehicle.h"
#include "WaypointManager.h"
#include "World.h"

ScriptMapMap sSpellScripts;
ScriptMapMap sEventScripts;
ScriptMapMap sWaypointScripts;

std::string GetScriptsTableNameByType(ScriptsType type)
{
    std::string res = "";
    switch (type)
    {
        case SCRIPTS_SPELL:         res = "spell_scripts";      break;
        case SCRIPTS_EVENT:         res = "event_scripts";      break;
        case SCRIPTS_WAYPOINT:      res = "waypoint_scripts";   break;
        default: break;
    }
    return res;
}

ScriptMapMap* GetScriptsMapByType(ScriptsType type)
{
    ScriptMapMap* res = NULL;
    switch (type)
    {
        case SCRIPTS_SPELL:         res = &sSpellScripts;       break;
        case SCRIPTS_EVENT:         res = &sEventScripts;       break;
        case SCRIPTS_WAYPOINT:      res = &sWaypointScripts;    break;
        default: break;
    }
    return res;
}

std::string GetScriptCommandName(ScriptCommands command)
{
    std::string res = "";
    switch (command)
    {
        case SCRIPT_COMMAND_TALK: res = "SCRIPT_COMMAND_TALK"; break;
        case SCRIPT_COMMAND_EMOTE: res = "SCRIPT_COMMAND_EMOTE"; break;
        case SCRIPT_COMMAND_FIELD_SET: res = "SCRIPT_COMMAND_FIELD_SET"; break;
        case SCRIPT_COMMAND_MOVE_TO: res = "SCRIPT_COMMAND_MOVE_TO"; break;
        case SCRIPT_COMMAND_FLAG_SET: res = "SCRIPT_COMMAND_FLAG_SET"; break;
        case SCRIPT_COMMAND_FLAG_REMOVE: res = "SCRIPT_COMMAND_FLAG_REMOVE"; break;
        case SCRIPT_COMMAND_TELEPORT_TO: res = "SCRIPT_COMMAND_TELEPORT_TO"; break;
        case SCRIPT_COMMAND_QUEST_EXPLORED: res = "SCRIPT_COMMAND_QUEST_EXPLORED"; break;
        case SCRIPT_COMMAND_KILL_CREDIT: res = "SCRIPT_COMMAND_KILL_CREDIT"; break;
        case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT: res = "SCRIPT_COMMAND_RESPAWN_GAMEOBJECT"; break;
        case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE: res = "SCRIPT_COMMAND_TEMP_SUMMON_CREATURE"; break;
        case SCRIPT_COMMAND_OPEN_DOOR: res = "SCRIPT_COMMAND_OPEN_DOOR"; break;
        case SCRIPT_COMMAND_CLOSE_DOOR: res = "SCRIPT_COMMAND_CLOSE_DOOR"; break;
        case SCRIPT_COMMAND_ACTIVATE_OBJECT: res = "SCRIPT_COMMAND_ACTIVATE_OBJECT"; break;
        case SCRIPT_COMMAND_REMOVE_AURA: res = "SCRIPT_COMMAND_REMOVE_AURA"; break;
        case SCRIPT_COMMAND_CAST_SPELL: res = "SCRIPT_COMMAND_CAST_SPELL"; break;
        case SCRIPT_COMMAND_PLAY_SOUND: res = "SCRIPT_COMMAND_PLAY_SOUND"; break;
        case SCRIPT_COMMAND_CREATE_ITEM: res = "SCRIPT_COMMAND_CREATE_ITEM"; break;
        case SCRIPT_COMMAND_DESPAWN_SELF: res = "SCRIPT_COMMAND_DESPAWN_SELF"; break;
        case SCRIPT_COMMAND_LOAD_PATH: res = "SCRIPT_COMMAND_LOAD_PATH"; break;
        case SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT: res = "SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT"; break;
        case SCRIPT_COMMAND_KILL: res = "SCRIPT_COMMAND_KILL"; break;
        // SunwellCore only
        case SCRIPT_COMMAND_ORIENTATION: res = "SCRIPT_COMMAND_ORIENTATION"; break;
        case SCRIPT_COMMAND_EQUIP: res = "SCRIPT_COMMAND_EQUIP"; break;
        case SCRIPT_COMMAND_MODEL: res = "SCRIPT_COMMAND_MODEL"; break;
        case SCRIPT_COMMAND_CLOSE_GOSSIP: res = "SCRIPT_COMMAND_CLOSE_GOSSIP"; break;
        case SCRIPT_COMMAND_PLAYMOVIE: res = "SCRIPT_COMMAND_PLAYMOVIE"; break;
        default:
        {
            char sz[32];
            sprintf(sz, "Unknown command: %d", command);
            res = sz;
            break;
        }
    }
    return res;
}

std::string ScriptInfo::GetDebugInfo() const
{
    char sz[256];
    sprintf(sz, "%s ('%s' script id: %u)", GetScriptCommandName(command).c_str(), GetScriptsTableNameByType(type).c_str(), id);
    return std::string(sz);
}

bool normalizePlayerName(std::string& name)
{
    if (name.empty())
        return false;

    wchar_t wstr_buf[MAX_INTERNAL_PLAYER_NAME+1];
    size_t wstr_len = MAX_INTERNAL_PLAYER_NAME;

    if (!Utf8toWStr(name, &wstr_buf[0], wstr_len))
        return false;

    wstr_buf[0] = wcharToUpper(wstr_buf[0]);
    for (size_t i = 1; i < wstr_len; ++i)
        wstr_buf[i] = wcharToLower(wstr_buf[i]);

	// if there's "gm" at the end, uppercase it!
	if (wstr_len>=2 && wstr_buf[wstr_len-2]==L'g' && wstr_buf[wstr_len-1]==L'm')
	{
		wstr_buf[wstr_len-2]=L'G';
		wstr_buf[wstr_len-1]=L'M';
	}

    if (!WStrToUtf8(wstr_buf, wstr_len, name))
        return false;

    return true;
}

LanguageDesc lang_description[LANGUAGES_COUNT] =
{
    { LANG_ADDON,           0, 0                       },
    { LANG_UNIVERSAL,       0, 0                       },
    { LANG_ORCISH,        669, SKILL_LANG_ORCISH       },
    { LANG_DARNASSIAN,    671, SKILL_LANG_DARNASSIAN   },
    { LANG_TAURAHE,       670, SKILL_LANG_TAURAHE      },
    { LANG_DWARVISH,      672, SKILL_LANG_DWARVEN      },
    { LANG_COMMON,        668, SKILL_LANG_COMMON       },
    { LANG_DEMONIC,       815, SKILL_LANG_DEMON_TONGUE },
    { LANG_TITAN,         816, SKILL_LANG_TITAN        },
    { LANG_THALASSIAN,    813, SKILL_LANG_THALASSIAN   },
    { LANG_DRACONIC,      814, SKILL_LANG_DRACONIC     },
    { LANG_KALIMAG,       817, SKILL_LANG_OLD_TONGUE   },
    { LANG_GNOMISH,      7340, SKILL_LANG_GNOMISH      },
    { LANG_TROLL,        7341, SKILL_LANG_TROLL        },
    { LANG_GUTTERSPEAK, 17737, SKILL_LANG_GUTTERSPEAK  },
    { LANG_DRAENEI,     29932, SKILL_LANG_DRAENEI      },
    { LANG_ZOMBIE,          0, 0                       },
    { LANG_GNOMISH_BINARY,  0, 0                       },
    { LANG_GOBLIN_BINARY,   0, 0                       }
};

LanguageDesc const* GetLanguageDescByID(uint32 lang)
{
    for (uint8 i = 0; i < LANGUAGES_COUNT; ++i)
    {
        if (uint32(lang_description[i].lang_id) == lang)
            return &lang_description[i];
    }

    return NULL;
}

bool SpellClickInfo::IsFitToRequirements(Unit const* clicker, Unit const* clickee) const
{
    Player const* playerClicker = clicker->ToPlayer();
    if (!playerClicker)
        return true;

    Unit const* summoner = NULL;
    // Check summoners for party
    if (clickee->IsSummon())
        summoner = clickee->ToTempSummon()->GetSummoner();
    if (!summoner)
        summoner = clickee;

    // This only applies to players
    switch (userType)
    {
        case SPELL_CLICK_USER_FRIEND:
            if (!playerClicker->IsFriendlyTo(summoner))
                return false;
            break;
        case SPELL_CLICK_USER_RAID:
            if (!playerClicker->IsInRaidWith(summoner))
                return false;
            break;
        case SPELL_CLICK_USER_PARTY:
            if (!playerClicker->IsInPartyWith(summoner))
                return false;
            break;
        default:
            break;
    }

    return true;
}

ObjectMgr::ObjectMgr():
    _auctionId(1),
    _equipmentSetGuid(1),
    _itemTextId(1),
    _mailId(1),
    _hiPetNumber(1),
    _hiCharGuid(1),
    _hiCreatureGuid(1),
    _hiPetGuid(1),
    _hiVehicleGuid(1),
    _hiItemGuid(1),
    _hiGoGuid(1),
    _hiDoGuid(1),
    _hiCorpseGuid(1),
    _hiMoTransGuid(1),
    DBCLocaleIndex(LOCALE_enUS)
{
    for (uint8 i = 0; i < MAX_CLASSES; ++i)
    {
        _playerClassInfo[i] = NULL;
        for (uint8 j = 0; j < MAX_RACES; ++j)
            _playerInfo[j][i] = NULL;
    }
}

ObjectMgr::~ObjectMgr()
{
    for (QuestMap::iterator i = _questTemplates.begin(); i != _questTemplates.end(); ++i)
        delete i->second;

    for (PetLevelInfoContainer::iterator i = _petInfoStore.begin(); i != _petInfoStore.end(); ++i)
        delete[] i->second;

    // free only if loaded
    for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
    {
        if (_playerClassInfo[class_])
            delete[] _playerClassInfo[class_]->levelInfo;
        delete _playerClassInfo[class_];
    }

    for (int race = 0; race < MAX_RACES; ++race)
    {
        for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
        {
            if (_playerInfo[race][class_])
                delete[] _playerInfo[race][class_]->levelInfo;
            delete _playerInfo[race][class_];
        }
    }

    for (CacheVendorItemContainer::iterator itr = _cacheVendorItemStore.begin(); itr != _cacheVendorItemStore.end(); ++itr)
        itr->second.Clear();

    _cacheTrainerSpellStore.clear();
}

void ObjectMgr::LoadCreatureTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                               0      1                   2                   3                   4            5            6         7         8
    QueryResult result = WorldDatabase.Query("SELECT entry, difficulty_entry_1, difficulty_entry_2, difficulty_entry_3, KillCredit1, KillCredit2, modelid1, modelid2, modelid3, "
    //                                        9         10    11       12        13              14        15        16   17       18       19          20
                                             "modelid4, name, subname, IconName, gossip_menu_id, minlevel, maxlevel, exp, faction, npcflag, speed_walk, speed_run, "
    //                                        21     22    23         24              25               26            27             28          29          30
                                             "scale, rank, dmgschool, BaseAttackTime, RangeAttackTime, BaseVariance, RangeVariance, unit_class, unit_flags, unit_flags2, "
    //                                        31            32      33            34             35             36            37
                                             "dynamicflags, family, trainer_type, trainer_spell, trainer_class, trainer_race, type, "
    //                                        38          39      40              41        42           43           44           45           46           47           48
                                             "type_flags, lootid, pickpocketloot, skinloot, resistance1, resistance2, resistance3, resistance4, resistance5, resistance6, spell1, "
    //                                        49      50      51      52      53      54      55      56              57         58       59       60      61
                                             "spell2, spell3, spell4, spell5, spell6, spell7, spell8, PetSpellDataId, VehicleId, mingold, maxgold, AIName, MovementType, "
    //                                        62           63           64              65            66             67              68                  69
                                             "InhabitType, HoverHeight, HealthModifier, ManaModifier, ArmorModifier, DamageModifier, ExperienceModifier, RacialLeader, "
    //                                        70          71          72          73          74          75
                                             "questItem1, questItem2, questItem3, questItem4, questItem5, questItem6, "
    //                                        76          77           78                    79           80
                                             "movementId, RegenHealth, mechanic_immune_mask, flags_extra, ScriptName "
                                             "FROM creature_template ");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature template definitions. DB table `creature_template` is empty.");
        return;
    }

    _creatureTemplateStore.rehash(result->GetRowCount());
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        LoadCreatureTemplate(fields);
        ++count;
    }
    while (result->NextRow());

	// pussywizard:
	{
		uint32 max = 0;
		for (CreatureTemplateContainer::const_iterator itr = _creatureTemplateStore.begin(); itr != _creatureTemplateStore.end(); ++itr)
			if (itr->first > max)
				max = itr->first;
		if (max)
		{
			_creatureTemplateStoreFast.clear();
			_creatureTemplateStoreFast.resize(max+1, NULL);
			for (CreatureTemplateContainer::iterator itr = _creatureTemplateStore.begin(); itr != _creatureTemplateStore.end(); ++itr)
				_creatureTemplateStoreFast[itr->first] = &(itr->second);
		}
	}

    // Checking needs to be done after loading because of the difficulty self referencing
    for (CreatureTemplateContainer::iterator itr = _creatureTemplateStore.begin(); itr != _creatureTemplateStore.end(); ++itr)
    {
        CheckCreatureTemplate(&itr->second);
        itr->second.InitializeQueryData();
    }

    sLog->outString(">> Loaded %u creature definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureTemplate(Field* fields)
{
    uint32 entry = fields[0].GetUInt32();

    CreatureTemplate& creatureTemplate = _creatureTemplateStore[entry];

    creatureTemplate.Entry = entry;

    for (uint8 i = 0; i < MAX_KILL_CREDIT; ++i)
        creatureTemplate.KillCredit[i] = fields[4 + i].GetUInt32();

    creatureTemplate.Modelid1         = fields[6].GetUInt32();
    creatureTemplate.Modelid2         = fields[7].GetUInt32();
    creatureTemplate.Modelid3         = fields[8].GetUInt32();
    creatureTemplate.Modelid4         = fields[9].GetUInt32();
    creatureTemplate.Name             = fields[10].GetString();
    creatureTemplate.Title            = fields[11].GetString();
    creatureTemplate.IconName         = fields[12].GetString();
    creatureTemplate.GossipMenuId     = fields[13].GetUInt32();
    creatureTemplate.minlevel         = fields[14].GetUInt8();
    creatureTemplate.maxlevel         = fields[15].GetUInt8();
    creatureTemplate.expansion        = uint32(fields[16].GetInt16());
    creatureTemplate.faction          = fields[17].GetUInt16();
    creatureTemplate.npcflag          = fields[18].GetUInt32();
    creatureTemplate.speed_walk       = fields[19].GetFloat();
    creatureTemplate.speed_run        = fields[20].GetFloat();
    creatureTemplate.scale            = fields[21].GetFloat();
    creatureTemplate.rank             = fields[22].GetUInt8();
    creatureTemplate.dmgschool        = uint32(fields[23].GetInt8());
    creatureTemplate.BaseAttackTime   = fields[24].GetUInt32();
    creatureTemplate.RangeAttackTime  = fields[25].GetUInt32();
    creatureTemplate.BaseVariance     = fields[26].GetFloat();
    creatureTemplate.RangeVariance    = fields[27].GetFloat();
    creatureTemplate.unit_class       = fields[28].GetUInt8();
    creatureTemplate.unit_flags       = fields[29].GetUInt32();
    creatureTemplate.unit_flags2      = fields[30].GetUInt32();
    creatureTemplate.dynamicflags     = fields[31].GetUInt32();
    creatureTemplate.family           = CreatureFamily(fields[32].GetUInt8());
    creatureTemplate.trainer_type     = fields[33].GetUInt8();
    creatureTemplate.trainer_spell    = fields[34].GetUInt32();
    creatureTemplate.trainer_class    = fields[35].GetUInt8();
    creatureTemplate.trainer_race     = fields[36].GetUInt8();
    creatureTemplate.type             = fields[37].GetUInt8();
    creatureTemplate.type_flags       = fields[38].GetUInt32();
    creatureTemplate.lootid           = fields[39].GetUInt32();
    creatureTemplate.pickpocketLootId = fields[40].GetUInt32();
    creatureTemplate.SkinLootId       = fields[41].GetUInt32();

    for (uint8 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        creatureTemplate.resistance[i] = fields[42 + i - 1].GetInt16();

    for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
        creatureTemplate.spells[i] = fields[48 + i].GetUInt32();

    creatureTemplate.PetSpellDataId = fields[56].GetUInt32();
    creatureTemplate.VehicleId      = fields[57].GetUInt32();
    creatureTemplate.mingold        = fields[58].GetUInt32();
    creatureTemplate.maxgold        = fields[59].GetUInt32();
    creatureTemplate.AIName         = fields[60].GetString();
    creatureTemplate.MovementType   = fields[61].GetUInt8();
    creatureTemplate.InhabitType    = fields[62].GetUInt8();
    creatureTemplate.HoverHeight    = fields[63].GetFloat();
    creatureTemplate.ModHealth      = fields[64].GetFloat();
    creatureTemplate.ModMana        = fields[65].GetFloat();
    creatureTemplate.ModArmor       = fields[66].GetFloat();
    creatureTemplate.ModDamage      = fields[67].GetFloat();
    creatureTemplate.ModExperience  = fields[68].GetFloat();
    creatureTemplate.RacialLeader   = fields[69].GetBool();

    for (uint8 i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
        creatureTemplate.questItems[i] = fields[70 + i].GetUInt32();

    creatureTemplate.movementId         = fields[76].GetUInt32();
    creatureTemplate.RegenHealth        = fields[77].GetBool();
    creatureTemplate.MechanicImmuneMask = fields[78].GetUInt32();
    creatureTemplate.flags_extra        = fields[79].GetUInt32();
    creatureTemplate.ScriptID           = GetScriptId(fields[80].GetCString());
}

void ObjectMgr::LoadCreatureTemplateAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                                0       1       2      3       4       5      6
    QueryResult result = WorldDatabase.Query("SELECT entry, path_id, mount, bytes1, bytes2, emote, auras FROM creature_template_addon");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature template addon definitions. DB table `creature_template_addon` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            sLog->outErrorDb("Creature template (Entry: %u) does not exist but has a record in `creature_template_addon`", entry);
            continue;
        }

        CreatureAddon& creatureAddon = _creatureTemplateAddonStore[entry];

        creatureAddon.path_id = fields[1].GetUInt32();
        creatureAddon.mount   = fields[2].GetUInt32();
        creatureAddon.bytes1  = fields[3].GetUInt32();
        creatureAddon.bytes2  = fields[4].GetUInt32();
        creatureAddon.emote   = fields[5].GetUInt32();

        Tokenizer tokens(fields[6].GetString(), ' ');
        uint8 i = 0;
        creatureAddon.auras.resize(tokens.size());
        for (Tokenizer::const_iterator itr = tokens.begin(); itr != tokens.end(); ++itr)
        {
            SpellInfo const* AdditionalSpellInfo = sSpellMgr->GetSpellInfo(uint32(atol(*itr)));
            if (!AdditionalSpellInfo)
            {
                sLog->outErrorDb("Creature (Entry: %u) has wrong spell %u defined in `auras` field in `creature_template_addon`.", entry, uint32(atol(*itr)));
                continue;
            }
            creatureAddon.auras[i++] = uint32(atol(*itr));
        }

        if (creatureAddon.mount)
        {
            if (!sCreatureDisplayInfoStore.LookupEntry(creatureAddon.mount))
            {
                sLog->outErrorDb("Creature (Entry: %u) has invalid displayInfoId (%u) for mount defined in `creature_template_addon`", entry, creatureAddon.mount);
                creatureAddon.mount = 0;
            }
        }

        if (!sEmotesStore.LookupEntry(creatureAddon.emote))
        {
            sLog->outErrorDb("Creature (Entry: %u) has invalid emote (%u) defined in `creature_addon`.", entry, creatureAddon.emote);
            creatureAddon.emote = 0;
        }

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u creature template addons in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::CheckCreatureTemplate(CreatureTemplate const* cInfo)
{
    if (!cInfo)
        return;

    bool ok = true;                                     // bool to allow continue outside this loop

    FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(cInfo->faction);
    if (!factionTemplate)
        sLog->outErrorDb("Creature (Entry: %u) has non-existing faction template (%u).", cInfo->Entry, cInfo->faction);

    // used later for scale
    CreatureDisplayInfoEntry const* displayScaleEntry = NULL;

    if (cInfo->Modelid1)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid1);
        if (!displayEntry)
        {
            sLog->outErrorDb("Creature (Entry: %u) lists non-existing Modelid1 id (%u), this can crash the client.", cInfo->Entry, cInfo->Modelid1);
            const_cast<CreatureTemplate*>(cInfo)->Modelid1 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid1);
        if (!modelInfo)
            sLog->outErrorDb("No model data exist for `Modelid1` = %u listed by creature (Entry: %u).", cInfo->Modelid1, cInfo->Entry);
    }

    if (cInfo->Modelid2)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid2);
        if (!displayEntry)
        {
            sLog->outErrorDb("Creature (Entry: %u) lists non-existing Modelid2 id (%u), this can crash the client.", cInfo->Entry, cInfo->Modelid2);
            const_cast<CreatureTemplate*>(cInfo)->Modelid2 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid2);
        if (!modelInfo)
            sLog->outErrorDb("No model data exist for `Modelid2` = %u listed by creature (Entry: %u).", cInfo->Modelid2, cInfo->Entry);
    }

    if (cInfo->Modelid3)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid3);
        if (!displayEntry)
        {
            sLog->outErrorDb("Creature (Entry: %u) lists non-existing Modelid3 id (%u), this can crash the client.", cInfo->Entry, cInfo->Modelid3);
            const_cast<CreatureTemplate*>(cInfo)->Modelid3 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid3);
        if (!modelInfo)
            sLog->outErrorDb("No model data exist for `Modelid3` = %u listed by creature (Entry: %u).", cInfo->Modelid3, cInfo->Entry);
    }

    if (cInfo->Modelid4)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid4);
        if (!displayEntry)
        {
            sLog->outErrorDb("Creature (Entry: %u) lists non-existing Modelid4 id (%u), this can crash the client.", cInfo->Entry, cInfo->Modelid4);
            const_cast<CreatureTemplate*>(cInfo)->Modelid4 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid4);
        if (!modelInfo)
            sLog->outErrorDb("No model data exist for `Modelid4` = %u listed by creature (Entry: %u).", cInfo->Modelid4, cInfo->Entry);
    }

    if (!displayScaleEntry)
        sLog->outErrorDb("Creature (Entry: %u) does not have any existing display id in Modelid1/Modelid2/Modelid3/Modelid4.", cInfo->Entry);

    for (uint8 k = 0; k < MAX_KILL_CREDIT; ++k)
    {
        if (cInfo->KillCredit[k])
        {
            if (!GetCreatureTemplate(cInfo->KillCredit[k]))
            {
                sLog->outErrorDb("Creature (Entry: %u) lists non-existing creature entry %u in `KillCredit%d`.", cInfo->Entry, cInfo->KillCredit[k], k + 1);
                const_cast<CreatureTemplate*>(cInfo)->KillCredit[k] = 0;
            }
        }
    }

    if (!cInfo->unit_class || ((1 << (cInfo->unit_class-1)) & CLASSMASK_ALL_CREATURES) == 0)
    {
        sLog->outErrorDb("Creature (Entry: %u) has invalid unit_class (%u) in creature_template. Set to 1 (UNIT_CLASS_WARRIOR).", cInfo->Entry, cInfo->unit_class);
        const_cast<CreatureTemplate*>(cInfo)->unit_class = UNIT_CLASS_WARRIOR;
    }

    if (cInfo->dmgschool >= MAX_SPELL_SCHOOL)
    {
        sLog->outErrorDb("Creature (Entry: %u) has invalid spell school value (%u) in `dmgschool`.", cInfo->Entry, cInfo->dmgschool);
        const_cast<CreatureTemplate*>(cInfo)->dmgschool = SPELL_SCHOOL_NORMAL;
    }

    if (cInfo->BaseAttackTime == 0)
        const_cast<CreatureTemplate*>(cInfo)->BaseAttackTime  = BASE_ATTACK_TIME;

    if (cInfo->RangeAttackTime == 0)
        const_cast<CreatureTemplate*>(cInfo)->RangeAttackTime = BASE_ATTACK_TIME;

    if ((cInfo->npcflag & UNIT_NPC_FLAG_TRAINER) && cInfo->trainer_type >= MAX_TRAINER_TYPE)
        sLog->outErrorDb("Creature (Entry: %u) has wrong trainer type %u.", cInfo->Entry, cInfo->trainer_type);

    if (cInfo->speed_walk == 0.0f)
    {
        sLog->outErrorDb("Creature (Entry: %u) has wrong value (%f) in speed_walk, set to 1.", cInfo->Entry, cInfo->speed_walk);
        const_cast<CreatureTemplate*>(cInfo)->speed_walk = 1.0f;
    }

    if (cInfo->speed_run == 0.0f)
    {
        sLog->outErrorDb("Creature (Entry: %u) has wrong value (%f) in speed_run, set to 1.14286.", cInfo->Entry, cInfo->speed_run);
        const_cast<CreatureTemplate*>(cInfo)->speed_run = 1.14286f;
    }

    if (cInfo->type && !sCreatureTypeStore.LookupEntry(cInfo->type))
    {
        sLog->outErrorDb("Creature (Entry: %u) has invalid creature type (%u) in `type`.", cInfo->Entry, cInfo->type);
        const_cast<CreatureTemplate*>(cInfo)->type = CREATURE_TYPE_HUMANOID;
    }

    // must exist or used hidden but used in data horse case
    if (cInfo->family && !sCreatureFamilyStore.LookupEntry(cInfo->family) && cInfo->family != CREATURE_FAMILY_HORSE_CUSTOM)
    {
        sLog->outErrorDb("Creature (Entry: %u) has invalid creature family (%u) in `family`.", cInfo->Entry, cInfo->family);
        const_cast<CreatureTemplate*>(cInfo)->family = 0;
    }

    if (cInfo->InhabitType <= 0 || cInfo->InhabitType > INHABIT_ANYWHERE)
    {
        sLog->outErrorDb("Creature (Entry: %u) has wrong value (%u) in `InhabitType`, creature will not correctly walk/swim/fly.", cInfo->Entry, cInfo->InhabitType);
        const_cast<CreatureTemplate*>(cInfo)->InhabitType = INHABIT_ANYWHERE;
    }

    if (cInfo->HoverHeight < 0.0f)
    {
        sLog->outErrorDb("Creature (Entry: %u) has wrong value (%f) in `HoverHeight`", cInfo->Entry, cInfo->HoverHeight);
        const_cast<CreatureTemplate*>(cInfo)->HoverHeight = 1.0f;
    }

    if (cInfo->VehicleId)
    {
        VehicleEntry const* vehId = sVehicleStore.LookupEntry(cInfo->VehicleId);
        if (!vehId)
        {
             sLog->outErrorDb("Creature (Entry: %u) has a non-existing VehicleId (%u). This *WILL* cause the client to freeze!", cInfo->Entry, cInfo->VehicleId);
             const_cast<CreatureTemplate*>(cInfo)->VehicleId = 0;
        }
    }

    if (cInfo->PetSpellDataId)
    {
        CreatureSpellDataEntry const* spellDataId = sCreatureSpellDataStore.LookupEntry(cInfo->PetSpellDataId);
        if (!spellDataId)
            sLog->outErrorDb("Creature (Entry: %u) has non-existing PetSpellDataId (%u).", cInfo->Entry, cInfo->PetSpellDataId);
    }

    for (uint8 j = 0; j < MAX_CREATURE_SPELLS; ++j)
    {
        if (cInfo->spells[j] && !sSpellMgr->GetSpellInfo(cInfo->spells[j]))
        {
            sLog->outErrorDb("Creature (Entry: %u) has non-existing Spell%d (%u), set to 0.", cInfo->Entry, j+1, cInfo->spells[j]);
            const_cast<CreatureTemplate*>(cInfo)->spells[j] = 0;
        }
    }

    if (cInfo->MovementType >= MAX_DB_MOTION_TYPE)
    {
        sLog->outErrorDb("Creature (Entry: %u) has wrong movement generator type (%u), ignored and set to IDLE.", cInfo->Entry, cInfo->MovementType);
        const_cast<CreatureTemplate*>(cInfo)->MovementType = IDLE_MOTION_TYPE;
    }

    /// if not set custom creature scale then load scale from CreatureDisplayInfo.dbc
    if (cInfo->scale <= 0.0f)
    {
        if (displayScaleEntry)
            const_cast<CreatureTemplate*>(cInfo)->scale = displayScaleEntry->scale;
        else
            const_cast<CreatureTemplate*>(cInfo)->scale = 1.0f;
    }

    if (cInfo->expansion > (MAX_EXPANSIONS - 1))
    {
        sLog->outErrorDb("Table `creature_template` lists creature (Entry: %u) with expansion %u. Ignored and set to 0.", cInfo->Entry, cInfo->expansion);
        const_cast<CreatureTemplate*>(cInfo)->expansion = 0;
    }

    if (uint32 badFlags = (cInfo->flags_extra & ~CREATURE_FLAG_EXTRA_DB_ALLOWED))
    {
        sLog->outErrorDb("Table `creature_template` lists creature (Entry: %u) with disallowed `flags_extra` %u, removing incorrect flag.", cInfo->Entry, badFlags);
        const_cast<CreatureTemplate*>(cInfo)->flags_extra &= CREATURE_FLAG_EXTRA_DB_ALLOWED;
    }

    const_cast<CreatureTemplate*>(cInfo)->ModDamage *= Creature::_GetDamageMod(cInfo->rank);
}

void ObjectMgr::LoadCreatureAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                                0       1       2      3       4       5      6
    QueryResult result = WorldDatabase.Query("SELECT guid, path_id, mount, bytes1, bytes2, emote, auras FROM creature_addon");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature addon definitions. DB table `creature_addon` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();

        CreatureData const* creData = GetCreatureData(guid);
        if (!creData)
        {
            sLog->outErrorDb("Creature (GUID: %u) does not exist but has a record in `creature_addon`", guid);
            continue;
        }

        CreatureAddon& creatureAddon = _creatureAddonStore[guid];

        creatureAddon.path_id = fields[1].GetUInt32();
        if (creData->movementType == WAYPOINT_MOTION_TYPE && !creatureAddon.path_id)
        {
            const_cast<CreatureData*>(creData)->movementType = IDLE_MOTION_TYPE;
            sLog->outErrorDb("Creature (GUID %u) has movement type set to WAYPOINT_MOTION_TYPE but no path assigned", guid);
        }

        creatureAddon.mount   = fields[2].GetUInt32();
        creatureAddon.bytes1  = fields[3].GetUInt32();
        creatureAddon.bytes2  = fields[4].GetUInt32();
        creatureAddon.emote   = fields[5].GetUInt32();

        Tokenizer tokens(fields[6].GetString(), ' ');
        uint8 i = 0;
        creatureAddon.auras.resize(tokens.size());
		for (Tokenizer::const_iterator itr = tokens.begin(); itr != tokens.end(); ++itr)
        {
            SpellInfo const* AdditionalSpellInfo = sSpellMgr->GetSpellInfo(uint32(atol(*itr)));
            if (!AdditionalSpellInfo)
            {
                sLog->outErrorDb("Creature (GUID: %u) has wrong spell %u defined in `auras` field in `creature_addon`.", guid, uint32(atol(*itr)));
                continue;
            }
            creatureAddon.auras[i++] = uint32(atol(*itr));
        }

        if (creatureAddon.mount)
        {
            if (!sCreatureDisplayInfoStore.LookupEntry(creatureAddon.mount))
            {
                sLog->outErrorDb("Creature (GUID: %u) has invalid displayInfoId (%u) for mount defined in `creature_addon`", guid, creatureAddon.mount);
                creatureAddon.mount = 0;
            }
        }

        if (!sEmotesStore.LookupEntry(creatureAddon.emote))
        {
            sLog->outErrorDb("Creature (GUID: %u) has invalid emote (%u) defined in `creature_addon`.", guid, creatureAddon.emote);
            creatureAddon.emote = 0;
        }

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u creature addons in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadGameObjectAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                               0     1                 2
    QueryResult result = WorldDatabase.Query("SELECT guid, invisibilityType, invisibilityValue FROM gameobject_addon");


    if (!result)
    {
        sLog->outString(">> Loaded 0 gameobject addon definitions. DB table `gameobject_addon` is empty.");
		sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 guid = fields[0].GetUInt32();

        const GameObjectData* goData = GetGOData(guid);
        if (!goData)
        {
            sLog->outErrorDb("GameObject (GUID: %u) does not exist but has a record in `gameobject_addon`", guid);
            continue;
        }

        GameObjectAddon& gameObjectAddon = _gameObjectAddonStore[guid];
        gameObjectAddon.invisibilityType = InvisibilityType(fields[1].GetUInt8());
        gameObjectAddon.InvisibilityValue = fields[2].GetUInt32();

        if (gameObjectAddon.invisibilityType >= TOTAL_INVISIBILITY_TYPES)
        {
            sLog->outErrorDb("GameObject (GUID: %u) has invalid InvisibilityType in `gameobject_addon`", guid);
            gameObjectAddon.invisibilityType = INVISIBILITY_GENERAL;
            gameObjectAddon.InvisibilityValue = 0;
        }

        if (gameObjectAddon.invisibilityType && !gameObjectAddon.InvisibilityValue)
        {
			sLog->outErrorDb("GameObject (GUID: %u) has InvisibilityType set but has no InvisibilityValue in `gameobject_addon`, set to 1", guid);
            gameObjectAddon.InvisibilityValue = 1;
        }

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u gameobject addons in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
	sLog->outString();
}

GameObjectAddon const* ObjectMgr::GetGameObjectAddon(uint32 lowguid)
{
    GameObjectAddonContainer::const_iterator itr = _gameObjectAddonStore.find(lowguid);
    if (itr != _gameObjectAddonStore.end())
        return &(itr->second);

    return NULL;
}

CreatureAddon const* ObjectMgr::GetCreatureAddon(uint32 lowguid)
{
    CreatureAddonContainer::const_iterator itr = _creatureAddonStore.find(lowguid);
    if (itr != _creatureAddonStore.end())
        return &(itr->second);

    return NULL;
}

CreatureAddon const* ObjectMgr::GetCreatureTemplateAddon(uint32 entry)
{
    CreatureAddonContainer::const_iterator itr = _creatureTemplateAddonStore.find(entry);
    if (itr != _creatureTemplateAddonStore.end())
        return &(itr->second);

    return NULL;
}

EquipmentInfo const* ObjectMgr::GetEquipmentInfo(uint32 entry, int8& id)
{
    EquipmentInfoContainer::const_iterator itr = _equipmentInfoStore.find(entry);
    if (itr == _equipmentInfoStore.end())
        return NULL;

    if (itr->second.empty())
        return NULL;

    if (id == -1) // select a random element
    {
        EquipmentInfoContainerInternal::const_iterator ritr = itr->second.begin();
        std::advance(ritr, urand(0u, itr->second.size() - 1));
        id = std::distance(itr->second.begin(), ritr) + 1;
        return &ritr->second;
    }
    else
    {
        EquipmentInfoContainerInternal::const_iterator itr2 = itr->second.find(id);
        if (itr2 != itr->second.end())
            return &itr2->second;
    }

    return NULL;
}

void ObjectMgr::LoadEquipmentTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0         1       2       3       4
    QueryResult result = WorldDatabase.Query("SELECT CreatureID, ID, ItemID1, ItemID2, ItemID3 FROM creature_equip_template");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature equipment templates. DB table `creature_equip_template` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            sLog->outError("Creature template (Entry: %u) does not exist but has a record in `creature_equip_template`", entry);
            continue;
        }

        uint8 id = fields[1].GetUInt8();
        if (!id)
        {
			sLog->outError("Creature equipment template with id 0 found for creature %u, skipped.", entry);
            continue;
        }

        EquipmentInfo& equipmentInfo = _equipmentInfoStore[entry][id];

        equipmentInfo.ItemEntry[0] = fields[2].GetUInt32();
        equipmentInfo.ItemEntry[1] = fields[3].GetUInt32();
        equipmentInfo.ItemEntry[2] = fields[4].GetUInt32();

        for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
        {
            if (!equipmentInfo.ItemEntry[i])
                continue;

            ItemEntry const* dbcItem = sItemStore.LookupEntry(equipmentInfo.ItemEntry[i]);

            if (!dbcItem)
            {
                sLog->outErrorDb("Unknown item (entry=%u) in creature_equip_template.itemEntry%u for entry = %u, forced to 0.",
                    equipmentInfo.ItemEntry[i], i+1, entry);
                equipmentInfo.ItemEntry[i] = 0;
                continue;
            }

            if (dbcItem->InventoryType != INVTYPE_WEAPON &&
                dbcItem->InventoryType != INVTYPE_SHIELD &&
                dbcItem->InventoryType != INVTYPE_RANGED &&
                dbcItem->InventoryType != INVTYPE_2HWEAPON &&
                dbcItem->InventoryType != INVTYPE_WEAPONMAINHAND &&
                dbcItem->InventoryType != INVTYPE_WEAPONOFFHAND &&
                dbcItem->InventoryType != INVTYPE_HOLDABLE &&
                dbcItem->InventoryType != INVTYPE_THROWN &&
                dbcItem->InventoryType != INVTYPE_RANGEDRIGHT)
            {
                sLog->outErrorDb("Item (entry=%u) in creature_equip_template.itemEntry%u for entry = %u is not equipable in a hand, forced to 0.",
                    equipmentInfo.ItemEntry[i], i+1, entry);
                equipmentInfo.ItemEntry[i] = 0;
            }
        }

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u equipment templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

CreatureModelInfo const* ObjectMgr::GetCreatureModelInfo(uint32 modelId)
{
    CreatureModelContainer::const_iterator itr = _creatureModelStore.find(modelId);
    if (itr != _creatureModelStore.end())
        return &(itr->second);

    return NULL;
}

uint32 ObjectMgr::ChooseDisplayId(CreatureTemplate const* cinfo, CreatureData const* data /*= NULL*/)
{
    // Load creature model (display id)
    if (data && data->displayid)
        return data->displayid;

    return cinfo->GetRandomValidModelId();
}

void ObjectMgr::ChooseCreatureFlags(const CreatureTemplate* cinfo, uint32& npcflag, uint32& unit_flags, uint32& dynamicflags, const CreatureData* data /*= NULL*/)
{
    npcflag = cinfo->npcflag;
    unit_flags = cinfo->unit_flags;
    dynamicflags = cinfo->dynamicflags;

    if (data)
    {
        if (data->npcflag)
            npcflag = data->npcflag;

        if (data->unit_flags)
            unit_flags = data->unit_flags;

        if (data->dynamicflags)
            dynamicflags = data->dynamicflags;
    }
}

CreatureModelInfo const* ObjectMgr::GetCreatureModelRandomGender(uint32* displayID)
{
    CreatureModelInfo const* modelInfo = GetCreatureModelInfo(*displayID);
    if (!modelInfo)
        return NULL;

    // If a model for another gender exists, 50% chance to use it
    if (modelInfo->modelid_other_gender != 0 && urand(0, 1) == 0)
    {
        CreatureModelInfo const* minfo_tmp = GetCreatureModelInfo(modelInfo->modelid_other_gender);
        if (!minfo_tmp)
            sLog->outErrorDb("Model (Entry: %u) has modelid_other_gender %u not found in table `creature_model_info`. ", *displayID, modelInfo->modelid_other_gender);
        else
        {
            // Model ID changed
            *displayID = modelInfo->modelid_other_gender;
            return minfo_tmp;
        }
    }

    return modelInfo;
}

void ObjectMgr::LoadCreatureModelInfo()
{
    uint32 oldMSTime = getMSTime();
    //                                                   0             1             2          3               4
    QueryResult result = WorldDatabase.Query("SELECT DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender FROM creature_model_info");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature model definitions. DB table `creature_model_info` is empty.");
        sLog->outString();
        return;
    }

    _creatureModelStore.rehash(result->GetRowCount());
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 modelId = fields[0].GetUInt32();

        CreatureModelInfo& modelInfo = _creatureModelStore[modelId];

        modelInfo.bounding_radius      = fields[1].GetFloat();
        modelInfo.combat_reach         = fields[2].GetFloat();
        modelInfo.gender               = fields[3].GetUInt8();
        modelInfo.modelid_other_gender = fields[4].GetUInt32();

        // Checks

        if (!sCreatureDisplayInfoStore.LookupEntry(modelId))
            sLog->outErrorDb("Table `creature_model_info` has model for not existed display id (%u).", modelId);

        if (modelInfo.gender > GENDER_NONE)
        {
            sLog->outErrorDb("Table `creature_model_info` has wrong gender (%u) for display id (%u).", uint32(modelInfo.gender), modelId);
            modelInfo.gender = GENDER_MALE;
        }

        if (modelInfo.modelid_other_gender && !sCreatureDisplayInfoStore.LookupEntry(modelInfo.modelid_other_gender))
        {
            sLog->outErrorDb("Table `creature_model_info` has not existed alt.gender model (%u) for existed display id (%u).", modelInfo.modelid_other_gender, modelId);
            modelInfo.modelid_other_gender = 0;
        }

        if (modelInfo.combat_reach < 0.1f)
            modelInfo.combat_reach = DEFAULT_COMBAT_REACH;

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u creature model based info in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadTempSummons()
{
    uint32 oldMSTime = getMSTime();

    _tempSummonDataStore.clear();   // needed for reload case

    //                                               0           1             2        3      4           5           6           7            8           9
    QueryResult result = WorldDatabase.Query("SELECT summonerId, summonerType, groupId, entry, position_x, position_y, position_z, orientation, summonType, summonTime FROM creature_summon_groups");

    if (!result)
    {
        sLog->outString(">> Loaded 0 temp summons. DB table `creature_summon_groups` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 summonerId               = fields[0].GetUInt32();
        SummonerType summonerType       = SummonerType(fields[1].GetUInt8());
        uint8 group                     = fields[2].GetUInt8();

        switch (summonerType)
        {
            case SUMMONER_TYPE_CREATURE:
                if (!GetCreatureTemplate(summonerId))
                {
                    sLog->outError("Table `creature_summon_groups` has summoner with non existing entry %u for creature summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            case SUMMONER_TYPE_GAMEOBJECT:
                if (!GetGameObjectTemplate(summonerId))
                {
                    sLog->outError("Table `creature_summon_groups` has summoner with non existing entry %u for gameobject summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            case SUMMONER_TYPE_MAP:
                if (!sMapStore.LookupEntry(summonerId))
                {
                    sLog->outError("Table `creature_summon_groups` has summoner with non existing entry %u for map summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            default:
                sLog->outError("Table `creature_summon_groups` has unhandled summoner type %u for summoner %u, skipped.", summonerType, summonerId);
                continue;
        }

        TempSummonData data;
        data.entry                      = fields[3].GetUInt32();

        if (!GetCreatureTemplate(data.entry))
        {
            sLog->outError("Table `creature_summon_groups` has creature in group [Summoner ID: %u, Summoner Type: %u, Group ID: %u] with non existing creature entry %u, skipped.", summonerId, summonerType, group, data.entry);
            continue;
        }

        float posX                      = fields[4].GetFloat();
        float posY                      = fields[5].GetFloat();
        float posZ                      = fields[6].GetFloat();
        float orientation               = fields[7].GetFloat();

        data.pos.Relocate(posX, posY, posZ, orientation);

        data.type                       = TempSummonType(fields[8].GetUInt8());

        if (data.type > TEMPSUMMON_MANUAL_DESPAWN)
        {
            sLog->outError("Table `creature_summon_groups` has unhandled temp summon type %u in group [Summoner ID: %u, Summoner Type: %u, Group ID: %u] for creature entry %u, skipped.", data.type, summonerId, summonerType, group, data.entry);
            continue;
        }

        data.time                       = fields[9].GetUInt32();

        TempSummonGroupKey key(summonerId, summonerType, group);
        _tempSummonDataStore[key].push_back(data);

        ++count;

    } while (result->NextRow());

    sLog->outString(">> Loaded %u temp summons in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatures()
{
    uint32 oldMSTime = getMSTime();

    //                                               0              1   2    3           4           5           6            7        8             9              10
    QueryResult result = WorldDatabase.Query("SELECT creature.guid, id, map, position_x, position_y, position_z, orientation, modelid, equipment_id, spawntimesecs, spawndist, "
    //   11               12         13       14            15         16          17          18                19                   20
        "currentwaypoint, curhealth, curmana, MovementType, phaseMask, eventEntry, pool_entry, creature.npcflag, creature.unit_flags, creature.dynamicflags "
        "FROM creature "
        "LEFT OUTER JOIN game_event_creature ON creature.guid = game_event_creature.guid "
        "LEFT OUTER JOIN pool_creature ON creature.guid = pool_creature.guid");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 creatures. DB table `creature` is empty.");
        sLog->outString();
        return;
    }

    _creatureDataStore.rehash(result->GetRowCount());
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 guid         = fields[0].GetUInt32();
        uint32 entry        = fields[1].GetUInt32();

        CreatureTemplate const* cInfo = GetCreatureTemplate(entry);
        if (!cInfo)
        {
            sLog->outErrorDb("Table `creature` has creature (GUID: %u) with non existing creature entry %u, skipped.", guid, entry);
            continue;
        }

        CreatureData& data = _creatureDataStore[guid];
        data.id             = entry;
        data.mapid          = fields[2].GetUInt16();
        data.posX           = fields[3].GetFloat();
        data.posY           = fields[4].GetFloat();
        data.posZ           = fields[5].GetFloat();
        data.orientation    = fields[6].GetFloat();
        data.displayid      = fields[7].GetUInt32();
        data.equipmentId    = fields[8].GetInt8();
        data.spawntimesecs  = fields[9].GetUInt32();
        data.spawndist      = fields[10].GetFloat();
        data.currentwaypoint= fields[11].GetUInt32();
        data.curhealth      = fields[12].GetUInt32();
        data.curmana        = fields[13].GetUInt32();
        data.movementType   = fields[14].GetUInt8();
        data.phaseMask      = fields[15].GetUInt32();
        int16 gameEvent     = fields[16].GetInt8();
        uint32 PoolId       = fields[17].GetUInt32();
        data.npcflag        = fields[18].GetUInt32();
        data.unit_flags     = fields[19].GetUInt32();
        data.dynamicflags   = fields[20].GetUInt32();

        MapEntry const* mapEntry = sMapStore.LookupEntry(data.mapid);
        if (!mapEntry)
        {
            sLog->outErrorDb("Table `creature` have creature (GUID: %u) that spawned at not existed map (Id: %u), skipped.", guid, data.mapid);
            continue;
        }

        // -1 random, 0 no equipment,
        if (data.equipmentId != 0)
        {
            if (!GetEquipmentInfo(data.id, data.equipmentId))
            {
                sLog->outErrorDb("Table `creature` have creature (Entry: %u) with equipment_id %u not found in table `creature_equip_template`, set to no equipment.", data.id, data.equipmentId);
                data.equipmentId = 0;
            }
        }

        if (data.spawndist < 0.0f)
        {
            sLog->outErrorDb("Table `creature` have creature (GUID: %u Entry: %u) with `spawndist`< 0, set to 0.", guid, data.id);
            data.spawndist = 0.0f;
        }
        else if (data.movementType == RANDOM_MOTION_TYPE)
        {
            if (data.spawndist == 0.0f)
            {
                sLog->outErrorDb("Table `creature` have creature (GUID: %u Entry: %u) with `MovementType`=1 (random movement) but with `spawndist`=0, replace by idle movement type (0).", guid, data.id);
                data.movementType = IDLE_MOTION_TYPE;
            }
        }
        else if (data.movementType == IDLE_MOTION_TYPE)
        {
            if (data.spawndist != 0.0f)
            {
                sLog->outErrorDb("Table `creature` have creature (GUID: %u Entry: %u) with `MovementType`=0 (idle) have `spawndist`<>0, set to 0.", guid, data.id);
                data.spawndist = 0.0f;
            }
        }

        if (data.phaseMask == 0)
        {
            sLog->outErrorDb("Table `creature` have creature (GUID: %u Entry: %u) with `phaseMask`=0 (not visible for anyone), set to 1.", guid, data.id);
            data.phaseMask = 1;
        }

        if (sWorld->getBoolConfig(CONFIG_CALCULATE_CREATURE_ZONE_AREA_DATA))
        {
            uint32 zoneId = sMapMgr->GetZoneId(data.mapid, data.posX, data.posY, data.posZ);
            uint32 areaId = sMapMgr->GetAreaId(data.mapid, data.posX, data.posY, data.posZ);

            PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_ZONE_AREA_DATA);

            stmt->setUInt32(0, zoneId);
            stmt->setUInt32(1, areaId);
            stmt->setUInt64(2, guid);

            WorldDatabase.Execute(stmt);
        }

        // Add to grid if not managed by the game event or pool system
        if (gameEvent == 0 && PoolId == 0)
            AddCreatureToGrid(guid, &data);

        ++count;

    } while (result->NextRow());

    sLog->outString(">> Loaded %u creatures in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::AddCreatureToGrid(uint32 guid, CreatureData const* data)
{
    uint8 mask = 1;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->posX, data->posY);
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapid, i)][cellCoord.GetId()];
            cell_guids.creatures.insert(guid);
        }
    }
}

void ObjectMgr::RemoveCreatureFromGrid(uint32 guid, CreatureData const* data)
{
    uint8 mask = 1;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->posX, data->posY);
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapid, i)][cellCoord.GetId()];
            cell_guids.creatures.erase(guid);
        }
    }
}

uint32 ObjectMgr::AddGOData(uint32 entry, uint32 mapId, float x, float y, float z, float o, uint32 spawntimedelay, float rotation0, float rotation1, float rotation2, float rotation3)
{
    GameObjectTemplate const* goinfo = GetGameObjectTemplate(entry);
    if (!goinfo)
        return 0;

    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return 0;

    uint32 guid = GenerateLowGuid(HIGHGUID_GAMEOBJECT);
    GameObjectData& data = NewGOData(guid);
    data.id             = entry;
    data.mapid          = mapId;
    data.posX           = x;
    data.posY           = y;
    data.posZ           = z;
    data.orientation    = o;
    data.rotation.x     = rotation0;
    data.rotation.y     = rotation1;
    data.rotation.z     = rotation2;
    data.rotation.w     = rotation3;
    data.spawntimesecs  = spawntimedelay;
    data.animprogress   = 100;
    data.go_state       = GO_STATE_READY;
    data.phaseMask      = PHASEMASK_NORMAL;
    data.artKit         = goinfo->type == GAMEOBJECT_TYPE_CAPTURE_POINT ? 21 : 0;
    data.dbData = false;

    AddGameobjectToGrid(guid, &data);

    // Spawn if necessary (loaded grids only)
    // We use spawn coords to spawn
    if (map->IsGridLoaded(x, y))
    {
        GameObject* go = sObjectMgr->IsGameObjectStaticTransport(data.id) ? new StaticTransport() : new GameObject();
        if (!go->LoadGameObjectFromDB(guid, map))
        {
            sLog->outError("AddGOData: cannot add gameobject entry %u to map", entry);
            delete go;
            return 0;
        }
    }

    ;//sLog->outDebug(LOG_FILTER_MAPS, "AddGOData: dbguid %u entry %u map %u x %f y %f z %f o %f", guid, entry, mapId, x, y, z, o);

    return guid;
}

bool ObjectMgr::MoveCreData(uint32 guid, uint32 mapId, Position pos)
{
    CreatureData& data = NewOrExistCreatureData(guid);
    if (!data.id)
        return false;

    RemoveCreatureFromGrid(guid, &data);
    if (data.posX == pos.GetPositionX() && data.posY == pos.GetPositionY() && data.posZ == pos.GetPositionZ())
        return true;
    data.posX = pos.GetPositionX();
    data.posY = pos.GetPositionY();
    data.posZ = pos.GetPositionZ();
    data.orientation = pos.GetOrientation();
    AddCreatureToGrid(guid, &data);

    // Spawn if necessary (loaded grids only)
    if (Map* map = sMapMgr->CreateBaseMap(mapId))
    {
        // We use spawn coords to spawn
        if (map->IsGridLoaded(data.posX, data.posY))
        {
            Creature* creature = new Creature();
            if (!creature->LoadCreatureFromDB(guid, map))
            {
                sLog->outError("MoveCreData: Cannot add creature guid %u to map", guid);
                delete creature;
                return false;
            }
        }
    }
    return true;
}

uint32 ObjectMgr::AddCreData(uint32 entry, uint32 mapId, float x, float y, float z, float o, uint32 spawntimedelay)
{
    CreatureTemplate const* cInfo = GetCreatureTemplate(entry);
    if (!cInfo)
        return 0;

    uint32 level = cInfo->minlevel == cInfo->maxlevel ? cInfo->minlevel : urand(cInfo->minlevel, cInfo->maxlevel); // Only used for extracting creature base stats
    CreatureBaseStats const* stats = GetCreatureBaseStats(level, cInfo->unit_class);

    uint32 guid = GenerateLowGuid(HIGHGUID_UNIT);
    CreatureData& data = NewOrExistCreatureData(guid);
    data.id = entry;
    data.mapid = mapId;
    data.displayid = 0;
    data.equipmentId = 0;
    data.posX = x;
    data.posY = y;
    data.posZ = z;
    data.orientation = o;
    data.spawntimesecs = spawntimedelay;
    data.spawndist = 0;
    data.currentwaypoint = 0;
    data.curhealth = stats->GenerateHealth(cInfo);
    data.curmana = stats->GenerateMana(cInfo);
    data.movementType = cInfo->MovementType;
    data.phaseMask = PHASEMASK_NORMAL;
    data.dbData = false;
    data.npcflag = cInfo->npcflag;
    data.unit_flags = cInfo->unit_flags;
    data.dynamicflags = cInfo->dynamicflags;

    AddCreatureToGrid(guid, &data);

    // Spawn if necessary (loaded grids only)
    if (Map* map = sMapMgr->CreateBaseMap(mapId))
    {
        // We use spawn coords to spawn
        if (!map->IsRemovalGrid(x, y))
        {
            Creature* creature = new Creature();
            if (!creature->LoadCreatureFromDB(guid, map))
            {
                sLog->outError("AddCreature: Cannot add creature entry %u to map", entry);
                delete creature;
                return 0;
            }
        }
    }

    return guid;
}

void ObjectMgr::LoadGameobjects()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = 0;

    //                                                0                1   2    3           4           5           6
    QueryResult result = WorldDatabase.Query("SELECT gameobject.guid, id, map, position_x, position_y, position_z, orientation, "
    //   7          8          9          10         11             12            13     14         15          16
        "rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, phaseMask, eventEntry, pool_entry "
        "FROM gameobject LEFT OUTER JOIN game_event_gameobject ON gameobject.guid = game_event_gameobject.guid "
        "LEFT OUTER JOIN pool_gameobject ON gameobject.guid = pool_gameobject.guid");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 gameobjects. DB table `gameobject` is empty.");
        sLog->outString();
        return;
    }

    _gameObjectDataStore.rehash(result->GetRowCount());
    do
    {
        Field* fields = result->Fetch();

        uint32 guid         = fields[0].GetUInt32();
        uint32 entry        = fields[1].GetUInt32();

        GameObjectTemplate const* gInfo = GetGameObjectTemplate(entry);
        if (!gInfo)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u) with non existing gameobject entry %u, skipped.", guid, entry);
            continue;
        }

        if (!gInfo->displayId)
        {
            switch (gInfo->type)
            {
                case GAMEOBJECT_TYPE_TRAP:
                case GAMEOBJECT_TYPE_SPELL_FOCUS:
                    break;
                default:
                    sLog->outErrorDb("Gameobject (GUID: %u Entry %u GoType: %u) doesn't have a displayId (%u), not loaded.", guid, entry, gInfo->type, gInfo->displayId);
                    break;
            }
        }

        if (gInfo->displayId && !sGameObjectDisplayInfoStore.LookupEntry(gInfo->displayId))
        {
            sLog->outErrorDb("Gameobject (GUID: %u Entry %u GoType: %u) has an invalid displayId (%u), not loaded.", guid, entry, gInfo->type, gInfo->displayId);
            continue;
        }

        GameObjectData& data = _gameObjectDataStore[guid];

        data.id             = entry;
        data.mapid          = fields[2].GetUInt16();
        data.posX           = fields[3].GetFloat();
        data.posY           = fields[4].GetFloat();
        data.posZ           = fields[5].GetFloat();
        data.orientation    = fields[6].GetFloat();
        data.rotation.x     = fields[7].GetFloat();
        data.rotation.y     = fields[8].GetFloat();
        data.rotation.z     = fields[9].GetFloat();
        data.rotation.w     = fields[10].GetFloat();
        data.spawntimesecs  = fields[11].GetInt32();

        MapEntry const* mapEntry = sMapStore.LookupEntry(data.mapid);
        if (!mapEntry)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) spawned on a non-existed map (Id: %u), skip", guid, data.id, data.mapid);
            continue;
        }

        if (data.spawntimesecs == 0 && gInfo->IsDespawnAtAction())
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with `spawntimesecs` (0) value, but the gameobejct is marked as despawnable at action.", guid, data.id);
        }

        data.animprogress   = fields[12].GetUInt8();
        data.artKit         = 0;

        uint32 go_state     = fields[13].GetUInt8();
        if (go_state >= MAX_GO_STATE)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid `state` (%u) value, skip", guid, data.id, go_state);
            continue;
        }
        data.go_state       = GOState(go_state);

        data.phaseMask      = fields[14].GetUInt32();
        int16 gameEvent     = fields[15].GetInt8();
        uint32 PoolId        = fields[16].GetUInt32();

        if (data.rotation.x < -1.0f || data.rotation.x > 1.0f)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid rotationX (%f) value, skip", guid, data.id, data.rotation.x);
            continue;
        }

        if (data.rotation.y < -1.0f || data.rotation.y > 1.0f)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid rotationY (%f) value, skip", guid, data.id, data.rotation.y);
            continue;
        }

        if (data.rotation.z < -1.0f || data.rotation.z > 1.0f)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid rotationZ (%f) value, skip", guid, data.id, data.rotation.z);
            continue;
        }

        if (data.rotation.w < -1.0f || data.rotation.w > 1.0f)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid rotationW (%f) value, skip", guid, data.id, data.rotation.w);
            continue;
        }

        if (!MapManager::IsValidMapCoord(data.mapid, data.posX, data.posY, data.posZ, data.orientation))
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with invalid coordinates, skip", guid, data.id);
            continue;
        }

        if (data.phaseMask == 0)
        {
            sLog->outErrorDb("Table `gameobject` has gameobject (GUID: %u Entry: %u) with `phaseMask`=0 (not visible for anyone), set to 1.", guid, data.id);
            data.phaseMask = 1;
        }

        if (sWorld->getBoolConfig(CONFIG_CALCULATE_GAMEOBJECT_ZONE_AREA_DATA))
        {
            uint32 zoneId = sMapMgr->GetZoneId(data.mapid, data.posX, data.posY, data.posZ);
            uint32 areaId = sMapMgr->GetAreaId(data.mapid, data.posX, data.posY, data.posZ);

            PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_GAMEOBJECT_ZONE_AREA_DATA);

            stmt->setUInt32(0, zoneId);
            stmt->setUInt32(1, areaId);
            stmt->setUInt64(2, guid);

            WorldDatabase.Execute(stmt);
        }

        if (gameEvent == 0 && PoolId == 0)                      // if not this is to be managed by GameEvent System or Pool system
            AddGameobjectToGrid(guid, &data);
        ++count;
    } while (result->NextRow());

    sLog->outString(">> Loaded %lu gameobjects in %u ms", (unsigned long)_gameObjectDataStore.size(), GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::AddGameobjectToGrid(uint32 guid, GameObjectData const* data)
{
    uint8 mask = 1;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->posX, data->posY);
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapid, i)][cellCoord.GetId()];
            cell_guids.gameobjects.insert(guid);
        }
    }
}

void ObjectMgr::RemoveGameobjectFromGrid(uint32 guid, GameObjectData const* data)
{
    uint8 mask = 1;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->posX, data->posY);
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapid, i)][cellCoord.GetId()];
            cell_guids.gameobjects.erase(guid);
        }
    }
}

uint64 ObjectMgr::GetPlayerGUIDByName(std::string const& name) const
{
	// Get data from global storage
	if (uint32 guidLow = sWorld->GetGlobalPlayerGUID(name))
		return MAKE_NEW_GUID(guidLow, 0, HIGHGUID_PLAYER);
	return 0;
}

bool ObjectMgr::GetPlayerNameByGUID(uint64 guid, std::string &name) const
{
	// Get data from global storage
	if (GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(GUID_LOPART(guid)))
	{
		name = playerData->name;
		return true;
	}

    return false;
}

TeamId ObjectMgr::GetPlayerTeamIdByGUID(uint64 guid) const
{
	// xinef: Get data from global storage
	if (GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(GUID_LOPART(guid)))
		return Player::TeamIdForRace(playerData->race);

    return TEAM_NEUTRAL;
}

uint32 ObjectMgr::GetPlayerAccountIdByGUID(uint64 guid) const
{
	// xinef: Get data from global storage
	if (GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(GUID_LOPART(guid)))
		return playerData->accountId;

    return 0;
}

uint32 ObjectMgr::GetPlayerAccountIdByPlayerName(const std::string& name) const
{
	// Get data from global storage
	if (uint32 guidLow = sWorld->GetGlobalPlayerGUID(name))
		if (GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(guidLow))
			return playerData->accountId;

    return 0;
}

void ObjectMgr::LoadItemTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0      1       2               3              4        5        6       7          8         9        10        11           12
    QueryResult result = WorldDatabase.Query("SELECT entry, class, subclass, SoundOverrideSubclass, name, displayid, Quality, Flags, FlagsExtra, BuyCount, BuyPrice, SellPrice, InventoryType, "
    //                                              13              14           15          16             17               18                19              20
                                             "AllowableClass, AllowableRace, ItemLevel, RequiredLevel, RequiredSkill, RequiredSkillRank, requiredspell, requiredhonorrank, "
    //                                              21                      22                       23               24        25          26             27           28
                                             "RequiredCityRank, RequiredReputationFaction, RequiredReputationRank, maxcount, stackable, ContainerSlots, StatsCount, stat_type1, "
    //                                            29           30          31           32          33           34          35           36          37           38
                                             "stat_value1, stat_type2, stat_value2, stat_type3, stat_value3, stat_type4, stat_value4, stat_type5, stat_value5, stat_type6, "
    //                                            39           40          41           42           43          44           45           46           47
                                             "stat_value6, stat_type7, stat_value7, stat_type8, stat_value8, stat_type9, stat_value9, stat_type10, stat_value10, "
    //                                                   48                    49           50        51        52         53        54         55      56      57        58
                                             "ScalingStatDistribution, ScalingStatValue, dmg_min1, dmg_max1, dmg_type1, dmg_min2, dmg_max2, dmg_type2, armor, holy_res, fire_res, "
    //                                            59          60         61          62       63       64            65            66          67               68
                                             "nature_res, frost_res, shadow_res, arcane_res, delay, ammo_type, RangedModRange, spellid_1, spelltrigger_1, spellcharges_1, "
    //                                              69              70                71                 72                 73           74               75
                                             "spellppmRate_1, spellcooldown_1, spellcategory_1, spellcategorycooldown_1, spellid_2, spelltrigger_2, spellcharges_2, "
    //                                              76               77              78                  79                 80           81               82
                                             "spellppmRate_2, spellcooldown_2, spellcategory_2, spellcategorycooldown_2, spellid_3, spelltrigger_3, spellcharges_3, "
    //                                              83               84              85                  86                 87           88               89
                                             "spellppmRate_3, spellcooldown_3, spellcategory_3, spellcategorycooldown_3, spellid_4, spelltrigger_4, spellcharges_4, "
    //                                              90               91              92                  93                  94          95               96
                                             "spellppmRate_4, spellcooldown_4, spellcategory_4, spellcategorycooldown_4, spellid_5, spelltrigger_5, spellcharges_5, "
    //                                              97               98              99                  100                 101        102         103       104          105
                                             "spellppmRate_5, spellcooldown_5, spellcategory_5, spellcategorycooldown_5, bonding, description, PageText, LanguageID, PageMaterial, "
    //                                            106       107     108      109          110            111       112     113         114       115   116     117
                                             "startquest, lockid, Material, sheath, RandomProperty, RandomSuffix, block, itemset, MaxDurability, area, Map, BagFamily, "
    //                                            118        119                        120                 121             122            123        124            125
                                             "TotemCategory, RequiredDisenchantSkill, ArmorDamageModifier, duration, ItemLimitCategory, HolidayId, ScriptName, DisenchantID, "
    //                                           126        127            128          129
                                             "FoodType, minMoneyLoot, maxMoneyLoot, flagsCustom FROM item_template");

    if (!result)
    {
        sLog->outString(">> Loaded 0 item templates. DB table `item_template` is empty.");
        sLog->outString();
        return;
    }

    _itemTemplateStore.rehash(result->GetRowCount());
    uint32 count = 0;
    bool enforceDBCAttributes = sWorld->getBoolConfig(CONFIG_DBC_ENFORCE_ITEM_ATTRIBUTES);

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        ItemTemplate& itemTemplate = _itemTemplateStore[entry];

        itemTemplate.ItemId                    = entry;
        itemTemplate.Class                     = uint32(fields[1].GetUInt8());
        itemTemplate.SubClass                  = uint32(fields[2].GetUInt8());
        itemTemplate.SoundOverrideSubclass     = int32(fields[3].GetInt8());
        itemTemplate.Name1                     = fields[4].GetString();
        itemTemplate.DisplayInfoID             = fields[5].GetUInt32();
        itemTemplate.Quality                   = uint32(fields[6].GetUInt8());
        itemTemplate.Flags                     = fields[7].GetUInt32();
        itemTemplate.Flags2                    = fields[8].GetUInt32();
        itemTemplate.BuyCount                  = uint32(fields[9].GetUInt8());
        itemTemplate.BuyPrice                  = int32(fields[10].GetInt64());
        itemTemplate.SellPrice                 = fields[11].GetUInt32();
        itemTemplate.InventoryType             = uint32(fields[12].GetUInt8());
        itemTemplate.AllowableClass            = fields[13].GetInt32();
        itemTemplate.AllowableRace             = fields[14].GetInt32();
        itemTemplate.ItemLevel                 = uint32(fields[15].GetUInt16());
        itemTemplate.RequiredLevel             = uint32(fields[16].GetUInt8());
        itemTemplate.RequiredSkill             = uint32(fields[17].GetUInt16());
        itemTemplate.RequiredSkillRank         = uint32(fields[18].GetUInt16());
        itemTemplate.RequiredSpell             = fields[19].GetUInt32();
        itemTemplate.RequiredHonorRank         = fields[20].GetUInt32();
        itemTemplate.RequiredCityRank          = fields[21].GetUInt32();
        itemTemplate.RequiredReputationFaction = uint32(fields[22].GetUInt16());
        itemTemplate.RequiredReputationRank    = uint32(fields[23].GetUInt16());
        itemTemplate.MaxCount                  = fields[24].GetInt32();
        itemTemplate.Stackable                 = fields[25].GetInt32();
        itemTemplate.ContainerSlots            = uint32(fields[26].GetUInt8());
        itemTemplate.StatsCount                = uint32(fields[27].GetUInt8());

        for (uint8 i = 0; i < itemTemplate.StatsCount; ++i)
        {
            itemTemplate.ItemStat[i].ItemStatType  = uint32(fields[28 + i*2].GetUInt8());
            itemTemplate.ItemStat[i].ItemStatValue = int32(fields[29 + i*2].GetInt16());
        }

        itemTemplate.ScalingStatDistribution = uint32(fields[48].GetUInt16());
        itemTemplate.ScalingStatValue        = fields[49].GetInt32();

        for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
        {
            itemTemplate.Damage[i].DamageMin  = fields[50 + i*3].GetFloat();
            itemTemplate.Damage[i].DamageMax  = fields[51 + i*3].GetFloat();
            itemTemplate.Damage[i].DamageType = uint32(fields[52 + i*3].GetUInt8());
        }

        itemTemplate.Armor          = uint32(fields[56].GetUInt16());
        itemTemplate.HolyRes        = uint32(fields[57].GetUInt8());
        itemTemplate.FireRes        = uint32(fields[58].GetUInt8());
        itemTemplate.NatureRes      = uint32(fields[59].GetUInt8());
        itemTemplate.FrostRes       = uint32(fields[60].GetUInt8());
        itemTemplate.ShadowRes      = uint32(fields[61].GetUInt8());
        itemTemplate.ArcaneRes      = uint32(fields[62].GetUInt8());
        itemTemplate.Delay          = uint32(fields[63].GetUInt16());
        itemTemplate.AmmoType       = uint32(fields[64].GetUInt8());
        itemTemplate.RangedModRange = fields[65].GetFloat();

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            itemTemplate.Spells[i].SpellId               = fields[66 + i*7  ].GetInt32();
            itemTemplate.Spells[i].SpellTrigger          = uint32(fields[67 + i*7].GetUInt8());
            itemTemplate.Spells[i].SpellCharges          = int32(fields[68 + i*7].GetInt16());
            itemTemplate.Spells[i].SpellPPMRate          = fields[69 + i*7].GetFloat();
            itemTemplate.Spells[i].SpellCooldown         = fields[70 + i*7].GetInt32();
            itemTemplate.Spells[i].SpellCategory         = uint32(fields[71 + i*7].GetUInt16());
            itemTemplate.Spells[i].SpellCategoryCooldown = fields[72 + i*7].GetInt32();
        }

        itemTemplate.Bonding        = uint32(fields[101].GetUInt8());
        itemTemplate.Description    = fields[102].GetString();
        itemTemplate.PageText       = fields[103].GetUInt32();
        itemTemplate.LanguageID     = uint32(fields[104].GetUInt8());
        itemTemplate.PageMaterial   = uint32(fields[105].GetUInt8());
        itemTemplate.StartQuest     = fields[106].GetUInt32();
        itemTemplate.LockID         = fields[107].GetUInt32();
        itemTemplate.Material       = int32(fields[108].GetInt8());
        itemTemplate.Sheath         = uint32(fields[109].GetUInt8());
        itemTemplate.RandomProperty = fields[110].GetUInt32();
        itemTemplate.RandomSuffix   = fields[111].GetInt32();
        itemTemplate.Block          = fields[112].GetUInt32();
        itemTemplate.ItemSet        = fields[113].GetUInt32();
        itemTemplate.MaxDurability  = uint32(fields[114].GetUInt16());
        itemTemplate.Area           = fields[115].GetUInt32();
        itemTemplate.Map            = uint32(fields[116].GetUInt16());
        itemTemplate.BagFamily      = fields[117].GetUInt32();
        itemTemplate.TotemCategory  = fields[118].GetUInt32();

        itemTemplate.RequiredDisenchantSkill = uint32(fields[119].GetInt16());
        itemTemplate.ArmorDamageModifier     = fields[120].GetFloat();
        itemTemplate.Duration                = fields[121].GetUInt32();
        itemTemplate.ItemLimitCategory       = uint32(fields[122].GetInt16());
        itemTemplate.HolidayId               = fields[123].GetUInt32();
        itemTemplate.ScriptId                = sObjectMgr->GetScriptId(fields[124].GetCString());
        itemTemplate.DisenchantID            = fields[125].GetUInt32();
        itemTemplate.FoodType                = uint32(fields[126].GetUInt8());
        itemTemplate.MinMoneyLoot            = fields[127].GetUInt32();
        itemTemplate.MaxMoneyLoot            = fields[128].GetUInt32();
        itemTemplate.FlagsCu                 = fields[129].GetUInt32();

        // Checks

        ItemEntry const* dbcitem = sItemStore.LookupEntry(entry);

        if (dbcitem)
        {
            if (itemTemplate.Class != dbcitem->Class)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have a correct class %u, must be %u .", entry, itemTemplate.Class, dbcitem->Class);
                if (enforceDBCAttributes)
                    itemTemplate.Class = dbcitem->Class;
            }

            if (itemTemplate.SoundOverrideSubclass != dbcitem->SoundOverrideSubclass)
            {
                sLog->outError("Item (Entry: %u) does not have a correct SoundOverrideSubclass (%i), must be %i .", entry, itemTemplate.SoundOverrideSubclass, dbcitem->SoundOverrideSubclass);
                if (enforceDBCAttributes)
                    itemTemplate.SoundOverrideSubclass = dbcitem->SoundOverrideSubclass;
            }
            if (itemTemplate.Material != dbcitem->Material)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have a correct material (%i), must be %i .", entry, itemTemplate.Material, dbcitem->Material);
                if (enforceDBCAttributes)
                    itemTemplate.Material = dbcitem->Material;
            }
            if (itemTemplate.InventoryType != dbcitem->InventoryType)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have a correct inventory type (%u), must be %u .", entry, itemTemplate.InventoryType, dbcitem->InventoryType);
                if (enforceDBCAttributes)
                    itemTemplate.InventoryType = dbcitem->InventoryType;
            }
            if (itemTemplate.DisplayInfoID != dbcitem->DisplayId)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have a correct display id (%u), must be %u .", entry, itemTemplate.DisplayInfoID, dbcitem->DisplayId);
                if (enforceDBCAttributes)
                    itemTemplate.DisplayInfoID = dbcitem->DisplayId;
            }
            if (itemTemplate.Sheath != dbcitem->Sheath)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have a correct sheathid (%u), must be %u .", entry, itemTemplate.Sheath, dbcitem->Sheath);
                if (enforceDBCAttributes)
                    itemTemplate.Sheath = dbcitem->Sheath;
            }

        }
        else
            sLog->outErrorDb("Item (Entry: %u) does not exist in item.dbc! (not correct id?).", entry);

        if (itemTemplate.Class >= MAX_ITEM_CLASS)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong Class value (%u)", entry, itemTemplate.Class);
            itemTemplate.Class = ITEM_CLASS_MISC;
        }

        if (itemTemplate.SubClass >= MaxItemSubclassValues[itemTemplate.Class])
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong Subclass value (%u) for class %u", entry, itemTemplate.SubClass, itemTemplate.Class);
            itemTemplate.SubClass = 0;// exist for all item classes
        }

        if (itemTemplate.Quality >= MAX_ITEM_QUALITY)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong Quality value (%u)", entry, itemTemplate.Quality);
            itemTemplate.Quality = ITEM_QUALITY_NORMAL;
        }

        if (itemTemplate.Flags2 & ITEM_FLAGS_EXTRA_HORDE_ONLY)
        {
            if (FactionEntry const* faction = sFactionStore.LookupEntry(HORDE))
                if ((itemTemplate.AllowableRace & faction->BaseRepRaceMask[0]) == 0)
                    sLog->outErrorDb("Item (Entry: %u) has value (%u) in `AllowableRace` races, not compatible with ITEM_FLAGS_EXTRA_HORDE_ONLY (%u) in Flags field, item cannot be equipped or used by these races.",
                        entry, itemTemplate.AllowableRace, ITEM_FLAGS_EXTRA_HORDE_ONLY);

            if (itemTemplate.Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY)
                sLog->outErrorDb("Item (Entry: %u) has value (%u) in `Flags2` flags (ITEM_FLAGS_EXTRA_ALLIANCE_ONLY) and ITEM_FLAGS_EXTRA_HORDE_ONLY (%u) in Flags field, this is a wrong combination.",
                    entry, ITEM_FLAGS_EXTRA_ALLIANCE_ONLY, ITEM_FLAGS_EXTRA_HORDE_ONLY);
        }
        else if (itemTemplate.Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY)
        {
            if (FactionEntry const* faction = sFactionStore.LookupEntry(ALLIANCE))
                if ((itemTemplate.AllowableRace & faction->BaseRepRaceMask[0]) == 0)
                    sLog->outErrorDb("Item (Entry: %u) has value (%u) in `AllowableRace` races, not compatible with ITEM_FLAGS_EXTRA_ALLIANCE_ONLY (%u) in Flags field, item cannot be equipped or used by these races.",
                        entry, itemTemplate.AllowableRace, ITEM_FLAGS_EXTRA_ALLIANCE_ONLY);
        }

        if (itemTemplate.BuyCount <= 0)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong BuyCount value (%u), set to default(1).", entry, itemTemplate.BuyCount);
            itemTemplate.BuyCount = 1;
        }

        if (itemTemplate.InventoryType >= MAX_INVTYPE)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong InventoryType value (%u)", entry, itemTemplate.InventoryType);
            itemTemplate.InventoryType = INVTYPE_NON_EQUIP;
        }

        if (itemTemplate.RequiredSkill >= MAX_SKILL_TYPE)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong RequiredSkill value (%u)", entry, itemTemplate.RequiredSkill);
            itemTemplate.RequiredSkill = 0;
        }

        {
            // can be used in equip slot, as page read use in inventory, or spell casting at use
            bool req = itemTemplate.InventoryType != INVTYPE_NON_EQUIP || itemTemplate.PageText;
            if (!req)
                for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
                {
                    if (itemTemplate.Spells[j].SpellId)
                    {
                        req = true;
                        break;
                    }
                }

            if (req)
            {
                if (!(itemTemplate.AllowableClass & CLASSMASK_ALL_PLAYABLE))
                    sLog->outErrorDb("Item (Entry: %u) does not have any playable classes (%u) in `AllowableClass` and can't be equipped or used.", entry, itemTemplate.AllowableClass);

                if (!(itemTemplate.AllowableRace & RACEMASK_ALL_PLAYABLE))
                    sLog->outErrorDb("Item (Entry: %u) does not have any playable races (%u) in `AllowableRace` and can't be equipped or used.", entry, itemTemplate.AllowableRace);
            }
        }

        if (itemTemplate.RequiredSpell && !sSpellMgr->GetSpellInfo(itemTemplate.RequiredSpell))
        {
            sLog->outErrorDb("Item (Entry: %u) has a wrong (non-existing) spell in RequiredSpell (%u)", entry, itemTemplate.RequiredSpell);
            itemTemplate.RequiredSpell = 0;
        }

        if (itemTemplate.RequiredReputationRank >= MAX_REPUTATION_RANK)
            sLog->outErrorDb("Item (Entry: %u) has wrong reputation rank in RequiredReputationRank (%u), item can't be used.", entry, itemTemplate.RequiredReputationRank);

        if (itemTemplate.RequiredReputationFaction)
        {
            if (!sFactionStore.LookupEntry(itemTemplate.RequiredReputationFaction))
            {
                sLog->outErrorDb("Item (Entry: %u) has wrong (not existing) faction in RequiredReputationFaction (%u)", entry, itemTemplate.RequiredReputationFaction);
                itemTemplate.RequiredReputationFaction = 0;
            }

            if (itemTemplate.RequiredReputationRank == MIN_REPUTATION_RANK)
                sLog->outErrorDb("Item (Entry: %u) has min. reputation rank in RequiredReputationRank (0) but RequiredReputationFaction > 0, faction setting is useless.", entry);
        }

        if (itemTemplate.MaxCount < -1)
        {
            sLog->outErrorDb("Item (Entry: %u) has too large negative in maxcount (%i), replace by value (-1) no storing limits.", entry, itemTemplate.MaxCount);
            itemTemplate.MaxCount = -1;
        }

        if (itemTemplate.Stackable == 0)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong value in stackable (%i), replace by default 1.", entry, itemTemplate.Stackable);
            itemTemplate.Stackable = 1;
        }
        else if (itemTemplate.Stackable < -1)
        {
            sLog->outErrorDb("Item (Entry: %u) has too large negative in stackable (%i), replace by value (-1) no stacking limits.", entry, itemTemplate.Stackable);
            itemTemplate.Stackable = -1;
        }

        if (itemTemplate.ContainerSlots > MAX_BAG_SIZE)
        {
            sLog->outErrorDb("Item (Entry: %u) has too large value in ContainerSlots (%u), replace by hardcoded limit (%u).", entry, itemTemplate.ContainerSlots, MAX_BAG_SIZE);
            itemTemplate.ContainerSlots = MAX_BAG_SIZE;
        }

        if (itemTemplate.StatsCount > MAX_ITEM_PROTO_STATS)
        {
            sLog->outErrorDb("Item (Entry: %u) has too large value in statscount (%u), replace by hardcoded limit (%u).", entry, itemTemplate.StatsCount, MAX_ITEM_PROTO_STATS);
            itemTemplate.StatsCount = MAX_ITEM_PROTO_STATS;
        }

        for (uint8 j = 0; j < itemTemplate.StatsCount; ++j)
        {
            // for ItemStatValue != 0
            if (itemTemplate.ItemStat[j].ItemStatValue && itemTemplate.ItemStat[j].ItemStatType >= MAX_ITEM_MOD)
            {
                sLog->outErrorDb("Item (Entry: %u) has wrong (non-existing?) stat_type%d (%u)", entry, j+1, itemTemplate.ItemStat[j].ItemStatType);
                itemTemplate.ItemStat[j].ItemStatType = 0;
            }

            switch (itemTemplate.ItemStat[j].ItemStatType)
            {
                case ITEM_MOD_SPELL_HEALING_DONE:
                case ITEM_MOD_SPELL_DAMAGE_DONE:
                    sLog->outErrorDb("Item (Entry: %u) has deprecated stat_type%d (%u)", entry, j+1, itemTemplate.ItemStat[j].ItemStatType);
                    break;
                default:
                    break;
            }
        }

        for (uint8 j = 0; j < MAX_ITEM_PROTO_DAMAGES; ++j)
        {
            if (itemTemplate.Damage[j].DamageType >= MAX_SPELL_SCHOOL)
            {
                sLog->outErrorDb("Item (Entry: %u) has wrong dmg_type%d (%u)", entry, j+1, itemTemplate.Damage[j].DamageType);
                itemTemplate.Damage[j].DamageType = 0;
            }
        }

        // special format
        if ((itemTemplate.Spells[0].SpellId == 483) || (itemTemplate.Spells[0].SpellId == 55884))
        {
            // spell_1
            if (itemTemplate.Spells[0].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            {
                sLog->outErrorDb("Item (Entry: %u) has wrong item spell trigger value in spelltrigger_%d (%u) for special learning format", entry, 0+1, itemTemplate.Spells[0].SpellTrigger);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[0].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                itemTemplate.Spells[1].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }

            // spell_2 have learning spell
            if (itemTemplate.Spells[1].SpellTrigger != ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
            {
                sLog->outErrorDb("Item (Entry: %u) has wrong item spell trigger value in spelltrigger_%d (%u) for special learning format.", entry, 1+1, itemTemplate.Spells[1].SpellTrigger);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[1].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }
            else if (!itemTemplate.Spells[1].SpellId)
            {
                sLog->outErrorDb("Item (Entry: %u) does not have an expected spell in spellid_%d in special learning format.", entry, 1+1);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }
            else if (itemTemplate.Spells[1].SpellId != -1)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[1].SpellId);
                if (!spellInfo && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, itemTemplate.Spells[1].SpellId, NULL))
                {
                    sLog->outErrorDb("Item (Entry: %u) has wrong (not existing) spell in spellid_%d (%d)", entry, 1+1, itemTemplate.Spells[1].SpellId);
                    itemTemplate.Spells[0].SpellId = 0;
                    itemTemplate.Spells[1].SpellId = 0;
                    itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
                // allowed only in special format
                else if ((itemTemplate.Spells[1].SpellId == 483) || (itemTemplate.Spells[1].SpellId == 55884))
                {
                    sLog->outErrorDb("Item (Entry: %u) has broken spell in spellid_%d (%d)", entry, 1+1, itemTemplate.Spells[1].SpellId);
                    itemTemplate.Spells[0].SpellId = 0;
                    itemTemplate.Spells[1].SpellId = 0;
                    itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
            }

            // spell_3*, spell_4*, spell_5* is empty
            for (uint8 j = 2; j < MAX_ITEM_PROTO_SPELLS; ++j)
            {
                if (itemTemplate.Spells[j].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                {
                    sLog->outErrorDb("Item (Entry: %u) has wrong item spell trigger value in spelltrigger_%d (%u)", entry, j+1, itemTemplate.Spells[j].SpellTrigger);
                    itemTemplate.Spells[j].SpellId = 0;
                    itemTemplate.Spells[j].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
                else if (itemTemplate.Spells[j].SpellId != 0)
                {
                    sLog->outErrorDb("Item (Entry: %u) has wrong spell in spellid_%d (%d) for learning special format", entry, j+1, itemTemplate.Spells[j].SpellId);
                    itemTemplate.Spells[j].SpellId = 0;
                }
            }
        }
        // normal spell list
        else
        {
            for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
            {
                if (itemTemplate.Spells[j].SpellTrigger >= MAX_ITEM_SPELLTRIGGER || itemTemplate.Spells[j].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
                {
                    sLog->outErrorDb("Item (Entry: %u) has wrong item spell trigger value in spelltrigger_%d (%u)", entry, j+1, itemTemplate.Spells[j].SpellTrigger);
                    itemTemplate.Spells[j].SpellId = 0;
                    itemTemplate.Spells[j].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }

                if (itemTemplate.Spells[j].SpellId && itemTemplate.Spells[j].SpellId != -1)
                {
                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[j].SpellId);
                    if (!spellInfo && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, itemTemplate.Spells[j].SpellId, NULL))
                    {
                        sLog->outErrorDb("Item (Entry: %u) has wrong (not existing) spell in spellid_%d (%d)", entry, j+1, itemTemplate.Spells[j].SpellId);
                        itemTemplate.Spells[j].SpellId = 0;
                    }
                    // allowed only in special format
                    else if ((itemTemplate.Spells[j].SpellId == 483) || (itemTemplate.Spells[j].SpellId == 55884))
                    {
                        sLog->outErrorDb("Item (Entry: %u) has broken spell in spellid_%d (%d)", entry, j+1, itemTemplate.Spells[j].SpellId);
                        itemTemplate.Spells[j].SpellId = 0;
                    }
                }
            }
        }

        if (itemTemplate.Bonding >= MAX_BIND_TYPE)
            sLog->outErrorDb("Item (Entry: %u) has wrong Bonding value (%u)", entry, itemTemplate.Bonding);

        if (itemTemplate.PageText && !GetPageText(itemTemplate.PageText))
            sLog->outErrorDb("Item (Entry: %u) has non existing first page (Id:%u)", entry, itemTemplate.PageText);

        if (itemTemplate.LockID && !sLockStore.LookupEntry(itemTemplate.LockID))
            sLog->outErrorDb("Item (Entry: %u) has wrong LockID (%u)", entry, itemTemplate.LockID);

        if (itemTemplate.Sheath >= MAX_SHEATHETYPE)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong Sheath (%u)", entry, itemTemplate.Sheath);
            itemTemplate.Sheath = SHEATHETYPE_NONE;
        }

        if (itemTemplate.RandomProperty)
        {
            // To be implemented later
            if (itemTemplate.RandomProperty == -1)
                itemTemplate.RandomProperty = 0;

            else if (!sItemRandomPropertiesStore.LookupEntry(GetItemEnchantMod(itemTemplate.RandomProperty)))
            {
                sLog->outErrorDb("Item (Entry: %u) has unknown (wrong or not listed in `item_enchantment_template`) RandomProperty (%u)", entry, itemTemplate.RandomProperty);
                itemTemplate.RandomProperty = 0;
            }
        }

        if (itemTemplate.RandomSuffix && !sItemRandomSuffixStore.LookupEntry(GetItemEnchantMod(itemTemplate.RandomSuffix)))
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong RandomSuffix (%u)", entry, itemTemplate.RandomSuffix);
            itemTemplate.RandomSuffix = 0;
        }

        if (itemTemplate.ItemSet && !sItemSetStore.LookupEntry(itemTemplate.ItemSet))
        {
            sLog->outErrorDb("Item (Entry: %u) have wrong ItemSet (%u)", entry, itemTemplate.ItemSet);
            itemTemplate.ItemSet = 0;
        }

        if (itemTemplate.Area && !sAreaTableStore.LookupEntry(itemTemplate.Area))
            sLog->outErrorDb("Item (Entry: %u) has wrong Area (%u)", entry, itemTemplate.Area);

        if (itemTemplate.Map && !sMapStore.LookupEntry(itemTemplate.Map))
            sLog->outErrorDb("Item (Entry: %u) has wrong Map (%u)", entry, itemTemplate.Map);

        if (itemTemplate.BagFamily)
        {
            // check bits
            for (uint32 j = 0; j < sizeof(itemTemplate.BagFamily)*8; ++j)
            {
                uint32 mask = 1 << j;
                if ((itemTemplate.BagFamily & mask) == 0)
                    continue;

                ItemBagFamilyEntry const* bf = sItemBagFamilyStore.LookupEntry(j+1);
                if (!bf)
                {
                    sLog->outErrorDb("Item (Entry: %u) has bag family bit set not listed in ItemBagFamily.dbc, remove bit", entry);
                    itemTemplate.BagFamily &= ~mask;
                    continue;
                }
            }
        }

        if (itemTemplate.TotemCategory && !sTotemCategoryStore.LookupEntry(itemTemplate.TotemCategory))
            sLog->outErrorDb("Item (Entry: %u) has wrong TotemCategory (%u)", entry, itemTemplate.TotemCategory);

        if (itemTemplate.FoodType >= MAX_PET_DIET)
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong FoodType value (%u)", entry, itemTemplate.FoodType);
            itemTemplate.FoodType = 0;
        }

        if (itemTemplate.ItemLimitCategory && !sItemLimitCategoryStore.LookupEntry(itemTemplate.ItemLimitCategory))
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong LimitCategory value (%u)", entry, itemTemplate.ItemLimitCategory);
            itemTemplate.ItemLimitCategory = 0;
        }

        if (itemTemplate.HolidayId && !sHolidaysStore.LookupEntry(itemTemplate.HolidayId))
        {
            sLog->outErrorDb("Item (Entry: %u) has wrong HolidayId value (%u)", entry, itemTemplate.HolidayId);
            itemTemplate.HolidayId = 0;
        }

		if (itemTemplate.FlagsCu & ITEM_FLAGS_CU_DURATION_REAL_TIME && !itemTemplate.Duration)
        {
            sLog->outErrorDb("Item (Entry %u) has flag ITEM_FLAGS_CU_DURATION_REAL_TIME but it does not have duration limit", entry);
            itemTemplate.FlagsCu &= ~ITEM_FLAGS_CU_DURATION_REAL_TIME;
        }

		// Fill categories map
		for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
			if (itemTemplate.Spells[i].SpellId && itemTemplate.Spells[i].SpellCategory && itemTemplate.Spells[i].SpellCategoryCooldown)
			{
				SpellCategoryStore::const_iterator ct = sSpellsByCategoryStore.find(itemTemplate.Spells[i].SpellCategory);
				if (ct != sSpellsByCategoryStore.end())
				{
					const SpellCategorySet& ct_set = ct->second;
					if (ct_set.find(itemTemplate.Spells[i].SpellId) == ct_set.end())
						sSpellsByCategoryStore[itemTemplate.Spells[i].SpellCategory].insert(itemTemplate.Spells[i].SpellId);
				}
				else
					sSpellsByCategoryStore[itemTemplate.Spells[i].SpellCategory].insert(itemTemplate.Spells[i].SpellId);
			}

        ++count;
    }
    while (result->NextRow());

	// pussywizard:
	{
		uint32 max = 0;
		for (ItemTemplateContainer::const_iterator itr = _itemTemplateStore.begin(); itr != _itemTemplateStore.end(); ++itr)
			if (itr->first > max)
				max = itr->first;
		if (max)
		{
			_itemTemplateStoreFast.clear();
			_itemTemplateStoreFast.resize(max+1, NULL);
			for (ItemTemplateContainer::iterator itr = _itemTemplateStore.begin(); itr != _itemTemplateStore.end(); ++itr)
				_itemTemplateStoreFast[itr->first] = &(itr->second);
		}
	}

	for (ItemTemplateContainer::iterator itr = _itemTemplateStore.begin(); itr != _itemTemplateStore.end(); ++itr)
		itr->second.InitializeQueryData();

    // Check if item templates for DBC referenced character start outfit are present
    std::set<uint32> notFoundOutfit;
    for (uint32 i = 1; i < sCharStartOutfitStore.GetNumRows(); ++i)
    {
        CharStartOutfitEntry const* entry = sCharStartOutfitStore.LookupEntry(i);
        if (!entry)
            continue;

        for (int j = 0; j < MAX_OUTFIT_ITEMS; ++j)
        {
            if (entry->ItemId[j] <= 0)
                continue;

            uint32 item_id = entry->ItemId[j];

            if (!GetItemTemplate(item_id))
                notFoundOutfit.insert(item_id);
        }
    }

    for (std::set<uint32>::const_iterator itr = notFoundOutfit.begin(); itr != notFoundOutfit.end(); ++itr)
        sLog->outErrorDb("Item (Entry: %u) does not exist in `item_template` but is referenced in `CharStartOutfit.dbc`", *itr);

    sLog->outString(">> Loaded %u item templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

ItemTemplate const* ObjectMgr::GetItemTemplate(uint32 entry)
{
    return entry < _itemTemplateStoreFast.size() ? _itemTemplateStoreFast[entry] : NULL;
}

void ObjectMgr::LoadItemSetNames()
{
    uint32 oldMSTime = getMSTime();

    _itemSetNameStore.clear();                               // needed for reload case

    std::set<uint32> itemSetItems;

    // fill item set member ids
    for (uint32 entryId = 0; entryId < sItemSetStore.GetNumRows(); ++entryId)
    {
        ItemSetEntry const* setEntry = sItemSetStore.LookupEntry(entryId);
        if (!setEntry)
            continue;

        for (uint32 i = 0; i < MAX_ITEM_SET_ITEMS; ++i)
            if (setEntry->itemId[i])
                itemSetItems.insert(setEntry->itemId[i]);
    }

    //                                                  0        1            2
    QueryResult result = WorldDatabase.Query("SELECT `entry`, `name`, `InventoryType` FROM `item_set_names`");

    if (!result)
    {
        sLog->outString(">> Loaded 0 item set names. DB table `item_set_names` is empty.");
        sLog->outString();
        return;
    }

    _itemSetNameStore.rehash(result->GetRowCount());
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        if (itemSetItems.find(entry) == itemSetItems.end())
        {
            sLog->outErrorDb("Item set name (Entry: %u) not found in ItemSet.dbc, data useless.", entry);
            continue;
        }

        ItemSetNameEntry &data = _itemSetNameStore[entry];
        data.name = fields[1].GetString();

        uint32 invType = fields[2].GetUInt8();
        if (invType >= MAX_INVTYPE)
        {
            sLog->outErrorDb("Item set name (Entry: %u) has wrong InventoryType value (%u)", entry, invType);
            invType = INVTYPE_NON_EQUIP;
        }

        data.InventoryType = invType;
        itemSetItems.erase(entry);
        ++count;
    } while (result->NextRow());

    if (!itemSetItems.empty())
    {
        ItemTemplate const* pProto;
        for (std::set<uint32>::iterator itr = itemSetItems.begin(); itr != itemSetItems.end(); ++itr)
        {
            uint32 entry = *itr;
            // add data from item_template if available
            pProto = sObjectMgr->GetItemTemplate(entry);
            if (pProto)
            {
                sLog->outErrorDb("Item set part (Entry: %u) does not have entry in `item_set_names`, adding data from `item_template`.", entry);
                ItemSetNameEntry &data = _itemSetNameStore[entry];
                data.name = pProto->Name1;
                data.InventoryType = pProto->InventoryType;
                ++count;
            }
            else
                sLog->outErrorDb("Item set part (Entry: %u) does not have entry in `item_set_names`, set will not display properly.", entry);
        }
    }

    sLog->outString(">> Loaded %u item set names in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadVehicleTemplateAccessories()
{
    uint32 oldMSTime = getMSTime();

    _vehicleTemplateAccessoryStore.clear();                           // needed for reload case

    uint32 count = 0;
    
    //                                                  0             1              2          3           4             5
    QueryResult result = WorldDatabase.Query("SELECT `entry`, `accessory_entry`, `seat_id`, `minion`, `summontype`, `summontimer` FROM `vehicle_template_accessory`");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 vehicle template accessories. DB table `vehicle_template_accessory` is empty.");
        sLog->outString();
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 uiEntry      = fields[0].GetUInt32();
        uint32 uiAccessory  = fields[1].GetUInt32();
        int8   uiSeat       = int8(fields[2].GetInt8());
        bool   bMinion      = fields[3].GetBool();
        uint8  uiSummonType = fields[4].GetUInt8();
        uint32 uiSummonTimer= fields[5].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(uiEntry))
        {
            sLog->outErrorDb("Table `vehicle_template_accessory`: creature template entry %u does not exist.", uiEntry);
            continue;
        }

        if (!sObjectMgr->GetCreatureTemplate(uiAccessory))
        {
            sLog->outErrorDb("Table `vehicle_template_accessory`: Accessory %u does not exist.", uiAccessory);
            continue;
        }

        if (_spellClickInfoStore.find(uiEntry) == _spellClickInfoStore.end())
        {
            sLog->outErrorDb("Table `vehicle_template_accessory`: creature template entry %u has no data in npc_spellclick_spells", uiEntry);
            continue;
        }

        _vehicleTemplateAccessoryStore[uiEntry].push_back(VehicleAccessory(uiAccessory, uiSeat, bMinion, uiSummonType, uiSummonTimer));

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u Vehicle Template Accessories in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadVehicleAccessories()
{
    uint32 oldMSTime = getMSTime();

    _vehicleAccessoryStore.clear();                           // needed for reload case

    uint32 count = 0;

    //                                                  0             1             2          3           4             5
    QueryResult result = WorldDatabase.Query("SELECT `guid`, `accessory_entry`, `seat_id`, `minion`, `summontype`, `summontimer` FROM `vehicle_accessory`");

    if (!result)
    {
        sLog->outString(">> Loaded 0 Vehicle Accessories in %u ms", GetMSTimeDiffToNow(oldMSTime));
        sLog->outString();
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 uiGUID       = fields[0].GetUInt32();
        uint32 uiAccessory  = fields[1].GetUInt32();
        int8   uiSeat       = int8(fields[2].GetInt16());
        bool   bMinion      = fields[3].GetBool();
        uint8  uiSummonType = fields[4].GetUInt8();
        uint32 uiSummonTimer= fields[5].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(uiAccessory))
        {
            sLog->outErrorDb("Table `vehicle_accessory`: Accessory %u does not exist.", uiAccessory);
            continue;
        }

        _vehicleAccessoryStore[uiGUID].push_back(VehicleAccessory(uiAccessory, uiSeat, bMinion, uiSummonType, uiSummonTimer));

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u Vehicle Accessories in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadPetLevelInfo()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0               1      2   3     4    5    6    7     8    9
    QueryResult result = WorldDatabase.Query("SELECT creature_entry, level, hp, mana, str, agi, sta, inte, spi, armor FROM pet_levelstats");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 level pet stats definitions. DB table `pet_levelstats` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 creature_id = fields[0].GetUInt32();
        if (!sObjectMgr->GetCreatureTemplate(creature_id))
        {
            sLog->outErrorDb("Wrong creature id %u in `pet_levelstats` table, ignoring.", creature_id);
            continue;
        }

        uint32 current_level = fields[1].GetUInt8();
        if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        {
            if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                sLog->outErrorDb("Wrong (> %u) level %u in `pet_levelstats` table, ignoring.", STRONG_MAX_LEVEL, current_level);
            else
            {
                ;//sLog->outDetail("Unused (> MaxPlayerLevel in worldserver.conf) level %u in `pet_levelstats` table, ignoring.", current_level);
                ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
            }
            continue;
        }
        else if (current_level < 1)
        {
            sLog->outErrorDb("Wrong (<1) level %u in `pet_levelstats` table, ignoring.", current_level);
            continue;
        }

        PetLevelInfo*& pInfoMapEntry = _petInfoStore[creature_id];

        if (pInfoMapEntry == NULL)
            pInfoMapEntry = new PetLevelInfo[sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)];

        // data for level 1 stored in [0] array element, ...
        PetLevelInfo* pLevelInfo = &pInfoMapEntry[current_level-1];

        pLevelInfo->health = fields[2].GetUInt16();
        pLevelInfo->mana   = fields[3].GetUInt16();
        pLevelInfo->armor  = fields[9].GetUInt32();

        for (int i = 0; i < MAX_STATS; i++)
        {
            pLevelInfo->stats[i] = fields[i+4].GetUInt16();
        }

        ++count;
    }
    while (result->NextRow());

    // Fill gaps and check integrity
    for (PetLevelInfoContainer::iterator itr = _petInfoStore.begin(); itr != _petInfoStore.end(); ++itr)
    {
        PetLevelInfo* pInfo = itr->second;

        // fatal error if no level 1 data
        if (!pInfo || pInfo[0].health == 0)
        {
            sLog->outErrorDb("Creature %u does not have pet stats data for Level 1!", itr->first);
            exit(1);
        }

        // fill level gaps
        for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
        {
            if (pInfo[level].health == 0)
            {
                sLog->outErrorDb("Creature %u has no data for Level %i pet stats data, using data of Level %i.", itr->first, level + 1, level);
                pInfo[level] = pInfo[level - 1];
            }
        }
    }

    sLog->outString(">> Loaded %u level pet stats definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

PetLevelInfo const* ObjectMgr::GetPetLevelInfo(uint32 creature_id, uint8 level) const
{
    if (level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        level = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    PetLevelInfoContainer::const_iterator itr = _petInfoStore.find(creature_id);
    if (itr == _petInfoStore.end())
        return NULL;

    return &itr->second[level-1];                           // data for level 1 stored in [0] array element, ...
}

void ObjectMgr::PlayerCreateInfoAddItemHelper(uint32 race_, uint32 class_, uint32 itemId, int32 count)
{
    if (!_playerInfo[race_][class_])
        return;

    if (count > 0)
        _playerInfo[race_][class_]->item.push_back(PlayerCreateInfoItem(itemId, count));
    else
    {
        if (count < -1)
            sLog->outError("Invalid count %i specified on item %u be removed from original player create info (use -1)!", count, itemId);

        for (uint32 gender = 0; gender < GENDER_NONE; ++gender)
        {
            if (CharStartOutfitEntry const* entry = GetCharStartOutfitEntry(race_, class_, gender))
            {
                bool found = false;
                for (uint8 x = 0; x < MAX_OUTFIT_ITEMS; ++x)
                {
                    if (entry->ItemId[x] > 0 && uint32(entry->ItemId[x]) == itemId)
                    {
                        found = true;
                        const_cast<CharStartOutfitEntry*>(entry)->ItemId[x] = 0;
                        break;
                    }
                }

                if (!found)
                    sLog->outError("Item %u specified to be removed from original create info not found in dbc!", itemId);
            }
        }
    }
}

void ObjectMgr::LoadPlayerInfo()
{
    // Load playercreate
    {
        uint32 oldMSTime = getMSTime();
        //                                                0     1      2    3        4          5           6
        QueryResult result = WorldDatabase.Query("SELECT race, class, map, zone, position_x, position_y, position_z, orientation FROM playercreateinfo");

        if (!result)
        {
            sLog->outString();
            sLog->outErrorDb(">> Loaded 0 player create definitions. DB table `playercreateinfo` is empty.");
            exit(1);
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race  = fields[0].GetUInt8();
                uint32 current_class = fields[1].GetUInt8();
                uint32 mapId         = fields[2].GetUInt16();
                uint32 areaId        = fields[3].GetUInt32(); // zone
                float  positionX     = fields[4].GetFloat();
                float  positionY     = fields[5].GetFloat();
                float  positionZ     = fields[6].GetFloat();
                float  orientation   = fields[7].GetFloat();

                if (current_race >= MAX_RACES)
                {
                    sLog->outErrorDb("Wrong race %u in `playercreateinfo` table, ignoring.", current_race);
                    continue;
                }

                ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(current_race);
                if (!rEntry)
                {
                    sLog->outErrorDb("Wrong race %u in `playercreateinfo` table, ignoring.", current_race);
                    continue;
                }

                if (current_class >= MAX_CLASSES)
                {
                    sLog->outErrorDb("Wrong class %u in `playercreateinfo` table, ignoring.", current_class);
                    continue;
                }

                if (!sChrClassesStore.LookupEntry(current_class))
                {
                    sLog->outErrorDb("Wrong class %u in `playercreateinfo` table, ignoring.", current_class);
                    continue;
                }

                // accept DB data only for valid position (and non instanceable)
                if (!MapManager::IsValidMapCoord(mapId, positionX, positionY, positionZ, orientation))
                {
                    sLog->outErrorDb("Wrong home position for class %u race %u pair in `playercreateinfo` table, ignoring.", current_class, current_race);
                    continue;
                }

                PlayerInfo* info = new PlayerInfo();
                info->mapId = mapId;
                info->areaId = areaId;
                info->positionX = positionX;
                info->positionY = positionY;
                info->positionZ = positionZ;
                info->orientation = orientation;
                info->displayId_m = rEntry->model_m;
                info->displayId_f = rEntry->model_f;
                _playerInfo[current_race][current_class] = info;

                ++count;
            }
            while (result->NextRow());

            sLog->outString(">> Loaded %u player create definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate items
    sLog->outString("Loading Player Create Items Data...");
    {
        uint32 oldMSTime = getMSTime();
        //                                                0     1      2       3
        QueryResult result = WorldDatabase.Query("SELECT race, class, itemid, amount FROM playercreateinfo_item");

        if (!result)
        {
            sLog->outString(">> Loaded 0 custom player create items. DB table `playercreateinfo_item` is empty.");
            sLog->outString();
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race = fields[0].GetUInt8();
                if (current_race >= MAX_RACES)
                {
                    sLog->outErrorDb("Wrong race %u in `playercreateinfo_item` table, ignoring.", current_race);
                    continue;
                }

                uint32 current_class = fields[1].GetUInt8();
                if (current_class >= MAX_CLASSES)
                {
                    sLog->outErrorDb("Wrong class %u in `playercreateinfo_item` table, ignoring.", current_class);
                    continue;
                }

                uint32 item_id = fields[2].GetUInt32();

                if (!GetItemTemplate(item_id))
                {
                    sLog->outErrorDb("Item id %u (race %u class %u) in `playercreateinfo_item` table but not listed in `item_template`, ignoring.", item_id, current_race, current_class);
                    continue;
                }

                int32 amount   = fields[3].GetInt8();

                if (!amount)
                {
                    sLog->outErrorDb("Item id %u (class %u race %u) have amount == 0 in `playercreateinfo_item` table, ignoring.", item_id, current_race, current_class);
                    continue;
                }

                if (!current_race || !current_class)
                {
                    uint32 min_race = current_race ? current_race : 1;
                    uint32 max_race = current_race ? current_race + 1 : MAX_RACES;
                    uint32 min_class = current_class ? current_class : 1;
                    uint32 max_class = current_class ? current_class + 1 : MAX_CLASSES;
                    for (uint32 r = min_race; r < max_race; ++r)
                        for (uint32 c = min_class; c < max_class; ++c)
                            PlayerCreateInfoAddItemHelper(r, c, item_id, amount);
                }
                else
                    PlayerCreateInfoAddItemHelper(current_race, current_class, item_id, amount);

                ++count;
            }
            while (result->NextRow());

            sLog->outString(">> Loaded %u custom player create items in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
            sLog->outString();
        }
    }

    // Load playercreate skills
    sLog->outString("Loading Player Create Skill Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT raceMask, classMask, skill, rank FROM playercreateinfo_skills");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 player create skills. DB table `playercreateinfo_skills` is empty.");
            sLog->outString();
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();
                uint32 raceMask = fields[0].GetUInt32();
                uint32 classMask = fields[1].GetUInt32();
                PlayerCreateInfoSkill skill;
                skill.SkillId = fields[2].GetUInt16();
                skill.Rank = fields[3].GetUInt16();

                if (skill.Rank >= MAX_SKILL_STEP)
                {
                    sLog->outErrorDb("Skill rank value %hu set for skill %hu raceMask %u classMask %u is too high, max allowed value is %d", skill.Rank, skill.SkillId, raceMask, classMask, MAX_SKILL_STEP);
                    continue;
                }

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong race mask %u in `playercreateinfo_skills` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong class mask %u in `playercreateinfo_skills` table, ignoring.", classMask);
                    continue;
                }

                if (!sSkillLineStore.LookupEntry(skill.SkillId))
                {
                    sLog->outErrorDb("Wrong skill id %u in `playercreateinfo_skills` table, ignoring.", skill.SkillId);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (!GetSkillRaceClassInfo(skill.SkillId, raceIndex, classIndex))
                                    continue;

                                if (PlayerInfo* info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->skills.push_back(skill);
                                    ++count;
                                }
                            }
                        }
                    }
                }
            } while (result->NextRow());

            sLog->outString(">> Loaded %u player create skills in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
            sLog->outString();
        }
    }

    // Load playercreate spells
    sLog->outString("Loading Player Create Spell Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT racemask, classmask, Spell FROM playercreateinfo_spell_custom");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 player create spells. DB table `playercreateinfo_spell_custom` is empty.");
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();
                uint32 raceMask = fields[0].GetUInt32();
                uint32 classMask = fields[1].GetUInt32();
                uint32 spellId = fields[2].GetUInt32();

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong race mask %u in `playercreateinfo_spell_custom` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong class mask %u in `playercreateinfo_spell_custom` table, ignoring.", classMask);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (PlayerInfo* info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->customSpells.push_back(spellId);
                                    ++count;
                                }
                                // We need something better here, the check is not accounting for spells used by multiple races/classes but not all of them.
                                // Either split the masks per class, or per race, which kind of kills the point yet.
                                // else if (raceMask != 0 && classMask != 0)
                                //     sLog->outError(LOG_FILTER_SQL, "Racemask/classmask (%u/%u) combination was found containing an invalid race/class combination (%u/%u) in `playercreateinfo_spell` (Spell %u), ignoring.", raceMask, classMask, raceIndex, classIndex, spellId);
                            }
                        }
                    }
                }
            }
            while (result->NextRow());

            sLog->outString(">> Loaded %u player create spells in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate cast spell
    sLog->outString("Loading Player Create Cast Spell Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT raceMask, classMask, spell FROM playercreateinfo_cast_spell");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 player create cast spells. DB table `playercreateinfo_cast_spell` is empty.");
            sLog->outString();
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields       = result->Fetch();
                uint32 raceMask     = fields[0].GetUInt32();
                uint32 classMask    = fields[1].GetUInt32();
                uint32 spellId      = fields[2].GetUInt32();

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong race mask %u in `playercreateinfo_cast_spell` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Wrong class mask %u in `playercreateinfo_cast_spell` table, ignoring.", classMask);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (PlayerInfo* info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->castSpells.push_back(spellId);
                                    ++count;
                                }
                            }
                        }
                    }
                }
            } while (result->NextRow());

            sLog->outString(">> Loaded %u player create cast spells in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
            sLog->outString();
        }
    }
    // Load playercreate actions
    sLog->outString("Loading Player Create Action Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0     1      2       3       4
        QueryResult result = WorldDatabase.Query("SELECT race, class, button, action, type FROM playercreateinfo_action");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 player create actions. DB table `playercreateinfo_action` is empty.");
            sLog->outString();
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race = fields[0].GetUInt8();
                if (current_race >= MAX_RACES)
                {
                    sLog->outErrorDb("Wrong race %u in `playercreateinfo_action` table, ignoring.", current_race);
                    continue;
                }

                uint32 current_class = fields[1].GetUInt8();
                if (current_class >= MAX_CLASSES)
                {
                    sLog->outErrorDb("Wrong class %u in `playercreateinfo_action` table, ignoring.", current_class);
                    continue;
                }

                if (PlayerInfo* info = _playerInfo[current_race][current_class])
                    info->action.push_back(PlayerCreateInfoAction(fields[2].GetUInt16(), fields[3].GetUInt32(), fields[4].GetUInt16()));

                ++count;
            }
            while (result->NextRow());

            sLog->outString(">> Loaded %u player create actions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
            sLog->outString();
        }
    }

    // Loading levels data (class only dependent)
    sLog->outString("Loading Player Create Level HP/Mana Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0      1      2       3
        QueryResult result  = WorldDatabase.Query("SELECT class, level, basehp, basemana FROM player_classlevelstats");

        if (!result)
        {
            sLog->outError(">> Loaded 0 level health/mana definitions. DB table `player_classlevelstats` is empty.");
            exit(1);
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_class = fields[0].GetUInt8();
            if (current_class >= MAX_CLASSES)
            {
                sLog->outError("Wrong class %u in `player_classlevelstats` table, ignoring.", current_class);
                continue;
            }

            uint8 current_level = fields[1].GetUInt8();      // Can't be > than STRONG_MAX_LEVEL (hardcoded level maximum) due to var type
            if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                sLog->outString("Unused (> MaxPlayerLevel in worldserver.conf) level %u in `player_classlevelstats` table, ignoring.", current_level);
                ++count;                                    // make result loading percent "expected" correct in case disabled detail mode for example.
                continue;
            }

            PlayerClassInfo* info = _playerClassInfo[current_class];
            if (!info)
            {
                info = new PlayerClassInfo();
                info->levelInfo = new PlayerClassLevelInfo[sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)];
                _playerClassInfo[current_class] = info;
            }

            PlayerClassLevelInfo& levelInfo = info->levelInfo[current_level - 1];

            levelInfo.basehealth = fields[2].GetUInt16();
            levelInfo.basemana   = fields[3].GetUInt16();

            ++count;
        }
        while (result->NextRow());

        // Fill gaps and check integrity
        for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
        {
            // skip non existed classes
            if (!sChrClassesStore.LookupEntry(class_))
                continue;

            PlayerClassInfo* pClassInfo = _playerClassInfo[class_];

            // fatal error if no level 1 data
            if (!pClassInfo->levelInfo || pClassInfo->levelInfo[0].basehealth == 0)
            {
                sLog->outError("Class %i Level 1 does not have health/mana data!", class_);
                exit(1);
            }

            // fill level gaps
            for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
            {
                if (pClassInfo->levelInfo[level].basehealth == 0)
                {
                    sLog->outError("Class %i Level %i does not have health/mana data. Using stats data of level %i.", class_, level + 1, level);
                    pClassInfo->levelInfo[level] = pClassInfo->levelInfo[level - 1];
                }
            }
        }

        sLog->outString(">> Loaded %u level health/mana definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
        sLog->outString();
    }

    // Loading levels data (class/race dependent)
    sLog->outString("Loading Player Create Level Stats Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 0     1      2      3    4    5    6    7
        QueryResult result  = WorldDatabase.Query("SELECT race, class, level, str, agi, sta, inte, spi FROM player_levelstats");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 level stats definitions. DB table `player_levelstats` is empty.");
            sLog->outString();
            exit(1);
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_race = fields[0].GetUInt8();
            if (current_race >= MAX_RACES)
            {
                sLog->outErrorDb("Wrong race %u in `player_levelstats` table, ignoring.", current_race);
                continue;
            }

            uint32 current_class = fields[1].GetUInt8();
            if (current_class >= MAX_CLASSES)
            {
                sLog->outErrorDb("Wrong class %u in `player_levelstats` table, ignoring.", current_class);
                continue;
            }

            uint32 current_level = fields[2].GetUInt8();
            if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                    sLog->outErrorDb("Wrong (> %u) level %u in `player_levelstats` table, ignoring.", STRONG_MAX_LEVEL, current_level);
                else
                {
                    ;//sLog->outDetail("Unused (> MaxPlayerLevel in worldserver.conf) level %u in `player_levelstats` table, ignoring.", current_level);
                    ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
                }
                continue;
            }

            if (PlayerInfo* info = _playerInfo[current_race][current_class])
            {
                if (!info->levelInfo)
                    info->levelInfo = new PlayerLevelInfo[sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)];

                PlayerLevelInfo& levelInfo = info->levelInfo[current_level - 1];
                for (int i = 0; i < MAX_STATS; i++)
                    levelInfo.stats[i] = fields[i + 3].GetUInt8();
            }

            ++count;
        }
        while (result->NextRow());

        // Fill gaps and check integrity
        for (int race = 0; race < MAX_RACES; ++race)
        {
            // skip non existed races
            if (!sChrRacesStore.LookupEntry(race))
                continue;

            for (int class_ = 0; class_ < MAX_CLASSES; ++class_)
            {
                // skip non existed classes
                if (!sChrClassesStore.LookupEntry(class_))
                    continue;

                PlayerInfo* info = _playerInfo[race][class_];
                if (!info)
                    continue;

                // skip expansion races if not playing with expansion
                if (sWorld->getIntConfig(CONFIG_EXPANSION) < EXPANSION_THE_BURNING_CRUSADE && (race == RACE_BLOODELF || race == RACE_DRAENEI))
                    continue;

                // skip expansion classes if not playing with expansion
                if (sWorld->getIntConfig(CONFIG_EXPANSION) < EXPANSION_WRATH_OF_THE_LICH_KING && class_ == CLASS_DEATH_KNIGHT)
                    continue;

                // fatal error if no level 1 data
                if (!info->levelInfo || info->levelInfo[0].stats[0] == 0)
                {
                    sLog->outError("Race %i Class %i Level 1 does not have stats data!", race, class_);
                    exit(1);
                }

                // fill level gaps
                for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
                {
                    if (info->levelInfo[level].stats[0] == 0)
                    {
                        sLog->outError("Race %i Class %i Level %i does not have stats data. Using stats data of level %i.", race, class_, level + 1, level);
                        info->levelInfo[level] = info->levelInfo[level - 1];
                    }
                }
            }
        }

        sLog->outString(">> Loaded %u level stats definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
        sLog->outString();
    }

    // Loading xp per level data
    sLog->outString("Loading Player Create XP Data...");
    {
        uint32 oldMSTime = getMSTime();

        _playerXPperLevel.resize(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        for (uint8 level = 0; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
            _playerXPperLevel[level] = 0;

        //                                                  0         1
        QueryResult result  = WorldDatabase.Query("SELECT Level, Experience FROM player_xp_for_level");

        if (!result)
        {
            sLog->outErrorDb(">> Loaded 0 xp for level definitions. DB table `player_xp_for_level` is empty.");
            sLog->outString();
            exit(1);
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_level = fields[0].GetUInt8();
            uint32 current_xp    = fields[1].GetUInt32();

            if (current_level >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                    sLog->outErrorDb("Wrong (> %u) level %u in `player_xp_for_level` table, ignoring.", STRONG_MAX_LEVEL, current_level);
                else
                {
                    ;//sLog->outDetail("Unused (> MaxPlayerLevel in worldserver.conf) level %u in `player_xp_for_levels` table, ignoring.", current_level);
                    ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
                }
                continue;
            }
            //PlayerXPperLevel
            _playerXPperLevel[current_level] = current_xp;
            ++count;
        }
        while (result->NextRow());

        // fill level gaps
        for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
        {
            if (_playerXPperLevel[level] == 0)
            {
                sLog->outErrorDb("Level %i does not have XP for level data. Using data of level [%i] + 100.", level + 1, level);
                _playerXPperLevel[level] = _playerXPperLevel[level - 1] + 100;
            }
        }

        sLog->outString(">> Loaded %u xp for level definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
        sLog->outString();
    }
}

void ObjectMgr::GetPlayerClassLevelInfo(uint32 class_, uint8 level, PlayerClassLevelInfo* info) const
{
    if (level < 1 || class_ >= MAX_CLASSES)
        return;

    PlayerClassInfo const* pInfo = _playerClassInfo[class_];

    if (level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        level = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    *info = pInfo->levelInfo[level-1];
}

void ObjectMgr::GetPlayerLevelInfo(uint32 race, uint32 class_, uint8 level, PlayerLevelInfo* info) const
{
    if (level < 1 || race >= MAX_RACES || class_ >= MAX_CLASSES)
        return;

    PlayerInfo const* pInfo = _playerInfo[race][class_];
    if (!pInfo)
        return;

    if (level <= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        *info = pInfo->levelInfo[level-1];
    else
        BuildPlayerLevelInfo(race, class_, level, info);
}

void ObjectMgr::BuildPlayerLevelInfo(uint8 race, uint8 _class, uint8 level, PlayerLevelInfo* info) const
{
    // base data (last known level)
    *info = _playerInfo[race][_class]->levelInfo[sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)-1];

    // if conversion from uint32 to uint8 causes unexpected behaviour, change lvl to uint32
    for (uint8 lvl = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)-1; lvl < level; ++lvl)
    {
        switch (_class)
        {
            case CLASS_WARRIOR:
                info->stats[STAT_STRENGTH]  += (lvl > 23 ? 2: (lvl > 1  ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 23 ? 2: (lvl > 1  ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 36 ? 1: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 9 && !(lvl%2) ? 1: 0);
                break;
            case CLASS_PALADIN:
                info->stats[STAT_STRENGTH]  += (lvl > 3  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 33 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 1: (lvl > 7 && !(lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 6 && (lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 7 ? 1: 0);
                break;
            case CLASS_HUNTER:
                info->stats[STAT_STRENGTH]  += (lvl > 4  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 4  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 33 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 8 && (lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 1: (lvl > 9 && !(lvl%2) ? 1: 0));
                break;
            case CLASS_ROGUE:
                info->stats[STAT_STRENGTH]  += (lvl > 5  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 4  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 16 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 8 && !(lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 1: (lvl > 9 && !(lvl%2) ? 1: 0));
                break;
            case CLASS_PRIEST:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 5  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 1: (lvl > 8 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 22 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 3  ? 1: 0);
                break;
            case CLASS_SHAMAN:
                info->stats[STAT_STRENGTH]  += (lvl > 34 ? 1: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 4 ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 7 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 5 ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 4 ? 1: 0);
                break;
            case CLASS_MAGE:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 5  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 24 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 33 ? 2: (lvl > 2 ? 1: 0));
                break;
            case CLASS_WARLOCK:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 38 ? 2: (lvl > 3 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 33 ? 2: (lvl > 2 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 2: (lvl > 3 ? 1: 0));
                break;
            case CLASS_DRUID:
                info->stats[STAT_STRENGTH]  += (lvl > 38 ? 2: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 32 ? 2: (lvl > 4 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 2: (lvl > 8 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 38 ? 3: (lvl > 4 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 3: (lvl > 5 ? 1: 0));
        }
    }
}

void ObjectMgr::LoadQuests()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    for (QuestMap::const_iterator itr=_questTemplates.begin(); itr != _questTemplates.end(); ++itr)
        delete itr->second;
    _questTemplates.clear();

    mExclusiveQuestGroups.clear();
    
    QueryResult result = WorldDatabase.Query("SELECT "
        //0      1           2         3           4            5                6             7            8
        "ID, QuestType, QuestLevel, MinLevel, QuestSortID, QuestInfoID, SuggestedGroupNum, LimitTime, RequiredRaces,"
        //      9                     10                   11                    12
        "RequiredFactionId1, RequiredFactionId2, RequiredFactionValue1, RequiredFactionValue2, "
        //      13             14               15                    16               17             18             19               20
        "NextQuestIdChain, RewardXPId, RewardOrRequiredMoney, RewardMoneyMaxLevel, RewardSpell, RewardSpellCast, RewardHonor, RewardHonorMultiplier, "
        //      21       22        23              24                25               26
        "SourceItemId, Flags, RewardTitle, RequiredPlayerKills, RewardTalents, RewardArenaPoints, "
        //    27            28            29           30             31            32            33            34
        "RewardItem1, RewardAmount1, RewardItem2, RewardAmount2, RewardItem3, RewardAmount3, RewardItem4, RewardAmount4, "
        //        35                      36                      37                      38                      39                      40                      41                      42                      43                      44                     45                      46
        "RewardChoiceItemID1, RewardChoiceItemQuantity1, RewardChoiceItemID2, RewardChoiceItemQuantity2, RewardChoiceItemID3, RewardChoiceItemQuantity3, RewardChoiceItemID4, RewardChoiceItemQuantity4, RewardChoiceItemID5, RewardChoiceItemQuantity5, RewardChoiceItemID6, RewardChoiceItemQuantity6, "
        //       47                 48                     49                  50                  51                     52                 53                  54                     55                  56                  57                    58                   59                 60                      61
        "RewardFactionID1, RewardFactionValue1, RewardFactionOverride1, RewardFactionID2, RewardFactionValue2, RewardFactionOverride2, RewardFactionID3, RewardFactionValue3, RewardFactionOverride3, RewardFactionID4, RewardFactionValue4, RewardFactionOverride4, RewardFactionID5, RewardFactionValue5,  RewardFactionOverride5,"
        //   62        63      64        65
        "PointMapId, PointX, PointY, PointOption, "
        //   66          67               68           69             70
        "LogTitle, LogDescription, QuestDescription, EndText, QuestCompletionLog, "
        //      71                72                73                74                   75                     76                    77                      78
        "RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4, RequiredNpcOrGoCount1, RequiredNpcOrGoCount2, RequiredNpcOrGoCount3, RequiredNpcOrGoCount4, "
        //         79                     80                    81                     82                        83                       84                        85                        86
        "RequiredSourceItemId1, RequiredSourceItemId2, RequiredSourceItemId3, RequiredSourceItemId4, RequiredSourceItemCount1, RequiredSourceItemCount2, RequiredSourceItemCount3, RequiredSourceItemCount4, "
        //      87               88               89               90               91               92                93                  94                  95                  96                  97                  98
        "RequiredItemId1, RequiredItemId2, RequiredItemId3, RequiredItemId4, RequiredItemId5, RequiredItemId6, RequiredItemCount1, RequiredItemCount2, RequiredItemCount3, RequiredItemCount4, RequiredItemCount5, RequiredItemCount6, "
        //  99          100             101             102             103        
        "Unknown0, ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4"
        " FROM quest_template ");
    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quests definitions. DB table `quest_template` is empty.");
        sLog->outString();
        return;
    }

    // create multimap previous quest for each existed quest
    // some quests can have many previous maps set by NextQuestId in previous quest
    // for example set of race quests can lead to single not race specific quest
    do
    {
        Field* fields = result->Fetch();

        Quest* newQuest = new Quest(fields);
        _questTemplates[newQuest->GetQuestId()] = newQuest;
    } while (result->NextRow());

	// pussywizard:
	{
		uint32 max = 0;
		for (QuestMap::const_iterator itr = _questTemplates.begin(); itr != _questTemplates.end(); ++itr)
			if (itr->first > max)
				max = itr->first;
		if (max)
		{
			_questTemplatesFast.clear();
			_questTemplatesFast.resize(max+1, NULL);
			for (QuestMap::iterator itr = _questTemplates.begin(); itr != _questTemplates.end(); ++itr)
				_questTemplatesFast[itr->first] = itr->second;
		}
	}

	for (QuestMap::iterator itr = _questTemplates.begin(); itr != _questTemplates.end(); ++itr)
		itr->second->InitializeQueryData();

    std::map<uint32, uint32> usedMailTemplates;
    
    // Load `quest_details`
    //                                   0   1       2       3       4       5            6            7            8
    result = WorldDatabase.Query("SELECT ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4 FROM quest_details");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest details. DB table `quest_details` is empty.");
    }
    else
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 questId = fields[0].GetUInt32();

            auto itr = _questTemplates.find(questId);
            if (itr != _questTemplates.end())
                itr->second->LoadQuestDetails(fields);
            else
                sLog->outErrorDb("Table `quest_details` has data for quest %u but such quest does not exist", questId);
        } while (result->NextRow());
    }

    // Load `quest_request_items`
    //                                   0   1                2                  3
    result = WorldDatabase.Query("SELECT ID, EmoteOnComplete, EmoteOnIncomplete, CompletionText FROM quest_request_items");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest request items. DB table `quest_request_items` is empty.");
    }
    else
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 questId = fields[0].GetUInt32();

            auto itr = _questTemplates.find(questId);
            if (itr != _questTemplates.end())
                itr->second->LoadQuestRequestItems(fields);
            else
                sLog->outErrorDb("Table `quest_request_items` has data for quest %u but such quest does not exist", questId);
        } while (result->NextRow());
    }

    // Load `quest_offer_reward`
    //                                   0   1       2       3       4       5            6            7            8            9
    result = WorldDatabase.Query("SELECT ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4, RewardText FROM quest_offer_reward");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest reward emotes. DB table `quest_offer_reward` is empty.");
    }
    else
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 questId = fields[0].GetUInt32();

            auto itr = _questTemplates.find(questId);
            if (itr != _questTemplates.end())
                itr->second->LoadQuestOfferReward(fields);
            else
                sLog->outErrorDb("Table `quest_offer_reward` has data for quest %u but such quest does not exist", questId);
        } while (result->NextRow());
    }

    // Load `quest_template_addon`
    //                                   0   1         2                 3              4            5            6               7                     8
    result = WorldDatabase.Query("SELECT ID, MaxLevel, AllowableClasses, SourceSpellID, PrevQuestID, NextQuestID, ExclusiveGroup, RewardMailTemplateID, RewardMailDelay, "
        //9               10                   11                     12                     13                   14                   15                 16
        "RequiredSkillID, RequiredSkillPoints, RequiredMinRepFaction, RequiredMaxRepFaction, RequiredMinRepValue, RequiredMaxRepValue, ProvidedItemCount, SpecialFlags FROM quest_template_addon");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest template addons. DB table `quest_template_addon` is empty.");
    }
    else
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 questId = fields[0].GetUInt32();

            auto itr = _questTemplates.find(questId);
            if (itr != _questTemplates.end())
                itr->second->LoadQuestTemplateAddon(fields);
            else
                sLog->outErrorDb("Table `quest_template_addon` has data for quest %u but such quest does not exist", questId);
        } while (result->NextRow());
    }

    // Post processing
    for (QuestMap::iterator iter = _questTemplates.begin(); iter != _questTemplates.end(); ++iter)
    {
        // skip post-loading checks for disabled quests
        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, iter->first, NULL))
            continue;

        Quest * qinfo = iter->second;

        // additional quest integrity checks (GO, creature_template and item_template must be loaded already)

        if (qinfo->GetQuestMethod() >= 3)
            sLog->outErrorDb("Quest %u has `Method` = %u, expected values are 0, 1 or 2.", qinfo->GetQuestId(), qinfo->GetQuestMethod());

        if (qinfo->SpecialFlags & ~QUEST_SPECIAL_FLAGS_DB_ALLOWED)
        {
            sLog->outErrorDb("Quest %u has `SpecialFlags` = %u > max allowed value. Correct `SpecialFlags` to value <= %u",
                qinfo->GetQuestId(), qinfo->SpecialFlags, QUEST_SPECIAL_FLAGS_DB_ALLOWED);
            qinfo->SpecialFlags &= QUEST_SPECIAL_FLAGS_DB_ALLOWED;
        }

        if (qinfo->Flags & QUEST_FLAGS_DAILY && qinfo->Flags & QUEST_FLAGS_WEEKLY)
        {
            sLog->outErrorDb("Weekly Quest %u is marked as daily quest in `Flags`, removed daily flag.", qinfo->GetQuestId());
            qinfo->Flags &= ~QUEST_FLAGS_DAILY;
        }

        if (qinfo->Flags & QUEST_FLAGS_DAILY)
        {
            if (!(qinfo->SpecialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                sLog->outErrorDb("Daily Quest %u not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->SpecialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->Flags & QUEST_FLAGS_WEEKLY)
        {
            if (!(qinfo->SpecialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                sLog->outErrorDb("Weekly Quest %u not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->SpecialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->SpecialFlags & QUEST_SPECIAL_FLAGS_MONTHLY)
        {
            if (!(qinfo->SpecialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                sLog->outError("Monthly quest %u not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->SpecialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->Flags & QUEST_FLAGS_TRACKING)
        {
            // at auto-reward can be rewarded only RewardChoiceItemId[0]
            for (int j = 1; j < QUEST_REWARD_CHOICES_COUNT; ++j )
            {
                if (uint32 id = qinfo->RewardChoiceItemId[j])
                {
                    sLog->outErrorDb("Quest %u has `RewardChoiceItemId%d` = %u but item from `RewardChoiceItemId%d` can't be rewarded with quest flag QUEST_FLAGS_TRACKING.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest ignore this data
                }
            }
        }

        // client quest log visual (area case)
        if (qinfo->ZoneOrSort > 0)
        {
            if (!sAreaTableStore.LookupEntry(qinfo->ZoneOrSort))
            {
                sLog->outErrorDb("Quest %u has `ZoneOrSort` = %u (zone case) but zone with this id does not exist.",
                    qinfo->GetQuestId(), qinfo->ZoneOrSort);
                // no changes, quest not dependent from this value but can have problems at client
            }
        }
        // client quest log visual (sort case)
        if (qinfo->ZoneOrSort < 0)
        {
            QuestSortEntry const* qSort = sQuestSortStore.LookupEntry(-int32(qinfo->ZoneOrSort));
            if (!qSort)
            {
                sLog->outErrorDb("Quest %u has `ZoneOrSort` = %i (sort case) but quest sort with this id does not exist.",
                    qinfo->GetQuestId(), qinfo->ZoneOrSort);
                // no changes, quest not dependent from this value but can have problems at client (note some may be 0, we must allow this so no check)
            }
            //check for proper RequiredSkillId value (skill case)
            if (uint32 skill_id = SkillByQuestSort(-int32(qinfo->ZoneOrSort)))
            {
                if (qinfo->RequiredSkillId != skill_id)
                {
                    sLog->outErrorDb("Quest %u has `ZoneOrSort` = %i but `RequiredSkillId` does not have a corresponding value (%d).",
                        qinfo->GetQuestId(), qinfo->ZoneOrSort, skill_id);
                    //override, and force proper value here?
                }
            }
        }

        // RequiredClasses, can be 0/CLASSMASK_ALL_PLAYABLE to allow any class
        if (qinfo->RequiredClasses)
        {
            if (!(qinfo->RequiredClasses & CLASSMASK_ALL_PLAYABLE))
            {
                sLog->outErrorDb("Quest %u does not contain any playable classes in `RequiredClasses` (%u), value set to 0 (all classes).", qinfo->GetQuestId(), qinfo->RequiredClasses);
                    qinfo->RequiredClasses = 0;
            }
        }
        // AllowableRaces, can be 0/RACEMASK_ALL_PLAYABLE to allow any race
        if (qinfo->AllowableRaces)
            {
            if (!(qinfo->AllowableRaces & RACEMASK_ALL_PLAYABLE))
                {
                    sLog->outErrorDb("Quest %u does not contain any playable races in `AllowableRaces` (%u), value set to 0 (all races).", qinfo->GetQuestId(), qinfo->AllowableRaces);
                    qinfo->AllowableRaces = 0;
                }
            }
        // RequiredSkillId, can be 0
        if (qinfo->RequiredSkillId)
        {
            if (!sSkillLineStore.LookupEntry(qinfo->RequiredSkillId))
            {
                sLog->outErrorDb("Quest %u has `RequiredSkillId` = %u but this skill does not exist",
                    qinfo->GetQuestId(), qinfo->RequiredSkillId);
            }
        }

        if (qinfo->RequiredSkillPoints)
        {
            if (qinfo->RequiredSkillPoints > sWorld->GetConfigMaxSkillValue())
            {
                sLog->outErrorDb("Quest %u has `RequiredSkillPoints` = %u but max possible skill is %u, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->RequiredSkillPoints, sWorld->GetConfigMaxSkillValue());
                // no changes, quest can't be done for this requirement
            }
        }
        // else Skill quests can have 0 skill level, this is ok

        if (qinfo->RequiredFactionId2 && !sFactionStore.LookupEntry(qinfo->RequiredFactionId2))
        {
            sLog->outErrorDb("Quest %u has `RequiredFactionId2` = %u but faction template %u does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredFactionId2, qinfo->RequiredFactionId2);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->RequiredFactionId1 && !sFactionStore.LookupEntry(qinfo->RequiredFactionId1))
        {
            sLog->outErrorDb("Quest %u has `RequiredFactionId1` = %u but faction template %u does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredFactionId1, qinfo->RequiredFactionId1);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->RequiredMinRepFaction && !sFactionStore.LookupEntry(qinfo->RequiredMinRepFaction))
        {
            sLog->outErrorDb("Quest %u has `RequiredMinRepFaction` = %u but faction template %u does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredMinRepFaction, qinfo->RequiredMinRepFaction);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->RequiredMaxRepFaction && !sFactionStore.LookupEntry(qinfo->RequiredMaxRepFaction))
        {
            sLog->outErrorDb("Quest %u has `RequiredMaxRepFaction` = %u but faction template %u does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredMaxRepFaction, qinfo->RequiredMaxRepFaction);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->RequiredMinRepValue && qinfo->RequiredMinRepValue > ReputationMgr::Reputation_Cap)
        {
            sLog->outErrorDb("Quest %u has `RequiredMinRepValue` = %d but max reputation is %u, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredMinRepValue, ReputationMgr::Reputation_Cap);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->RequiredMinRepValue && qinfo->RequiredMaxRepValue && qinfo->RequiredMaxRepValue <= qinfo->RequiredMinRepValue)
        {
            sLog->outErrorDb("Quest %u has `RequiredMaxRepValue` = %d and `RequiredMinRepValue` = %d, quest can't be done.",
                qinfo->GetQuestId(), qinfo->RequiredMaxRepValue, qinfo->RequiredMinRepValue);
            // no changes, quest can't be done for this requirement
        }

        if (!qinfo->RequiredFactionId1 && qinfo->RequiredFactionValue1 != 0)
        {
            sLog->outErrorDb("Quest %u has `RequiredFactionValue1` = %d but `RequiredFactionId1` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->RequiredFactionValue1);
            // warning
        }

        if (!qinfo->RequiredFactionId2 && qinfo->RequiredFactionValue2 != 0)
        {
            sLog->outErrorDb("Quest %u has `RequiredFactionValue2` = %d but `RequiredFactionId2` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->RequiredFactionValue2);
            // warning
        }

        if (!qinfo->RequiredMinRepFaction && qinfo->RequiredMinRepValue != 0)
        {
            sLog->outErrorDb("Quest %u has `RequiredMinRepValue` = %d but `RequiredMinRepFaction` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->RequiredMinRepValue);
            // warning
        }

        if (!qinfo->RequiredMaxRepFaction && qinfo->RequiredMaxRepValue != 0)
        {
            sLog->outErrorDb("Quest %u has `RequiredMaxRepValue` = %d but `RequiredMaxRepFaction` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->RequiredMaxRepValue);
            // warning
        }

        if (qinfo->RewardTitleId && !sCharTitlesStore.LookupEntry(qinfo->RewardTitleId))
        {
            sLog->outErrorDb("Quest %u has `RewardTitleId` = %u but CharTitle Id %u does not exist, quest can't be rewarded with title.",
                qinfo->GetQuestId(), qinfo->GetCharTitleId(), qinfo->GetCharTitleId());
            qinfo->RewardTitleId = 0;
            // quest can't reward this title
        }

        if (qinfo->StartItem)
        {
            if (!sObjectMgr->GetItemTemplate(qinfo->StartItem))
            {
                sLog->outErrorDb("Quest %u has `StartItem` = %u but item with entry %u does not exist, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->StartItem, qinfo->StartItem);
                qinfo->StartItem = 0;                       // quest can't be done for this requirement
            }
            else if (qinfo->StartItemCount == 0)
            {
                sLog->outErrorDb("Quest %u has `StartItem` = %u but `StartItemCount` = 0, set to 1 but need fix in DB.",
                    qinfo->GetQuestId(), qinfo->StartItem);
                qinfo->StartItemCount = 1;                    // update to 1 for allow quest work for backward compatibility with DB
            }
        }
        else if (qinfo->StartItemCount>0)
        {
            sLog->outErrorDb("Quest %u has `StartItem` = 0 but `StartItemCount` = %u, useless value.",
                qinfo->GetQuestId(), qinfo->StartItemCount);
            qinfo->StartItemCount=0;                          // no quest work changes in fact
        }

        if (qinfo->SourceSpellid)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->SourceSpellid);
            if (!spellInfo)
            {
                sLog->outErrorDb("Quest %u has `SourceSpellid` = %u but spell %u doesn't exist, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->SourceSpellid, qinfo->SourceSpellid);
                qinfo->SourceSpellid = 0;                        // quest can't be done for this requirement
            }
            else if (!SpellMgr::ComputeIsSpellValid(spellInfo))
            {
                sLog->outErrorDb("Quest %u has `SourceSpellid` = %u but spell %u is broken, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->SourceSpellid, qinfo->SourceSpellid);
                qinfo->SourceSpellid = 0;                        // quest can't be done for this requirement
            }
        }

        for (uint8 j = 0; j < QUEST_ITEM_OBJECTIVES_COUNT; ++j)
        {
            uint32 id = qinfo->RequiredItemId[j];
            if (id)
            {
                if (qinfo->RequiredItemCount[j] == 0)
                {
                    sLog->outErrorDb("Quest %u has `RequiredItemId%d` = %u but `RequiredItemCount%d` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can't be done for this requirement
                }

                qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_DELIVER);

                if (!sObjectMgr->GetItemTemplate(id))
                {
                    sLog->outErrorDb("Quest %u has `RequiredItemId%d` = %u but item with entry %u does not exist, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RequiredItemCount[j] = 0;             // prevent incorrect work of quest
                }
            }
            else if (qinfo->RequiredItemCount[j]>0)
            {
                sLog->outErrorDb("Quest %u has `RequiredItemId%d` = 0 but `RequiredItemCount%d` = %u, quest can't be done.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RequiredItemCount[j]);
                qinfo->RequiredItemCount[j] = 0;                 // prevent incorrect work of quest
            }
        }

        for (uint8 j = 0; j < QUEST_SOURCE_ITEM_IDS_COUNT; ++j)
        {
            uint32 id = qinfo->ItemDrop[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    sLog->outErrorDb("Quest %u has `ItemDrop%d` = %u but item with entry %u does not exist, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, id);
                    // no changes, quest can't be done for this requirement
                }
            }
            else
            {
                if (qinfo->ItemDropQuantity[j]>0)
                {
                    sLog->outErrorDb("Quest %u has `ItemDrop%d` = 0 but `ItemDropQuantity%d` = %u.",
                        qinfo->GetQuestId(), j+1, j+1, qinfo->ItemDropQuantity[j]);
                    // no changes, quest ignore this data
                }
            }
        }

        for (uint8 j = 0; j < QUEST_OBJECTIVES_COUNT; ++j)
        {
            int32 id = qinfo->RequiredNpcOrGo[j];
            if (id < 0 && !sObjectMgr->GetGameObjectTemplate(-id))
            {
                sLog->outErrorDb("Quest %u has `RequiredNpcOrGo%d` = %i but gameobject %u does not exist, quest can't be done.",
                    qinfo->GetQuestId(), j+1, id, uint32(-id));
                qinfo->RequiredNpcOrGo[j] = 0;            // quest can't be done for this requirement
            }

            if (id > 0 && !sObjectMgr->GetCreatureTemplate(id))
            {
                sLog->outErrorDb("Quest %u has `RequiredNpcOrGo%d` = %i but creature with entry %u does not exist, quest can't be done.",
                    qinfo->GetQuestId(), j+1, id, uint32(id));
                qinfo->RequiredNpcOrGo[j] = 0;            // quest can't be done for this requirement
            }

            if (id)
            {
                // In fact SpeakTo and Kill are quite same: either you can speak to mob:SpeakTo or you can't:Kill/Cast

                qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_KILL | QUEST_SPECIAL_FLAGS_CAST | QUEST_SPECIAL_FLAGS_SPEAKTO);

                if (!qinfo->RequiredNpcOrGoCount[j])
                {
                    sLog->outErrorDb("Quest %u has `RequiredNpcOrGo%d` = %u but `RequiredNpcOrGoCount%d` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can be incorrectly done, but we already report this
                }
            }
            else if (qinfo->RequiredNpcOrGoCount[j]>0)
            {
                sLog->outErrorDb("Quest %u has `RequiredNpcOrGo%d` = 0 but `RequiredNpcOrGoCount%d` = %u.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RequiredNpcOrGoCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REWARD_CHOICES_COUNT; ++j)
        {
            uint32 id = qinfo->RewardChoiceItemId[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    sLog->outErrorDb("Quest %u has `RewardChoiceItemId%d` = %u but item with entry %u does not exist, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RewardChoiceItemId[j] = 0;          // no changes, quest will not reward this
                }

                if (!qinfo->RewardChoiceItemCount[j])
                {
                    sLog->outErrorDb("Quest %u has `RewardChoiceItemId%d` = %u but `RewardChoiceItemCount%d` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can't be done
                }
            }
            else if (qinfo->RewardChoiceItemCount[j]>0)
            {
                sLog->outErrorDb("Quest %u has `RewardChoiceItemId%d` = 0 but `RewardChoiceItemCount%d` = %u.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardChoiceItemCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REWARDS_COUNT; ++j)
        {
            uint32 id = qinfo->RewardItemId[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    sLog->outErrorDb("Quest %u has `RewardItemId%d` = %u but item with entry %u does not exist, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RewardItemId[j] = 0;                // no changes, quest will not reward this item
                }

                if (!qinfo->RewardItemIdCount[j])
                {
                    sLog->outErrorDb("Quest %u has `RewardItemId%d` = %u but `RewardItemIdCount%d` = 0, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes
                }
            }
            else if (qinfo->RewardItemIdCount[j]>0)
            {
                sLog->outErrorDb("Quest %u has `RewardItemId%d` = 0 but `RewardItemIdCount%d` = %u.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardItemIdCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REPUTATIONS_COUNT; ++j)
        {
            if (qinfo->RewardFactionId[j])
            {
                if (abs(qinfo->RewardFactionValueId[j]) > 9)
                {
               sLog->outErrorDb("Quest %u has RewardFactionValueId%d = %i. That is outside the range of valid values (-9 to 9).", qinfo->GetQuestId(), j+1, qinfo->RewardFactionValueId[j]);
                }
                if (!sFactionStore.LookupEntry(qinfo->RewardFactionId[j]))
                {
                    sLog->outErrorDb("Quest %u has `RewardFactionId%d` = %u but raw faction (faction.dbc) %u does not exist, quest will not reward reputation for this faction.", qinfo->GetQuestId(), j+1, qinfo->RewardFactionId[j], qinfo->RewardFactionId[j]);
                    qinfo->RewardFactionId[j] = 0;            // quest will not reward this
                }
            }

            else if (qinfo->RewardFactionValueIdOverride[j] != 0)
            {
                sLog->outErrorDb("Quest %u has `RewardFactionId%d` = 0 but `RewardFactionValueIdOverride%d` = %i.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardFactionValueIdOverride[j]);
                // no changes, quest ignore this data
            }
        }

        if (qinfo->RewardDisplaySpell)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->RewardDisplaySpell);

            if (!spellInfo)
            {
                sLog->outErrorDb("Quest %u has `RewardDisplaySpell` = %u but spell %u does not exist, spell removed as display reward.",
                    qinfo->GetQuestId(), qinfo->RewardDisplaySpell, qinfo->RewardDisplaySpell);
                qinfo->RewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }

            else if (!SpellMgr::ComputeIsSpellValid(spellInfo))
            {
                sLog->outErrorDb("Quest %u has `RewardDisplaySpell` = %u but spell %u is broken, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->RewardDisplaySpell, qinfo->RewardDisplaySpell);
                qinfo->RewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }

            else if (GetTalentSpellCost(qinfo->RewardDisplaySpell))
            {
                sLog->outErrorDb("Quest %u has `RewardDisplaySpell` = %u but spell %u is talent, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->RewardDisplaySpell, qinfo->RewardDisplaySpell);
                qinfo->RewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }
        }

        if (qinfo->RewardSpell > 0)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->RewardSpell);

            if (!spellInfo)
            {
                sLog->outErrorDb("Quest %u has `RewardSpell` = %u but spell %u does not exist, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->RewardSpell, qinfo->RewardSpell);
                qinfo->RewardSpell = 0;                    // no spell will be casted on player
            }

            else if (!SpellMgr::ComputeIsSpellValid(spellInfo))
            {
                sLog->outErrorDb("Quest %u has `RewardSpell` = %u but spell %u is broken, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->RewardSpell, qinfo->RewardSpell);
                qinfo->RewardSpell = 0;                    // no spell will be casted on player
            }

            else if (GetTalentSpellCost(qinfo->RewardSpell))
            {
                sLog->outErrorDb("Quest %u has `RewardDisplaySpell` = %u but spell %u is talent, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->RewardSpell, qinfo->RewardSpell);
                qinfo->RewardSpell = 0;                    // no spell will be casted on player
            }
        }

        if (qinfo->RewardNextQuest)
        {
            QuestMap::iterator qNextItr = _questTemplates.find(qinfo->RewardNextQuest);
            if (qNextItr == _questTemplates.end())
            {
                sLog->outErrorDb("Quest %u has `RewardNextQuest` = %u but quest %u does not exist, quest chain will not work.",
                    qinfo->GetQuestId(), qinfo->RewardNextQuest, qinfo->RewardNextQuest);
                qinfo->RewardNextQuest = 0;
            }
            else
                qNextItr->second->prevChainQuests.push_back(qinfo->GetQuestId());
        }

        // fill additional data stores
        if (qinfo->PrevQuestId)
        {
            if (_questTemplates.find(abs(qinfo->GetPrevQuestId())) == _questTemplates.end())
            {
                sLog->outErrorDb("Quest %d has PrevQuestId %i, but no such quest", qinfo->GetQuestId(), qinfo->GetPrevQuestId());
            }
            else
            {
                qinfo->prevQuests.push_back(qinfo->PrevQuestId);
            }
        }

        if (qinfo->NextQuestId)
        {
            QuestMap::iterator qNextItr = _questTemplates.find(abs(qinfo->GetNextQuestId()));
            if (qNextItr == _questTemplates.end())
            {
                sLog->outErrorDb("Quest %d has NextQuestId %i, but no such quest", qinfo->GetQuestId(), qinfo->GetNextQuestId());
            }
            else
            {
                int32 signedQuestId = qinfo->NextQuestId < 0 ? -int32(qinfo->GetQuestId()) : int32(qinfo->GetQuestId());
                qNextItr->second->prevQuests.push_back(signedQuestId);
            }
        }

        if (qinfo->ExclusiveGroup)
            mExclusiveQuestGroups.insert(std::pair<int32, uint32>(qinfo->ExclusiveGroup, qinfo->GetQuestId()));
        if (qinfo->TimeAllowed)
            qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED);
        if (qinfo->RequiredPlayerKills)
            qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_PLAYER_KILL);
    }

    // check QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT for spell with SPELL_EFFECT_QUEST_COMPLETE
    for (uint32 i = 0; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(i);
        if (!spellInfo)
            continue;

        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            if (spellInfo->Effects[j].Effect != SPELL_EFFECT_QUEST_COMPLETE)
                continue;

            uint32 quest_id = spellInfo->Effects[j].MiscValue;

            Quest const* quest = GetQuestTemplate(quest_id);

            // some quest referenced in spells not exist (outdated spells)
            if (!quest)
                continue;

            if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
            {
                sLog->outErrorDb("Spell (id: %u) have SPELL_EFFECT_QUEST_COMPLETE for quest %u, but quest not have specialflag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT. Quest flags must be fixed, quest modified to enable objective.", spellInfo->Id, quest_id);

                // this will prevent quest completing without objective
				// xinef: remove this, leave error but do not break the quest
                // const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);
            }
        }
    }

    sLog->outString(">> Loaded %lu quests definitions in %u ms", (unsigned long)_questTemplates.size(), GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadScripts(ScriptsType type)
{
    uint32 oldMSTime = getMSTime();

    ScriptMapMap* scripts = GetScriptsMapByType(type);
    if (!scripts)
        return;

    std::string tableName = GetScriptsTableNameByType(type);
    if (tableName.empty())
        return;

    if (sScriptMgr->IsScriptScheduled())                    // function cannot be called when scripts are in use.
        return;

    sLog->outString("Loading %s...", tableName.c_str());

    scripts->clear();                                       // need for reload support

    bool isSpellScriptTable = (type == SCRIPTS_SPELL);
    //                                                 0    1       2         3         4          5    6  7  8  9
    QueryResult result = WorldDatabase.PQuery("SELECT id, delay, command, datalong, datalong2, dataint, x, y, z, o%s FROM %s", isSpellScriptTable ? ", effIndex" : "", tableName.c_str());

    if (!result)
    {
        sLog->outString(">> Loaded 0 script definitions. DB table `%s` is empty!", tableName.c_str());
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        ScriptInfo tmp;
        tmp.type      = type;
        tmp.id           = fields[0].GetUInt32();
        if (isSpellScriptTable)
            tmp.id      |= fields[10].GetUInt8() << 24;
        tmp.delay        = fields[1].GetUInt32();
        tmp.command      = ScriptCommands(fields[2].GetUInt32());
        tmp.Raw.nData[0] = fields[3].GetUInt32();
        tmp.Raw.nData[1] = fields[4].GetUInt32();
        tmp.Raw.nData[2] = fields[5].GetInt32();
        tmp.Raw.fData[0] = fields[6].GetFloat();
        tmp.Raw.fData[1] = fields[7].GetFloat();
        tmp.Raw.fData[2] = fields[8].GetFloat();
        tmp.Raw.fData[3] = fields[9].GetFloat();

        // generic command args check
        switch (tmp.command)
        {
            case SCRIPT_COMMAND_TALK:
            {
                if (tmp.Talk.ChatType > CHAT_TYPE_WHISPER && tmp.Talk.ChatType != CHAT_MSG_RAID_BOSS_WHISPER)
                {
                    sLog->outErrorDb("Table `%s` has invalid talk type (datalong = %u) in SCRIPT_COMMAND_TALK for script id %u",
                        tableName.c_str(), tmp.Talk.ChatType, tmp.id);
                    continue;
                }
                if (!tmp.Talk.TextID)
                {
                    sLog->outErrorDb("Table `%s` has invalid talk text id (dataint = %i) in SCRIPT_COMMAND_TALK for script id %u",
                        tableName.c_str(), tmp.Talk.TextID, tmp.id);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_EMOTE:
            {
                if (!sEmotesStore.LookupEntry(tmp.Emote.EmoteID))
                {
                    sLog->outErrorDb("Table `%s` has invalid emote id (datalong = %u) in SCRIPT_COMMAND_EMOTE for script id %u",
                        tableName.c_str(), tmp.Emote.EmoteID, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_TELEPORT_TO:
            {
                if (!sMapStore.LookupEntry(tmp.TeleportTo.MapID))
                {
                    sLog->outErrorDb("Table `%s` has invalid map (Id: %u) in SCRIPT_COMMAND_TELEPORT_TO for script id %u",
                        tableName.c_str(), tmp.TeleportTo.MapID, tmp.id);
                    continue;
                }

                if (!Trinity::IsValidMapCoord(tmp.TeleportTo.DestX, tmp.TeleportTo.DestY, tmp.TeleportTo.DestZ, tmp.TeleportTo.Orientation))
                {
                    sLog->outErrorDb("Table `%s` has invalid coordinates (X: %f Y: %f Z: %f O: %f) in SCRIPT_COMMAND_TELEPORT_TO for script id %u",
                        tableName.c_str(), tmp.TeleportTo.DestX, tmp.TeleportTo.DestY, tmp.TeleportTo.DestZ, tmp.TeleportTo.Orientation, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_QUEST_EXPLORED:
            {
                Quest const* quest = GetQuestTemplate(tmp.QuestExplored.QuestID);
                if (!quest)
                {
                    sLog->outErrorDb("Table `%s` has invalid quest (ID: %u) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u",
                        tableName.c_str(), tmp.QuestExplored.QuestID, tmp.id);
                    continue;
                }

                if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
                {
                    sLog->outErrorDb("Table `%s` has quest (ID: %u) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, but quest not have specialflag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT in quest flags. Script command or quest flags wrong. Quest modified to require objective.",
                        tableName.c_str(), tmp.QuestExplored.QuestID, tmp.id);

                    // this will prevent quest completing without objective
                    const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);

                    // continue; - quest objective requirement set and command can be allowed
                }

                if (float(tmp.QuestExplored.Distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    sLog->outErrorDb("Table `%s` has too large distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u",
                        tableName.c_str(), tmp.QuestExplored.Distance, tmp.id);
                    continue;
                }

                if (tmp.QuestExplored.Distance && float(tmp.QuestExplored.Distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    sLog->outErrorDb("Table `%s` has too large distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, max distance is %f or 0 for disable distance check",
                        tableName.c_str(), tmp.QuestExplored.Distance, tmp.id, DEFAULT_VISIBILITY_DISTANCE);
                    continue;
                }

                if (tmp.QuestExplored.Distance && float(tmp.QuestExplored.Distance) < INTERACTION_DISTANCE)
                {
                    sLog->outErrorDb("Table `%s` has too small distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, min distance is %f or 0 for disable distance check",
                        tableName.c_str(), tmp.QuestExplored.Distance, tmp.id, INTERACTION_DISTANCE);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_KILL_CREDIT:
            {
                if (!GetCreatureTemplate(tmp.KillCredit.CreatureEntry))
                {
                    sLog->outErrorDb("Table `%s` has invalid creature (Entry: %u) in SCRIPT_COMMAND_KILL_CREDIT for script id %u",
                        tableName.c_str(), tmp.KillCredit.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT:
            {
                GameObjectData const* data = GetGOData(tmp.RespawnGameobject.GOGuid);
                if (!data)
                {
                    sLog->outErrorDb("Table `%s` has invalid gameobject (GUID: %u) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u",
                        tableName.c_str(), tmp.RespawnGameobject.GOGuid, tmp.id);
                    continue;
                }

                GameObjectTemplate const* info = GetGameObjectTemplate(data->id);
                if (!info)
                {
                    sLog->outErrorDb("Table `%s` has gameobject with invalid entry (GUID: %u Entry: %u) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u",
                        tableName.c_str(), tmp.RespawnGameobject.GOGuid, data->id, tmp.id);
                    continue;
                }

                if (info->type == GAMEOBJECT_TYPE_FISHINGNODE ||
                    info->type == GAMEOBJECT_TYPE_FISHINGHOLE ||
                    info->type == GAMEOBJECT_TYPE_DOOR        ||
                    info->type == GAMEOBJECT_TYPE_BUTTON      ||
                    info->type == GAMEOBJECT_TYPE_TRAP)
                {
                    sLog->outErrorDb("Table `%s` have gameobject type (%u) unsupported by command SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u",
                        tableName.c_str(), info->entry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE:
            {
                if (!Trinity::IsValidMapCoord(tmp.TempSummonCreature.PosX, tmp.TempSummonCreature.PosY, tmp.TempSummonCreature.PosZ, tmp.TempSummonCreature.Orientation))
                {
                    sLog->outErrorDb("Table `%s` has invalid coordinates (X: %f Y: %f Z: %f O: %f) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id %u",
                        tableName.c_str(), tmp.TempSummonCreature.PosX, tmp.TempSummonCreature.PosY, tmp.TempSummonCreature.PosZ, tmp.TempSummonCreature.Orientation, tmp.id);
                    continue;
                }

				uint32 entry = tmp.TempSummonCreature.CreatureEntry;
                if (!GetCreatureTemplate(entry))
                {
                    sLog->outErrorDb("Table `%s` has invalid creature (Entry: %u) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id %u",
                        tableName.c_str(), tmp.TempSummonCreature.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_OPEN_DOOR:
            case SCRIPT_COMMAND_CLOSE_DOOR:
            {
                GameObjectData const* data = GetGOData(tmp.ToggleDoor.GOGuid);
                if (!data)
                {
                    sLog->outErrorDb("Table `%s` has invalid gameobject (GUID: %u) in %s for script id %u",
                        tableName.c_str(), tmp.ToggleDoor.GOGuid, GetScriptCommandName(tmp.command).c_str(), tmp.id);
                    continue;
                }

                GameObjectTemplate const* info = GetGameObjectTemplate(data->id);
                if (!info)
                {
                    sLog->outErrorDb("Table `%s` has gameobject with invalid entry (GUID: %u Entry: %u) in %s for script id %u",
                        tableName.c_str(), tmp.ToggleDoor.GOGuid, data->id, GetScriptCommandName(tmp.command).c_str(), tmp.id);
                    continue;
                }

                if (info->type != GAMEOBJECT_TYPE_DOOR)
                {
                    sLog->outErrorDb("Table `%s` has gameobject type (%u) non supported by command %s for script id %u",
                        tableName.c_str(), info->entry, GetScriptCommandName(tmp.command).c_str(), tmp.id);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_REMOVE_AURA:
            {
                if (!sSpellMgr->GetSpellInfo(tmp.RemoveAura.SpellID))
                {
                    sLog->outErrorDb("Table `%s` using non-existent spell (id: %u) in SCRIPT_COMMAND_REMOVE_AURA for script id %u",
                        tableName.c_str(), tmp.RemoveAura.SpellID, tmp.id);
                    continue;
                }
                if (tmp.RemoveAura.Flags & ~0x1)                    // 1 bits (0, 1)
                {
                    sLog->outErrorDb("Table `%s` using unknown flags in datalong2 (%u) in SCRIPT_COMMAND_REMOVE_AURA for script id %u",
                        tableName.c_str(), tmp.RemoveAura.Flags, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_CAST_SPELL:
            {
                if (!sSpellMgr->GetSpellInfo(tmp.CastSpell.SpellID))
                {
                    sLog->outErrorDb("Table `%s` using non-existent spell (id: %u) in SCRIPT_COMMAND_CAST_SPELL for script id %u",
                        tableName.c_str(), tmp.CastSpell.SpellID, tmp.id);
                    continue;
                }
                if (tmp.CastSpell.Flags > 4)                      // targeting type
                {
                    sLog->outErrorDb("Table `%s` using unknown target in datalong2 (%u) in SCRIPT_COMMAND_CAST_SPELL for script id %u",
                        tableName.c_str(), tmp.CastSpell.Flags, tmp.id);
                    continue;
                }
                if (tmp.CastSpell.Flags != 4 && tmp.CastSpell.CreatureEntry & ~0x1)                      // 1 bit (0, 1)
                {
                    sLog->outErrorDb("Table `%s` using unknown flags in dataint (%u) in SCRIPT_COMMAND_CAST_SPELL for script id %u",
                        tableName.c_str(), tmp.CastSpell.CreatureEntry, tmp.id);
                    continue;
                }
                else if (tmp.CastSpell.Flags == 4 && !GetCreatureTemplate(tmp.CastSpell.CreatureEntry))
                {
                    sLog->outErrorDb("Table `%s` using invalid creature entry in dataint (%u) in SCRIPT_COMMAND_CAST_SPELL for script id %u",
                        tableName.c_str(), tmp.CastSpell.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_CREATE_ITEM:
            {
                if (!GetItemTemplate(tmp.CreateItem.ItemEntry))
                {
                    sLog->outErrorDb("Table `%s` has nonexistent item (entry: %u) in SCRIPT_COMMAND_CREATE_ITEM for script id %u",
                        tableName.c_str(), tmp.CreateItem.ItemEntry, tmp.id);
                    continue;
                }
                if (!tmp.CreateItem.Amount)
                {
                    sLog->outErrorDb("Table `%s` SCRIPT_COMMAND_CREATE_ITEM but amount is %u for script id %u",
                        tableName.c_str(), tmp.CreateItem.Amount, tmp.id);
                    continue;
                }
                break;
            }
            default:
                break;
        }

        if (scripts->find(tmp.id) == scripts->end())
        {
            ScriptMap emptyMap;
            (*scripts)[tmp.id] = emptyMap;
        }
        (*scripts)[tmp.id].insert(std::pair<uint32, ScriptInfo>(tmp.delay, tmp));

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u script definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadSpellScripts()
{
    LoadScripts(SCRIPTS_SPELL);

    // check ids
    for (ScriptMapMap::const_iterator itr = sSpellScripts.begin(); itr != sSpellScripts.end(); ++itr)
    {
        uint32 spellId = uint32(itr->first) & 0x00FFFFFF;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);

        if (!spellInfo)
        {
            sLog->outErrorDb("Table `spell_scripts` has not existing spell (Id: %u) as script id", spellId);
            continue;
        }

        uint8 i = (uint8)((uint32(itr->first) >> 24) & 0x000000FF);
        //check for correct spellEffect
        if (!spellInfo->Effects[i].Effect || (spellInfo->Effects[i].Effect != SPELL_EFFECT_SCRIPT_EFFECT && spellInfo->Effects[i].Effect != SPELL_EFFECT_DUMMY))
            sLog->outErrorDb("Table `spell_scripts` - spell %u effect %u is not SPELL_EFFECT_SCRIPT_EFFECT or SPELL_EFFECT_DUMMY", spellId, i);
    }
}

void ObjectMgr::LoadEventScripts()
{
    LoadScripts(SCRIPTS_EVENT);

    std::set<uint32> evt_scripts;
    // Load all possible script entries from gameobjects
    GameObjectTemplateContainer const* gotc = sObjectMgr->GetGameObjectTemplates();
    for (GameObjectTemplateContainer::const_iterator itr = gotc->begin(); itr != gotc->end(); ++itr)
        if (uint32 eventId = itr->second.GetEventScriptId())
            evt_scripts.insert(eventId);

    // Load all possible script entries from spells
    for (uint32 i = 1; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
        if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(i))
            for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
                if (spell->Effects[j].Effect == SPELL_EFFECT_SEND_EVENT)
                    if (spell->Effects[j].MiscValue)
                        evt_scripts.insert(spell->Effects[j].MiscValue);

    for (size_t path_idx = 0; path_idx < sTaxiPathNodesByPath.size(); ++path_idx)
    {
        for (size_t node_idx = 0; node_idx < sTaxiPathNodesByPath[path_idx].size(); ++node_idx)
        {
            TaxiPathNodeEntry const* node = sTaxiPathNodesByPath[path_idx][node_idx];

            if (node->arrivalEventID)
                evt_scripts.insert(node->arrivalEventID);

            if (node->departureEventID)
                evt_scripts.insert(node->departureEventID);
        }
    }

    // Then check if all scripts are in above list of possible script entries
    for (ScriptMapMap::const_iterator itr = sEventScripts.begin(); itr != sEventScripts.end(); ++itr)
    {
        std::set<uint32>::const_iterator itr2 = evt_scripts.find(itr->first);
        if (itr2 == evt_scripts.end())
            sLog->outErrorDb("Table `event_scripts` has script (Id: %u) not referring to any gameobject_template type 10 data2 field, type 3 data6 field, type 13 data 2 field or any spell effect %u",
                itr->first, SPELL_EFFECT_SEND_EVENT);
    }
}

//Load WP Scripts
void ObjectMgr::LoadWaypointScripts()
{
    LoadScripts(SCRIPTS_WAYPOINT);

    std::set<uint32> actionSet;

    for (ScriptMapMap::const_iterator itr = sWaypointScripts.begin(); itr != sWaypointScripts.end(); ++itr)
        actionSet.insert(itr->first);

    PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_WAYPOINT_DATA_ACTION);
    PreparedQueryResult result = WorldDatabase.Query(stmt);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 action = fields[0].GetUInt32();

            actionSet.erase(action);
        }
        while (result->NextRow());
    }

    for (std::set<uint32>::iterator itr = actionSet.begin(); itr != actionSet.end(); ++itr)
        sLog->outErrorDb("There is no waypoint which links to the waypoint script %u", *itr);
}

void ObjectMgr::LoadSpellScriptNames()
{
    uint32 oldMSTime = getMSTime();

    _spellScriptsStore.clear();                            // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT spell_id, ScriptName FROM spell_script_names");

    if (!result)
    {
        sLog->outString(">> Loaded 0 spell script names. DB table `spell_script_names` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {

        Field* fields = result->Fetch();

        int32 spellId          = fields[0].GetInt32();
        const char *scriptName = fields[1].GetCString();

        bool allRanks = false;
        if (spellId <= 0)
        {
            allRanks = true;
            spellId = -spellId;
        }

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
        {
            sLog->outErrorDb("Scriptname:`%s` spell (spell_id:%d) does not exist in `Spell.dbc`.", scriptName, fields[0].GetInt32());
            continue;
        }

        if (allRanks)
        {
            if (sSpellMgr->GetFirstSpellInChain(spellId) != uint32(spellId))
            {
                sLog->outErrorDb("Scriptname:`%s` spell (spell_id:%d) is not first rank of spell.", scriptName, fields[0].GetInt32());
                continue;
            }
            while (spellInfo)
            {
                _spellScriptsStore.insert(SpellScriptsContainer::value_type(spellInfo->Id, GetScriptId(scriptName)));
                spellInfo = spellInfo->GetNextRankSpell();
            }
        }
        else
            _spellScriptsStore.insert(SpellScriptsContainer::value_type(spellInfo->Id, GetScriptId(scriptName)));
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u spell script names in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::ValidateSpellScripts()
{
    uint32 oldMSTime = getMSTime();

    if (_spellScriptsStore.empty())
    {
        sLog->outString(">> Validated 0 scripts.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    for (SpellScriptsContainer::iterator itr = _spellScriptsStore.begin(); itr != _spellScriptsStore.end();)
    {
        SpellInfo const* spellEntry = sSpellMgr->GetSpellInfo(itr->first);
        std::vector<std::pair<SpellScriptLoader *, SpellScriptsContainer::iterator> > SpellScriptLoaders;
        sScriptMgr->CreateSpellScriptLoaders(itr->first, SpellScriptLoaders);
        itr = _spellScriptsStore.upper_bound(itr->first);

        for (std::vector<std::pair<SpellScriptLoader *, SpellScriptsContainer::iterator> >::iterator sitr = SpellScriptLoaders.begin(); sitr != SpellScriptLoaders.end(); ++sitr)
        {
            SpellScript* spellScript = sitr->first->GetSpellScript();
            AuraScript* auraScript = sitr->first->GetAuraScript();
            bool valid = true;
            if (!spellScript && !auraScript)
            {
                sLog->outError("TSCR: Functions GetSpellScript() and GetAuraScript() of script `%s` do not return objects - script skipped",  GetScriptName(sitr->second->second));
                valid = false;
            }
            if (spellScript)
            {
                spellScript->_Init(&sitr->first->GetName(), spellEntry->Id);
                spellScript->_Register();
                if (!spellScript->_Validate(spellEntry))
                    valid = false;
                delete spellScript;
            }
            if (auraScript)
            {
                auraScript->_Init(&sitr->first->GetName(), spellEntry->Id);
                auraScript->_Register();
                if (!auraScript->_Validate(spellEntry))
                    valid = false;
                delete auraScript;
            }
            if (!valid)
            {
                _spellScriptsStore.erase(sitr->second);
            }
        }
        ++count;
    }

    sLog->outString(">> Validated %u scripts in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::InitializeSpellInfoPrecomputedData()
{
	uint32 limit = sSpellStore.GetNumRows();
	for(uint32 i=0; i<=limit; ++i)
		if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(i))
		{
			const_cast<SpellInfo*>(spellInfo)->SetStackableWithRanks(spellInfo->ComputeIsStackableWithRanks());
			const_cast<SpellInfo*>(spellInfo)->SetCritCapable(spellInfo->ComputeIsCritCapable());
			const_cast<SpellInfo*>(spellInfo)->SetSpellValid(SpellMgr::ComputeIsSpellValid(spellInfo, false));
		}
}

void ObjectMgr::LoadPageTexts()
{
    uint32 oldMSTime = getMSTime();

    //                                               0     1       2
    QueryResult result = WorldDatabase.Query("SELECT ID, Text, NextPageID FROM page_text");

    if (!result)
    {
        sLog->outString(">> Loaded 0 page texts. DB table `page_text` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        PageText& pageText = _pageTextStore[fields[0].GetUInt32()];

        pageText.Text     = fields[1].GetString();
        pageText.NextPage = fields[2].GetUInt32();

        ++count;
    }
    while (result->NextRow());

    for (PageTextContainer::const_iterator itr = _pageTextStore.begin(); itr != _pageTextStore.end(); ++itr)
    {
        if (itr->second.NextPage)
        {
            PageTextContainer::const_iterator itr2 = _pageTextStore.find(itr->second.NextPage);
            if (itr2 == _pageTextStore.end())
                sLog->outErrorDb("Page text (Id: %u) has not existing next page (Id: %u)", itr->first, itr->second.NextPage);

        }
    }

    sLog->outString(">> Loaded %u page texts in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

PageText const* ObjectMgr::GetPageText(uint32 pageEntry)
{
    PageTextContainer::const_iterator itr = _pageTextStore.find(pageEntry);
    if (itr != _pageTextStore.end())
        return &(itr->second);

    return NULL;
}

GossipText const* ObjectMgr::GetGossipText(uint32 Text_ID) const
{
    GossipTextContainer::const_iterator itr = _gossipTextStore.find(Text_ID);
    if (itr != _gossipTextStore.end())
        return &itr->second;
    return NULL;
}

void ObjectMgr::LoadGossipText()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT ID, "
        "text0_0, text0_1, BroadcastTextID0, lang0, Probability0, em0_0, em0_1, em0_2, em0_3, em0_4, em0_5, "
        "text1_0, text1_1, BroadcastTextID1, lang1, Probability1, em1_0, em1_1, em1_2, em1_3, em1_4, em1_5, "
        "text2_0, text2_1, BroadcastTextID2, lang2, Probability2, em2_0, em2_1, em2_2, em2_3, em2_4, em2_5, "
        "text3_0, text3_1, BroadcastTextID3, lang3, Probability3, em3_0, em3_1, em3_2, em3_3, em3_4, em3_5, "
        "text4_0, text4_1, BroadcastTextID4, lang4, Probability4, em4_0, em4_1, em4_2, em4_3, em4_4, em4_5, "
        "text5_0, text5_1, BroadcastTextID5, lang5, Probability5, em5_0, em5_1, em5_2, em5_3, em5_4, em5_5, "
        "text6_0, text6_1, BroadcastTextID6, lang6, Probability6, em6_0, em6_1, em6_2, em6_3, em6_4, em6_5, "
        "text7_0, text7_1, BroadcastTextID7, lang7, Probability7, em7_0, em7_1, em7_2, em7_3, em7_4, em7_5 "
        "FROM npc_text");
;
    if (!result)
    {
        sLog->outString(">> Loaded 0 npc texts, table is empty!");
        sLog->outString();
        return;
    }
    _gossipTextStore.rehash(result->GetRowCount());

    uint32 count = 0;
    uint8 cic;

    do
    {
        ++count;
        cic = 0;

        Field* fields = result->Fetch();

        uint32 Text_ID    = fields[cic++].GetUInt32();
        if (!Text_ID)
        {
            sLog->outErrorDb("Table `npc_text` has record with reserved id 0, ignore.");
            continue;
        }

        GossipText& gText = _gossipTextStore[Text_ID];

        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; i++)
        {
            gText.Options[i].Text_0           = fields[cic++].GetString();
            gText.Options[i].Text_1           = fields[cic++].GetString();
//            gText.Options[i].BroadcastTextID = fields[cic++].GetUInt32();
            cic++; // To skip broadcasttext from query.

            gText.Options[i].Language         = fields[cic++].GetUInt8();
            gText.Options[i].Probability      = fields[cic++].GetFloat();

            for (uint8 j=0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                gText.Options[i].Emotes[j]._Delay  = fields[cic++].GetUInt16();
                gText.Options[i].Emotes[j]._Emote  = fields[cic++].GetUInt16();
            }
        }
    } while (result->NextRow());

    sLog->outString(">> Loaded %u npc texts in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::ReturnOrDeleteOldMails(bool serverUp)
{
    uint32 oldMSTime = getMSTime();

    time_t curTime = time(NULL);

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL);
    stmt->setUInt32(0, curTime);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return;

    std::map<uint32 /*messageId*/, MailItemInfoVec> itemsCache;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL_ITEMS);
    stmt->setUInt32(0, curTime);
    if (PreparedQueryResult items = CharacterDatabase.Query(stmt))
    {
        MailItemInfo item;
        do
        {
            Field* fields = items->Fetch();
            item.item_guid = fields[0].GetUInt32();
            item.item_template = fields[1].GetUInt32();
            uint32 mailId = fields[2].GetUInt32();
            itemsCache[mailId].push_back(item);
        } while (items->NextRow());
    }

    uint32 deletedCount = 0;
    uint32 returnedCount = 0;
    do
    {
        Field* fields = result->Fetch();
        Mail* m = new Mail;
        m->messageID      = fields[0].GetUInt32();
        m->messageType    = fields[1].GetUInt8();
        m->sender         = fields[2].GetUInt32();
        m->receiver       = fields[3].GetUInt32();
        bool has_items    = fields[4].GetBool();
        m->expire_time    = time_t(fields[5].GetUInt32());
        m->deliver_time   = 0;
        m->COD            = fields[6].GetUInt32();
        m->checked        = fields[7].GetUInt8();
        m->mailTemplateId = fields[8].GetInt16();

        Player* player = NULL;
        if (serverUp)
            player = ObjectAccessor::FindPlayerInOrOutOfWorld(MAKE_NEW_GUID(m->receiver, 0, HIGHGUID_PLAYER));

        if (player) // don't modify mails of a logged in player
        {
            delete m;
            continue;
        }

        // Delete or return mail
        if (has_items)
        {
            // read items from cache
            m->items.swap(itemsCache[m->messageID]);

            // don't return if: is mail from non-player, or sent to self, or already returned, or read and isn't COD
            if (m->messageType != MAIL_NORMAL || m->receiver == m->sender || (m->checked & (MAIL_CHECK_MASK_COD_PAYMENT | MAIL_CHECK_MASK_RETURNED)) || ((m->checked & MAIL_CHECK_MASK_READ) && !m->COD))
            {
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
                    stmt->setUInt32(0, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);
                }

				stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM_BY_ID);
				stmt->setUInt32(0, m->messageID);
				CharacterDatabase.Execute(stmt);
            }
            else
            {
                // Mail will be returned
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_RETURNED);
                stmt->setUInt32(0, m->receiver);
                stmt->setUInt32(1, m->sender);
                stmt->setUInt32(2, curTime + 30 * DAY);
                stmt->setUInt32(3, curTime);
                stmt->setUInt8 (4, uint8(MAIL_CHECK_MASK_RETURNED));
                stmt->setUInt32(5, m->messageID);
                CharacterDatabase.Execute(stmt);
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    // Update receiver in mail items for its proper delivery, and in instance_item for avoid lost item at sender delete
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_ITEM_RECEIVER);
                    stmt->setUInt32(0, m->sender);
                    stmt->setUInt32(1, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_OWNER);
                    stmt->setUInt32(0, m->sender);
                    stmt->setUInt32(1, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);
                }

				// xinef: update global data
				sWorld->UpdateGlobalPlayerMails(m->sender, 1);
				sWorld->UpdateGlobalPlayerMails(m->receiver, -1);

                delete m;
                ++returnedCount;
                continue;
            }
        }

		// xinef: update global data
		sWorld->UpdateGlobalPlayerMails(m->receiver, -1);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
        stmt->setUInt32(0, m->messageID);
        CharacterDatabase.Execute(stmt);
        delete m;
        ++deletedCount;
    }
    while (result->NextRow());

    sLog->outString(">> Processed %u expired mails: %u deleted and %u returned in %u ms", deletedCount + returnedCount, deletedCount, returnedCount, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadQuestAreaTriggers()
{
    uint32 oldMSTime = getMSTime();

    _questAreaTriggerStore.clear();                           // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT id, quest FROM areatrigger_involvedrelation");

    if (!result)
    {
        sLog->outString(">> Loaded 0 quest trigger points. DB table `areatrigger_involvedrelation` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 trigger_ID = fields[0].GetUInt32();
        uint32 quest_ID   = fields[1].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(trigger_ID);
        if (!atEntry)
        {
            sLog->outErrorDb("Area trigger (ID:%u) does not exist in `AreaTrigger.dbc`.", trigger_ID);
            continue;
        }

        Quest const* quest = GetQuestTemplate(quest_ID);

        if (!quest)
        {
            sLog->outErrorDb("Table `areatrigger_involvedrelation` has record (id: %u) for not existing quest %u", trigger_ID, quest_ID);
            continue;
        }

        if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        {
            sLog->outErrorDb("Table `areatrigger_involvedrelation` has record (id: %u) for not quest %u, but quest not have specialflag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT. Trigger or quest flags must be fixed, quest modified to require objective.", trigger_ID, quest_ID);

            // this will prevent quest completing without objective
            const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);

            // continue; - quest modified to required objective and trigger can be allowed.
        }

        _questAreaTriggerStore[trigger_ID] = quest_ID;

    } while (result->NextRow());

    sLog->outString(">> Loaded %u quest trigger points in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadTavernAreaTriggers()
{
    uint32 oldMSTime = getMSTime();

    _tavernAreaTriggerStore.clear();                          // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT id FROM areatrigger_tavern");

    if (!result)
    {
        sLog->outString(">> Loaded 0 tavern triggers. DB table `areatrigger_tavern` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 Trigger_ID      = fields[0].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            sLog->outErrorDb("Area trigger (ID:%u) does not exist in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }

        _tavernAreaTriggerStore.insert(Trigger_ID);
    } while (result->NextRow());

    sLog->outString(">> Loaded %u tavern triggers in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadAreaTriggerScripts()
{
    uint32 oldMSTime = getMSTime();

    _areaTriggerScriptStore.clear();                            // need for reload case
    QueryResult result = WorldDatabase.Query("SELECT entry, ScriptName FROM areatrigger_scripts");

    if (!result)
    {
        sLog->outString(">> Loaded 0 areatrigger scripts. DB table `areatrigger_scripts` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 Trigger_ID      = fields[0].GetUInt32();
        const char *scriptName = fields[1].GetCString();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            sLog->outErrorDb("Area trigger (ID:%u) does not exist in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }
        _areaTriggerScriptStore[Trigger_ID] = GetScriptId(scriptName);
    } while (result->NextRow());

    sLog->outString(">> Loaded %u areatrigger scripts in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

uint32 ObjectMgr::GetNearestTaxiNode(float x, float y, float z, uint32 mapid, uint32 teamId)
{
    bool found = false;
    float dist = 10000;
    uint32 id = 0;

    for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i);

        if (!node || node->map_id != mapid || (!node->MountCreatureID[teamId == TEAM_ALLIANCE ? 1 : 0] && node->MountCreatureID[0] != 32981)) // dk flight
            continue;

        uint8  field   = (uint8)((i - 1) / 32);
        uint32 submask = 1<<((i-1)%32);

        // skip not taxi network nodes
        if ((sTaxiNodesMask[field] & submask) == 0)
            continue;

        float dist2 = (node->x - x)*(node->x - x)+(node->y - y)*(node->y - y)+(node->z - z)*(node->z - z);
        if (found)
        {
            if (dist2 < dist)
            {
                dist = dist2;
                id = i;
            }
        }
        else
        {
            found = true;
            dist = dist2;
            id = i;
        }
    }

    return id;
}

void ObjectMgr::GetTaxiPath(uint32 source, uint32 destination, uint32 &path, uint32 &cost)
{
    TaxiPathSetBySource::iterator src_i = sTaxiPathSetBySource.find(source);
    if (src_i == sTaxiPathSetBySource.end())
    {
        path = 0;
        cost = 0;
        return;
    }

    TaxiPathSetForSource& pathSet = src_i->second;

    TaxiPathSetForSource::iterator dest_i = pathSet.find(destination);
    if (dest_i == pathSet.end())
    {
        path = 0;
        cost = 0;
        return;
    }

    cost = dest_i->second.price;
    path = dest_i->second.ID;
}

uint32 ObjectMgr::GetTaxiMountDisplayId(uint32 id, TeamId teamId, bool allowed_alt_team /* = false */)
{
    uint32 mount_id = 0;

    // select mount creature id
    TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(id);
    if (node)
    {
        uint32 mount_entry = node->MountCreatureID[teamId == TEAM_ALLIANCE ? 1 : 0];

        // Fix for Alliance not being able to use Acherus taxi
        // only one mount type for both sides
        if (mount_entry == 0 && allowed_alt_team)
        {
            // Simply reverse the selection. At least one team in theory should have a valid mount ID to choose.
            mount_entry = node->MountCreatureID[teamId];
        }

        CreatureTemplate const* mount_info = GetCreatureTemplate(mount_entry);
        if (mount_info)
        {
            mount_id = mount_info->GetRandomValidModelId();
            if (!mount_id)
            {
                sLog->outErrorDb("No displayid found for the taxi mount with the entry %u! Can't load it!", mount_entry);
                return 0;
            }
        }
    }

    // minfo is not actually used but the mount_id was updated
    GetCreatureModelRandomGender(&mount_id);

    return mount_id;
}

void ObjectMgr::LoadGraveyardZones()
{
    uint32 oldMSTime = getMSTime();

    GraveyardStore.clear();                                  // need for reload case

    //                                                0       1         2
    QueryResult result = WorldDatabase.Query("SELECT id, ghost_zone, faction FROM game_graveyard_zone");

    if (!result)
    {
        sLog->outString(">> Loaded 0 graveyard-zone links. DB table `game_graveyard_zone` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 safeLocId = fields[0].GetUInt32();
        uint32 zoneId = fields[1].GetUInt32();
        uint32 team   = fields[2].GetUInt16();
		TeamId teamId = team == 0 ? TEAM_NEUTRAL : (team == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE);

        WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(safeLocId);
        if (!entry)
        {
            sLog->outErrorDb("Table `game_graveyard_zone` has a record for not existing graveyard (WorldSafeLocs.dbc id) %u, skipped.", safeLocId);
            continue;
        }

        AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(zoneId);
        if (!areaEntry)
        {
            sLog->outErrorDb("Table `game_graveyard_zone` has a record for not existing zone id (%u), skipped.", zoneId);
            continue;
        }

        if (areaEntry->zone != 0)
        {
            sLog->outErrorDb("Table `game_graveyard_zone` has a record for subzone id (%u) instead of zone, skipped.", zoneId);
            continue;
        }

        if (team != 0 && team != HORDE && team != ALLIANCE)
        {
            sLog->outErrorDb("Table `game_graveyard_zone` has a record for non player faction (%u), skipped.", team);
            continue;
        }

        if (!AddGraveyardLink(safeLocId, zoneId, teamId, false))
            sLog->outErrorDb("Table `game_graveyard_zone` has a duplicate record for Graveyard (ID: %u) and Zone (ID: %u), skipped.", safeLocId, zoneId);
    } while (result->NextRow());

    sLog->outString(">> Loaded %u graveyard-zone links in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

WorldSafeLocsEntry const* ObjectMgr::GetDefaultGraveyard(TeamId teamId)
{
    enum DefaultGraveyard
    {
        HORDE_GRAVEYARD = 10, // Crossroads
        ALLIANCE_GRAVEYARD = 4, // Westfall
    };

	return sWorldSafeLocsStore.LookupEntry(teamId == TEAM_HORDE ? HORDE_GRAVEYARD : ALLIANCE_GRAVEYARD);
}

WorldSafeLocsEntry const* ObjectMgr::GetClosestGraveyard(float x, float y, float z, uint32 MapId, TeamId teamId)
{
    // search for zone associated closest graveyard
    uint32 zoneId = sMapMgr->GetZoneId(MapId, x, y, z);

    if (!zoneId)
    {
        if (z > -500)
        {
            sLog->outError("ZoneId not found for map %u coords (%f, %f, %f)", MapId, x, y, z);
            return GetDefaultGraveyard(teamId);
        }
    }

    // Simulate std. algorithm:
    //   found some graveyard associated to (ghost_zone, ghost_map)
    //
    //   if mapId == graveyard.mapId (ghost in plain zone or city or battleground) and search graveyard at same map
    //     then check faction
    //   if mapId != graveyard.mapId (ghost in instance) and search any graveyard associated
    //     then check faction
    GraveyardMapBounds range = GraveyardStore.equal_range(zoneId);
    MapEntry const* map = sMapStore.LookupEntry(MapId);

    // not need to check validity of map object; MapId _MUST_ be valid here
    if (range.first == range.second)
    {
        sLog->outErrorDb("Table `game_graveyard_zone` incomplete: Zone %u Team %u does not have a linked graveyard.", zoneId, teamId);
        return GetDefaultGraveyard(teamId);
    }

    // at corpse map
    bool foundNear = false;
    float distNear = 10000;
    WorldSafeLocsEntry const* entryNear = NULL;

    // at entrance map for corpse map
    bool foundEntr = false;
    float distEntr = 10000;
    WorldSafeLocsEntry const* entryEntr = NULL;

    // some where other
    WorldSafeLocsEntry const* entryFar = NULL;

    MapEntry const* mapEntry = sMapStore.LookupEntry(MapId);

    for (; range.first != range.second; ++range.first)
    {
        GraveyardData const& data = range.first->second;
        WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(data.safeLocId);
        if (!entry)
        {
            sLog->outErrorDb("Table `game_graveyard_zone` has record for not existing graveyard (WorldSafeLocs.dbc id) %u, skipped.", data.safeLocId);
            continue;
        }

        // skip enemy faction graveyard
        // team == 0 case can be at call from .neargrave
        if (data.teamId != TEAM_NEUTRAL && teamId != TEAM_NEUTRAL && data.teamId != teamId)
            continue;

        // find now nearest graveyard at other map
        if (MapId != entry->map_id)
        {
            // if find graveyard at different map from where entrance placed (or no entrance data), use any first
            if (!mapEntry
                || mapEntry->entrance_map < 0
                || uint32(mapEntry->entrance_map) != entry->map_id
                || (mapEntry->entrance_x == 0 && mapEntry->entrance_y == 0))
            {
                // not have any corrdinates for check distance anyway
                entryFar = entry;
                continue;
            }

            // at entrance map calculate distance (2D);
            float dist2 = (entry->x - mapEntry->entrance_x)*(entry->x - mapEntry->entrance_x)
                +(entry->y - mapEntry->entrance_y)*(entry->y - mapEntry->entrance_y);
            if (foundEntr)
            {
                if (dist2 < distEntr)
                {
                    distEntr = dist2;
                    entryEntr = entry;
                }
            }
            else
            {
                foundEntr = true;
                distEntr = dist2;
                entryEntr = entry;
            }
        }
        // find now nearest graveyard at same map
        else
        {
            float dist2 = (entry->x - x)*(entry->x - x)+(entry->y - y)*(entry->y - y)+(entry->z - z)*(entry->z - z);
            if (foundNear)
            {
                if (dist2 < distNear)
                {
                    distNear = dist2;
                    entryNear = entry;
                }
            }
            else
            {
                foundNear = true;
                distNear = dist2;
                entryNear = entry;
            }
        }
    }

    if (entryNear)
        return entryNear;

    if (entryEntr)
        return entryEntr;

    return entryFar;
}

GraveyardData const* ObjectMgr::FindGraveyardData(uint32 id, uint32 zoneId)
{
    GraveyardMapBounds range = GraveyardStore.equal_range(zoneId);
    for (; range.first != range.second; ++range.first)
    {
        GraveyardData const& data = range.first->second;
        if (data.safeLocId == id)
            return &data;
    }
    return NULL;
}

bool ObjectMgr::AddGraveyardLink(uint32 id, uint32 zoneId, TeamId teamId, bool persist /*= true*/)
{
    if (FindGraveyardData(id, zoneId))
        return false;

    // add link to loaded data
    GraveyardData data;
    data.safeLocId = id;
	data.teamId = teamId;

    GraveyardStore.insert(GraveyardContainer::value_type(zoneId, data));

    // add link to DB
    if (persist)
    {
        PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GRAVEYARD_ZONE);

        stmt->setUInt32(0, id);
        stmt->setUInt32(1, zoneId);
		// Xinef: DB Data compatibility...
        stmt->setUInt16(2, uint16(teamId == TEAM_NEUTRAL ? 0 : (teamId == TEAM_ALLIANCE ? ALLIANCE : HORDE)));

        WorldDatabase.Execute(stmt);
    }

    return true;
}

void ObjectMgr::RemoveGraveyardLink(uint32 id, uint32 zoneId, TeamId teamId, bool persist /*= false*/)
{
    GraveyardMapBoundsNonConst range = GraveyardStore.equal_range(zoneId);
    if (range.first == range.second)
    {
        //sLog->outError("Table `game_graveyard_zone` incomplete: Zone %u Team %u does not have a linked graveyard.", zoneId, team);
        return;
    }

    bool found = false;


    for (; range.first != range.second; ++range.first)
    {
        GraveyardData & data = range.first->second;

        // skip not matching safezone id
        if (data.safeLocId != id)
            continue;

        // skip enemy faction graveyard at same map (normal area, city, or battleground)
        // team == 0 case can be at call from .neargrave
        if (data.teamId != TEAM_NEUTRAL && teamId != TEAM_NEUTRAL && data.teamId != teamId)
            continue;

        found = true;
        break;
    }

    // no match, return
    if (!found)
        return;

    // remove from links
    GraveyardStore.erase(range.first);

    // remove link from DB
    if (persist)
    {
        PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GRAVEYARD_ZONE);

        stmt->setUInt32(0, id);
        stmt->setUInt32(1, zoneId);
		// Xinef: DB Data compatibility...
		stmt->setUInt16(2, uint16(teamId == TEAM_NEUTRAL ? 0 : (teamId == TEAM_ALLIANCE ? ALLIANCE : HORDE)));

        WorldDatabase.Execute(stmt);
    }
}

void ObjectMgr::LoadAreaTriggerTeleports()
{
    uint32 oldMSTime = getMSTime();

    _areaTriggerStore.clear();                                  // need for reload case

    //                                               0        1              2                  3                  4                   5
    QueryResult result = WorldDatabase.Query("SELECT ID,  target_map, target_position_x, target_position_y, target_position_z, target_orientation FROM areatrigger_teleport");
    if (!result)
    {
        sLog->outString(">> Loaded 0 area trigger teleport definitions. DB table `areatrigger_teleport` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        ++count;

        uint32 Trigger_ID = fields[0].GetUInt32();

        AreaTrigger at;

        at.target_mapId             = fields[1].GetUInt16();
        at.target_X                 = fields[2].GetFloat();
        at.target_Y                 = fields[3].GetFloat();
        at.target_Z                 = fields[4].GetFloat();
        at.target_Orientation       = fields[5].GetFloat();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            sLog->outErrorDb("Area trigger (ID:%u) does not exist in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }

        MapEntry const* mapEntry = sMapStore.LookupEntry(at.target_mapId);
        if (!mapEntry)
        {
            sLog->outErrorDb("Area trigger (ID:%u) target map (ID: %u) does not exist in `Map.dbc`.", Trigger_ID, at.target_mapId);
            continue;
        }

        if (at.target_X == 0 && at.target_Y == 0 && at.target_Z == 0)
        {
            sLog->outErrorDb("Area trigger (ID:%u) target coordinates not provided.", Trigger_ID);
            continue;
        }

        _areaTriggerStore[Trigger_ID] = at;

    } while (result->NextRow());

    sLog->outString(">> Loaded %u area trigger teleport definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

/*
 * Searches for the areatrigger which teleports players out of the given map with instance_template.parent field support
 */
AreaTrigger const* ObjectMgr::GetGoBackTrigger(uint32 Map) const
{
    bool useParentDbValue = false;
    uint32 parentId = 0;
    const MapEntry* mapEntry = sMapStore.LookupEntry(Map);
    if (!mapEntry || mapEntry->entrance_map < 0)
        return NULL;

    uint32 entrance_map = uint32(mapEntry->entrance_map);
    for (AreaTriggerContainer::const_iterator itr = _areaTriggerStore.begin(); itr != _areaTriggerStore.end(); ++itr)
        if ((!useParentDbValue && itr->second.target_mapId == entrance_map) || (useParentDbValue && itr->second.target_mapId == parentId))
        {
            AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(itr->first);
            if (atEntry && atEntry->mapid == Map)
                return &itr->second;
        }
    return NULL;
}

/**
 * Searches for the areatrigger which teleports players to the given map
 */
AreaTrigger const* ObjectMgr::GetMapEntranceTrigger(uint32 Map) const
{
    for (AreaTriggerContainer::const_iterator itr = _areaTriggerStore.begin(); itr != _areaTriggerStore.end(); ++itr)
    {
		if (itr->second.target_mapId == Map) // Id is used to determine correct Scarlet Monastery instance
        {
			// xinef: no need to check, already done at loading
            //AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(itr->first);
            //if (atEntry)
                return &itr->second;
        }
    }
    return NULL;
}

void ObjectMgr::SetHighestGuids()
{
    QueryResult result = CharacterDatabase.Query("SELECT MAX(guid) FROM characters");
    if (result)
        _hiCharGuid = (*result)[0].GetUInt32()+1;

    result = WorldDatabase.Query("SELECT MAX(guid) FROM creature");
    if (result)
        _hiCreatureGuid = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(guid) FROM item_instance");
    if (result)
        _hiItemGuid = (*result)[0].GetUInt32()+1;

    // Cleanup other tables from not existed guids ( >= _hiItemGuid)
    CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE item >= '%u'", _hiItemGuid);      // One-time query
    CharacterDatabase.PExecute("DELETE FROM mail_items WHERE item_guid >= '%u'", _hiItemGuid);          // One-time query
    CharacterDatabase.PExecute("DELETE FROM auctionhouse WHERE itemguid >= '%u'", _hiItemGuid);         // One-time query
    CharacterDatabase.PExecute("DELETE FROM guild_bank_item WHERE item_guid >= '%u'", _hiItemGuid);     // One-time query

    result = WorldDatabase.Query("SELECT MAX(guid) FROM gameobject");
    if (result)
        _hiGoGuid = (*result)[0].GetUInt32()+1;

    result = WorldDatabase.Query("SELECT MAX(guid) FROM transports");
    if (result)
        _hiMoTransGuid = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(id) FROM auctionhouse");
    if (result)
        _auctionId = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(id) FROM mail");
    if (result)
        _mailId = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(corpseGuid) FROM corpse");
    if (result)
        _hiCorpseGuid = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(setguid) FROM character_equipmentsets");
    if (result)
        _equipmentSetGuid = (*result)[0].GetUInt64()+1;

    result = CharacterDatabase.Query("SELECT MAX(guildId) FROM guild");
    if (result)
        sGuildMgr->SetNextGuildId((*result)[0].GetUInt32()+1);
}

uint32 ObjectMgr::GenerateAuctionID()
{
    if (_auctionId >= 0xFFFFFFFE)
    {
        sLog->outError("Auctions ids overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _auctionId++;
}

uint64 ObjectMgr::GenerateEquipmentSetGuid()
{
    if (_equipmentSetGuid >= uint64(0xFFFFFFFFFFFFFFFELL))
    {
        sLog->outError("EquipmentSet guid overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _equipmentSetGuid++;
}

uint32 ObjectMgr::GenerateMailID()
{
    if (_mailId >= 0xFFFFFFFE)
    {
        sLog->outError("Mail ids overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    TRINITY_GUARD(ACE_Thread_Mutex, _mailIdMutex);
    return _mailId++;
}

uint32 ObjectMgr::GenerateLowGuid(HighGuid guidhigh)
{
    switch (guidhigh)
    {
        case HIGHGUID_ITEM:
        {
            ASSERT(_hiItemGuid < 0xFFFFFFFE && "Item guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiItemGuidMutex);
            return _hiItemGuid++;
        }
        case HIGHGUID_UNIT:
        {
            ASSERT(_hiCreatureGuid < 0x00FFFFFE && "Creature guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiCreatureGuidMutex);
            return _hiCreatureGuid++;
        }
        case HIGHGUID_PET:
        {
            ASSERT(_hiPetGuid < 0x00FFFFFE && "Pet guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiPetGuidMutex);
            return _hiPetGuid++;
        }
        case HIGHGUID_VEHICLE:
        {
            ASSERT(_hiVehicleGuid < 0x00FFFFFF && "Vehicle guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiVehicleGuidMutex);
            return _hiVehicleGuid++;
        }
        case HIGHGUID_PLAYER:
        {
            ASSERT(_hiCharGuid < 0xFFFFFFFE && "Player guid overflow!");
            return _hiCharGuid++;
        }
        case HIGHGUID_GAMEOBJECT:
        {
            ASSERT(_hiGoGuid < 0x00FFFFFE && "Gameobject guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiGoGuidMutex);
            return _hiGoGuid++;
        }
        case HIGHGUID_CORPSE:
        {
            ASSERT(_hiCorpseGuid < 0xFFFFFFFE && "Corpse guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiCorpseGuidMutex);
            return _hiCorpseGuid++;
        }
        case HIGHGUID_DYNAMICOBJECT:
        {
            ASSERT(_hiDoGuid < 0xFFFFFFFE && "DynamicObject guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiDoGuidMutex);
            return _hiDoGuid++;
        }
        case HIGHGUID_MO_TRANSPORT:
        {
            ASSERT(_hiMoTransGuid < 0xFFFFFFFE && "MO Transport guid overflow!");
            TRINITY_GUARD(ACE_Thread_Mutex, _hiMoTransGuidMutex);
            return _hiMoTransGuid++;
        }
        default:
            ASSERT(false && "ObjectMgr::GenerateLowGuid - Unknown HIGHGUID type");
            return 0;
    }
}

inline void CheckGOLockId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (sLockStore.LookupEntry(dataN))
        return;

    sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but lock (Id: %u) not found.",
        goInfo->entry, goInfo->type, N, goInfo->door.lockId, goInfo->door.lockId);
}

inline void CheckGOLinkedTrapId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (GameObjectTemplate const* trapInfo = sObjectMgr->GetGameObjectTemplate(dataN))
    {
        if (trapInfo->type != GAMEOBJECT_TYPE_TRAP)
            sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but GO (Entry %u) have not GAMEOBJECT_TYPE_TRAP (%u) type.",
            goInfo->entry, goInfo->type, N, dataN, dataN, GAMEOBJECT_TYPE_TRAP);
    }
}

inline void CheckGOSpellId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (sSpellMgr->GetSpellInfo(dataN))
        return;

    sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but Spell (Entry %u) not exist.",
        goInfo->entry, goInfo->type, N, dataN, dataN);
}

inline void CheckAndFixGOChairHeightId(GameObjectTemplate const* goInfo, uint32 const& dataN, uint32 N)
{
    if (dataN <= (UNIT_STAND_STATE_SIT_HIGH_CHAIR-UNIT_STAND_STATE_SIT_LOW_CHAIR))
        return;

    sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but correct chair height in range 0..%i.",
        goInfo->entry, goInfo->type, N, dataN, UNIT_STAND_STATE_SIT_HIGH_CHAIR-UNIT_STAND_STATE_SIT_LOW_CHAIR);

    // prevent client and server unexpected work
    const_cast<uint32&>(dataN) = 0;
}

inline void CheckGONoDamageImmuneId(GameObjectTemplate* goTemplate, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
        return;

    sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but expected boolean (0/1) noDamageImmune field value.", goTemplate->entry, goTemplate->type, N, dataN);
}

inline void CheckGOConsumable(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
        return;

    sLog->outErrorDb("Gameobject (Entry: %u GoType: %u) have data%d=%u but expected boolean (0/1) consumable field value.",
        goInfo->entry, goInfo->type, N, dataN);
}

void ObjectMgr::LoadGameObjectTemplate()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0      1      2        3       4             5          6      7       8     9
    QueryResult result = WorldDatabase.Query("SELECT entry, type, displayId, name, IconName, castBarCaption, unk1, faction, flags, size, "
    //                                          10          11          12          13          14          15
                                             "questItem1, questItem2, questItem3, questItem4, questItem5, questItem6, "
    //                                          16     17     18     19     20     21     22     23     24     25     26      27      28
                                             "Data0, Data1, Data2, Data3, Data4, Data5, Data6, Data7, Data8, Data9, Data10, Data11, Data12, "
    //                                          29      30      31      32      33      34      35      36      37      38      39      40        41
                                             "Data13, Data14, Data15, Data16, Data17, Data18, Data19, Data20, Data21, Data22, Data23, AIName, ScriptName "
                                             "FROM gameobject_template ");

    if (!result)
    {
        sLog->outString(">> Loaded 0 gameobject definitions. DB table `gameobject_template` is empty.");
        sLog->outString();
        return;
    }

    _gameObjectTemplateStore.rehash(result->GetRowCount());
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        GameObjectTemplate& got = _gameObjectTemplateStore[entry];

        got.entry          = entry;
        got.type           = uint32(fields[1].GetUInt8());
        got.displayId      = fields[2].GetUInt32();
        got.name           = fields[3].GetString();
        got.IconName       = fields[4].GetString();
        got.castBarCaption = fields[5].GetString();
        got.unk1           = fields[6].GetString();
        got.faction        = uint32(fields[7].GetUInt16());
        got.flags          = fields[8].GetUInt32();
        got.size           = fields[9].GetFloat();

        if (entry == 3640)
        {
            uint8 a = 1;
        }
        for (uint8 i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; ++i)
            got.questItems[i] = fields[10 + i].GetUInt32();

        for (uint8 i = 0; i < MAX_GAMEOBJECT_DATA; ++i)
            got.raw.data[i] = fields[16 + i].GetInt32(); // data1 and data6 can be -1

        got.AIName = fields[40].GetString();
        got.ScriptId = GetScriptId(fields[41].GetCString());
        got.IsForQuests = false;

        // Checks

        switch (got.type)
        {
            case GAMEOBJECT_TYPE_DOOR:                      //0
            {
                if (got.door.lockId)
                    CheckGOLockId(&got, got.door.lockId, 1);
                CheckGONoDamageImmuneId(&got, got.door.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_BUTTON:                    //1
            {
                if (got.button.lockId)
                    CheckGOLockId(&got, got.button.lockId, 1);
                CheckGONoDamageImmuneId(&got, got.button.noDamageImmune, 4);
                break;
            }
            case GAMEOBJECT_TYPE_QUESTGIVER:                //2
            {
                if (got.questgiver.lockId)
                    CheckGOLockId(&got, got.questgiver.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.questgiver.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_CHEST:                     //3
            {
                if (got.chest.lockId)
                    CheckGOLockId(&got, got.chest.lockId, 0);

                CheckGOConsumable(&got, got.chest.consumable, 3);

                if (got.chest.linkedTrapId)              // linked trap
                    CheckGOLinkedTrapId(&got, got.chest.linkedTrapId, 7);
                break;
            }
            case GAMEOBJECT_TYPE_TRAP:                      //6
            {
                if (got.trap.lockId)
                    CheckGOLockId(&got, got.trap.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_CHAIR:                     //7
                CheckAndFixGOChairHeightId(&got, got.chair.height, 1);
                break;
            case GAMEOBJECT_TYPE_SPELL_FOCUS:               //8
            {
                if (got.spellFocus.focusId)
                {
                    if (!sSpellFocusObjectStore.LookupEntry(got.spellFocus.focusId))
                        sLog->outErrorDb("GameObject (Entry: %u GoType: %u) have data0=%u but SpellFocus (Id: %u) not exist.",
                        entry, got.type, got.spellFocus.focusId, got.spellFocus.focusId);
                }

                if (got.spellFocus.linkedTrapId)        // linked trap
                    CheckGOLinkedTrapId(&got, got.spellFocus.linkedTrapId, 2);
                break;
            }
            case GAMEOBJECT_TYPE_GOOBER:                    //10
            {
                if (got.goober.lockId)
                    CheckGOLockId(&got, got.goober.lockId, 0);

                CheckGOConsumable(&got, got.goober.consumable, 3);

                if (got.goober.pageId)                  // pageId
                {
                    if (!GetPageText(got.goober.pageId))
                        sLog->outErrorDb("GameObject (Entry: %u GoType: %u) have data7=%u but PageText (Entry %u) not exist.",
                        entry, got.type, got.goober.pageId, got.goober.pageId);
                }
                CheckGONoDamageImmuneId(&got, got.goober.noDamageImmune, 11);
                if (got.goober.linkedTrapId)            // linked trap
                    CheckGOLinkedTrapId(&got, got.goober.linkedTrapId, 12);
                break;
            }
            case GAMEOBJECT_TYPE_AREADAMAGE:                //12
            {
                if (got.areadamage.lockId)
                    CheckGOLockId(&got, got.areadamage.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_CAMERA:                    //13
            {
                if (got.camera.lockId)
                    CheckGOLockId(&got, got.camera.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_MO_TRANSPORT:              //15
            {
                if (got.moTransport.taxiPathId)
                {
                    if (got.moTransport.taxiPathId >= sTaxiPathNodesByPath.size() || sTaxiPathNodesByPath[got.moTransport.taxiPathId].empty())
                        sLog->outErrorDb("GameObject (Entry: %u GoType: %u) have data0=%u but TaxiPath (Id: %u) not exist.",
                        entry, got.type, got.moTransport.taxiPathId, got.moTransport.taxiPathId);
                }
                if (uint32 transportMap = got.moTransport.mapID)
                    _transportMaps.insert(transportMap);
                break;
            }
            case GAMEOBJECT_TYPE_SUMMONING_RITUAL:          //18
                break;
            case GAMEOBJECT_TYPE_SPELLCASTER:               //22
            {
                // always must have spell
                CheckGOSpellId(&got, got.spellcaster.spellId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_FLAGSTAND:                 //24
            {
                if (got.flagstand.lockId)
                    CheckGOLockId(&got, got.flagstand.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.flagstand.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_FISHINGHOLE:               //25
            {
                if (got.fishinghole.lockId)
                    CheckGOLockId(&got, got.fishinghole.lockId, 4);
                break;
            }
            case GAMEOBJECT_TYPE_FLAGDROP:                  //26
            {
                if (got.flagdrop.lockId)
                    CheckGOLockId(&got, got.flagdrop.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.flagdrop.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_BARBER_CHAIR:              //32
                CheckAndFixGOChairHeightId(&got, got.barberChair.chairheight, 0);
                break;
        }

       ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u game object templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadExplorationBaseXP()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT level, basexp FROM exploration_basexp");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 BaseXP definitions. DB table `exploration_basexp` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint8 level  = fields[0].GetUInt8();
        uint32 basexp = fields[1].GetInt32();
        _baseXPTable[level] = basexp;
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u BaseXP definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

uint32 ObjectMgr::GetBaseXP(uint8 level)
{
    return _baseXPTable[level] ? _baseXPTable[level] : 0;
}

uint32 ObjectMgr::GetXPForLevel(uint8 level) const
{
    if (level < _playerXPperLevel.size())
        return _playerXPperLevel[level];
    return 0;
}

void ObjectMgr::LoadPetNames()
{
    uint32 oldMSTime = getMSTime();
    //                                                0     1      2
    QueryResult result = WorldDatabase.Query("SELECT word, entry, half FROM pet_name_generation");

    if (!result)
    {
        sLog->outString(">> Loaded 0 pet name parts. DB table `pet_name_generation` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        std::string word = fields[0].GetString();
        uint32 entry     = fields[1].GetUInt32();
        bool   half      = fields[2].GetBool();
        if (half)
            _petHalfName1[entry].push_back(word);
        else
            _petHalfName0[entry].push_back(word);
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u pet name parts in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadPetNumber()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = CharacterDatabase.Query("SELECT MAX(id) FROM character_pet");
    if (result)
    {
        Field* fields = result->Fetch();
        _hiPetNumber = fields[0].GetUInt32()+1;
    }

    sLog->outString(">> Loaded the max pet number: %d in %u ms", _hiPetNumber-1, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

std::string ObjectMgr::GeneratePetName(uint32 entry)
{
    StringVector & list0 = _petHalfName0[entry];
    StringVector & list1 = _petHalfName1[entry];

    if (list0.empty() || list1.empty())
    {
        CreatureTemplate const* cinfo = GetCreatureTemplate(entry);
        char* petname = GetPetName(cinfo->family, sWorld->GetDefaultDbcLocale());
        if (!petname)
            return cinfo->Name;

        return std::string(petname);
    }

    return *(list0.begin()+urand(0, list0.size()-1)) + *(list1.begin()+urand(0, list1.size()-1));
}

uint32 ObjectMgr::GeneratePetNumber()
{
    TRINITY_GUARD(ACE_Thread_Mutex, _hiPetNumberMutex);
    return ++_hiPetNumber;
}

void ObjectMgr::LoadCorpses()
{
    uint32 oldMSTime = getMSTime();

    PreparedQueryResult result = CharacterDatabase.Query(CharacterDatabase.GetPreparedStatement(CHAR_SEL_CORPSES));
    if (!result)
    {
        sLog->outString(">> Loaded 0 corpses. DB table `corpse` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 guid = fields[16].GetUInt32();
        CorpseType type = CorpseType(fields[13].GetUInt8());
        if (type >= MAX_CORPSE_TYPE)
        {
            sLog->outError("Corpse (guid: %u) have wrong corpse type (%u), not loading.", guid, type);
            continue;
        }

        Corpse* corpse = new Corpse(type);
        if (!corpse->LoadCorpseFromDB(guid, fields))
        {
            delete corpse;
            continue;
        }

        sObjectAccessor->AddCorpse(corpse);
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u corpses in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadReputationRewardRate()
{
    uint32 oldMSTime = getMSTime();

    _repRewardRateStore.clear();                             // for reload case

    uint32 count = 0; //                                0          1             2                  3                  4                 5                      6             7
    QueryResult result = WorldDatabase.Query("SELECT faction, quest_rate, quest_daily_rate, quest_weekly_rate, quest_monthly_rate, quest_repeatable_rate, creature_rate, spell_rate FROM reputation_reward_rate");
    if (!result)
    {
        sLog->outError(">> Loaded `reputation_reward_rate`, table is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 factionId            = fields[0].GetUInt32();

        RepRewardRate repRate;

        repRate.questRate           = fields[1].GetFloat();
        repRate.questDailyRate      = fields[2].GetFloat();
        repRate.questWeeklyRate     = fields[3].GetFloat();
        repRate.questMonthlyRate    = fields[4].GetFloat();
        repRate.questRepeatableRate = fields[5].GetFloat();
        repRate.creatureRate        = fields[6].GetFloat();
        repRate.spellRate           = fields[7].GetFloat();

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry)
        {
            sLog->outError("Faction (faction.dbc) %u does not exist but is used in `reputation_reward_rate`", factionId);
            continue;
        }

        if (repRate.questRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has quest_rate with invalid rate %f, skipping data for faction %u", repRate.questRate, factionId);
            continue;
        }

        if (repRate.questDailyRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has quest_daily_rate with invalid rate %f, skipping data for faction %u", repRate.questDailyRate, factionId);
            continue;
        }

        if (repRate.questWeeklyRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has quest_weekly_rate with invalid rate %f, skipping data for faction %u", repRate.questWeeklyRate, factionId);
            continue;
        }

        if (repRate.questMonthlyRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has quest_monthly_rate with invalid rate %f, skipping data for faction %u", repRate.questMonthlyRate, factionId);
            continue;
        }

        if (repRate.questRepeatableRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has quest_repeatable_rate with invalid rate %f, skipping data for faction %u", repRate.questRepeatableRate, factionId);
            continue;
        }

        if (repRate.creatureRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has creature_rate with invalid rate %f, skipping data for faction %u", repRate.creatureRate, factionId);
            continue;
        }

        if (repRate.spellRate < 0.0f)
        {
            sLog->outError("Table reputation_reward_rate has spell_rate with invalid rate %f, skipping data for faction %u", repRate.spellRate, factionId);
            continue;
        }

        _repRewardRateStore[factionId] = repRate;

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u reputation_reward_rate in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadReputationOnKill()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    _repOnKillStore.clear();

    uint32 count = 0;

    //                                                0            1                     2
    QueryResult result = WorldDatabase.Query("SELECT creature_id, RewOnKillRepFaction1, RewOnKillRepFaction2, "
        //   3             4             5                   6             7             8                   9
        "IsTeamAward1, MaxStanding1, RewOnKillRepValue1, IsTeamAward2, MaxStanding2, RewOnKillRepValue2, TeamDependent "
        "FROM creature_onkill_reputation");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 creature award reputation definitions. DB table `creature_onkill_reputation` is empty.");
        sLog->outString();
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 creature_id = fields[0].GetUInt32();

        ReputationOnKillEntry repOnKill;
        repOnKill.RepFaction1          = fields[1].GetInt16();
        repOnKill.RepFaction2          = fields[2].GetInt16();
        repOnKill.IsTeamAward1        = fields[3].GetBool();
        repOnKill.ReputationMaxCap1  = fields[4].GetUInt8();
        repOnKill.RepValue1            = fields[5].GetInt32();
        repOnKill.IsTeamAward2        = fields[6].GetBool();
        repOnKill.ReputationMaxCap2  = fields[7].GetUInt8();
        repOnKill.RepValue2            = fields[8].GetInt32();
        repOnKill.TeamDependent       = fields[9].GetUInt8();

        if (!GetCreatureTemplate(creature_id))
        {
            sLog->outErrorDb("Table `creature_onkill_reputation` have data for not existed creature entry (%u), skipped", creature_id);
            continue;
        }

        if (repOnKill.RepFaction1)
        {
            FactionEntry const* factionEntry1 = sFactionStore.LookupEntry(repOnKill.RepFaction1);
            if (!factionEntry1)
            {
                sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `creature_onkill_reputation`", repOnKill.RepFaction1);
                continue;
            }
        }

        if (repOnKill.RepFaction2)
        {
            FactionEntry const* factionEntry2 = sFactionStore.LookupEntry(repOnKill.RepFaction2);
            if (!factionEntry2)
            {
                sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `creature_onkill_reputation`", repOnKill.RepFaction2);
                continue;
            }
        }

        _repOnKillStore[creature_id] = repOnKill;

        ++count;
    } while (result->NextRow());

    sLog->outString(">> Loaded %u creature award reputation definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadReputationSpilloverTemplate()
{
    uint32 oldMSTime = getMSTime();

    _repSpilloverTemplateStore.clear();                      // for reload case

    uint32 count = 0; //                                0         1        2       3        4       5       6         7        8      9        10       11     12
    QueryResult result = WorldDatabase.Query("SELECT faction, faction1, rate_1, rank_1, faction2, rate_2, rank_2, faction3, rate_3, rank_3, faction4, rate_4, rank_4 FROM reputation_spillover_template");

    if (!result)
    {
        sLog->outString(">> Loaded `reputation_spillover_template`, table is empty.");
        sLog->outString();
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 factionId                = fields[0].GetUInt16();

        RepSpilloverTemplate repTemplate;

        repTemplate.faction[0]          = fields[1].GetUInt16();
        repTemplate.faction_rate[0]     = fields[2].GetFloat();
        repTemplate.faction_rank[0]     = fields[3].GetUInt8();
        repTemplate.faction[1]          = fields[4].GetUInt16();
        repTemplate.faction_rate[1]     = fields[5].GetFloat();
        repTemplate.faction_rank[1]     = fields[6].GetUInt8();
        repTemplate.faction[2]          = fields[7].GetUInt16();
        repTemplate.faction_rate[2]     = fields[8].GetFloat();
        repTemplate.faction_rank[2]     = fields[9].GetUInt8();
        repTemplate.faction[3]          = fields[10].GetUInt16();
        repTemplate.faction_rate[3]     = fields[11].GetFloat();
        repTemplate.faction_rank[3]     = fields[12].GetUInt8();

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);

        if (!factionEntry)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template`", factionId);
            continue;
        }

        if (factionEntry->team == 0)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u in `reputation_spillover_template` does not belong to any team, skipping", factionId);
            continue;
        }

        for (uint32 i = 0; i < MAX_SPILLOVER_FACTIONS; ++i)
        {
            if (repTemplate.faction[i])
            {
                FactionEntry const* factionSpillover = sFactionStore.LookupEntry(repTemplate.faction[i]);

                if (!factionSpillover)
                {
                    sLog->outErrorDb("Spillover faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template` for faction %u, skipping", repTemplate.faction[i], factionId);
                    continue;
                }

                if (factionSpillover->reputationListID < 0)
                {
                    sLog->outErrorDb("Spillover faction (faction.dbc) %u for faction %u in `reputation_spillover_template` can not be listed for client, and then useless, skipping", repTemplate.faction[i], factionId);
                    continue;
                }

                if (repTemplate.faction_rank[i] >= MAX_REPUTATION_RANK)
                {
                    sLog->outErrorDb("Rank %u used in `reputation_spillover_template` for spillover faction %u is not valid, skipping", repTemplate.faction_rank[i], repTemplate.faction[i]);
                    continue;
                }
            }
        }

        FactionEntry const* factionEntry0 = sFactionStore.LookupEntry(repTemplate.faction[0]);
        if (repTemplate.faction[0] && !factionEntry0)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template`", repTemplate.faction[0]);
            continue;
        }
        FactionEntry const* factionEntry1 = sFactionStore.LookupEntry(repTemplate.faction[1]);
        if (repTemplate.faction[1] && !factionEntry1)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template`", repTemplate.faction[1]);
            continue;
        }
        FactionEntry const* factionEntry2 = sFactionStore.LookupEntry(repTemplate.faction[2]);
        if (repTemplate.faction[2] && !factionEntry2)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template`", repTemplate.faction[2]);
            continue;
        }
        FactionEntry const* factionEntry3 = sFactionStore.LookupEntry(repTemplate.faction[3]);
        if (repTemplate.faction[3] && !factionEntry3)
        {
            sLog->outErrorDb("Faction (faction.dbc) %u does not exist but is used in `reputation_spillover_template`", repTemplate.faction[3]);
            continue;
        }

        _repSpilloverTemplateStore[factionId] = repTemplate;

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u reputation_spillover_template in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadPointsOfInterest()
{
    uint32 oldMSTime = getMSTime();

    _pointsOfInterestStore.clear();                              // need for reload case

    uint32 count = 0;

    //                                               0       1          2        3     4      5    6
    QueryResult result = WorldDatabase.Query("SELECT ID, PositionX, PositionY, Icon, Flags, Data, Name FROM points_of_interest");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 Points of Interest definitions. DB table `points_of_interest` is empty.");
        sLog->outString();
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 point_id = fields[0].GetUInt32();

        PointOfInterest POI;
        POI.entry = point_id;
        POI.x = fields[1].GetFloat();
        POI.y = fields[2].GetFloat();
        POI.icon = fields[3].GetUInt32();
        POI.flags = fields[4].GetUInt32();
        POI.data = fields[5].GetUInt32();
        POI.icon_name = fields[6].GetString();

        if (!Trinity::IsValidMapCoord(POI.x, POI.y))
        {
            sLog->outErrorDb("Table `points_of_interest` (Entry: %u) have invalid coordinates (X: %f Y: %f), ignored.", point_id, POI.x, POI.y);
            continue;
        }

        _pointsOfInterestStore[point_id] = POI;

        ++count;
    } while (result->NextRow());

    sLog->outString(">> Loaded %u Points of Interest definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadQuestPOI()
{
    uint32 oldMSTime = getMSTime();

    _questPOIStore.clear();                              // need for reload case

    uint32 count = 0;

    //                                               0        1          2          3           4          5       6        7
    QueryResult result = WorldDatabase.Query("SELECT QuestID, id, ObjectiveIndex, MapID, WorldMapAreaId, Floor, Priority, Flags FROM quest_poi order by QuestID");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest POI definitions. DB table `quest_poi` is empty.");
        sLog->outString();
        return;
    }

    //                                                  0       1   2  3
    QueryResult points = WorldDatabase.Query("SELECT QuestID, Idx1, X, Y FROM quest_poi_points ORDER BY QuestID DESC, Idx2");

    std::vector<std::vector<std::vector<QuestPOIPoint> > > POIs;

    if (points)
    {
        // The first result should have the highest questId
        Field* fields = points->Fetch();
        uint32 questIdMax = fields[0].GetUInt32();
        POIs.resize(questIdMax + 1);

        do
        {
            fields = points->Fetch();

            uint32 questId            = fields[0].GetUInt32();
            uint32 id                 = fields[1].GetUInt32();
            int32  x                  = fields[2].GetInt32();
            int32  y                  = fields[3].GetInt32();

            if (POIs[questId].size() <= id + 1)
                POIs[questId].resize(id + 10);

            QuestPOIPoint point(x, y);
            POIs[questId][id].push_back(point);
        } while (points->NextRow());
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 questId            = fields[0].GetUInt32();
        uint32 id                 = fields[1].GetUInt32();
        int32 objIndex            = fields[2].GetInt32();
        uint32 mapId              = fields[3].GetUInt32();
        uint32 WorldMapAreaId     = fields[4].GetUInt32();
        uint32 FloorId            = fields[5].GetUInt32();
        uint32 unk3               = fields[6].GetUInt32();
        uint32 unk4               = fields[7].GetUInt32();

        QuestPOI POI(id, objIndex, mapId, WorldMapAreaId, FloorId, unk3, unk4);
        if (questId < POIs.size() && id < POIs[questId].size())
        {
            POI.points = POIs[questId][id];
            _questPOIStore[questId].push_back(POI);
        }
        else
			sLog->outError("Table quest_poi references unknown quest points for quest %u POI id %u", questId, id);

        ++count;
    } while (result->NextRow());

    sLog->outString(">> Loaded %u quest POI definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadNPCSpellClickSpells()
{
    uint32 oldMSTime = getMSTime();

    _spellClickInfoStore.clear();
    //                                                0          1         2            3
    QueryResult result = WorldDatabase.Query("SELECT npc_entry, spell_id, cast_flags, user_type FROM npc_spellclick_spells");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 spellclick spells. DB table `npc_spellclick_spells` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 npc_entry = fields[0].GetUInt32();
        CreatureTemplate const* cInfo = GetCreatureTemplate(npc_entry);
        if (!cInfo)
        {
            sLog->outErrorDb("Table npc_spellclick_spells references unknown creature_template %u. Skipping entry.", npc_entry);
            continue;
        }

        uint32 spellid = fields[1].GetUInt32();
        SpellInfo const* spellinfo = sSpellMgr->GetSpellInfo(spellid);
        if (!spellinfo)
        {
            sLog->outErrorDb("Table npc_spellclick_spells references unknown spellid %u. Skipping entry.", spellid);
            continue;
        }

        uint8 userType = fields[3].GetUInt16();
        if (userType >= SPELL_CLICK_USER_MAX)
            sLog->outErrorDb("Table npc_spellclick_spells references unknown user type %u. Skipping entry.", uint32(userType));

        uint8 castFlags = fields[2].GetUInt8();
        SpellClickInfo info;
        info.spellId = spellid;
        info.castFlags = castFlags;
        info.userType = SpellClickUserTypes(userType);
        _spellClickInfoStore.insert(SpellClickInfoContainer::value_type(npc_entry, info));

        ++count;
    }
    while (result->NextRow());

    // all spellclick data loaded, now we check if there are creatures with NPC_FLAG_SPELLCLICK but with no data
    // NOTE: It *CAN* be the other way around: no spellclick flag but with spellclick data, in case of creature-only vehicle accessories
    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {
        if ((itr->second.npcflag & UNIT_NPC_FLAG_SPELLCLICK) && _spellClickInfoStore.find(itr->second.Entry) == _spellClickInfoStore.end())
        {
            sLog->outErrorDb("npc_spellclick_spells: Creature template %u has UNIT_NPC_FLAG_SPELLCLICK but no data in spellclick table! Removing flag", itr->second.Entry);
            const_cast<CreatureTemplate*>(&itr->second)->npcflag &= ~UNIT_NPC_FLAG_SPELLCLICK;
        }
    }

    sLog->outString(">> Loaded %u spellclick definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::DeleteCreatureData(uint32 guid)
{
    // remove mapid*cellid -> guid_set map
    CreatureData const* data = GetCreatureData(guid);
    if (data)
        RemoveCreatureFromGrid(guid, data);

    _creatureDataStore.erase(guid);
}

void ObjectMgr::DeleteGOData(uint32 guid)
{
    // remove mapid*cellid -> guid_set map
    GameObjectData const* data = GetGOData(guid);
    if (data)
        RemoveGameobjectFromGrid(guid, data);

    _gameObjectDataStore.erase(guid);
}

void ObjectMgr::AddCorpseCellData(uint32 mapid, uint32 cellid, uint32 player_guid, uint32 instance)
{
    // corpses are always added to spawn mode 0 and they are spawned by their instance id
    CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(mapid, 0)][cellid];
    cell_guids.corpses[player_guid] = instance;
}

void ObjectMgr::DeleteCorpseCellData(uint32 mapid, uint32 cellid, uint32 player_guid)
{
    // corpses are always added to spawn mode 0 and they are spawned by their instance id
    CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(mapid, 0)][cellid];
    cell_guids.corpses.erase(player_guid);
}

void ObjectMgr::LoadQuestRelationsHelper(QuestRelations& map, std::string const& table, bool starter, bool go)
{
    uint32 oldMSTime = getMSTime();

    map.clear();                                            // need for reload case

    uint32 count = 0;

    QueryResult result = WorldDatabase.PQuery("SELECT id, quest, pool_entry FROM %s qr LEFT JOIN pool_quest pq ON qr.quest = pq.entry", table.c_str());

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 quest relations from `%s`, table is empty.", table.c_str());
        sLog->outString();
        return;
    }

    PooledQuestRelation* poolRelationMap = go ? &sPoolMgr->mQuestGORelation : &sPoolMgr->mQuestCreatureRelation;
    if (starter)
        poolRelationMap->clear();

    do
    {
        uint32 id     = result->Fetch()[0].GetUInt32();
        uint32 quest  = result->Fetch()[1].GetUInt32();
        uint32 poolId = result->Fetch()[2].GetUInt32();

        if (_questTemplates.find(quest) == _questTemplates.end())
        {
            sLog->outErrorDb("Table `%s`: Quest %u listed for entry %u does not exist.", table.c_str(), quest, id);
            continue;
        }

        if (!poolId || !starter)
            map.insert(QuestRelations::value_type(id, quest));
        else if (starter)
            poolRelationMap->insert(PooledQuestRelation::value_type(quest, id));

        ++count;
    } while (result->NextRow());

    sLog->outString(">> Loaded %u quest relations from %s in %u ms", count, table.c_str(), GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadGameobjectQuestStarters()
{
    LoadQuestRelationsHelper(_goQuestRelations, "gameobject_queststarter", true, true);

    for (QuestRelations::iterator itr = _goQuestRelations.begin(); itr != _goQuestRelations.end(); ++itr)
    {
        GameObjectTemplate const* goInfo = GetGameObjectTemplate(itr->first);
        if (!goInfo)
            sLog->outErrorDb("Table `gameobject_queststarter` have data for not existed gameobject entry (%u) and existed quest %u", itr->first, itr->second);
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
            sLog->outErrorDb("Table `gameobject_queststarter` have data gameobject entry (%u) for quest %u, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadGameobjectQuestEnders()
{
    LoadQuestRelationsHelper(_goQuestInvolvedRelations, "gameobject_questender", false, true);

    for (QuestRelations::iterator itr = _goQuestInvolvedRelations.begin(); itr != _goQuestInvolvedRelations.end(); ++itr)
    {
        GameObjectTemplate const* goInfo = GetGameObjectTemplate(itr->first);
        if (!goInfo)
            sLog->outErrorDb("Table `gameobject_questender` have data for not existed gameobject entry (%u) and existed quest %u", itr->first, itr->second);
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
            sLog->outErrorDb("Table `gameobject_questender` have data gameobject entry (%u) for quest %u, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadCreatureQuestStarters()
{
    LoadQuestRelationsHelper(_creatureQuestRelations, "creature_queststarter", true, false);

    for (QuestRelations::iterator itr = _creatureQuestRelations.begin(); itr != _creatureQuestRelations.end(); ++itr)
    {
        CreatureTemplate const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
            sLog->outErrorDb("Table `creature_queststarter` have data for not existed creature entry (%u) and existed quest %u", itr->first, itr->second);
        else if (!(cInfo->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
            sLog->outErrorDb("Table `creature_queststarter` has creature entry (%u) for quest %u, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadCreatureQuestEnders()
{
    LoadQuestRelationsHelper(_creatureQuestInvolvedRelations, "creature_questender", false, false);

    for (QuestRelations::iterator itr = _creatureQuestInvolvedRelations.begin(); itr != _creatureQuestInvolvedRelations.end(); ++itr)
    {
        CreatureTemplate const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
            sLog->outErrorDb("Table `creature_questender` have data for not existed creature entry (%u) and existed quest %u", itr->first, itr->second);
        else if (!(cInfo->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
            sLog->outErrorDb("Table `creature_questender` has creature entry (%u) for quest %u, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadReservedPlayersNames()
{
    uint32 oldMSTime = getMSTime();

    _reservedNamesStore.clear();                                // need for reload case

    QueryResult result = CharacterDatabase.Query("SELECT name FROM reserved_name");

    if (!result)
    {
        sLog->outString(">> Loaded 0 reserved player names. DB table `reserved_name` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    Field* fields;
    do
    {
        fields = result->Fetch();
        std::string name= fields[0].GetString();

        std::wstring wstr;
        if (!Utf8toWStr (name, wstr))
        {
            sLog->outError("Table `reserved_name` have invalid name: %s", name.c_str());
            continue;
        }

        wstrToLower(wstr);

        _reservedNamesStore.insert(wstr);
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u reserved player names in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

bool ObjectMgr::IsReservedName(const std::string& name) const
{
	// pussywizard
	if (name.size() >= 2 && (name[name.size()-2] == 'G' || name[name.size()-2] == 'g') && (name[name.size()-1] == 'M' || name[name.size()-1] == 'm'))
		return true;

    std::wstring wstr;
    if (!Utf8toWStr (name, wstr))
        return false;

    wstrToLower(wstr);

    return _reservedNamesStore.find(wstr) != _reservedNamesStore.end();
}

enum LanguageType
{
    LT_BASIC_LATIN    = 0x0000,
    LT_EXTENDEN_LATIN = 0x0001,
    LT_CYRILLIC       = 0x0002,
    LT_EAST_ASIA      = 0x0004,
    LT_ANY            = 0xFFFF
};

static LanguageType GetRealmLanguageType(bool create)
{
    switch (sWorld->getIntConfig(CONFIG_REALM_ZONE))
    {
        case REALM_ZONE_UNKNOWN:                            // any language
        case REALM_ZONE_DEVELOPMENT:
        case REALM_ZONE_TEST_SERVER:
        case REALM_ZONE_QA_SERVER:
            return LT_ANY;
        case REALM_ZONE_UNITED_STATES:                      // extended-Latin
        case REALM_ZONE_OCEANIC:
        case REALM_ZONE_LATIN_AMERICA:
        case REALM_ZONE_ENGLISH:
        case REALM_ZONE_GERMAN:
        case REALM_ZONE_FRENCH:
        case REALM_ZONE_SPANISH:
            return LT_EXTENDEN_LATIN;
        case REALM_ZONE_KOREA:                              // East-Asian
        case REALM_ZONE_TAIWAN:
        case REALM_ZONE_CHINA:
            return LT_EAST_ASIA;
        case REALM_ZONE_RUSSIAN:                            // Cyrillic
            return LT_CYRILLIC;
        default:
            return create ? LT_BASIC_LATIN : LT_ANY;        // basic-Latin at create, any at login
    }
}

bool isValidString(std::wstring wstr, uint32 strictMask, bool numericOrSpace, bool create = false)
{
    if (strictMask == 0)                                       // any language, ignore realm
    {
        if (isExtendedLatinString(wstr, numericOrSpace))
            return true;
        if (isCyrillicString(wstr, numericOrSpace))
            return true;
        if (isEastAsianString(wstr, numericOrSpace))
            return true;
        return false;
    }

    if (strictMask & 0x2)                                    // realm zone specific
    {
        LanguageType lt = GetRealmLanguageType(create);
        if (lt & LT_EXTENDEN_LATIN)
            if (isExtendedLatinString(wstr, numericOrSpace))
                return true;
        if (lt & LT_CYRILLIC)
            if (isCyrillicString(wstr, numericOrSpace))
                return true;
        if (lt & LT_EAST_ASIA)
            if (isEastAsianString(wstr, numericOrSpace))
                return true;
    }

    if (strictMask & 0x1)                                    // basic Latin
    {
        if (isBasicLatinString(wstr, numericOrSpace))
            return true;
    }

    return false;
}

uint8 ObjectMgr::CheckPlayerName(const std::string& name, bool create)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return CHAR_NAME_INVALID_CHARACTER;

    if (wname.size() > MAX_PLAYER_NAME)
        return CHAR_NAME_TOO_LONG;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_PLAYER_NAME);
    if (wname.size() < minName)
        return CHAR_NAME_TOO_SHORT;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_PLAYER_NAMES);
    if (!isValidString(wname, strictMask, false, create))
        return CHAR_NAME_MIXED_LANGUAGES;

    wstrToLower(wname);
    for (size_t i = 2; i < wname.size(); ++i)
        if (wname[i] == wname[i-1] && wname[i] == wname[i-2])
            return CHAR_NAME_THREE_CONSECUTIVE;

    return CHAR_NAME_SUCCESS;
}

bool ObjectMgr::IsValidCharterName(const std::string& name)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return false;

    if (wname.size() > MAX_CHARTER_NAME)
        return false;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_CHARTER_NAME);
    if (wname.size() < minName)
        return false;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_CHARTER_NAMES);

    return isValidString(wname, strictMask, true);
}

bool ObjectMgr::IsValidChannelName(const std::string& name)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return false;

    if (wname.size() > MAX_CHANNEL_NAME)
        return false;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_CHANNEL_NAMES);

    return isValidString(wname, strictMask, true);
}

PetNameInvalidReason ObjectMgr::CheckPetName(const std::string& name)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return PET_NAME_INVALID;

    if (wname.size() > MAX_PET_NAME)
        return PET_NAME_TOO_LONG;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_PET_NAME);
    if (wname.size() < minName)
        return PET_NAME_TOO_SHORT;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_PET_NAMES);
    if (!isValidString(wname, strictMask, false))
        return PET_NAME_MIXED_LANGUAGES;

    return PET_NAME_SUCCESS;
}

void ObjectMgr::LoadGameObjectForQuests()
{
    uint32 oldMSTime = getMSTime();

    if (sObjectMgr->GetGameObjectTemplates()->empty())
    {
        sLog->outString(">> Loaded 0 GameObjects for quests");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    // collect GO entries for GO that must activated
    GameObjectTemplateContainer* gotc = const_cast<GameObjectTemplateContainer*>(sObjectMgr->GetGameObjectTemplates());
    for (GameObjectTemplateContainer::iterator itr = gotc->begin(); itr != gotc->end(); ++itr)
    {
        itr->second.IsForQuests = false;
        switch (itr->second.type)
        {
            case GAMEOBJECT_TYPE_QUESTGIVER:
                itr->second.IsForQuests = true;
                ++count;
                break;
            case GAMEOBJECT_TYPE_CHEST:
            {
                // scan GO chest with loot including quest items
                uint32 loot_id = (itr->second.GetLootId());

                // find quest loot for GO
                if (itr->second.chest.questId || LootTemplates_Gameobject.HaveQuestLootFor(loot_id))
                {
                    itr->second.IsForQuests = true;
                    ++count;
                }
                break;
            }
            case GAMEOBJECT_TYPE_GENERIC:
            {
                if (itr->second._generic.questID > 0)            //quests objects
                {
                    itr->second.IsForQuests = true;
                    ++count;
                }
                break;
            }
            case GAMEOBJECT_TYPE_GOOBER:
            {
                if (itr->second.goober.questId > 0)              //quests objects
                {
                    itr->second.IsForQuests = true;
                    ++count;
                }
                break;
            }
            default:
                break;
        }
    }

    sLog->outString(">> Loaded %u GameObjects for quests in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

bool ObjectMgr::LoadTrinityStrings()
{
    uint32 oldMSTime = getMSTime();

    uint32 min_value = MIN_TRINITY_STRING_ID;
    uint32 max_value = MAX_TRINITY_STRING_ID;

    // cleanup affected map part for reloading case
    for (TrinityStringLocaleContainer::iterator itr = _trinityStringLocaleStore.begin(); itr != _trinityStringLocaleStore.end();)
    {
        if (itr->first >= min_value && itr->first < max_value)
            _trinityStringLocaleStore.erase(itr++);
        else
            ++itr;
    }

    QueryResult result = WorldDatabase.Query("SELECT entry, content_default FROM trinity_string");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 trinity strings. DB table `trinity_string` is empty. Cannot continue.");
        sLog->outString();
        return false;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (entry == 0)
        {
            sLog->outErrorDb("Table `trinity_string` contain reserved entry 0, ignored.");
            continue;
        }
        else if (entry < min_value || entry >= max_value)
        {
            sLog->outErrorDb("Table `trinity_string` contain entry %i out of allowed range (%d - %d), ignored.", entry, min_value, max_value);
            continue;
        }

        TrinityStringLocale& data = _trinityStringLocaleStore[entry];

        if (!data.Content.empty())
        {
            sLog->outErrorDb("Table `trinity_string` contain data for already loaded entry  %i (from another table?), ignored.", entry);
            continue;
        }

        data.Content = fields[1].GetString();
    } while (result->NextRow());

    sLog->outString(">> Loaded %u Trinity strings from table trinity_string in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
    return true;
}

const char *ObjectMgr::GetTrinityString(int32 entry) const
{
    if (TrinityStringLocale const* msl = GetTrinityStringLocale(entry))
        return msl->Content.c_str();

    if (entry > 0)
        sLog->outErrorDb("Entry %i not found in `trinity_string` table.", entry);
    else
        sLog->outErrorDb("Trinity string entry %i not found in DB.", entry);
    return "<error>";
}

void ObjectMgr::LoadFishingBaseSkillLevel()
{
    uint32 oldMSTime = getMSTime();

    _fishingBaseForAreaStore.clear();                            // for reload case

    QueryResult result = WorldDatabase.Query("SELECT entry, skill FROM skill_fishing_base_level");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 areas for fishing base skill level. DB table `skill_fishing_base_level` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry  = fields[0].GetUInt32();
        int32 skill   = fields[1].GetInt16();

        AreaTableEntry const* fArea = sAreaTableStore.LookupEntry(entry);
        if (!fArea)
        {
            sLog->outErrorDb("AreaId %u defined in `skill_fishing_base_level` does not exist", entry);
            continue;
        }

        _fishingBaseForAreaStore[entry] = skill;
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u areas for fishing base skill level in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

bool ObjectMgr::CheckDeclinedNames(std::wstring w_ownname, DeclinedName const& names)
{
    // get main part of the name
    std::wstring mainpart = GetMainPartOfName(w_ownname, 0);
    // prepare flags
    bool x = true;
    bool y = true;

    // check declined names
    for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
    {
        std::wstring wname;
        if (!Utf8toWStr(names.name[i], wname))
            return false;

        if (mainpart != GetMainPartOfName(wname, i+1))
            x = false;

        if (w_ownname != wname)
            y = false;
    }
    return (x || y);
}

uint32 ObjectMgr::GetAreaTriggerScriptId(uint32 trigger_id)
{
    AreaTriggerScriptContainer::const_iterator i = _areaTriggerScriptStore.find(trigger_id);
    if (i!= _areaTriggerScriptStore.end())
        return i->second;
    return 0;
}

SpellScriptsBounds ObjectMgr::GetSpellScriptsBounds(uint32 spell_id)
{
    return SpellScriptsBounds(_spellScriptsStore.lower_bound(spell_id), _spellScriptsStore.upper_bound(spell_id));
}

// this allows calculating base reputations to offline players, just by race and class
int32 ObjectMgr::GetBaseReputationOf(FactionEntry const* factionEntry, uint8 race, uint8 playerClass)
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = (1 << (race - 1));
    uint32 classMask = (1 << (playerClass-1));

    for (int i = 0; i < 4; i++)
    {
        if ((!factionEntry->BaseRepClassMask[i] ||
            factionEntry->BaseRepClassMask[i] & classMask) &&
            (!factionEntry->BaseRepRaceMask[i] ||
            factionEntry->BaseRepRaceMask[i] & raceMask))
            return factionEntry->BaseRepValue[i];
    }

    return 0;
}

SkillRangeType GetSkillRangeType(SkillRaceClassInfoEntry const* rcEntry)
{
    SkillLineEntry const* skill = sSkillLineStore.LookupEntry(rcEntry->SkillId);
    if (!skill)
        return SKILL_RANGE_NONE;

    if (sSkillTiersStore.LookupEntry(rcEntry->SkillTier))
        return SKILL_RANGE_RANK;

    if (rcEntry->SkillId == SKILL_RUNEFORGING)
        return SKILL_RANGE_MONO;

    switch (skill->categoryId)
    {
        case SKILL_CATEGORY_ARMOR:
            return SKILL_RANGE_MONO;
        case SKILL_CATEGORY_LANGUAGES:
            return SKILL_RANGE_LANGUAGE;
    }

    return SKILL_RANGE_LEVEL;
}

void ObjectMgr::LoadGameTele()
{
    uint32 oldMSTime = getMSTime();

    _gameTeleStore.clear();                                  // for reload case

    //                                                0       1           2           3           4        5     6
    QueryResult result = WorldDatabase.Query("SELECT id, position_x, position_y, position_z, orientation, map, name FROM game_tele");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 GameTeleports. DB table `game_tele` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 id         = fields[0].GetUInt32();

        GameTele gt;

        gt.position_x     = fields[1].GetFloat();
        gt.position_y     = fields[2].GetFloat();
        gt.position_z     = fields[3].GetFloat();
        gt.orientation    = fields[4].GetFloat();
        gt.mapId          = fields[5].GetUInt16();
        gt.name           = fields[6].GetString();

        if (!MapManager::IsValidMapCoord(gt.mapId, gt.position_x, gt.position_y, gt.position_z, gt.orientation))
        {
            sLog->outErrorDb("Wrong position for id %u (name: %s) in `game_tele` table, ignoring.", id, gt.name.c_str());
            continue;
        }

        if (!Utf8toWStr(gt.name, gt.wnameLow))
        {
            sLog->outErrorDb("Wrong UTF8 name for id %u in `game_tele` table, ignoring.", id);
            continue;
        }

        wstrToLower(gt.wnameLow);

        _gameTeleStore[id] = gt;

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u GameTeleports in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

GameTele const* ObjectMgr::GetGameTele(const std::string& name) const
{
    // explicit name case
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return NULL;

    // converting string that we try to find to lower case
    wstrToLower(wname);

    // Alternative first GameTele what contains wnameLow as substring in case no GameTele location found
    const GameTele* alt = NULL;
    for (GameTeleContainer::const_iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
    {
        if (itr->second.wnameLow == wname)
            return &itr->second;
        else if (alt == NULL && itr->second.wnameLow.find(wname) != std::wstring::npos)
            alt = &itr->second;
    }

    return alt;
}

bool ObjectMgr::AddGameTele(GameTele& tele)
{
    // find max id
    uint32 new_id = 0;
    for (GameTeleContainer::const_iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
        if (itr->first > new_id)
            new_id = itr->first;

    // use next
    ++new_id;

    if (!Utf8toWStr(tele.name, tele.wnameLow))
        return false;

    wstrToLower(tele.wnameLow);

    _gameTeleStore[new_id] = tele;

    PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GAME_TELE);

    stmt->setUInt32(0, new_id);
    stmt->setFloat(1, tele.position_x);
    stmt->setFloat(2, tele.position_y);
    stmt->setFloat(3, tele.position_z);
    stmt->setFloat(4, tele.orientation);
    stmt->setUInt16(5, uint16(tele.mapId));
    stmt->setString(6, tele.name);

    WorldDatabase.Execute(stmt);

    return true;
}

bool ObjectMgr::DeleteGameTele(const std::string& name)
{
    // explicit name case
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return false;

    // converting string that we try to find to lower case
    wstrToLower(wname);

    for (GameTeleContainer::iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
    {
        if (itr->second.wnameLow == wname)
        {
            PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAME_TELE);

            stmt->setString(0, itr->second.name);

            WorldDatabase.Execute(stmt);

            _gameTeleStore.erase(itr);
            return true;
        }
    }

    return false;
}

void ObjectMgr::AddSpellToTrainer(uint32 entry, uint32 spell, uint32 spellCost, uint32 reqSkill, uint32 reqSkillValue, uint32 reqLevel)
{
    if (entry >= TRINITY_TRAINER_START_REF)
        return;

    CreatureTemplate const* cInfo = GetCreatureTemplate(entry);
    if (!cInfo)
    {
        sLog->outErrorDb("Table `npc_trainer` contains an entry for a non-existing creature template (Entry: %u), ignoring", entry);
        return;
    }

    if (!(cInfo->npcflag & UNIT_NPC_FLAG_TRAINER))
    {
        sLog->outErrorDb("Table `npc_trainer` contains an entry for a creature template (Entry: %u) without trainer flag, ignoring", entry);
        return;
    }

    SpellInfo const* spellinfo = sSpellMgr->GetSpellInfo(spell);
    if (!spellinfo)
    {
        sLog->outErrorDb("Table `npc_trainer` contains an entry (Entry: %u) for a non-existing spell (Spell: %u), ignoring", entry, spell);
        return;
    }

    if (!SpellMgr::ComputeIsSpellValid(spellinfo))
    {
        sLog->outErrorDb("Table `npc_trainer` contains an entry (Entry: %u) for a broken spell (Spell: %u), ignoring", entry, spell);
        return;
    }

    if (GetTalentSpellCost(spell))
    {
        sLog->outErrorDb("Table `npc_trainer` contains an entry (Entry: %u) for a non-existing spell (Spell: %u) which is a talent, ignoring", entry, spell);
        return;
    }

    TrainerSpellData& data = _cacheTrainerSpellStore[entry];

    TrainerSpell& trainerSpell = data.spellList[spell];
    trainerSpell.spell         = spell;
    trainerSpell.spellCost     = spellCost;
    trainerSpell.reqSkill      = reqSkill;
    trainerSpell.reqSkillValue = reqSkillValue;
    trainerSpell.reqLevel      = reqLevel;

    if (!trainerSpell.reqLevel)
        trainerSpell.reqLevel = spellinfo->SpellLevel;

    // calculate learned spell for profession case when stored cast-spell
    trainerSpell.learnedSpell[0] = spell;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (spellinfo->Effects[i].Effect != SPELL_EFFECT_LEARN_SPELL)
            continue;
        if (trainerSpell.learnedSpell[0] == spell)
            trainerSpell.learnedSpell[0] = 0;
        // player must be able to cast spell on himself
        if (spellinfo->Effects[i].TargetA.GetTarget() != 0 && spellinfo->Effects[i].TargetA.GetTarget() != TARGET_UNIT_TARGET_ALLY
            && spellinfo->Effects[i].TargetA.GetTarget() != TARGET_UNIT_TARGET_ANY && spellinfo->Effects[i].TargetA.GetTarget() != TARGET_UNIT_CASTER)
        {
            sLog->outErrorDb("Table `npc_trainer` has spell %u for trainer entry %u with learn effect which has incorrect target type, ignoring learn effect!", spell, entry);
            continue;
        }

        trainerSpell.learnedSpell[i] = spellinfo->Effects[i].TriggerSpell;

        if (trainerSpell.learnedSpell[i])
        {
            SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(trainerSpell.learnedSpell[i]);
            if (learnedSpellInfo && learnedSpellInfo->IsProfession())
                data.trainerType = 2;
        }
    }

    return;
}

void ObjectMgr::LoadTrainerSpell()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    _cacheTrainerSpellStore.clear();

    QueryResult result = WorldDatabase.Query("SELECT b.ID, a.SpellID, a.MoneyCost, a.ReqSkillLine, a.ReqSkillRank, a.ReqLevel FROM npc_trainer AS a "
        "INNER JOIN npc_trainer AS b ON a.ID = -(b.SpellID) "
        "UNION SELECT * FROM npc_trainer WHERE SpellID > 0");

    if (!result)
    {
        sLog->outErrorDb(">>  Loaded 0 Trainers. DB table `npc_trainer` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry         = fields[0].GetUInt32();
        uint32 spell         = fields[1].GetUInt32();
        uint32 spellCost     = fields[2].GetUInt32();
        uint32 reqSkill      = fields[3].GetUInt16();
        uint32 reqSkillValue = fields[4].GetUInt16();
        uint32 reqLevel      = fields[5].GetUInt8();

        AddSpellToTrainer(entry, spell, spellCost, reqSkill, reqSkillValue, reqLevel);

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %d Trainers in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

int ObjectMgr::LoadReferenceVendor(int32 vendor, int32 item, std::set<uint32> *skip_vendors)
{
    // find all items from the reference vendor
    PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_NPC_VENDOR_REF);
    stmt->setUInt32(0, uint32(item));
    PreparedQueryResult result = WorldDatabase.Query(stmt);

    if (!result)
        return 0;

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        int32 item_id = fields[0].GetInt32();

        // if item is a negative, its a reference
        if (item_id < 0)
            count += LoadReferenceVendor(vendor, -item_id, skip_vendors);
        else
        {
            int32  maxcount     = fields[1].GetUInt8();
            uint32 incrtime     = fields[2].GetUInt32();
            uint32 ExtendedCost = fields[3].GetUInt32();

            if (!IsVendorItemValid(vendor, item_id, maxcount, incrtime, ExtendedCost, NULL, skip_vendors))
                continue;

            VendorItemData& vList = _cacheVendorItemStore[vendor];

            vList.AddItem(item_id, maxcount, incrtime, ExtendedCost);
            ++count;
        }
    } while (result->NextRow());

    return count;
}

void ObjectMgr::LoadVendors()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    for (CacheVendorItemContainer::iterator itr = _cacheVendorItemStore.begin(); itr != _cacheVendorItemStore.end(); ++itr)
        itr->second.Clear();
    _cacheVendorItemStore.clear();

    std::set<uint32> skip_vendors;

    QueryResult result = WorldDatabase.Query("SELECT entry, item, maxcount, incrtime, ExtendedCost FROM npc_vendor ORDER BY entry, slot ASC");

    if (!result)
    {
        sLog->outString();
        sLog->outErrorDb(">>  Loaded 0 Vendors. DB table `npc_vendor` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry        = fields[0].GetUInt32();
        int32 item_id      = fields[1].GetInt32();

        // if item is a negative, its a reference
        if (item_id < 0)
            count += LoadReferenceVendor(entry, -item_id, &skip_vendors);
        else
        {
            uint32 maxcount     = fields[2].GetUInt8();
            uint32 incrtime     = fields[3].GetUInt32();
            uint32 ExtendedCost = fields[4].GetUInt32();

            if (!IsVendorItemValid(entry, item_id, maxcount, incrtime, ExtendedCost, NULL, &skip_vendors))
                continue;

            VendorItemData& vList = _cacheVendorItemStore[entry];

            vList.AddItem(item_id, maxcount, incrtime, ExtendedCost);
            ++count;
        }
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %d Vendors in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadGossipMenu()
{
    uint32 oldMSTime = getMSTime();

    _gossipMenusStore.clear();

    QueryResult result = WorldDatabase.Query("SELECT entry, text_id FROM gossip_menu");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0  gossip_menu entries. DB table `gossip_menu` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        GossipMenus gMenu;

        gMenu.entry             = fields[0].GetUInt16();
        gMenu.text_id           = fields[1].GetUInt32();

        if (!GetGossipText(gMenu.text_id))
        {
            sLog->outErrorDb("Table gossip_menu entry %u are using non-existing text_id %u", gMenu.entry, gMenu.text_id);
            continue;
        }

        _gossipMenusStore.insert(GossipMenusContainer::value_type(gMenu.entry, gMenu));

        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u gossip_menu entries in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::LoadGossipMenuItems()
{
    uint32 oldMSTime = getMSTime();

    _gossipMenuItemsStore.clear();

    QueryResult result = WorldDatabase.Query(
        //          0              1            2           3              4
        "SELECT menu_id, id, option_icon, option_text, option_id, npc_option_npcflag, "
        //       5              6           7          8         9
        "action_menu_id, action_poi_id, box_coded, box_money, box_text "
        "FROM gossip_menu_option ORDER BY menu_id, id");

    if (!result)
    {
        sLog->outErrorDb(">> Loaded 0 gossip_menu_option entries. DB table `gossip_menu_option` is empty!");
        sLog->outString();
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        GossipMenuItems gMenuItem;

        gMenuItem.MenuId                = fields[0].GetUInt16();
        gMenuItem.OptionIndex           = fields[1].GetUInt16();
        gMenuItem.OptionIcon            = fields[2].GetUInt32();
        gMenuItem.OptionText            = fields[3].GetString();
        gMenuItem.OptionType            = fields[4].GetUInt8();
        gMenuItem.OptionNpcflag         = fields[5].GetUInt32();
        gMenuItem.ActionMenuId          = fields[6].GetUInt32();
        gMenuItem.ActionPoiId           = fields[7].GetUInt32();
        gMenuItem.BoxCoded              = fields[8].GetBool();
        gMenuItem.BoxMoney              = fields[9].GetUInt32();
        gMenuItem.BoxText               = fields[10].GetString();

        if (gMenuItem.OptionIcon >= GOSSIP_ICON_MAX)
        {
            sLog->outErrorDb("Table gossip_menu_option for menu %u, id %u has unknown icon id %u. Replacing with GOSSIP_ICON_CHAT", gMenuItem.MenuId, gMenuItem.OptionIndex, gMenuItem.OptionIcon);
            gMenuItem.OptionIcon = GOSSIP_ICON_CHAT;
        }

        if (gMenuItem.OptionType >= GOSSIP_OPTION_MAX)
            sLog->outErrorDb("Table gossip_menu_option for menu %u, id %u has unknown option id %u. Option will not be used", gMenuItem.MenuId, gMenuItem.OptionIndex, gMenuItem.OptionType);

        if (gMenuItem.ActionPoiId && !GetPointOfInterest(gMenuItem.ActionPoiId))
        {
            sLog->outErrorDb("Table gossip_menu_option for menu %u, id %u use non-existing action_poi_id %u, ignoring", gMenuItem.MenuId, gMenuItem.OptionIndex, gMenuItem.ActionPoiId);
            gMenuItem.ActionPoiId = 0;
        }

        _gossipMenuItemsStore.insert(GossipMenuItemsContainer::value_type(gMenuItem.MenuId, gMenuItem));
        ++count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u gossip_menu_option entries in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void ObjectMgr::AddVendorItem(uint32 entry, uint32 item, int32 maxcount, uint32 incrtime, uint32 extendedCost, bool persist /*= true*/)
{
    VendorItemData& vList = _cacheVendorItemStore[entry];
    vList.AddItem(item, maxcount, incrtime, extendedCost);

    if (persist)
    {
        PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_NPC_VENDOR);

        stmt->setUInt32(0, entry);
        stmt->setUInt32(1, item);
        stmt->setUInt8(2, maxcount);
        stmt->setUInt32(3, incrtime);
        stmt->setUInt32(4, extendedCost);

        WorldDatabase.Execute(stmt);
    }
}

bool ObjectMgr::RemoveVendorItem(uint32 entry, uint32 item, bool persist /*= true*/)
{
    CacheVendorItemContainer::iterator  iter = _cacheVendorItemStore.find(entry);
    if (iter == _cacheVendorItemStore.end())
        return false;

    if (!iter->second.RemoveItem(item))
        return false;

    if (persist)
    {
        PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_NPC_VENDOR);

        stmt->setUInt32(0, entry);
        stmt->setUInt32(1, item);

        WorldDatabase.Execute(stmt);
    }

    return true;
}

bool ObjectMgr::IsVendorItemValid(uint32 vendor_entry, uint32 item_id, int32 maxcount, uint32 incrtime, uint32 ExtendedCost, Player* player, std::set<uint32>* skip_vendors, uint32 ORnpcflag) const
{
    CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(vendor_entry);
    if (!cInfo)
    {
        if (player)
            ChatHandler(player->GetSession()).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
        else
            sLog->outErrorDb("Table `(game_event_)npc_vendor` have data for not existed creature template (Entry: %u), ignore", vendor_entry);
        return false;
    }

    if (!((cInfo->npcflag | ORnpcflag) & UNIT_NPC_FLAG_VENDOR))
    {
        if (!skip_vendors || skip_vendors->count(vendor_entry) == 0)
        {
            if (player)
                ChatHandler(player->GetSession()).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
            else
                sLog->outErrorDb("Table `(game_event_)npc_vendor` have data for not creature template (Entry: %u) without vendor flag, ignore", vendor_entry);

            if (skip_vendors)
                skip_vendors->insert(vendor_entry);
        }
        return false;
    }

    if (!sObjectMgr->GetItemTemplate(item_id))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ITEM_NOT_FOUND, item_id);
        else
            sLog->outErrorDb("Table `(game_event_)npc_vendor` for Vendor (Entry: %u) have in item list non-existed item (%u), ignore", vendor_entry, item_id);
        return false;
    }

    if (ExtendedCost && !sItemExtendedCostStore.LookupEntry(ExtendedCost))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_EXTENDED_COST_NOT_EXIST, ExtendedCost);
        else
            sLog->outErrorDb("Table `(game_event_)npc_vendor` have Item (Entry: %u) with wrong ExtendedCost (%u) for vendor (%u), ignore", item_id, ExtendedCost, vendor_entry);
        return false;
    }

    if (maxcount > 0 && incrtime == 0)
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage("MaxCount != 0 (%u) but IncrTime == 0", maxcount);
        else
            sLog->outErrorDb("Table `(game_event_)npc_vendor` has `maxcount` (%u) for item %u of vendor (Entry: %u) but `incrtime`=0, ignore", maxcount, item_id, vendor_entry);
        return false;
    }
    else if (maxcount == 0 && incrtime > 0)
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage("MaxCount == 0 but IncrTime<>= 0");
        else
            sLog->outErrorDb("Table `(game_event_)npc_vendor` has `maxcount`=0 for item %u of vendor (Entry: %u) but `incrtime`<>0, ignore", item_id, vendor_entry);
        return false;
    }

    VendorItemData const* vItems = GetNpcVendorItemList(vendor_entry);
    if (!vItems)
        return true;                                        // later checks for non-empty lists

    if (vItems->FindItemCostPair(item_id, ExtendedCost))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ITEM_ALREADY_IN_LIST, item_id, ExtendedCost);
        else
            sLog->outErrorDb("Table `npc_vendor` has duplicate items %u (with extended cost %u) for vendor (Entry: %u), ignoring", item_id, ExtendedCost, vendor_entry);
        return false;
    }

    return true;
}

void ObjectMgr::LoadScriptNames()
{
    uint32 oldMSTime = getMSTime();

    _scriptNamesStore.push_back("");
    QueryResult result = WorldDatabase.Query(
      "SELECT DISTINCT(ScriptName) FROM creature_template WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM gameobject_template WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM item_template WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM areatrigger_scripts WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM spell_script_names WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM transports WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM game_weather WHERE ScriptName <> '' "
      "UNION "
      "SELECT DISTINCT(ScriptName) FROM conditions WHERE ScriptName <> '' ");

    if (!result)
    {
        sLog->outString();
        sLog->outErrorDb(">> Loaded empty set of Script Names!");
        return;
    }

    uint32 count = 1;

    do
    {
        _scriptNamesStore.push_back((*result)[0].GetString());
        ++count;
    }
    while (result->NextRow());

    std::sort(_scriptNamesStore.begin(), _scriptNamesStore.end());
    sLog->outString(">> Loaded %d Script Names in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

uint32 ObjectMgr::GetScriptId(const char *name)
{
    // use binary search to find the script name in the sorted vector
    // assume "" is the first element
    if (!name)
        return 0;

    ScriptNameContainer::const_iterator itr = std::lower_bound(_scriptNamesStore.begin(), _scriptNamesStore.end(), name);
    if (itr == _scriptNamesStore.end() || *itr != name)
        return 0;

    return uint32(itr - _scriptNamesStore.begin());
}

void ObjectMgr::CheckScripts(ScriptsType type, std::set<int32>& ids)
{
    ScriptMapMap* scripts = GetScriptsMapByType(type);
    if (!scripts)
        return;

    for (ScriptMapMap::const_iterator itrMM = scripts->begin(); itrMM != scripts->end(); ++itrMM)
    {
        for (ScriptMap::const_iterator itrM = itrMM->second.begin(); itrM != itrMM->second.end(); ++itrM)
        {
            switch (itrM->second.command)
            {
                case SCRIPT_COMMAND_TALK:
                {
                    if (!GetTrinityStringLocale (itrM->second.Talk.TextID))
                        sLog->outErrorDb("Table `%s` references invalid text id %u from `db_script_string`, script id: %u.", GetScriptsTableNameByType(type).c_str(), itrM->second.Talk.TextID, itrMM->first);

                    if (ids.find(itrM->second.Talk.TextID) != ids.end())
                        ids.erase(itrM->second.Talk.TextID);
                }
                default:
                    break;
            }
        }
    }
}

CreatureBaseStats const* ObjectMgr::GetCreatureBaseStats(uint8 level, uint8 unitClass)
{
    CreatureBaseStatsContainer::const_iterator it = _creatureBaseStatsStore.find(MAKE_PAIR16(level, unitClass));

    if (it != _creatureBaseStatsStore.end())
        return &(it->second);

    struct DefaultCreatureBaseStats : public CreatureBaseStats
    {
        DefaultCreatureBaseStats()
        {
            BaseArmor = 1;
            for (uint8 j = 0; j < MAX_EXPANSIONS; ++j)
            {
                BaseHealth[j] = 1;
                BaseDamage[j] = 0.0f;
            }
            BaseMana = 0;
            AttackPower = 0;
            RangedAttackPower = 0;
        }
    };
    static const DefaultCreatureBaseStats defStats;
    return &defStats;
}

void ObjectMgr::LoadCreatureClassLevelStats()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT level, class, basehp0, basehp1, basehp2, basemana, basearmor, attackpower, rangedattackpower, damage_base, damage_exp1, damage_exp2 FROM creature_classlevelstats");

    if (!result)
    {
        sLog->outString(">> Loaded 0 creature base stats. DB table `creature_classlevelstats` is empty.");
        sLog->outString();
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint8 Level = fields[0].GetUInt8();
        uint8 Class = fields[1].GetUInt8();

        if (!Class || ((1 << (Class - 1)) & CLASSMASK_ALL_CREATURES) == 0)
            sLog->outErrorDb("Creature base stats for level %u has invalid class %u", Level, Class);

        CreatureBaseStats stats;

        for (uint8 i = 0; i < MAX_EXPANSIONS; ++i)
        {
            stats.BaseHealth[i] = fields[2 + i].GetUInt16();

            if (stats.BaseHealth[i] == 0)
            {
                sLog->outErrorDb("Creature base stats for class %u, level %u has invalid zero base HP[%u] - set to 1", Class, Level, i);
                stats.BaseHealth[i] = 1;
            }

			// xinef: if no data is available, get them from lower expansions
			if (stats.BaseHealth[i] <= 1)
			{
				for (uint8 j = i; j > 0;)
				{
					--j;
					if (stats.BaseHealth[j] > 1)
					{
						stats.BaseHealth[i] = stats.BaseHealth[j];
						break;
					}
				}
			}

            stats.BaseDamage[i] = fields[9 + i].GetFloat();
            if (stats.BaseDamage[i] < 0.0f)
            {
                sLog->outErrorDb("Creature base stats for class %u, level %u has invalid negative base damage[%u] - set to 0.0", Class, Level, i);
                stats.BaseDamage[i] = 0.0f;
            }
        }

        stats.BaseMana = fields[5].GetUInt16();
        stats.BaseArmor = fields[6].GetUInt16();

        stats.AttackPower = fields[7].GetUInt16();
        stats.RangedAttackPower = fields[8].GetUInt16();

        _creatureBaseStatsStore[MAKE_PAIR16(Level, Class)] = stats;

        ++count;
    }
    while (result->NextRow());

    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {
        for (uint16 lvl = itr->second.minlevel; lvl <= itr->second.maxlevel; ++lvl)
        {
            if (_creatureBaseStatsStore.find(MAKE_PAIR16(lvl, itr->second.unit_class)) == _creatureBaseStatsStore.end())
                sLog->outErrorDb("Missing base stats for creature class %u level %u", itr->second.unit_class, lvl);
        }
    }

    sLog->outString(">> Loaded %u creature base stats in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

GameObjectTemplate const* ObjectMgr::GetGameObjectTemplate(uint32 entry)
{
    GameObjectTemplateContainer::const_iterator itr = _gameObjectTemplateStore.find(entry);
    if (itr != _gameObjectTemplateStore.end())
        return &(itr->second);

    return NULL;
}

bool ObjectMgr::IsGameObjectStaticTransport(uint32 entry)
{
	GameObjectTemplate const* goinfo = GetGameObjectTemplate(entry);
	return goinfo && goinfo->type == GAMEOBJECT_TYPE_TRANSPORT;
}

CreatureTemplate const* ObjectMgr::GetCreatureTemplate(uint32 entry)
{
    return entry < _creatureTemplateStoreFast.size() ? _creatureTemplateStoreFast[entry] : NULL;
}

VehicleAccessoryList const* ObjectMgr::GetVehicleAccessoryList(Vehicle* veh) const
{
    if (Creature* cre = veh->GetBase()->ToCreature())
    {
        // Give preference to GUID-based accessories
        VehicleAccessoryContainer::const_iterator itr = _vehicleAccessoryStore.find(cre->GetDBTableGUIDLow());
        if (itr != _vehicleAccessoryStore.end())
            return &itr->second;
    }

    // Otherwise return entry-based
    VehicleAccessoryContainer::const_iterator itr = _vehicleTemplateAccessoryStore.find(veh->GetCreatureEntry());
    if (itr != _vehicleTemplateAccessoryStore.end())
        return &itr->second;
    return NULL;
}

PlayerInfo const* ObjectMgr::GetPlayerInfo(uint32 race, uint32 class_) const
{
    if (race >= MAX_RACES)
        return NULL;
    if (class_ >= MAX_CLASSES)
        return NULL;
    PlayerInfo const* info = _playerInfo[race][class_];
    if (!info)
        return NULL;
    return info;
}
