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

#include "MapManager.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Transport.h"
#include "GridDefines.h"
#include "Config.h"
#include "World.h"
#include "CellImpl.h"
#include "Corpse.h"
#include "ObjectMgr.h"
#include "Language.h"
#include "WorldPacket.h"
#include "Group.h"
#include "Player.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Chat.h"
#include "AvgDiffTracker.h"

MapManager::MapManager()
{
    i_timer[3].SetInterval(sWorld->getIntConfig(CONFIG_INTERVAL_MAPUPDATE));
    mapUpdateStep = 0;
    _nextInstanceId = 0;
}

MapManager::~MapManager()
{
}

void MapManager::Initialize()
{
    int num_threads(sWorld->getIntConfig(CONFIG_NUMTHREADS));
    // Start mtmaps if needed.
    if (num_threads > 0 && m_updater.activate(num_threads) == -1)
        abort();
}

void MapManager::InitializeVisibilityDistanceInfo()
{
    for (MapMapType::iterator iter=i_maps.begin(); iter != i_maps.end(); ++iter)
        (*iter).second->InitVisibilityDistance();
}

Map* MapManager::CreateBaseMap(uint32 id)
{
    Map* map = FindBaseMap(id);

    if (map == NULL)
    {
        TRINITY_GUARD(ACE_Thread_Mutex, Lock);

        map = FindBaseMap(id);
        if (map == NULL) // pussywizard: check again after acquiring mutex
        {
            MapEntry const* entry = sMapStore.LookupEntry(id);
            ASSERT(entry);

            map = new Map(id, 0, REGULAR_DIFFICULTY);
            map->LoadRespawnTimes();

            i_maps[id] = map;
        }
    }

    ASSERT(map);
    return map;
}

Map* MapManager::FindBaseNonInstanceMap(uint32 mapId) const
{
    Map* map = FindBaseMap(mapId);

    return map;
}

Map* MapManager::CreateMap(uint32 id, Player* player)
{
    Map* m = CreateBaseMap(id);

    return m;
}

Map* MapManager::FindMap(uint32 mapid, uint32 instanceId) const
{
    Map* map = FindBaseMap(mapid);
    if (!map)
        return NULL;

    return instanceId == 0 ? map : NULL;
}

bool MapManager::CanPlayerEnter(uint32 mapid, Player* player, bool loginCheck)
{
    MapEntry const* entry = sMapStore.LookupEntry(mapid);
    if (!entry)
       return false;

    if (!entry->IsDungeon())
        return true;

    InstanceTemplate const* instance = sObjectMgr->GetInstanceTemplate(mapid);
    if (!instance)
        return false;

    Difficulty targetDifficulty, requestedDifficulty;
    targetDifficulty = requestedDifficulty = Difficulty(0);

    //Bypass checks for GMs
    if (player->IsGameMaster())
        return true;

    // Maczuga: Crash fix
    if (player->GetSession() != nullptr)
        return true;

    char const* mapName = entry->name[player->GetSession()->GetSessionDbcLocale()];

    Group* group = player->GetGroup();

    if (!player->IsAlive())
    {
        if (Corpse* corpse = player->GetCorpse())
        {
            // let enter in ghost mode in instance that connected to inner instance with corpse
            uint32 corpseMap = corpse->GetMapId();
            do
            {
                if (corpseMap == mapid)
                    break;

                InstanceTemplate const* corpseInstance = sObjectMgr->GetInstanceTemplate(corpseMap);
                corpseMap = corpseInstance ? corpseInstance->Parent : 0;
            } while (corpseMap);

            if (!corpseMap)
            {
                WorldPacket data(SMSG_CORPSE_NOT_IN_INSTANCE, 0);
                player->GetSession()->SendPacket(&data);
                ;//sLog->outDebug(LOG_FILTER_MAPS, "MAP: Player '%s' does not have a corpse in instance '%s' and cannot enter.", player->GetName().c_str(), mapName);
                return false;
            }
            ;//sLog->outDebug(LOG_FILTER_MAPS, "MAP: Player '%s' has corpse in instance '%s' and can enter.", player->GetName().c_str(), mapName);
            player->ResurrectPlayer(0.5f, false);
            player->SpawnCorpseBones();
        }
        else
            ;//sLog->outDebug(LOG_FILTER_MAPS, "Map::CanPlayerEnter - player '%s' is dead but does not have a corpse!", player->GetName().c_str());
    }

    //Other requirements
    return player->Satisfy(sObjectMgr->GetAccessRequirement(mapid, targetDifficulty), mapid, true);
}

void MapManager::Update(uint32 diff)
{
    for (uint8 i=0; i<4; ++i)
        i_timer[i].Update(diff);

    MapMapType::iterator iter = i_maps.begin();
    for (; iter != i_maps.end(); ++iter)
    {
        bool full = mapUpdateStep<3 && mapUpdateStep==0;
        if (m_updater.activated())
            m_updater.schedule_update(*iter->second, uint32(full ? i_timer[mapUpdateStep].GetCurrent() : 0), diff);
        else
            iter->second->Update(uint32(full ? i_timer[mapUpdateStep].GetCurrent() : 0), diff);
    }

    if (m_updater.activated())
        m_updater.wait();

    sObjectAccessor->ProcessDelayedCorpseActions();

    if (mapUpdateStep<3)
    {
        for (iter = i_maps.begin(); iter != i_maps.end(); ++iter)
        {
            bool full = mapUpdateStep == 0;
            if (full)
                iter->second->DelayedUpdate(uint32(i_timer[mapUpdateStep].GetCurrent()));
        }

        i_timer[mapUpdateStep].SetCurrent(0);
        ++mapUpdateStep;
    }

    sObjectAccessor->Update(0);

    if (mapUpdateStep == 3 && i_timer[3].Passed())
    {
        mapUpdateStep = 0;
        i_timer[3].SetCurrent(0);
    }
}

void MapManager::DoDelayedMovesAndRemoves()
{
}

bool MapManager::ExistMapAndVMap(uint32 mapid, float x, float y)
{
    GridCoord p = Trinity::ComputeGridCoord(x, y);

    int gx=63-p.x_coord;
    int gy=63-p.y_coord;

    return Map::ExistMap(mapid, gx, gy) && Map::ExistVMap(mapid, gx, gy);
}

bool MapManager::IsValidMAP(uint32 mapid, bool startUp)
{
    MapEntry const* mEntry = sMapStore.LookupEntry(mapid);

    if (startUp)
        return mEntry ? true : false;
    else
        return mEntry && (!mEntry->IsDungeon() || sObjectMgr->GetInstanceTemplate(mapid));

    // TODO: add check for battleground template
}

void MapManager::UnloadAll()
{
    for (MapMapType::iterator iter = i_maps.begin(); iter != i_maps.end();)
    {
        iter->second->UnloadAll();
        delete iter->second;
        i_maps.erase(iter++);
    }

    if (m_updater.activated())
        m_updater.deactivate();
}

void MapManager::InitInstanceIds()
{
    _nextInstanceId = 1;

    QueryResult result = CharacterDatabase.Query("SELECT MAX(id) FROM instance");
    if (result)
    {
        uint32 maxId = (*result)[0].GetUInt32();
        _instanceIds.resize(maxId+1);
    }
}

void MapManager::RegisterInstanceId(uint32 instanceId)
{
    // Allocation was done in InitInstanceIds()
    _instanceIds[instanceId] = true;

	// Instances are pulled in ascending order from db and _nextInstanceId is initialized with 1,
    // so if the instance id is used, increment
	if (_nextInstanceId == instanceId)
		++_nextInstanceId;
}

uint32 MapManager::GenerateInstanceId()
{
    uint32 newInstanceId = _nextInstanceId;

    // find the lowest available id starting from the current _nextInstanceId
	while (_nextInstanceId < 0xFFFFFFFF && ++_nextInstanceId < _instanceIds.size() && _instanceIds[_nextInstanceId]);

    if (_nextInstanceId == 0xFFFFFFFF)
    {
        sLog->outError("Instance ID overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }

    return newInstanceId;
}
