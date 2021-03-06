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
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "SocialMgr.h"
#include "SpellAuras.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "MapManager.h"
#include "UpdateFieldFlags.h"

Roll::Roll(uint64 _guid, LootItem const& li) : itemGUID(_guid), itemid(li.itemid),
itemRandomPropId(li.randomPropertyId), itemRandomSuffix(li.randomSuffix), itemCount(li.count),
totalPlayersRolling(0), totalNeed(0), totalGreed(0), totalPass(0), itemSlot(0),
rollVoteMask(ROLL_ALL_TYPE_NO_DISENCHANT)
{
}

Roll::~Roll()
{
}

void Roll::setLoot(Loot* pLoot)
{
    link(pLoot, this);
}

Loot* Roll::getLoot()
{
    return getTarget();
}

Group::Group() : m_leaderGuid(0), m_leaderName(""), m_groupType(GROUPTYPE_NORMAL),
m_lootMethod(FREE_FOR_ALL), m_lootThreshold(ITEM_QUALITY_UNCOMMON), m_looterGuid(0),
m_masterLooterGuid(0), m_subGroupsCounts(nullptr), m_guid(0), m_counter(0), m_maxEnchantingLevel(0)
{
    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
        m_targetIcons[i] = 0;
}

Group::~Group()
{
    Rolls::iterator itr;
    while (!RollId.empty())
    {
        itr = RollId.begin();
        Roll *r = *itr;
        RollId.erase(itr);
        delete(r);
    }

    // Sub group counters clean up
    delete[] m_subGroupsCounts;
}

bool Group::Create(Player* leader)
{
    uint64 leaderGuid = leader->GetGUID();
    uint32 lowguid = sGroupMgr->GenerateGroupId();

    m_guid = MAKE_NEW_GUID(lowguid, 0, HIGHGUID_GROUP);
    m_leaderGuid = leaderGuid;
    m_leaderName = leader->GetName();
    leader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    m_lootMethod = GROUP_LOOT;

    m_lootThreshold = ITEM_QUALITY_UNCOMMON;
    m_looterGuid = leaderGuid;
    m_masterLooterGuid = 0;

    // Store group in database
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GROUP);

    uint8 index = 0;

    stmt->setUInt32(index++, lowguid);
    stmt->setUInt32(index++, GUID_LOPART(m_leaderGuid));
    stmt->setUInt8(index++, uint8(m_lootMethod));
    stmt->setUInt32(index++, GUID_LOPART(m_looterGuid));
    stmt->setUInt8(index++, uint8(m_lootThreshold));
    stmt->setUInt32(index++, uint32(m_targetIcons[0]));
    stmt->setUInt32(index++, uint32(m_targetIcons[1]));
    stmt->setUInt32(index++, uint32(m_targetIcons[2]));
    stmt->setUInt32(index++, uint32(m_targetIcons[3]));
    stmt->setUInt32(index++, uint32(m_targetIcons[4]));
    stmt->setUInt32(index++, uint32(m_targetIcons[5]));
    stmt->setUInt32(index++, uint32(m_targetIcons[6]));
    stmt->setUInt32(index++, uint32(m_targetIcons[7]));
    stmt->setUInt8(index++, uint8(m_groupType));
    stmt->setUInt32(index++, uint8(0));
    stmt->setUInt32(index++, uint8(0));
    stmt->setUInt32(index++, GUID_LOPART(m_masterLooterGuid));

    CharacterDatabase.Execute(stmt);


    ASSERT(AddMember(leader)); // If the leader can't be added to a new group because it appears full, something is clearly wrong.

    return true;
}

bool Group::LoadGroupFromDB(Field* fields)
{
    m_guid = MAKE_NEW_GUID(fields[16].GetUInt32(), 0, HIGHGUID_GROUP);
    m_leaderGuid = MAKE_NEW_GUID(fields[0].GetUInt32(), 0, HIGHGUID_PLAYER);

    // group leader not exist
    if (!sObjectMgr->GetPlayerNameByGUID(fields[0].GetUInt32(), m_leaderName))
	{
		uint32 groupLowGuid = fields[16].GetUInt32();
		SQLTransaction trans = CharacterDatabase.BeginTransaction();
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
        stmt->setUInt32(0, groupLowGuid);
        trans->Append(stmt);
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
        stmt->setUInt32(0, groupLowGuid);
        trans->Append(stmt);
        CharacterDatabase.CommitTransaction(trans);
        return false;
	}

    m_lootMethod = LootMethod(fields[1].GetUInt8());
    m_looterGuid = MAKE_NEW_GUID(fields[2].GetUInt32(), 0, HIGHGUID_PLAYER);
    m_lootThreshold = ItemQualities(fields[3].GetUInt8());

    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
        m_targetIcons[i] = fields[4+i].GetUInt32();

    m_groupType  = GroupType(fields[12].GetUInt8());

    m_masterLooterGuid = MAKE_NEW_GUID(fields[15].GetUInt32(), 0, HIGHGUID_PLAYER);

	return true;
}

void Group::LoadMemberFromDB(uint32 guidLow, uint8 memberFlags, uint8 subgroup, uint8 roles)
{
    MemberSlot member;
    member.guid = MAKE_NEW_GUID(guidLow, 0, HIGHGUID_PLAYER);

    // skip non-existed member
    if (!sObjectMgr->GetPlayerNameByGUID(member.guid, member.name))
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
        stmt->setUInt32(0, guidLow);
		stmt->setUInt32(1, GetLowGUID());
        CharacterDatabase.Execute(stmt);
        return;
    }

    member.group = subgroup;
    member.flags = memberFlags;
    member.roles = roles;

    m_memberSlots.push_back(member);
    sWorld->UpdateGlobalPlayerGroup(guidLow, GetLowGUID());
}

bool Group::AddInvite(Player* player)
{
    if (!player || player->GetGroupInvite())
        return false;
    Group* group = player->GetGroup();
    if (group)
        return false;

    RemoveInvite(player);

    m_invitees.insert(player);

    player->SetGroupInvite(this);

    sScriptMgr->OnGroupInviteMember(this, player->GetGUID());

    return true;
}

bool Group::AddLeaderInvite(Player* player)
{
    if (!AddInvite(player))
        return false;

    m_leaderGuid = player->GetGUID();
    m_leaderName = player->GetName();
    return true;
}

void Group::RemoveInvite(Player* player)
{
    if (player)
    {
		if (!m_invitees.empty())
			m_invitees.erase(player);
        player->SetGroupInvite(NULL);
    }
}

void Group::RemoveAllInvites()
{
    for (InvitesList::iterator itr=m_invitees.begin(); itr != m_invitees.end(); ++itr)
        if (*itr)
            (*itr)->SetGroupInvite(NULL);

    m_invitees.clear();
}

Player* Group::GetInvited(uint64 guid) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetGUID() == guid)
            return (*itr);
    }
    return NULL;
}

Player* Group::GetInvited(const std::string& name) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetName() == name)
            return (*itr);
    }
    return NULL;
}

bool Group::AddMember(Player* player)
{
    if (m_subGroupsCounts[0] >= MAXGROUPSIZE)
        return false;

    MemberSlot member;
    member.guid      = player->GetGUID();
    member.name      = player->GetName();
    member.group     = 0;
    member.flags     = 0;
    member.roles     = 0;
    m_memberSlots.push_back(member);
    sWorld->UpdateGlobalPlayerGroup(player->GetGUIDLow(), GetLowGUID());

    //if (player)
    {
        player->SetGroupInvite(NULL);
        player->SetGroup(this, 0);
    }

    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
        m_targetIcons[i] = 0;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_GROUP_MEMBER);
    stmt->setUInt32(0, GetLowGUID());
    stmt->setUInt32(1, GUID_LOPART(member.guid));
    stmt->setUInt8(2, member.flags);
    stmt->setUInt8(3, member.group);
    stmt->setUInt8(4, member.roles);
    CharacterDatabase.Execute(stmt);

    SendUpdate();
    sScriptMgr->OnGroupAddMember(this, player->GetGUID());

    if (player)
    {
        player->SetGroupUpdateFlag(GROUP_UPDATE_FULL);
        UpdatePlayerOutOfRange(player);

        {
            // Broadcast new player group member fields to rest of the group
            player->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);

            UpdateData groupData;
            WorldPacket groupDataPacket;

            // Broadcast group members' fields to player
            for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
            {
                if (itr->GetSource() == player) // pussywizard: no check same map, adding members is single threaded
                    continue;

                if (Player* member = itr->GetSource())
                {
                    if (player->HaveAtClient(member))
                    {
                        member->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                        member->BuildValuesUpdateBlockForPlayer(&groupData, player);
                        member->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                    }

                    if (member->HaveAtClient(player))
                    {
                        UpdateData newData;
                        WorldPacket newDataPacket;
                        player->BuildValuesUpdateBlockForPlayer(&newData, member);
                        if (newData.HasData())
                        {
                            newData.BuildPacket(&newDataPacket);
                            member->SendDirectMessage(&newDataPacket);
                        }
                    }
                }
            }

            if (groupData.HasData())
            {
                groupData.BuildPacket(&groupDataPacket);
                player->SendDirectMessage(&groupDataPacket);
            }

            player->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
        }

        if (m_maxEnchantingLevel < player->GetSkillValue(SKILL_ENCHANTING))
            m_maxEnchantingLevel = player->GetSkillValue(SKILL_ENCHANTING);
    }

    return true;
}

bool Group::RemoveMember(uint64 guid, const RemoveMethod &method /*= GROUP_REMOVEMETHOD_DEFAULT*/, uint64 kicker /*= 0*/, const char* reason /*= NULL*/)
{
    BroadcastGroupUpdate();

    // remove member and change leader (if need) only if strong more 2 members _before_ member remove (BG/BF allow 1 member group)
    if (GetMembersCount() > 2u)
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(guid);
        if (player)
        {
            player->SetGroup(NULL);

            // quest related GO state dependent from raid membership
            player->UpdateForQuestWorldObjects();

            WorldPacket data;

            if (method == GROUP_REMOVEMETHOD_KICK || method == GROUP_REMOVEMETHOD_KICK_LFG)
            {
                data.Initialize(SMSG_GROUP_UNINVITE, 0);
                player->GetSession()->SendPacket(&data);
            }

            // Do we really need to send this opcode?
            data.Initialize(SMSG_GROUP_LIST, 1+1+1+1+8+4+4+8);
            data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
            data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
            player->GetSession()->SendPacket(&data);
        }

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
        stmt->setUInt32(0, GUID_LOPART(guid));
        stmt->setUInt32(1, GetLowGUID());
        CharacterDatabase.Execute(stmt);

        // Reevaluate group enchanter if the leaving player had enchanting skill or the player is offline
        if ((player && player->GetSkillValue(SKILL_ENCHANTING)) || !player)
            ResetMaxEnchantingLevel();

        // Remove player from loot rolls
        for (Rolls::iterator it = RollId.begin(); it != RollId.end();)
        {
            Roll* roll = *it;
            Roll::PlayerVote::iterator itr2 = roll->playerVote.find(guid);
            if (itr2 == roll->playerVote.end())
			{
				++it;
                continue;
			}

            if (itr2->second == GREED || itr2->second == DISENCHANT)
                --roll->totalGreed;
            else if (itr2->second == NEED)
                --roll->totalNeed;
            else if (itr2->second == PASS)
                --roll->totalPass;

            if (itr2->second != NOT_VALID)
                --roll->totalPlayersRolling;

            roll->playerVote.erase(itr2);

			// Xinef: itr can be erased inside
			// Xinef: player is removed from all vote lists so it will not pass above playerVote == playerVote.end statement during second iteration
            if (CountRollVote(guid, roll->itemGUID, MAX_ROLL_TYPE))
				it = RollId.begin();
			else
				++it;
        }

        // Update subgroups
        member_witerator slot = _getMemberWSlot(guid);
        if (slot != m_memberSlots.end())
        {
            m_memberSlots.erase(slot);
            sWorld->UpdateGlobalPlayerGroup(GUID_LOPART(guid), 0);
        }

        // Pick new leader if necessary
		bool validLeader = true;
        if (m_leaderGuid == guid)
        {
			validLeader = false;
            for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
            {
                if (ObjectAccessor::FindPlayerInOrOutOfWorld(itr->guid))
                {
                    ChangeLeader(itr->guid);
					validLeader = true;
                    break;
                }
            }
        }

		sScriptMgr->OnGroupRemoveMember(this, guid, method, kicker, reason);

        SendUpdate();

		if (!validLeader)
		{
			// pussywizard: temp do nothing, something causes crashes in MakeNewGroup
			//Disband();
			//return false;
		}

        if (m_memberMgr.getSize() < 2u)
		{
            Disband();
			return false;
		}

        return true;
    }
    // If group size before player removal <= 2 then disband it
    else
    {
		sScriptMgr->OnGroupRemoveMember(this, guid, method, kicker, reason);
        Disband();
        return false;
    }
}

void Group::ChangeLeader(uint64 newLeaderGuid)
{
    member_witerator slot = _getMemberWSlot(newLeaderGuid);

    if (slot == m_memberSlots.end())
        return;

    Player* newLeader = ObjectAccessor::FindPlayerInOrOutOfWorld(slot->guid);

    // Don't allow switching leader to offline players
    if (!newLeader)
        return;

    sScriptMgr->OnGroupChangeLeader(this, newLeaderGuid, m_leaderGuid);

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    // Update the group leader
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_LEADER);
    stmt->setUInt32(0, newLeader->GetGUIDLow());
    stmt->setUInt32(1, GetLowGUID());
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    if (Player* oldLeader = ObjectAccessor::FindPlayerInOrOutOfWorld(m_leaderGuid))
        oldLeader->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    newLeader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);
    m_leaderGuid = newLeader->GetGUID();
    m_leaderName = newLeader->GetName();
    ToggleGroupMemberFlag(slot, MEMBER_FLAG_ASSISTANT, false);

    WorldPacket data(SMSG_GROUP_SET_LEADER, m_leaderName.size()+1);
    data << slot->name;
    BroadcastPacket(&data, true);
}

void Group::Disband(bool hideDestroy /* = false */)
{
    sScriptMgr->OnGroupDisband(this);

    Player* player;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        sWorld->UpdateGlobalPlayerGroup(GUID_LOPART(citr->guid), 0);

        player = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);

        if (!player)
            continue;

        player->SetGroup(NULL);

        WorldPacket data;
        if (!hideDestroy)
        {
            data.Initialize(SMSG_GROUP_DESTROYED, 0);
            player->GetSession()->SendPacket(&data);
        }

        //we already removed player from group and in player->GetGroup() is his original group, send update
        if (Group* group = player->GetGroup())
        {
            group->SendUpdate();
        }
        else
        {
            data.Initialize(SMSG_GROUP_LIST, 1+1+1+1+8+4+4+8);
            data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
            data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
            player->GetSession()->SendPacket(&data);
        }
    }
    RollId.clear();
    m_memberSlots.clear();

    RemoveAllInvites();

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
    stmt->setUInt32(0, GetLowGUID());
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
    stmt->setUInt32(0, GetLowGUID());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    sGroupMgr->RemoveGroup(this);
    delete this;
}

/*********************************************************/
/***                   LOOT SYSTEM                     ***/
/*********************************************************/

void Group::SendLootStartRoll(uint32 CountDown, uint32 mapid, const Roll &r)
{
    WorldPacket data(SMSG_LOOT_START_ROLL, (8+4+4+4+4+4+4+1));
    data << uint64(r.itemGUID);                             // guid of rolled item
    data << uint32(mapid);                                  // 3.3.3 mapid
    data << uint32(r.itemSlot);                             // itemslot
    data << uint32(r.itemid);                               // the itemEntryId for the item that shall be rolled for
    data << uint32(r.itemRandomSuffix);                     // randomSuffix
    data << uint32(r.itemRandomPropId);                     // item random property ID
    data << uint32(r.itemCount);                            // items in stack
    data << uint32(CountDown);                              // the countdown time to choose "need" or "greed"
    data << uint8(r.rollVoteMask);                          // roll type mask

    for (Roll::PlayerVote::const_iterator itr=r.playerVote.begin(); itr != r.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second == NOT_EMITED_YET)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootStartRollToPlayer(uint32 countDown, uint32 mapId, Player* p, bool canNeed, Roll const& r)
{
    if (!p)
        return;

    WorldPacket data(SMSG_LOOT_START_ROLL, (8 + 4 + 4 + 4 + 4 + 4 + 4 + 1));
    data << uint64(r.itemGUID);                             // guid of rolled item
    data << uint32(mapId);                                  // 3.3.3 mapid
    data << uint32(r.itemSlot);                             // itemslot
    data << uint32(r.itemid);                               // the itemEntryId for the item that shall be rolled for
    data << uint32(r.itemRandomSuffix);                     // randomSuffix
    data << uint32(r.itemRandomPropId);                     // item random property ID
    data << uint32(r.itemCount);                            // items in stack
    data << uint32(countDown);                              // the countdown time to choose "need" or "greed"
    uint8 voteMask = r.rollVoteMask;
    if (!canNeed)
        voteMask &= ~ROLL_FLAG_TYPE_NEED;
    data << uint8(voteMask);                                // roll type mask

    p->GetSession()->SendPacket(&data);
}

void Group::SendLootRoll(uint64 sourceGuid, uint64 targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ROLL, (8+4+8+4+4+4+1+1+1));
    data << uint64(sourceGuid);                             // guid of the item rolled
    data << uint32(roll.itemSlot);                          // slot
    data << uint64(targetGuid);
    data << uint32(roll.itemid);                            // the itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomSuffix);                  // randomSuffix
    data << uint32(roll.itemRandomPropId);                  // Item random property ID
    data << uint8(rollNumber);                              // 0: "Need for: [item name]" > 127: "you passed on: [item name]"      Roll number
    data << uint8(rollType);                                // 0: "Need for: [item name]" 0: "You have selected need for [item name] 1: need roll 2: greed roll
    data << uint8(0);                                       // 1: "You automatically passed on: %s because you cannot loot that item." - Possibly used in need befor greed

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second != NOT_VALID)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootRollWon(uint64 sourceGuid, uint64 targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ROLL_WON, (8+4+4+4+4+8+1+1));
    data << uint64(sourceGuid);                             // guid of the item rolled
    data << uint32(roll.itemSlot);                          // slot
    data << uint32(roll.itemid);                            // the itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomSuffix);                  // randomSuffix
    data << uint32(roll.itemRandomPropId);                  // Item random property
    data << uint64(targetGuid);                             // guid of the player who won.
    data << uint8(rollNumber);                              // rollnumber realted to SMSG_LOOT_ROLL
    data << uint8(rollType);                                // rollType related to SMSG_LOOT_ROLL

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second != NOT_VALID)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootAllPassed(Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ALL_PASSED, (8+4+4+4+4));
    data << uint64(roll.itemGUID);                             // Guid of the item rolled
    data << uint32(roll.itemSlot);                             // Item loot slot
    data << uint32(roll.itemid);                               // The itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomPropId);                     // Item random property ID
    data << uint32(roll.itemRandomSuffix);                     // Item random suffix ID

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!player)
            continue;

        if (itr->second != NOT_VALID)
            player->GetSession()->SendPacket(&data);
    }
}

// notify group members which player is the allowed looter for the given creature
void Group::SendLooter(Creature* creature, Player* groupLooter)
{
    ASSERT(creature);

    WorldPacket data(SMSG_LOOT_LIST, (8+8));
    data << uint64(creature->GetGUID());

    if (GetLootMethod() == MASTER_LOOT && creature->loot.hasOverThresholdItem())
        data.appendPackGUID(GetMasterLooterGuid());
    else
        data << uint8(0);

    if (groupLooter)
        data.append(groupLooter->GetPackGUID());
    else
        data << uint8(0);

    BroadcastPacket(&data, false);
}

void Group::GroupLoot(Loot* loot, WorldObject* pLootedObject)
{
    std::vector<LootItem>::iterator i;
    ItemTemplate const* item;
    uint8 itemSlot = 0;

    for (i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            //sLog->outDebug("Group::GroupLoot: missing item prototype for item with id: %d", i->itemid);
            continue;
        }

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
            Roll* r = new Roll(newitemGUID, *i);

            //a vector is filled with only near party members
            for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member)
                    continue;
                if (member->IsAtGroupRewardDistance(pLootedObject))
                {
                    if (i->AllowedForPlayer(member))
                    {
                        r->totalPlayersRolling++;

                        if (member->GetPassOnGroupLoot())
                        {
                            r->playerVote[member->GetGUID()] = PASS;
                            r->totalPass++;
                            // can't broadcast the pass now. need to wait until all rolling players are known.
                        }
                        else
                            r->playerVote[member->GetGUID()] = NOT_EMITED_YET;
                    }
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                loot->items[itemSlot].is_blocked = true;

                // If there is any "auto pass", broadcast the pass now.
                if (r->totalPass)
                {
                    for (Roll::PlayerVote::const_iterator itr=r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                    {
                        Player* p = ObjectAccessor::FindPlayer(itr->first);
                        if (!p)
                            continue;

                        if (itr->second == PASS)
                            SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                    }
                }

                SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

                RollId.push_back(r);

                if (Creature* creature = pLootedObject->ToCreature())
                {
                    creature->m_groupLootTimer = 60000;
                    creature->lootingGroupLowGUID = GetLowGUID();
                }
                else if (GameObject* go = pLootedObject->ToGameObject())
                {
                    go->m_groupLootTimer = 60000;
                    go->lootingGroupLowGUID = GetLowGUID();
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            //sLog->outDebug("Group::GroupLoot: missing item prototype for item with id: %d", i->itemid);
            continue;
        }

        uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
        Roll* r = new Roll(newitemGUID, *i);

        //a vector is filled with only near party members
        for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            if (member->IsAtGroupRewardDistance(pLootedObject))
            {
                if (i->AllowedForPlayer(member))
                {
                    r->totalPlayersRolling++;
                    r->playerVote[member->GetGUID()] = NOT_EMITED_YET;
                }
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

            RollId.push_back(r);

            if (Creature* creature = pLootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = pLootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::NeedBeforeGreed(Loot* loot, WorldObject* lootedObject)
{
    ItemTemplate const* item;
    uint8 itemSlot = 0;
    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
            Roll* r = new Roll(newitemGUID, *i);

            for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
            {
                Player* playerToRoll = itr->GetSource();
                if (!playerToRoll)
                    continue;

                if (i->AllowedForPlayer(playerToRoll) && playerToRoll->IsAtGroupRewardDistance(lootedObject))
                {
                    r->totalPlayersRolling++;
                    if (playerToRoll->GetPassOnGroupLoot())
                    {
                        r->playerVote[playerToRoll->GetGUID()] = PASS;
                        r->totalPass++;
                        // can't broadcast the pass now. need to wait until all rolling players are known.
                    }
                    else
                        r->playerVote[playerToRoll->GetGUID()] = NOT_EMITED_YET;
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                if (item->Flags2 & ITEM_FLAGS_EXTRA_NEED_ROLL_DISABLED)
                    r->rollVoteMask &= ~ROLL_FLAG_TYPE_NEED;

                loot->items[itemSlot].is_blocked = true;

                //Broadcast Pass and Send Rollstart
                for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                {
                    Player* p = ObjectAccessor::FindPlayer(itr->first);
                    if (!p)
                        continue;

                    if (itr->second == PASS)
                        SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                    else
                        SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, true, *r);
                }

                RollId.push_back(r);

                if (Creature* creature = lootedObject->ToCreature())
                {
                    creature->m_groupLootTimer = 60000;
                    creature->lootingGroupLowGUID = GetLowGUID();
                }
                else if (GameObject* go = lootedObject->ToGameObject())
                {
                    go->m_groupLootTimer = 60000;
                    go->lootingGroupLowGUID = GetLowGUID();
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
        Roll* r = new Roll(newitemGUID, *i);

        for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* playerToRoll = itr->GetSource();
            if (!playerToRoll)
                continue;

            if (i->AllowedForPlayer(playerToRoll) && playerToRoll->IsAtGroupRewardDistance(lootedObject))
            {
                r->totalPlayersRolling++;
                r->playerVote[playerToRoll->GetGUID()] = NOT_EMITED_YET;
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            //Broadcast Pass and Send Rollstart
            for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
            {
                Player* p = ObjectAccessor::FindPlayer(itr->first);
                if (!p)
                    continue;

                if (itr->second == PASS)
                    SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                else
                    SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, true, *r);
            }

            RollId.push_back(r);

            if (Creature* creature = lootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = lootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::MasterLoot(Loot* loot, WorldObject* pLootedObject)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Group::MasterLoot (SMSG_LOOT_MASTER_LIST, 330)");

    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i)
    {
        if (i->freeforall)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i)
    {
        if (!i->follow_loot_rules)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    uint32 real_count = 0;

    WorldPacket data(SMSG_LOOT_MASTER_LIST, 330);
    data << (uint8)GetMembersCount();

    for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (!looter->IsInWorld())
            continue;

        if (looter->IsAtGroupRewardDistance(pLootedObject))
        {
            data << uint64(looter->GetGUID());
            ++real_count;
        }
    }

    data.put<uint8>(0, real_count);

    for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (looter->IsAtGroupRewardDistance(pLootedObject))
            looter->GetSession()->SendPacket(&data);
    }
}

bool Group::CountRollVote(uint64 playerGUID, uint64 Guid, uint8 Choice)
{
    Rolls::iterator rollI = GetRoll(Guid);
    if (rollI == RollId.end())
        return false;
    Roll* roll = *rollI;

    Roll::PlayerVote::iterator itr = roll->playerVote.find(playerGUID);
    // this condition means that player joins to the party after roll begins
	// Xinef: if choice == MAX_ROLL_TYPE, player was removed from the map in removefromgroup
	// Xinef: itr can be invalid as it is not used below
    if (Choice < MAX_ROLL_TYPE && itr == roll->playerVote.end())
        return false;

    if (roll->getLoot())
        if (roll->getLoot()->items.empty())
            return false;

    switch (Choice)
    {
        case ROLL_PASS:                                     // Player choose pass
            SendLootRoll(0, playerGUID, 128, ROLL_PASS, *roll);
            ++roll->totalPass;
            itr->second = PASS;
            break;
        case ROLL_NEED:                                     // player choose Need
            SendLootRoll(0, playerGUID, 0, 0, *roll);
            ++roll->totalNeed;
            itr->second = NEED;
            break;
        case ROLL_GREED:                                    // player choose Greed
            SendLootRoll(0, playerGUID, 128, ROLL_GREED, *roll);
            ++roll->totalGreed;
            itr->second = GREED;
            break;
        case ROLL_DISENCHANT:                               // player choose Disenchant
            SendLootRoll(0, playerGUID, 128, ROLL_DISENCHANT, *roll);
            ++roll->totalGreed;
            itr->second = DISENCHANT;
            break;
    }

    if (roll->totalPass + roll->totalNeed + roll->totalGreed >= roll->totalPlayersRolling)
	{
        CountTheRoll(rollI, NULL);
		return true;
	}
	return false;
}

//called when roll timer expires
void Group::EndRoll(Loot* pLoot, Map* allowedMap)
{
    for (Rolls::iterator itr = RollId.begin(); itr != RollId.end();)
    {
        if ((*itr)->getLoot() == pLoot) {
            CountTheRoll(itr, allowedMap);           //i don't have to edit player votes, who didn't vote ... he will pass
            itr = RollId.begin();
        }
        else
            ++itr;
    }
}

void Group::CountTheRoll(Rolls::iterator rollI, Map* allowedMap)
{
    Roll* roll = *rollI;
    if (!roll->isValid())                                   // is loot already deleted ?
    {
        RollId.erase(rollI);
        delete roll;
        return;
    }

    //end of the roll
    if (roll->totalNeed > 0)
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;
            uint64 maxguid = 0; // pussywizard: start with 0 >_>
            Player* player = NULL;

            for (Roll::PlayerVote::const_iterator itr=roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                if (itr->second != NEED)
                    continue;

				player = ObjectAccessor::FindPlayer(itr->first);
				if (!player || allowedMap != NULL && player->FindMap() != allowedMap)
				{
					--roll->totalNeed;
					continue;
				}

                uint8 randomN = urand(1, 100);
                SendLootRoll(0, itr->first, randomN, ROLL_NEED, *roll);
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                }
            }

			if (maxguid) // pussywizard: added condition
			{
				SendLootRollWon(0, maxguid, maxresul, ROLL_NEED, *roll);
				player = ObjectAccessor::FindPlayer(maxguid);

				if (player)
				{
					ItemPosCountVec dest;
					LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
					InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
					if (msg == EQUIP_ERR_OK)
					{
						item->is_looted = true;
						roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
						roll->getLoot()->unlootedCount--;
						AllowedLooterSet looters = item->GetAllowedLooters();
						player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, looters);
					}
					else
					{
						item->is_blocked = false;
						item->rollWinnerGUID = player->GetGUID();
						player->SendEquipError(msg, NULL, NULL, roll->itemid);
					}
				}
			}
			else
				roll->totalNeed = 0;
        }
    }
    if (roll->totalNeed == 0 && roll->totalGreed > 0) // pussywizard: if (roll->totalNeed == 0 && ...), not else if, because numbers can be modified above if player is on a different map
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;
            uint64 maxguid = 0; // pussywizard: start with 0
            Player* player = NULL;
            RollVote rollvote = NOT_VALID;

            Roll::PlayerVote::iterator itr;
            for (itr = roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                if (itr->second != GREED && itr->second != DISENCHANT)
                    continue;

				player = ObjectAccessor::FindPlayer(itr->first);
				if (!player || allowedMap != NULL && player->FindMap() != allowedMap)
				{
					--roll->totalGreed;
					continue;
				}

                uint8 randomN = urand(1, 100);
                SendLootRoll(0, itr->first, randomN, itr->second, *roll);
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                    rollvote = itr->second;
                }
            }

			if (maxguid) // pussywizard: added condition
			{
				SendLootRollWon(0, maxguid, maxresul, rollvote, *roll);
				player = ObjectAccessor::FindPlayer(maxguid);

				if (player)
				{
					LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);

					if (rollvote == GREED)
					{
						ItemPosCountVec dest;
						InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
						if (msg == EQUIP_ERR_OK)
						{
							item->is_looted = true;
							roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
							roll->getLoot()->unlootedCount--;
							AllowedLooterSet looters = item->GetAllowedLooters();
							player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, looters);
						}
						else
						{
							item->is_blocked = false;
							item->rollWinnerGUID = player->GetGUID();
							player->SendEquipError(msg, NULL, NULL, roll->itemid);
						}
					}
					else if (rollvote == DISENCHANT)
					{
						item->is_looted = true;
						roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
						roll->getLoot()->unlootedCount--;
						ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(roll->itemid);
						player->AutoStoreLoot(pProto->DisenchantID, LootTemplates_Disenchant, true);
					}
				}
			}
			else
				roll->totalGreed = 0;
        }
    }
    if (roll->totalNeed == 0 && roll->totalGreed == 0) // pussywizard: if, not else, because numbers can be modified above if player is on a different map
    {
        SendLootAllPassed(*roll);

        // remove is_blocked so that the item is lootable by all players
        LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
        if (item)
            item->is_blocked = false;
    }

    RollId.erase(rollI);
    delete roll;
}

void Group::SetTargetIcon(uint8 id, uint64 whoGuid, uint64 targetGuid)
{
    if (id >= TARGETICONCOUNT)
        return;

    // clean other icons
    if (targetGuid != 0)
        for (int i=0; i<TARGETICONCOUNT; ++i)
            if (m_targetIcons[i] == targetGuid)
                SetTargetIcon(i, 0, 0);

    m_targetIcons[id] = targetGuid;

    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1+8+1+8));
    data << uint8(0);                                       // set targets
    data << uint64(whoGuid);
    data << uint8(id);
    data << uint64(targetGuid);
    BroadcastPacket(&data, true);
}

void Group::SendTargetIconList(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1+TARGETICONCOUNT*9));
    data << uint8(1);                                       // list targets

    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
    {
        if (m_targetIcons[i] == 0)
            continue;

        data << uint8(i);
        data << uint64(m_targetIcons[i]);
    }

    session->SendPacket(&data);
}

void Group::SendUpdate()
{
    for (member_witerator witr = m_memberSlots.begin(); witr != m_memberSlots.end(); ++witr)
        SendUpdateToPlayer(witr->guid, &(*witr));
}

void Group::SendUpdateToPlayer(uint64 playerGUID, MemberSlot* slot)
{
    Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(playerGUID);

    if (!player || player->GetGroup() != this)
        return;

    // if MemberSlot wasn't provided
    if (!slot)
    {
        member_witerator witr = _getMemberWSlot(playerGUID);

        if (witr == m_memberSlots.end()) // if there is no MemberSlot for such a player
            return;

        slot = &(*witr);
    }

    WorldPacket data(SMSG_GROUP_LIST, (1+1+1+1+1+4+8+4+4+(GetMembersCount()-1)*(13+8+1+1+1+1)+8+1+8+1+1+1+1));
    data << uint8(m_groupType);                         // group type (flags in 3.3)
    data << uint8(slot->group);
    data << uint8(slot->flags);
    data << uint8(slot->roles);

    data << uint64(m_guid);
    data << uint32(m_counter++);                        // 3.3, value increases every time this packet gets sent
    data << uint32(GetMembersCount()-1);
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        if (slot->guid == citr->guid)
            continue;

        Player* member = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);

        uint8 onlineState = (member && !member->GetSession()->PlayerLogout()) ? MEMBER_STATUS_ONLINE : MEMBER_STATUS_OFFLINE;
        onlineState = onlineState | 0;

        data << citr->name;
        data << uint64(citr->guid);                     // guid
        data << uint8(onlineState);                     // online-state
        data << uint8(citr->group);                     // groupid
        data << uint8(citr->flags);                     // See enum GroupMemberFlags
        data << uint8(citr->roles);                     // Lfg Roles
    }

    data << uint64(m_leaderGuid);                       // leader guid

    if (GetMembersCount() - 1)
    {
        data << uint8(m_lootMethod);                    // loot method

        if (m_lootMethod == MASTER_LOOT)
            data << uint64(m_masterLooterGuid);         // master looter guid
        else
            data << uint64(0);                          // looter guid

        data << uint8(m_lootThreshold);                 // loot threshold
        data << uint8(0);             // Dungeon Difficulty
        data << uint8(0);                // Raid Difficulty
        data << uint8(0);    // 3.3 Dynamic Raid Difficulty - 0 normal/1 heroic
    }

    player->GetSession()->SendPacket(&data);
}

void Group::UpdatePlayerOutOfRange(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    WorldPacket data;
    player->GetSession()->BuildPartyMemberStatsChangedPacket(player, &data);

    Player* member;
    for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        member = itr->GetSource();
        if (member && (!member->IsInMap(player) || !member->IsWithinDist(player, member->GetSightRange(player), false)))
            member->GetSession()->SendPacket(&data);
    }
}

void Group::BroadcastPacket(WorldPacket* packet, bool ignorePlayersInBGRaid, int group, uint64 ignore)
{
    for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (!player || (ignore != 0 && player->GetGUID() == ignore) || (ignorePlayersInBGRaid && player->GetGroup() != this))
            continue;

        if (group == -1 || itr->getSubGroup() == group)
            player->GetSession()->SendPacket(packet);
    }
}

void Group::BroadcastReadyCheck(WorldPacket* packet)
{
    for (GroupReference* itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (player)
            if (IsLeader(player->GetGUID()) || IsAssistant(player->GetGUID()))
                player->GetSession()->SendPacket(packet);
    }
}

void Group::OfflineReadyCheck()
{
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);
        if (!player)
        {
            WorldPacket data(MSG_RAID_READY_CHECK_CONFIRM, 9);
            data << uint64(citr->guid);
            data << uint8(0);
            BroadcastReadyCheck(&data);
        }
    }
}

bool Group::SameSubGroup(Player const* member1, Player const* member2) const
{
    if (!member1 || !member2)
        return false;

    if (member1->GetGroup() != this || member2->GetGroup() != this)
        return false;
    else
        return member1->GetSubGroup() == member2->GetSubGroup();
}

// Retrieve the next Round-Roubin player for the group
//
// No update done if loot method is FFA.
//
// If the RR player is not yet set for the group, the first group member becomes the round-robin player.
// If the RR player is set, the next player in group becomes the round-robin player.
//
// If ifneed is true,
//      the current RR player is checked to be near the looted object.
//      if yes, no update done.
//      if not, he loses his turn.
void Group::UpdateLooterGuid(WorldObject* pLootedObject, bool ifneed)
{
    // round robin style looting applies for all low
    // quality items in each loot method except free for all
    if (GetLootMethod() == FREE_FOR_ALL)
        return;

    uint64 oldLooterGUID = GetLooterGuid();
    member_citerator guid_itr = _getMemberCSlot(oldLooterGUID);
    if (guid_itr != m_memberSlots.end())
    {
        if (ifneed)
        {
            // not update if only update if need and ok
            Player* looter = ObjectAccessor::FindPlayer(guid_itr->guid);
            if (looter && looter->IsAtGroupRewardDistance(pLootedObject))
                return;
        }
        ++guid_itr;
    }

    // search next after current
    Player* pNewLooter = NULL;
    for (member_citerator itr = guid_itr; itr != m_memberSlots.end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
            if (player->IsAtGroupRewardDistance(pLootedObject))
            {
                pNewLooter = player;
                break;
            }
    }

    if (!pNewLooter)
    {
        // search from start
        for (member_citerator itr = m_memberSlots.begin(); itr != guid_itr; ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
                if (player->IsAtGroupRewardDistance(pLootedObject))
                {
                    pNewLooter = player;
                    break;
                }
        }
    }

    if (pNewLooter)
    {
        if (oldLooterGUID != pNewLooter->GetGUID())
        {
            SetLooterGuid(pNewLooter->GetGUID());
            SendUpdate();
        }
    }
    else
    {
        SetLooterGuid(0);
        SendUpdate();
    }
}

//===================================================
//============== Roll ===============================
//===================================================

void Roll::targetObjectBuildLink()
{
    // called from link()
    getTarget()->addLootValidatorRef(this);
}

void Group::BroadcastGroupUpdate(void)
{
    // FG: HACK: force flags update on group leave - for values update hack
    // -- not very efficient but safe
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* pp = ObjectAccessor::FindPlayer(citr->guid);
        if (pp)
        {
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_BYTES_2);
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
            ;//sLog->outStaticDebug("-- Forced group value update for '%s'", pp->GetName().c_str());
        }
    }
}

void Group::ResetMaxEnchantingLevel()
{
    m_maxEnchantingLevel = 0;
    Player* pMember = NULL;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        pMember = ObjectAccessor::FindPlayer(citr->guid);
        if (pMember && m_maxEnchantingLevel < pMember->GetSkillValue(SKILL_ENCHANTING))
            m_maxEnchantingLevel = pMember->GetSkillValue(SKILL_ENCHANTING);
    }
}

void Group::SetLootMethod(LootMethod method)
{
    m_lootMethod = method;
}

void Group::SetLooterGuid(uint64 guid)
{
    m_looterGuid = guid;
}

void Group::SetMasterLooterGuid(uint64 guid)
{
    m_masterLooterGuid = guid;
}

void Group::SetLootThreshold(ItemQualities threshold)
{
    m_lootThreshold = threshold;
}

void Group::SetLfgRoles(uint64 guid, const uint8 roles)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    slot->roles = roles;
         SendUpdate();
}

bool Group::IsFull() const
{
    return m_memberSlots.size() >= MAXGROUPSIZE;
}

bool Group::IsCreated() const
{
    return GetMembersCount() > 0;
}

uint64 Group::GetLeaderGUID() const
{
    return m_leaderGuid;
}

uint64 Group::GetGUID() const
{
    return m_guid;
}

uint32 Group::GetLowGUID() const
{
    return GUID_LOPART(m_guid);
}

const char * Group::GetLeaderName() const
{
    return m_leaderName.c_str();
}

LootMethod Group::GetLootMethod() const
{
    return m_lootMethod;
}

uint64 Group::GetLooterGuid() const
{
    return m_looterGuid;
}

uint64 Group::GetMasterLooterGuid() const
{
    return m_masterLooterGuid;
}

ItemQualities Group::GetLootThreshold() const
{
    return m_lootThreshold;
}

bool Group::IsMember(uint64 guid) const
{
    return _getMemberCSlot(guid) != m_memberSlots.end();
}

bool Group::IsLeader(uint64 guid) const
{
    return (GetLeaderGUID() == guid);
}

uint64 Group::GetMemberGUID(const std::string& name)
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->name == name)
            return itr->guid;
    return 0;
}

bool Group::IsAssistant(uint64 guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
        return false;
    return mslot->flags & MEMBER_FLAG_ASSISTANT;
}

bool Group::SameSubGroup(uint64 guid1, uint64 guid2) const
{
    member_citerator mslot2 = _getMemberCSlot(guid2);
    if (mslot2 == m_memberSlots.end())
       return false;
    return SameSubGroup(guid1, &*mslot2);
}

bool Group::SameSubGroup(uint64 guid1, MemberSlot const* slot2) const
{
    member_citerator mslot1 = _getMemberCSlot(guid1);
    if (mslot1 == m_memberSlots.end() || !slot2)
        return false;
    return (mslot1->group == slot2->group);
}

bool Group::HasFreeSlotSubGroup(uint8 subgroup) const
{
    return (m_subGroupsCounts && m_subGroupsCounts[subgroup] < MAXGROUPSIZE);
}

uint8 Group::GetMemberGroup(uint64 guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
       return (MAX_RAID_SUBGROUPS+1);
    return mslot->group;
}

Difficulty Group::GetDifficulty(bool isRaid) const
{
    return Difficulty(0);
}

bool Group::isRollLootActive() const
{
    return !RollId.empty();
}

Group::Rolls::iterator Group::GetRoll(uint64 Guid)
{
    Rolls::iterator iter;
    for (iter=RollId.begin(); iter != RollId.end(); ++iter)
        if ((*iter)->itemGUID == Guid && (*iter)->isValid())
            return iter;
    return RollId.end();
}

void Group::LinkMember(GroupReference* pRef)
{
    m_memberMgr.insertFirst(pRef);
}

Group::member_citerator Group::_getMemberCSlot(uint64 Guid) const
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

Group::member_witerator Group::_getMemberWSlot(uint64 Guid)
{
    for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

void Group::ToggleGroupMemberFlag(member_witerator slot, uint8 flag, bool apply)
{
    if (apply)
        slot->flags |= flag;
    else
        slot->flags &= ~flag;
}

void Group::SubGroupCounterIncrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        ++m_subGroupsCounts[subgroup];
}

void Group::SubGroupCounterDecrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        --m_subGroupsCounts[subgroup];
}
