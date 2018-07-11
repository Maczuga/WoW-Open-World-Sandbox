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
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "GossipDef.h"
#include "UpdateMask.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Pet.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "SpellInfo.h"

void WorldSession::SendShowMailBox(uint64 guid)
{
    WorldPacket data(SMSG_SHOW_MAILBOX, 8);
    data << guid;
    SendPacket(&data);
}

void WorldSession::SendShowBank(uint64 guid)
{
    WorldPacket data(SMSG_SHOW_BANK, 8);
    data << guid;
    SendPacket(&data);
}

void WorldSession::SendTrainerList(uint64 guid)
{
    std::string str = GetTrinityString(LANG_NPC_TAINER_HELLO);
    SendTrainerList(guid, str);
}

void WorldSession::SendTrainerList(uint64 guid, const std::string& strTitle)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList");

    Creature* trainer = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TRAINER);
    if (!trainer)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(guid)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    CreatureTemplate const* ci = trainer->GetCreatureTemplate();

    if (!ci)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - (GUID: %u) NO CREATUREINFO!", GUID_LOPART(guid));
        return;
    }

    TrainerSpellData const* trainer_spells = trainer->GetTrainerSpells();
    if (!trainer_spells)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: SendTrainerList - Training spells not found for creature (GUID: %u Entry: %u)",
        //    GUID_LOPART(guid), unit->GetEntry());
        return;
    }

    WorldPacket data(SMSG_TRAINER_LIST, 8+4+4+trainer_spells->spellList.size()*38 + strTitle.size()+1);
    data << guid;
    data << uint32(trainer_spells->trainerType);

    size_t count_pos = data.wpos();
    data << uint32(trainer_spells->spellList.size());

    // reputation discount
    float fDiscountMod = _player->GetReputationPriceDiscount(trainer);
    bool can_learn_primary_prof = GetPlayer()->GetFreePrimaryProfessionPoints() > 0;

    uint32 count = 0;
    for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin(); itr != trainer_spells->spellList.end(); ++itr)
    {
        TrainerSpell const* tSpell = &itr->second;

        bool valid = true;
        bool primary_prof_first_rank = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (!tSpell->learnedSpell[i])
                continue;
            if (!_player->IsSpellFitByClassAndRace(tSpell->learnedSpell[i]))
            {
                valid = false;
                break;
            }
            SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(tSpell->learnedSpell[i]);
            if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank())
                primary_prof_first_rank = true;
        }
        if (!valid)
            continue;

        TrainerSpellState state = _player->GetTrainerSpellState(tSpell);

        data << uint32(tSpell->spell);                      // learned spell (or cast-spell in profession case)
        data << uint8(state == TRAINER_SPELL_GREEN_DISABLED ? TRAINER_SPELL_GREEN : state);
        data << uint32(floor(tSpell->spellCost * fDiscountMod));

        data << uint32(primary_prof_first_rank && can_learn_primary_prof ? 1 : 0);
                                                            // primary prof. learn confirmation dialog
        data << uint32(primary_prof_first_rank ? 1 : 0);    // must be equal prev. field to have learn button in enabled state
        data << uint8(tSpell->reqLevel);
        data << uint32(tSpell->reqSkill);
        data << uint32(tSpell->reqSkillValue);
        //prev + req or req + 0
        uint8 maxReq = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (!tSpell->learnedSpell[i])
                continue;
            if (uint32 prevSpellId = sSpellMgr->GetPrevSpellInChain(tSpell->learnedSpell[i]))
            {
                data << uint32(prevSpellId);
                ++maxReq;
            }
            if (maxReq == 3)
                break;
            SpellsRequiringSpellMapBounds spellsRequired = sSpellMgr->GetSpellsRequiredForSpellBounds(tSpell->learnedSpell[i]);
            for (SpellsRequiringSpellMap::const_iterator itr2 = spellsRequired.first; itr2 != spellsRequired.second && maxReq < 3; ++itr2)
            {
                data << uint32(itr2->second);
                ++maxReq;
            }
            if (maxReq == 3)
                break;
        }
        while (maxReq < 3)
        {
            data << uint32(0);
            ++maxReq;
        }

        ++count;
    }

    data << strTitle;

    data.put<uint32>(count_pos, count);
    SendPacket(&data);
}

void WorldSession::HandleGossipHelloOpcode(WorldPacket & recvData)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_GOSSIP_HELLO");

    uint64 guid;
    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    if (!unit)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleGossipHelloOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(guid)));
        return;
    }

	// xinef: check if we have ANY npc flags
	if (unit->GetUInt32Value(UNIT_NPC_FLAGS) == UNIT_NPC_FLAG_NONE)
		return;

	// xinef: do not allow to open gossip when npc is in combat
	if (unit->GetUInt32Value(UNIT_NPC_FLAGS) == UNIT_NPC_FLAG_GOSSIP && unit->IsInCombat()) // should work on all flags?
		return;

    // set faction visible if needed
    if (FactionTemplateEntry const* factionTemplateEntry = sFactionTemplateStore.LookupEntry(unit->getFaction()))
        _player->GetReputationMgr().SetVisible(factionTemplateEntry);

    GetPlayer()->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TALK);
    // remove fake death
    //if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
    //    GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

	// xinef: and if he has pure gossip or is banker and moves or is tabard designer?
    //if (unit->IsArmorer() || unit->IsCivilian() || unit->IsQuestGiver() || unit->IsServiceProvider() || unit->IsGuard())
    {
        //if (!unit->GetTransport()) // pussywizard: reverted with new spline (old: without this check, npc would stay in place and the transport would continue moving, so the npc falls off. NPCs on transports don't have waypoints, so stopmoving is not needed)
            unit->StopMoving();
    }
    
    if (!sScriptMgr->OnGossipHello(_player, unit))
    {
//        _player->TalkedToCreature(unit->GetEntry(), unit->GetGUID());
        _player->PrepareGossipMenu(unit, unit->GetCreatureTemplate()->GossipMenuId, true);
        _player->SendPreparedGossip(unit);
    }
    unit->AI()->sGossipHello(_player);
}

/*void WorldSession::HandleGossipSelectOptionOpcode(WorldPacket & recvData)
{
    ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: CMSG_GOSSIP_SELECT_OPTION");

    uint32 option;
    uint32 unk;
    uint64 guid;
    std::string code = "";

    recvData >> guid >> unk >> option;

    if (_player->PlayerTalkClass->GossipOptionCoded(option))
    {
        ;//sLog->outDebug(LOG_FILTER_PACKETIO, "reading string");
        recvData >> code;
        ;//sLog->outDebug(LOG_FILTER_PACKETIO, "string read: %s", code.c_str());
    }

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    if (!unit)
    {
        ;//sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: HandleGossipSelectOptionOpcode - Unit (GUID: %u) not found or you can't interact with him.", uint32(GUID_LOPART(guid)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (!code.empty())
    {
        if (!Script->GossipSelectWithCode(_player, unit, _player->PlayerTalkClass->GossipOptionSender (option), _player->PlayerTalkClass->GossipOptionAction(option), code.c_str()))
            unit->OnGossipSelect (_player, option);
    }
    else
    {
        if (!Script->OnGossipSelect (_player, unit, _player->PlayerTalkClass->GossipOptionSender (option), _player->PlayerTalkClass->GossipOptionAction (option)))
           unit->OnGossipSelect (_player, option);
    }
}*/

void WorldSession::HandleSpiritHealerActivateOpcode(WorldPacket & recvData)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_SPIRIT_HEALER_ACTIVATE");

    uint64 guid;

    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_SPIRITHEALER);
    if (!unit)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleSpiritHealerActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(guid)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendSpiritResurrect();
}

void WorldSession::SendSpiritResurrect()
{
    _player->ResurrectPlayer(0.5f, true);

    _player->DurabilityLossAll(0.25f, true);

    // get corpse nearest graveyard
    WorldSafeLocsEntry const* corpseGrave = NULL;
    Corpse* corpse = _player->GetCorpse();
    if (corpse)
        corpseGrave = sObjectMgr->GetClosestGraveyard(corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), corpse->GetMapId(), _player->GetTeamId());

    // now can spawn bones
    _player->SpawnCorpseBones();

    // teleport to nearest from corpse graveyard, if different from nearest to player ghost
    if (corpseGrave)
    {
        WorldSafeLocsEntry const* ghostGrave = sObjectMgr->GetClosestGraveyard(_player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), _player->GetMapId(), _player->GetTeamId());

        if (corpseGrave != ghostGrave)
            _player->TeleportTo(corpseGrave->map_id, corpseGrave->x, corpseGrave->y, corpseGrave->z, _player->GetOrientation());
        // or update at original position
        //else
        //    _player->UpdateObjectVisibility(); // xinef: not needed, called in ResurrectPlayer
    }
    // or update at original position
    //else
    //    _player->UpdateObjectVisibility(); // xinef: not needed, called in ResurrectPlayer
}

void WorldSession::HandleBinderActivateOpcode(WorldPacket & recvData)
{
    uint64 npcGUID;
    recvData >> npcGUID;

    if (!GetPlayer()->IsInWorld() || !GetPlayer()->IsAlive())
        return;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_INNKEEPER);
    if (!unit)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleBinderActivateOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(npcGUID)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendBindPoint(unit);
}

void WorldSession::SendBindPoint(Creature* npc)
{
    uint32 bindspell = 3286;

    // send spell for homebinding (3286)
    npc->CastSpell(_player, bindspell, true);

    WorldPacket data(SMSG_TRAINER_BUY_SUCCEEDED, (8+4));
    data << uint64(npc->GetGUID());
    data << uint32(bindspell);
    SendPacket(&data);

    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleRepairItemOpcode(WorldPacket & recvData)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: CMSG_REPAIR_ITEM");

    uint64 npcGUID, itemGUID;
    uint8 guildBank;                                        // new in 2.3.2, bool that means from guild bank money

    recvData >> npcGUID >> itemGUID >> guildBank;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_REPAIR);
    if (!unit)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: HandleRepairItemOpcode - Unit (GUID: %u) not found or you can not interact with him.", uint32(GUID_LOPART(npcGUID)));
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // reputation discount
    float discountMod = _player->GetReputationPriceDiscount(unit);

    if (itemGUID)
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "ITEM: Repair item, itemGUID = %u, npcGUID = %u", GUID_LOPART(itemGUID), GUID_LOPART(npcGUID));

        Item* item = _player->GetItemByGuid(itemGUID);
        if (item)
            _player->DurabilityRepair(item->GetPos(), true, discountMod, guildBank);
    }
    else
    {
        ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "ITEM: Repair all items, npcGUID = %u", GUID_LOPART(npcGUID));
        _player->DurabilityRepairAll(true, discountMod, guildBank);
    }
}

