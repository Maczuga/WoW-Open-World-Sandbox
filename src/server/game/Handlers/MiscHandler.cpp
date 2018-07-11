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

#include "Common.h"
#include "Language.h"
#include "DatabaseEnv.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Log.h"
#include "Player.h"
#include "GossipDef.h"
#include "World.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "WorldSession.h"
#include "BigNumber.h"
#include "SHA1.h"
#include "UpdateData.h"
#include "LootMgr.h"
#include "Chat.h"
#include "zlib.h"
#include "ObjectAccessor.h"
#include "Object.h"
#include "Pet.h"
#include "SocialMgr.h"
#include "CellImpl.h"
#include "AccountMgr.h"
#include "Vehicle.h"
#include "CreatureAI.h"
#include "DBCEnums.h"
#include "ScriptMgr.h"
#include "MapManager.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "AccountMgr.h"
#include "Spell.h"
#include "WhoListCache.h"

void WorldSession::HandleRepopRequestOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_REPOP_REQUEST Message");

    recv_data.read_skip<uint8>();

    if (GetPlayer()->IsAlive() || GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return;

    if (GetPlayer()->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
        return; // silently return, client should display the error by itself

    // the world update order is sessions, players, creatures
    // the netcode runs in parallel with all of these
    // creatures can kill players
    // so if the server is lagging enough the player can
    // release spirit after he's killed but before he is updated
    if (GetPlayer()->getDeathState() == JUST_DIED)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "HandleRepopRequestOpcode: got request after player %s(%d) was killed and before he was updated", GetPlayer()->GetName().c_str(), GetPlayer()->GetGUIDLow());
        GetPlayer()->KillPlayer();
    }

    //this is spirit release confirm?
    GetPlayer()->RemovePet(NULL, PET_SAVE_NOT_IN_SLOT, true);
    GetPlayer()->BuildPlayerRepop();
    GetPlayer()->RepopAtGraveyard();
}

void WorldSession::HandleGossipSelectOptionOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_GOSSIP_SELECT_OPTION");

    uint32 gossipListId;
    uint32 menuId;
    uint64 guid;
    std::string code = "";

    recv_data >> guid >> menuId >> gossipListId;

    if (_player->PlayerTalkClass->IsGossipOptionCoded(gossipListId))
        recv_data >> code;

    Creature* unit = NULL;
    GameObject* go = NULL;
    if (IS_CRE_OR_VEH_GUID(guid))
    {
        unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (!unit)
        {
            ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipSelectOptionOpcode - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(guid)));
            return;
        }
    }
    else if (IS_GAMEOBJECT_GUID(guid))
    {
        go = _player->GetMap()->GetGameObject(guid);
        if (!go)
        {
            ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipSelectOptionOpcode - GameObject (GUID: %u) not found.", uint32(GUID_LOPART(guid)));
            return;
        }
    }
    else
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipSelectOptionOpcode - unsupported GUID type for highguid %u. lowpart %u.", uint32(GUID_HIPART(guid)), uint32(GUID_LOPART(guid)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if ((unit && unit->GetCreatureTemplate()->ScriptID != unit->LastUsedScriptID) || (go && go->GetGOInfo()->ScriptId != go->LastUsedScriptID))
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipSelectOptionOpcode - Script reloaded while in use, ignoring and set new scipt id");
        if (unit)
            unit->LastUsedScriptID = unit->GetCreatureTemplate()->ScriptID;
        if (go)
            go->LastUsedScriptID = go->GetGOInfo()->ScriptId;
        _player->PlayerTalkClass->SendCloseGossip();
        return;
    }
    if (!code.empty())
    {
        if (unit)
        {
            unit->AI()->sGossipSelectCode(_player, menuId, gossipListId, code.c_str());
            if (!sScriptMgr->OnGossipSelectCode(_player, unit, _player->PlayerTalkClass->GetGossipOptionSender(gossipListId), _player->PlayerTalkClass->GetGossipOptionAction(gossipListId), code.c_str()))
                _player->OnGossipSelect(unit, gossipListId, menuId);
        }
        else
        {
            go->AI()->GossipSelectCode(_player, menuId, gossipListId, code.c_str());
            sScriptMgr->OnGossipSelectCode(_player, go, _player->PlayerTalkClass->GetGossipOptionSender(gossipListId), _player->PlayerTalkClass->GetGossipOptionAction(gossipListId), code.c_str());
        }
    }
    else
    {
        if (unit)
        {
            unit->AI()->sGossipSelect(_player, menuId, gossipListId);
            if (!sScriptMgr->OnGossipSelect(_player, unit, _player->PlayerTalkClass->GetGossipOptionSender(gossipListId), _player->PlayerTalkClass->GetGossipOptionAction(gossipListId)))
                _player->OnGossipSelect(unit, gossipListId, menuId);
        }
        else
        {
            go->AI()->GossipSelect(_player, menuId, gossipListId);
            if (!sScriptMgr->OnGossipSelect(_player, go, _player->PlayerTalkClass->GetGossipOptionSender(gossipListId), _player->PlayerTalkClass->GetGossipOptionAction(gossipListId)))
                _player->OnGossipSelect(go, gossipListId, menuId);
        }
    }
}

void WorldSession::HandleWhoOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_WHO Message");

    time_t now = time(NULL);
    if (now < timeWhoCommandAllowed)
        return;
    timeWhoCommandAllowed = now+3;

    uint32 matchcount = 0;

    uint32 level_min, level_max, racemask, classmask, zones_count, str_count;
    uint32 zoneids[10];                                     // 10 is client limit
    std::string player_name, guild_name;

    recv_data >> level_min;                                 // maximal player level, default 0
    recv_data >> level_max;                                 // minimal player level, default 100 (MAX_LEVEL)
    recv_data >> player_name;                               // player name, case sensitive...

    recv_data >> guild_name;                                // guild name, case sensitive...

    recv_data >> racemask;                                  // race mask
    recv_data >> classmask;                                 // class mask
    recv_data >> zones_count;                               // zones count, client limit = 10 (2.0.10)

    if (zones_count > 10)
        return;                                             // can't be received from real client or broken packet

    for (uint32 i = 0; i < zones_count; ++i)
    {
        uint32 temp;
        recv_data >> temp;                                  // zone id, 0 if zone is unknown...
        zoneids[i] = temp;
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Zone %u: %u", i, zoneids[i]);
    }

    recv_data >> str_count;                                 // user entered strings count, client limit=4 (checked on 2.0.10)

    if (str_count > 4)
        return;                                             // can't be received from real client or broken packet

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Minlvl %u, maxlvl %u, name %s, guild %s, racemask %u, classmask %u, zones %u, strings %u", level_min, level_max, player_name.c_str(), guild_name.c_str(), racemask, classmask, zones_count, str_count);

    std::wstring str[4];                                    // 4 is client limit
    for (uint32 i = 0; i < str_count; ++i)
    {
        std::string temp;
        recv_data >> temp;                                  // user entered string, it used as universal search pattern(guild+player name)?

        if (!Utf8toWStr(temp, str[i]))
            continue;

        wstrToLower(str[i]);

        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "String %u: %s", i, temp.c_str());
    }

    std::wstring wplayer_name;
    std::wstring wguild_name;
    if (!(Utf8toWStr(player_name, wplayer_name) && Utf8toWStr(guild_name, wguild_name)))
        return;
    wstrToLower(wplayer_name);
    wstrToLower(wguild_name);

    // client send in case not set max level value 100 but Trinity supports 255 max level,
    // update it to show GMs with characters after 100 level
    if (level_max >= MAX_LEVEL)
        level_max = STRONG_MAX_LEVEL;

    TeamId teamId = _player->GetTeamId();
    uint32 security = GetSecurity();
    uint32 displaycount = 0;

    WorldPacket data(SMSG_WHO, 500);                      // guess size
    data << uint32(matchcount);                           // placeholder, count of players matching criteria
    data << uint32(displaycount);                         // placeholder, count of players displayed

	std::vector<WhoListPlayerInfo> * m = WhoListCacheMgr::GetWhoList();
	for (std::vector<WhoListPlayerInfo>::const_iterator itr = m->begin(); itr != m->end(); ++itr)
    {
        if (AccountMgr::IsPlayerAccount(security))
        {
			if ((*itr).teamId != teamId)
                continue;
        }

        //do not process players which are not in world
        //if (!(itr->second->IsInWorld()))
        //    continue;

        // check if target is globally visible for player
        //if (!(itr->second->IsVisibleGloballyFor(_player)))
        //    continue;

        // check if target's level is in level range
        uint8 lvl = (*itr).level;
        if (lvl < level_min || lvl > level_max)
            continue;

        // check if class matches classmask
        uint32 class_ = (*itr).clas;
        if (!(classmask & (1 << class_)))
            continue;

        // check if race matches racemask
        uint32 race = (*itr).race;
        if (!(racemask & (1 << race)))
            continue;

        uint32 pzoneid = (*itr).zoneid;
        uint8 gender = (*itr).gender;

        bool z_show = true;
        for (uint32 i = 0; i < zones_count; ++i)
        {
            if (zoneids[i] == pzoneid)
            {
                z_show = true;
                break;
            }

            z_show = false;
        }
        if (!z_show)
            continue;

        std::wstring wpname = (*itr).wpname;

        if (!(wplayer_name.empty() || wpname.find(wplayer_name) != std::wstring::npos))
            continue;

        std::wstring wgname = (*itr).wgname;

        if (!(wguild_name.empty() || wgname.find(wguild_name) != std::wstring::npos))
            continue;

        std::string aname = (*itr).aname;

        bool s_show = true;
        for (uint32 i = 0; i < str_count; ++i)
        {
            if (!str[i].empty())
            {
                if (wgname.find(str[i]) != std::wstring::npos ||
                    wpname.find(str[i]) != std::wstring::npos ||
                    Utf8FitTo(aname, str[i]))
                {
                    s_show = true;
                    break;
                }
                s_show = false;
            }
        }
        if (!s_show)
            continue;

		// 50 is maximum player count sent to client
		if (matchcount >= 50)
		{
			++matchcount;
			continue;
		}

        data << (*itr).pname;                             // player name
        data << (*itr).gname;                             // guild name
        data << uint32(lvl);                              // player level
        data << uint32(class_);                           // player class
        data << uint32(race);                             // player race
        data << uint8(gender);                            // player gender
        data << uint32(pzoneid);                          // player zone id

        ++matchcount;
        ++displaycount;
    }

    data.put(0, displaycount);                            // insert right count, count displayed
    data.put(4, matchcount);                              // insert right count, count of matches

    SendPacket(&data);
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Send SMSG_WHO Message");
}

void WorldSession::HandleLogoutRequestOpcode(WorldPacket & /*recv_data*/)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_LOGOUT_REQUEST Message, security - %u", GetSecurity());

    if (uint64 lguid = GetPlayer()->GetLootGUID())
        DoLootRelease(lguid);

    bool instantLogout = (GetSecurity() > SEC_PLAYER || GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && !GetPlayer()->IsInCombat()) || GetPlayer()->IsInFlight();

    /// TODO: Possibly add RBAC permission to log out in combat
    bool canLogoutInCombat = GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);

    uint32 reason = 0;
    if (GetPlayer()->IsInCombat() && !canLogoutInCombat)
        reason = 1;
    else if (GetPlayer()->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))
        reason = 3;                                         // is jumping or falling
    else if (GetPlayer()->duel || GetPlayer()->HasAura(9454)) // is dueling or frozen by GM via freeze command
        reason = 2;                                         // FIXME - Need the correct value

    WorldPacket data(SMSG_LOGOUT_RESPONSE, 1+4);
    data << uint32(reason);
    data << uint8(instantLogout);
    SendPacket(&data);

    if (reason)
    {
        LogoutRequest(0);
        return;
    }

    //instant logout in taverns/cities or on taxi or for admins, gm's, mod's if its enabled in worldserver.conf
    if (instantLogout)
    {
        LogoutPlayer(true);
        return;
    }

    // not set flags if player can't free move to prevent lost state at logout cancel
    if (GetPlayer()->CanFreeMove())
    {
        if (GetPlayer()->getStandState() == UNIT_STAND_STATE_STAND)
            GetPlayer()->SetStandState(UNIT_STAND_STATE_SIT);

        WorldPacket data(SMSG_FORCE_MOVE_ROOT, (8+4));    // guess size
        data.append(GetPlayer()->GetPackGUID());
        data << (uint32)2;
        SendPacket(&data);
        GetPlayer()->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);
    }

    LogoutRequest(time(NULL));
}

void WorldSession::HandlePlayerLogoutOpcode(WorldPacket & /*recv_data*/)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_PLAYER_LOGOUT Message");
}

void WorldSession::HandleLogoutCancelOpcode(WorldPacket & /*recv_data*/)
{
    LogoutRequest(0);

    WorldPacket data(SMSG_LOGOUT_CANCEL_ACK, 0);
    SendPacket(&data);

    // not remove flags if can't free move - its not set in Logout request code.
    if (GetPlayer()->CanFreeMove())
    {
        data.Initialize(SMSG_FORCE_MOVE_UNROOT, 9+4);
        data.append(GetPlayer()->GetPackGUID());
        data << uint32(0);
        SendPacket(&data);

        GetPlayer()->SetStandState(UNIT_STAND_STATE_STAND);
        GetPlayer()->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED);
    }
}

void WorldSession::HandleTogglePvP(WorldPacket & recv_data)
{
    // this opcode can be used in two ways: Either set explicit new status or toggle old status
    if (recv_data.size() == 1)
    {
        bool newPvPStatus;
        recv_data >> newPvPStatus;
        GetPlayer()->ApplyModFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP, newPvPStatus);
    }
    else
        GetPlayer()->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP);

    if (GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
        GetPlayer()->UpdatePvP(true, true);
    else if (!GetPlayer()->pvpInfo.IsHostile && GetPlayer()->IsPvP()) // pussywizard: in pvp mode, but doesn't want to be and not in hostile territory, so start timer
        GetPlayer()->UpdatePvP(true, false);

    //if (OutdoorPvP* pvp = _player->GetOutdoorPvP())
    //    pvp->HandlePlayerActivityChanged(_player);
}

void WorldSession::HandleZoneUpdateOpcode(WorldPacket & recv_data)
{
    uint32 newZone;
    recv_data >> newZone;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd ZONE_UPDATE: %u", newZone);

    // use server size data
    uint32 newzone, newarea;
    GetPlayer()->GetZoneAndAreaId(newzone, newarea, true);
    GetPlayer()->UpdateZone(newzone, newarea);
    //GetPlayer()->SendInitWorldStates(true, newZone);
}

void WorldSession::HandleSetSelectionOpcode(WorldPacket & recv_data)
{
    uint64 guid;
    recv_data >> guid;

    _player->SetSelection(guid);
}

void WorldSession::HandleStandStateChangeOpcode(WorldPacket & recv_data)
{
    // ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: Received CMSG_STANDSTATECHANGE"); -- too many spam in log at lags/debug stop
    uint32 animstate;
    recv_data >> animstate;

    _player->SetStandState(animstate);
}

void WorldSession::HandleContactListOpcode(WorldPacket & recv_data)
{
    uint32 unk;
    recv_data >> unk;
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_CONTACT_LIST - Unk: %d", unk);
    _player->GetSocial()->SendSocialList(_player);
}

void WorldSession::HandleAddFriendOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_ADD_FRIEND");

    std::string friendName = GetTrinityString(LANG_FRIEND_IGNORE_UNKNOWN);
    std::string friendNote;

    recv_data >> friendName;

    recv_data >> friendNote;

    if (!normalizePlayerName(friendName))
        return;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: %s asked to add friend : '%s'", GetPlayer()->GetName().c_str(), friendName.c_str());

	// xinef: Get Data From global storage
	uint32 guidLow = sWorld->GetGlobalPlayerGUID(friendName);
	if (!guidLow)
		return;

	GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(guidLow);
	if (!playerData)
		return;

    uint64 friendGuid = MAKE_NEW_GUID(guidLow, 0, HIGHGUID_PLAYER);
	uint32 friendAccountId = playerData->accountId;
    TeamId teamId = Player::TeamIdForRace(playerData->race);
    FriendsResult friendResult = FRIEND_NOT_FOUND;

    if (!AccountMgr::IsPlayerAccount(GetSecurity()) || AccountMgr::IsPlayerAccount(AccountMgr::GetSecurity(friendAccountId, realmID)))
    {
        if (friendGuid)
        {
            if (friendGuid == GetPlayer()->GetGUID())
                friendResult = FRIEND_SELF;
            else if (GetPlayer()->GetTeamId() != teamId && AccountMgr::IsPlayerAccount(GetSecurity()))
                friendResult = FRIEND_ENEMY;
            else if (GetPlayer()->GetSocial()->HasFriend(guidLow))
                friendResult = FRIEND_ALREADY;
            else
            {
                Player* pFriend = ObjectAccessor::FindPlayerInOrOutOfWorld(friendGuid);
                if (pFriend && pFriend->IsVisibleGloballyFor(GetPlayer()) && !AccountMgr::IsGMAccount(pFriend->GetSession()->GetSecurity()))
                    friendResult = FRIEND_ADDED_ONLINE;
                else
                    friendResult = FRIEND_ADDED_OFFLINE;
                if (!GetPlayer()->GetSocial()->AddToSocialList(guidLow, false))
                {
                    friendResult = FRIEND_LIST_FULL;
                    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: %s's friend list is full.", GetPlayer()->GetName().c_str());
                }
            }
            GetPlayer()->GetSocial()->SetFriendNote(guidLow, friendNote);
        }
    }

    sSocialMgr->SendFriendStatus(GetPlayer(), friendResult, guidLow, false);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent (SMSG_FRIEND_STATUS)");
}

void WorldSession::HandleDelFriendOpcode(WorldPacket & recv_data)
{
    uint64 FriendGUID;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_DEL_FRIEND");

    recv_data >> FriendGUID;

    _player->GetSocial()->RemoveFromSocialList(GUID_LOPART(FriendGUID), false);

    sSocialMgr->SendFriendStatus(GetPlayer(), FRIEND_REMOVED, GUID_LOPART(FriendGUID), false);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent motd (SMSG_FRIEND_STATUS)");
}

void WorldSession::HandleAddIgnoreOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_ADD_IGNORE");

    std::string ignoreName = GetTrinityString(LANG_FRIEND_IGNORE_UNKNOWN);

    recv_data >> ignoreName;

    if (!normalizePlayerName(ignoreName))
        return;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: %s asked to Ignore: '%s'",
    //    GetPlayer()->GetName().c_str(), ignoreName.c_str());

	uint32 lowGuid = sWorld->GetGlobalPlayerGUID(ignoreName);
    if (!lowGuid)
		return;

    uint64 IgnoreGuid = MAKE_NEW_GUID(lowGuid, 0, HIGHGUID_PLAYER);
    FriendsResult ignoreResult = FRIEND_IGNORE_NOT_FOUND;

    if (IgnoreGuid == GetPlayer()->GetGUID())              //not add yourself
        ignoreResult = FRIEND_IGNORE_SELF;
    else if (GetPlayer()->GetSocial()->HasIgnore(lowGuid))
        ignoreResult = FRIEND_IGNORE_ALREADY;
    else
    {
        ignoreResult = FRIEND_IGNORE_ADDED;

        // ignore list full
        if (!GetPlayer()->GetSocial()->AddToSocialList(lowGuid, true))
            ignoreResult = FRIEND_IGNORE_FULL;
    }

    sSocialMgr->SendFriendStatus(GetPlayer(), ignoreResult, lowGuid, false);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent (SMSG_FRIEND_STATUS)");
}

void WorldSession::HandleLoadActionsSwitchSpec(PreparedQueryResult result)
{
    if (!GetPlayer())
        return;

    if (result)
        GetPlayer()->_LoadActions(result);

    GetPlayer()->SendActionButtons(1);
}

void WorldSession::HandleDelIgnoreOpcode(WorldPacket & recv_data)
{
    uint64 IgnoreGUID;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_DEL_IGNORE");

    recv_data >> IgnoreGUID;

    _player->GetSocial()->RemoveFromSocialList(GUID_LOPART(IgnoreGUID), true);

    sSocialMgr->SendFriendStatus(GetPlayer(), FRIEND_IGNORE_REMOVED, GUID_LOPART(IgnoreGUID), false);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent motd (SMSG_FRIEND_STATUS)");
}

void WorldSession::HandleSetContactNotesOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_SET_CONTACT_NOTES");
    uint64 guid;
    std::string note;
    recv_data >> guid >> note;
    _player->GetSocial()->SetFriendNote(GUID_LOPART(guid), note);
}

void WorldSession::HandleBugOpcode(WorldPacket & recv_data)
{
    uint32 suggestion, contentlen, typelen;
    std::string content, type;

    recv_data >> suggestion >> contentlen >> content;

    recv_data >> typelen >> type;

    if (suggestion == 0)
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_BUG [Bug Report]");
    else
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_BUG [Suggestion]");

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "%s", type.c_str());
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "%s", content.c_str());

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_BUG_REPORT);

    stmt->setString(0, type);
    stmt->setString(1, content);

    CharacterDatabase.Execute(stmt);
}

void WorldSession::HandleReclaimCorpseOpcode(WorldPacket &recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RECLAIM_CORPSE");

    uint64 guid;
    recv_data >> guid;

    if (_player->IsAlive())
        return;

    // body not released yet
    if (!_player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return;

    Corpse* corpse = _player->GetCorpse();

    if (!corpse)
        return;

    // prevent resurrect before 30-sec delay after body release not finished
    if (time_t(corpse->GetGhostTime() + _player->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) > time_t(time(NULL)))
        return;

    if (!corpse->IsWithinDistInMap(_player, CORPSE_RECLAIM_RADIUS, true))
        return;

    // resurrect
    _player->ResurrectPlayer(0.5f);

    // spawn bones
    _player->SpawnCorpseBones();
}

void WorldSession::HandleResurrectResponseOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_RESURRECT_RESPONSE");

    uint64 guid;
    uint8 status;
    recv_data >> guid;
    recv_data >> status;

	// Xinef: Prevent resurrect with prevent resurrection aura
	if (GetPlayer()->IsAlive() || GetPlayer()->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
        return;

    if (status == 0)
    {
        GetPlayer()->clearResurrectRequestData();           // reject
        return;
    }

    if (!GetPlayer()->isResurrectRequestedBy(guid))
        return;

    GetPlayer()->ResurectUsingRequestData();
}

void WorldSession::SendAreaTriggerMessage(const char* Text, ...)
{
    va_list ap;
    char szStr [1024];
    szStr[0] = '\0';

    va_start(ap, Text);
    vsnprintf(szStr, 1024, Text, ap);
    va_end(ap);

    uint32 length = strlen(szStr)+1;
    WorldPacket data(SMSG_AREA_TRIGGER_MESSAGE, 4+length);
    data << length;
    data << szStr;
    SendPacket(&data);
}

void WorldSession::HandleAreaTriggerOpcode(WorldPacket& recv_data)
{
    uint32 triggerId;
    recv_data >> triggerId;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_AREATRIGGER. Trigger ID: %u", triggerId);

    Player* player = GetPlayer();
    if (player->IsInFlight())
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "HandleAreaTriggerOpcode: Player '%s' (GUID: %u) in flight, ignore Area Trigger ID:%u",
        //    player->GetName().c_str(), player->GetGUIDLow(), triggerId);
        return;
    }

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if (!atEntry)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "HandleAreaTriggerOpcode: Player '%s' (GUID: %u) send unknown (by DBC) Area Trigger ID:%u",
        //    player->GetName().c_str(), player->GetGUIDLow(), triggerId);
        return;
    }

    if (!player->IsInAreaTriggerRadius(atEntry))
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "HandleAreaTriggerOpcode: Player '%s' (GUID: %u) too far (trigger map: %u player map: %u), ignore Area Trigger ID: %u",
        //    player->GetName().c_str(), atEntry->mapid, player->GetMapId(), player->GetGUIDLow(), triggerId);
        return;
    }

    if (player->isDebugAreaTriggers)
        ChatHandler(this).PSendSysMessage(LANG_DEBUG_AREATRIGGER_REACHED, triggerId);

    if (sScriptMgr->OnAreaTrigger(player, atEntry))
        return;

    if (player->IsAlive())
        if (uint32 questId = sObjectMgr->GetQuestForAreaTrigger(triggerId))
            if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                player->AreaExploredOrEventHappens(questId);

    if (sObjectMgr->IsTavernAreaTrigger(triggerId))
    {
        // set resting flag we are in the inn
        player->SetRestState(atEntry->id);

        if (sWorld->IsFFAPvPRealm())
            player->RemoveByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP);

        return;
    }

    AreaTrigger const* at = sObjectMgr->GetAreaTrigger(triggerId);
    if (!at)
        return;

    bool teleported = false;
    if (player->GetMapId() != at->target_mapId)
    {
        if (!sMapMgr->CanPlayerEnter(at->target_mapId, player, false))
            return;
    }

    if (!teleported)
        player->TeleportTo(at->target_mapId, at->target_X, at->target_Y, at->target_Z, at->target_Orientation, TELE_TO_NOT_LEAVE_TRANSPORT);
}

void WorldSession::HandleUpdateAccountData(WorldPacket &recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_UPDATE_ACCOUNT_DATA");

    uint32 type, timestamp, decompressedSize;
    recv_data >> type >> timestamp >> decompressedSize;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "UAD: type %u, time %u, decompressedSize %u", type, timestamp, decompressedSize);

    if (type > NUM_ACCOUNT_DATA_TYPES)
        return;

    if (decompressedSize == 0)                               // erase
    {
        SetAccountData(AccountDataType(type), 0, "");

        WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4+4);
        data << uint32(type);
        data << uint32(0);
        SendPacket(&data);

        return;
    }

    if (decompressedSize > 0xFFFF)
    {
        recv_data.rfinish();                   // unnneded warning spam in this case
        sLog->outError("UAD: Account data packet too big, size %u", decompressedSize);
        return;
    }

    ByteBuffer dest;
    dest.resize(decompressedSize);

    uLongf realSize = decompressedSize;
    if (uncompress(dest.contents(), &realSize, recv_data.contents() + recv_data.rpos(), recv_data.size() - recv_data.rpos()) != Z_OK)
    {
        recv_data.rfinish();                   // unnneded warning spam in this case
        sLog->outError("UAD: Failed to decompress account data");
        return;
    }

    recv_data.rfinish();                       // uncompress read (recv_data.size() - recv_data.rpos())

    std::string adata;
    dest >> adata;

    SetAccountData(AccountDataType(type), timestamp, adata);

    WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4+4);
    data << uint32(type);
    data << uint32(0);
    SendPacket(&data);
}

void WorldSession::HandleRequestAccountData(WorldPacket& recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_REQUEST_ACCOUNT_DATA");

    uint32 type;
    recv_data >> type;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "RAD: type %u", type);

    if (type >= NUM_ACCOUNT_DATA_TYPES)
        return;

    AccountData* adata = GetAccountData(AccountDataType(type));

    uint32 size = adata->Data.size();

    uLongf destSize = compressBound(size);

    ByteBuffer dest;
    dest.resize(destSize);

    if (size && compress(dest.contents(), &destSize, (uint8 const*)adata->Data.c_str(), size) != Z_OK)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "RAD: Failed to compress account data");
        return;
    }

    dest.resize(destSize);

    WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA, 8+4+4+4+destSize);
    data << uint64(_player ? _player->GetGUID() : 0);       // player guid
    data << uint32(type);                                   // type (0-7)
    data << uint32(adata->Time);                            // unix time
    data << uint32(size);                                   // decompressed length
    data.append(dest);                                      // compressed data
    SendPacket(&data);
}

void WorldSession::HandleSetActionButtonOpcode(WorldPacket& recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_SET_ACTION_BUTTON");
    uint8 button;
    uint32 packetData;
    recv_data >> button >> packetData;

    uint32 action = ACTION_BUTTON_ACTION(packetData);
    uint8  type   = ACTION_BUTTON_TYPE(packetData);

    ;//sLog->outDetail("BUTTON: %u ACTION: %u TYPE: %u", button, action, type);
    if (!packetData)
    {
        ;//sLog->outDetail("MISC: Remove action from button %u", button);
        GetPlayer()->removeActionButton(button);
    }
    else
    {
        switch (type)
        {
            case ACTION_BUTTON_MACRO:
            case ACTION_BUTTON_CMACRO:
                ;//sLog->outDetail("MISC: Added Macro %u into button %u", action, button);
                break;
            case ACTION_BUTTON_EQSET:
                ;//sLog->outDetail("MISC: Added EquipmentSet %u into button %u", action, button);
                break;
            case ACTION_BUTTON_SPELL:
                ;//sLog->outDetail("MISC: Added Spell %u into button %u", action, button);
                break;
            case ACTION_BUTTON_ITEM:
                ;//sLog->outDetail("MISC: Added Item %u into button %u", action, button);
                break;
            default:
                sLog->outError("MISC: Unknown action button type %u for action %u into button %u for player %s (GUID: %u)", type, action, button, _player->GetName().c_str(), _player->GetGUIDLow());
                return;
        }
        GetPlayer()->addActionButton(button, action, type);
    }
}

void WorldSession::HandleCompleteCinematic(WorldPacket & /*recv_data*/)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_COMPLETE_CINEMATIC");
}

void WorldSession::HandleNextCinematicCamera(WorldPacket & /*recv_data*/)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_NEXT_CINEMATIC_CAMERA");
}

void WorldSession::HandleMoveTimeSkippedOpcode(WorldPacket & recv_data)
{
    uint64 guid;
    uint32 time_skipped;
    recv_data.readPackGUID(guid);
    recv_data >> time_skipped;
}

void WorldSession::HandleFeatherFallAck(WorldPacket &recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_MOVE_FEATHER_FALL_ACK");

    // no used
    recv_data.rfinish();                       // prevent warnings spam
}

void WorldSession::HandleMoveUnRootAck(WorldPacket& recv_data)
{
    // no used
    recv_data.rfinish();                       // prevent warnings spam
/*
    uint64 guid;
    recv_data >> guid;

    // now can skip not our packet
    if (_player->GetGUID() != guid)
    {
        recv_data.rfinish();                   // prevent warnings spam
        return;
    }

    ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: CMSG_FORCE_MOVE_UNROOT_ACK");

    recv_data.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recv_data, &movementInfo);
    recv_data.read_skip<float>();                           // unk2
*/
}

void WorldSession::HandleMoveRootAck(WorldPacket& recv_data)
{
    // no used
    recv_data.rfinish();                       // prevent warnings spam
/*
    uint64 guid;
    recv_data >> guid;

    // now can skip not our packet
    if (_player->GetGUID() != guid)
    {
        recv_data.rfinish();                   // prevent warnings spam
        return;
    }

    ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: CMSG_FORCE_MOVE_ROOT_ACK");

    recv_data.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    ReadMovementInfo(recv_data, &movementInfo);
*/
}

void WorldSession::HandleSetActionBarToggles(WorldPacket& recv_data)
{
    uint8 ActionBar;

    recv_data >> ActionBar;

    if (!GetPlayer())                                        // ignore until not logged (check needed because STATUS_AUTHED)
    {
        if (ActionBar != 0)
            sLog->outError("WorldSession::HandleSetActionBarToggles in not logged state with value: %u, ignored", uint32(ActionBar));
        return;
    }

    GetPlayer()->SetByteValue(PLAYER_FIELD_BYTES, 2, ActionBar);
}

void WorldSession::HandlePlayedTime(WorldPacket& recv_data)
{
    uint8 unk1;
    recv_data >> unk1;                                      // 0 or 1 expected

    WorldPacket data(SMSG_PLAYED_TIME, 4 + 4 + 1);
    data << uint32(_player->GetTotalPlayedTime());
    data << uint32(_player->GetLevelPlayedTime());
    data << uint8(unk1);                                    // 0 - will not show in chat frame
    SendPacket(&data);
}

void WorldSession::HandleInspectOpcode(WorldPacket& recv_data)
{
    uint64 guid;
    recv_data >> guid;

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_INSPECT");

    Player* player = ObjectAccessor::GetPlayer(*_player, guid);
    if (!player)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_INSPECT: No player found from GUID: " UI64FMTD, guid);
        return;
    }

    uint32 talent_points = 0x47;
    uint32 guid_size = player->GetPackGUID().wpos();
    WorldPacket data(SMSG_INSPECT_TALENT, guid_size+4+talent_points);
    data.append(player->GetPackGUID());

    if (_player->IsGameMaster() || sWorld->getIntConfig(CONFIG_TALENTS_INSPECTING) + (GetPlayer()->GetTeamId() == player->GetTeamId()) > 1)
    {
        player->BuildPlayerTalentsInfoData(&data);
    }
    else
    {
        data << uint32(0);                                  // unspentTalentPoints
        data << uint8(0);                                   // talentGroupCount
        data << uint8(0);                                   // talentGroupIndex
    }

    player->BuildEnchantmentsInfoData(&data);
    SendPacket(&data);
}

void WorldSession::HandleInspectHonorStatsOpcode(WorldPacket& recv_data)
{
    uint64 guid;
    recv_data >> guid;

    Player* player = ObjectAccessor::GetPlayer(*_player, guid);
    if (!player)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "MSG_INSPECT_HONOR_STATS: No player found from GUID: " UI64FMTD, guid);
        return;
    }

    WorldPacket data(MSG_INSPECT_HONOR_STATS, 8+1+4*4);
    data << uint64(player->GetGUID());
    data << uint8(player->GetHonorPoints());
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_KILLS));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS));
    SendPacket(&data);
}

void WorldSession::HandleWorldTeleportOpcode(WorldPacket& recv_data)
{
    uint32 time;
    uint32 mapid;
    float PositionX;
    float PositionY;
    float PositionZ;
    float Orientation;

    recv_data >> time;                                      // time in m.sec.
    recv_data >> mapid;
    recv_data >> PositionX;
    recv_data >> PositionY;
    recv_data >> PositionZ;
    recv_data >> Orientation;                               // o (3.141593 = 180 degrees)

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_WORLD_TELEPORT");

    if (GetPlayer()->IsInFlight())
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Player '%s' (GUID: %u) in flight, ignore worldport command.", GetPlayer()->GetName().c_str(), GetPlayer()->GetGUIDLow());
        return;
    }

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_WORLD_TELEPORT: Player = %s, Time = %u, map = %u, x = %f, y = %f, z = %f, o = %f", GetPlayer()->GetName().c_str(), time, mapid, PositionX, PositionY, PositionZ, Orientation);

    if (AccountMgr::IsAdminAccount(GetSecurity()))
        GetPlayer()->TeleportTo(mapid, PositionX, PositionY, PositionZ, Orientation);
    else
        SendNotification(LANG_YOU_NOT_HAVE_PERMISSION);
}

void WorldSession::HandleWhoisOpcode(WorldPacket& recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Received opcode CMSG_WHOIS");
    std::string charname;
    recv_data >> charname;

    if (!AccountMgr::IsAdminAccount(GetSecurity()))
    {
        SendNotification(LANG_YOU_NOT_HAVE_PERMISSION);
        return;
    }

    if (charname.empty() || !normalizePlayerName (charname))
    {
        SendNotification(LANG_NEED_CHARACTER_NAME);
        return;
    }

    Player* player = ObjectAccessor::FindPlayerByName(charname, false);

    if (!player)
    {
        SendNotification(LANG_PLAYER_NOT_EXIST_OR_OFFLINE, charname.c_str());
        return;
    }

    uint32 accid = player->GetSession()->GetAccountId();

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_WHOIS);

    stmt->setUInt32(0, accid);

    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
    {
        SendNotification(LANG_ACCOUNT_FOR_PLAYER_NOT_FOUND, charname.c_str());
        return;
    }

    Field* fields = result->Fetch();
    std::string acc = fields[0].GetString();
    if (acc.empty())
        acc = "Unknown";
    std::string email = fields[1].GetString();
    if (email.empty())
        email = "Unknown";
    std::string lastip = fields[2].GetString();
    if (lastip.empty())
        lastip = "Unknown";

    std::string msg = charname + "'s " + "account is " + acc + ", e-mail: " + email + ", last ip: " + lastip;

    WorldPacket data(SMSG_WHOIS, msg.size()+1);
    data << msg;
    SendPacket(&data);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Received whois command from player %s for character %s", GetPlayer()->GetName().c_str(), charname.c_str());
}

void WorldSession::HandleComplainOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_COMPLAIN");

    uint8 spam_type;                                        // 0 - mail, 1 - chat
    uint64 spammer_guid;
    uint32 unk1 = 0;
    uint32 unk2 = 0;
    uint32 unk3 = 0;
    uint32 unk4 = 0;
    std::string description = "";
    recv_data >> spam_type;                                 // unk 0x01 const, may be spam type (mail/chat)
    recv_data >> spammer_guid;                              // player guid
    switch (spam_type)
    {
        case 0:
            recv_data >> unk1;                              // const 0
            recv_data >> unk2;                              // probably mail id
            recv_data >> unk3;                              // const 0
            break;
        case 1:
            recv_data >> unk1;                              // probably language
            recv_data >> unk2;                              // message type?
            recv_data >> unk3;                              // probably channel id
            recv_data >> unk4;                              // unk random value
            recv_data >> description;                       // spam description string (messagetype, channel name, player name, message)
            break;
    }

    // NOTE: all chat messages from this spammer automatically ignored by spam reporter until logout in case chat spam.
    // if it's mail spam - ALL mails from this spammer automatically removed by client

    // Complaint Received message
    WorldPacket data(SMSG_COMPLAIN_RESULT, 1);
    data << uint8(0);
    SendPacket(&data);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "REPORT SPAM: type %u, guid %u, unk1 %u, unk2 %u, unk3 %u, unk4 %u, message %s", spam_type, GUID_LOPART(spammer_guid), unk1, unk2, unk3, unk4, description.c_str());
}

void WorldSession::HandleRealmSplitOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_REALM_SPLIT");

    uint32 unk;
    std::string split_date = "01/01/01";
    recv_data >> unk;

    WorldPacket data(SMSG_REALM_SPLIT, 4+4+split_date.size()+1);
    data << unk;
    data << uint32(0x00000000);                             // realm split state
    // split states:
    // 0x0 realm normal
    // 0x1 realm split
    // 0x2 realm split pending
    data << split_date;
    SendPacket(&data);
    //sLog->outDebug("response sent %u", unk);
}

void WorldSession::HandleFarSightOpcode(WorldPacket& recvData)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_FAR_SIGHT");

    bool apply;
    recvData >> apply;

    if (apply)
    {
        ;//sLog->outDebug("Added FarSight " UI64FMTD " to player %u", _player->GetUInt64Value(PLAYER_FARSIGHT), _player->GetGUIDLow());
        if (WorldObject* target = _player->GetViewpoint())
            _player->SetSeer(target);
        else
            ;//sLog->outError("Player %s requests non-existing seer " UI64FMTD, _player->GetName().c_str(), _player->GetUInt64Value(PLAYER_FARSIGHT));
    }
    else
    {
        ;//sLog->outDebug("Player %u set vision to self", _player->GetGUIDLow());
        _player->SetSeer(_player);
    }

    GetPlayer()->UpdateVisibilityForPlayer();
}

void WorldSession::HandleSetTitleOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_SET_TITLE");

    int32 title;
    recv_data >> title;

    // -1 at none
    if (title > 0 && title < MAX_TITLE_INDEX)
    {
       if (!GetPlayer()->HasTitle(title))
            return;
    }
    else
        title = 0;

    GetPlayer()->SetUInt32Value(PLAYER_CHOSEN_TITLE, title);
}

void WorldSession::HandleTimeSyncResp(WorldPacket & recv_data)
{
    uint32 counter, clientTicks;
    recv_data >> counter >> clientTicks;
    //uint32 ourTicks = clientTicks + (World::GetGameTimeMS() - _player->m_timeSyncServer);
    _player->m_timeSyncClient = clientTicks;
}

void WorldSession::HandleCancelMountAuraOpcode(WorldPacket & /*recv_data*/)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_CANCEL_MOUNT_AURA");

    //If player is not mounted, so go out :)
    if (!_player->IsMounted())                              // not blizz like; no any messages on blizz
    {
        ChatHandler(this).SendSysMessage(LANG_CHAR_NON_MOUNTED);
        return;
    }

    if (_player->IsInFlight())                               // not blizz like; no any messages on blizz
    {
        ChatHandler(this).SendSysMessage(LANG_YOU_IN_FLIGHT);
        return;
    }

    _player->Dismount();
    _player->RemoveAurasByType(SPELL_AURA_MOUNTED);
}

void WorldSession::HandleMoveSetCanFlyAckOpcode(WorldPacket & recv_data)
{
    // fly mode on/off
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_MOVE_SET_CAN_FLY_ACK");

    uint64 guid;                                            // guid - unused
    recv_data.readPackGUID(guid);

	// pussywizard: typical check for incomming movement packets
	if (!_player->m_mover || !_player->m_mover->IsInWorld() || _player->m_mover->IsDuringRemoveFromWorld() || guid != _player->m_mover->GetGUID())
	{
		recv_data.rfinish(); // prevent warnings spam
		return;
	}

    recv_data.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recv_data, &movementInfo);

    recv_data.read_skip<float>();                           // unk2

    _player->m_mover->m_movementInfo.flags = movementInfo.GetMovementFlags();
}

void WorldSession::HandleRequestPetInfoOpcode(WorldPacket & /*recv_data */)
{
    /*
        ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: CMSG_REQUEST_PET_INFO");
        recv_data.hexlike();
    */
}

void WorldSession::HandleSetTaxiBenchmarkOpcode(WorldPacket & recv_data)
{
    uint8 mode;
    recv_data >> mode;

    mode ? _player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_TAXI_BENCHMARK) : _player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_TAXI_BENCHMARK);

    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Client used \"/timetest %d\" command", mode);
}

void WorldSession::HandleQueryInspectAchievements(WorldPacket & recv_data)
{
    uint64 guid;
    recv_data.readPackGUID(guid);

    Player* player = ObjectAccessor::GetPlayer(*_player, guid);
    if (!player)
        return;

    player->SendRespondInspectAchievements(_player);
}

void WorldSession::HandleWorldStateUITimerUpdate(WorldPacket& /*recv_data*/)
{
    // empty opcode
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_WORLD_STATE_UI_TIMER_UPDATE");

    WorldPacket data(SMSG_WORLD_STATE_UI_TIMER_UPDATE, 4);
    data << uint32(time(NULL));
    SendPacket(&data);
}

void WorldSession::HandleReadyForAccountDataTimes(WorldPacket& /*recv_data*/)
{
    // empty opcode
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_READY_FOR_ACCOUNT_DATA_TIMES");

    SendAccountDataTimes(GLOBAL_CACHE_MASK);
}

void WorldSession::SendSetPhaseShift(uint32 PhaseShift)
{
    WorldPacket data(SMSG_SET_PHASE_SHIFT, 4);
    data << uint32(PhaseShift);
    SendPacket(&data);
}

//Battlefield and Battleground
void WorldSession::HandleAreaSpiritHealerQueryOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_AREA_SPIRIT_HEALER_QUERY");

    uint64 guid;
    recv_data >> guid;

    Creature* unit = GetPlayer()->GetMap()->GetCreature(guid);
    if (!unit)
        return;

    if (!unit->IsSpiritService())                            // it's not spirit service
        return;
}

void WorldSession::HandleAreaSpiritHealerQueueOpcode(WorldPacket & recv_data)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_AREA_SPIRIT_HEALER_QUEUE");

    uint64 guid;
    recv_data >> guid;

    Creature* unit = GetPlayer()->GetMap()->GetCreature(guid);
    if (!unit)
        return;

    if (!unit->IsSpiritService())                            // it's not spirit service
        return;
}

void WorldSession::HandleHearthAndResurrect(WorldPacket& /*recv_data*/)
{
    if (_player->IsInFlight())
        return;

    AreaTableEntry const* atEntry = sAreaTableStore.LookupEntry(_player->GetAreaId());
    if (!atEntry || !(atEntry->flags & AREA_FLAG_WINTERGRASP_2))
        return;

    _player->BuildPlayerRepop();
    _player->ResurrectPlayer(1.0f);
    _player->SpawnCorpseBones();
    _player->TeleportTo(_player->m_homebindMapId, _player->m_homebindX, _player->m_homebindY, _player->m_homebindZ, _player->GetOrientation());
}

void WorldSession::HandleUpdateMissileTrajectory(WorldPacket& recvPacket)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_UPDATE_MISSILE_TRAJECTORY");

    uint64 guid;
    uint32 spellId;
    float elevation, speed;
    float curX, curY, curZ;
    float targetX, targetY, targetZ;
    uint8 moveStop;

    recvPacket >> guid >> spellId >> elevation >> speed;
    recvPacket >> curX >> curY >> curZ;
    recvPacket >> targetX >> targetY >> targetZ;
    recvPacket >> moveStop;

    Unit* caster = ObjectAccessor::GetUnit(*_player, guid);
    Spell* spell = caster ? caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) : NULL;
    if (!spell || spell->m_spellInfo->Id != spellId || !spell->m_targets.HasDst() || !spell->m_targets.HasSrc())
    {
        recvPacket.rfinish();
        return;
    }

    Position pos = *spell->m_targets.GetSrcPos();
    pos.Relocate(curX, curY, curZ);
    spell->m_targets.ModSrc(pos);

    pos = *spell->m_targets.GetDstPos();
    pos.Relocate(targetX, targetY, targetZ);
    spell->m_targets.ModDst(pos);

    spell->m_targets.SetElevation(elevation);
    spell->m_targets.SetSpeed(speed);

    if (moveStop)
    {
        uint32 opcode;
        recvPacket >> opcode;
        recvPacket.SetOpcode(opcode);
        HandleMovementOpcodes(recvPacket);
    }
}
