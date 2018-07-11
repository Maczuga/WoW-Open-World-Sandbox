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

#include "DisableMgr.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "VMapManager2.h"
#include "Player.h"
#include "SpellInfo.h"

namespace DisableMgr
{

namespace
{
    struct DisableData
    {
        uint8 flags;
        std::set<uint32> params[2];                             // params0, params1
    };

    // single disables here with optional data
    typedef std::map<uint32, DisableData> DisableTypeMap;
    // global disable map by source
    typedef std::map<DisableType, DisableTypeMap> DisableMap;

    DisableMap m_DisableMap;

    uint8 MAX_DISABLE_TYPES = 9;
}

void LoadDisables()
{
    uint32 oldMSTime = getMSTime();

    // reload case
    for (DisableMap::iterator itr = m_DisableMap.begin(); itr != m_DisableMap.end(); ++itr)
        itr->second.clear();

    m_DisableMap.clear();

    QueryResult result = WorldDatabase.Query("SELECT sourceType, entry, flags, params_0, params_1 FROM disables");

    uint32 total_count = 0;

    if (!result)
    {
        sLog->outString(">> Loaded 0 disables. DB table `disables` is empty!");
        sLog->outString();
        return;
    }

    Field* fields;
    do
    {
        fields = result->Fetch();
        DisableType type = DisableType(fields[0].GetUInt32());
        if (type >= MAX_DISABLE_TYPES)
        {
            sLog->outErrorDb("Invalid type %u specified in `disables` table, skipped.", type);
            continue;
        }

        uint32 entry = fields[1].GetUInt32();
        uint8 flags = fields[2].GetUInt8();
        std::string params_0 = fields[3].GetString();
        std::string params_1 = fields[4].GetString();

        DisableData data;
        data.flags = flags;

        switch (type)
        {
            case DISABLE_TYPE_GO_LOS:
                if (!sObjectMgr->GetGameObjectTemplate(entry))
                {
                    sLog->outErrorDb("Gameobject entry %u from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                if (flags)
                    sLog->outErrorDb("Disable flags specified for gameobject %u, useless data.", entry);
                break;
            case DISABLE_TYPE_SPELL:
                if (!(sSpellMgr->GetSpellInfo(entry) || flags & SPELL_DISABLE_DEPRECATED_SPELL))
                {
                    sLog->outErrorDb("Spell entry %u from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }

                if (!flags || flags > MAX_SPELL_DISABLE_TYPE)
                {
                    sLog->outErrorDb("Disable flags for spell %u are invalid, skipped.", entry);
                    continue;
                }

                if (flags & SPELL_DISABLE_MAP)
                {
                    Tokenizer tokens(params_0, ',');
                    for (uint8 i = 0; i < tokens.size(); )
                        data.params[0].insert(atoi(tokens[i++]));
                }

                if (flags & SPELL_DISABLE_AREA)
                {
                    Tokenizer tokens(params_1, ',');
                    for (uint8 i = 0; i < tokens.size(); )
                        data.params[1].insert(atoi(tokens[i++]));
                }

				// xinef: if spell has disabled los, add flag
				if (flags & SPELL_DISABLE_LOS)
				{
					SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(entry));
					spellInfo->AttributesEx2 |= SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS;
				}

                break;
            // checked later
            case DISABLE_TYPE_QUEST:
                break;
            case DISABLE_TYPE_VMAP:
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (!mapEntry)
                {
                    sLog->outErrorDb("Map entry %u from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                if (flags & VMAP_DISABLE_AREAFLAG)
                    sLog->outString("Areaflag disabled for map %u.", entry);
                if (flags & VMAP_DISABLE_LIQUIDSTATUS)
                    sLog->outString("Liquid status disabled for map %u.", entry);
                break;
            }
            case DISABLE_TYPE_MMAP:
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (!mapEntry)
                {
                    sLog->outError("Map entry %u from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                sLog->outString("Pathfinding disabled for map %u.", entry);
                break;
            }
            default:
                break;
        }

        m_DisableMap[type].insert(DisableTypeMap::value_type(entry, data));
        ++total_count;
    }
    while (result->NextRow());

    sLog->outString(">> Loaded %u disables in %u ms", total_count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

void CheckQuestDisables()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = m_DisableMap[DISABLE_TYPE_QUEST].size();
    if (!count)
    {
        sLog->outString(">> Checked 0 quest disables.");
        sLog->outString();
        return;
    }

    // check only quests, rest already done at startup
    for (DisableTypeMap::iterator itr = m_DisableMap[DISABLE_TYPE_QUEST].begin(); itr != m_DisableMap[DISABLE_TYPE_QUEST].end();)
    {
        const uint32 entry = itr->first;
        if (!sObjectMgr->GetQuestTemplate(entry))
        {
            sLog->outErrorDb("Quest entry %u from `disables` doesn't exist, skipped.", entry);
            m_DisableMap[DISABLE_TYPE_QUEST].erase(itr++);
            continue;
        }
        if (itr->second.flags)
            sLog->outErrorDb("Disable flags specified for quest %u, useless data.", entry);
        ++itr;
    }

    sLog->outString(">> Checked %u quest disables in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    sLog->outString();
}

bool IsDisabledFor(DisableType type, uint32 entry, Unit const* unit, uint8 flags)
{
    ASSERT(type < MAX_DISABLE_TYPES);
    if (m_DisableMap[type].empty())
        return false;

    DisableTypeMap::iterator itr = m_DisableMap[type].find(entry);
    if (itr == m_DisableMap[type].end())    // not disabled
        return false;

    switch (type)
    {
        case DISABLE_TYPE_SPELL:
        {
            uint8 spellFlags = itr->second.flags;
            if (unit)
            {
                if ((spellFlags & SPELL_DISABLE_PLAYER && unit->GetTypeId() == TYPEID_PLAYER) ||
                    (unit->GetTypeId() == TYPEID_UNIT && ((unit->IsPet() && spellFlags & SPELL_DISABLE_PET) || spellFlags & SPELL_DISABLE_CREATURE)))
                {
                    if (spellFlags & SPELL_DISABLE_MAP)
                    {
                        std::set<uint32> const& mapIds = itr->second.params[0];
                        if (mapIds.find(unit->GetMapId()) != mapIds.end())
                            return true;                                        // Spell is disabled on current map

                        if (!(spellFlags & SPELL_DISABLE_AREA))
                            return false;                                       // Spell is disabled on another map, but not this one, return false

                        // Spell is disabled in an area, but not explicitly our current mapId. Continue processing.
                    }

                    if (spellFlags & SPELL_DISABLE_AREA)
                    {
                        std::set<uint32> const& areaIds = itr->second.params[1];
                        if (areaIds.find(unit->GetAreaId()) != areaIds.end())
                            return true;                                        // Spell is disabled in this area
                        return false;                                           // Spell is disabled in another area, but not this one, return false
                    }
                    else
                        return true;                                            // Spell disabled for all maps
                }

                return false;
            }
            else if (spellFlags & SPELL_DISABLE_DEPRECATED_SPELL)    // call not from spellcast
                return true;

            break;
        }
        case DISABLE_TYPE_QUEST:
            return true;
        case DISABLE_TYPE_MMAP:
            return true;
        case DISABLE_TYPE_VMAP:
            return flags & itr->second.flags;
        case DISABLE_TYPE_GO_LOS:
            return true;
    }

    return false;
}

bool IsVMAPDisabledFor(uint32 entry, uint8 flags)
{
    return IsDisabledFor(DISABLE_TYPE_VMAP, entry, NULL, flags);
}

bool IsPathfindingEnabled(uint32 mapId)
{
    return sWorld->getBoolConfig(CONFIG_ENABLE_MMAPS)
        && !IsDisabledFor(DISABLE_TYPE_MMAP, mapId, NULL, MMAP_DISABLE_PATHFINDING);
}

bool IsPathfindingEnabled(const Map* map, bool force)
{
    return sWorld->getBoolConfig(CONFIG_ENABLE_MMAPS)
        && !IsDisabledFor(DISABLE_TYPE_MMAP, map->GetId(), NULL, MMAP_DISABLE_PATHFINDING);
}

} // Namespace
