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
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"

#include "CellImpl.h"
#include "Chat.h"
#include "ChannelMgr.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "Language.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "Util.h"
#include "ScriptMgr.h"
#include "AccountMgr.h"

void WorldSession::HandleMessagechatOpcode(WorldPacket & recvData)
{
    uint32 type;
    uint32 lang;

    recvData >> type;
    recvData >> lang;

    if (type >= MAX_CHAT_MSG_TYPE)
    {
        sLog->outError("CHAT: Wrong message type received: %u", type);
        recvData.rfinish();
        return;
    }

    Player* sender = GetPlayer();

    //sLog->outDebug("CHAT: packet received. type %u, lang %u", type, lang);

	// pussywizard: chatting on most chat types requires 2 hours played to prevent spam/abuse
	if (AccountMgr::IsPlayerAccount(GetSecurity()))
		switch (type)
		{
			case CHAT_MSG_ADDON:
			case CHAT_MSG_PARTY:
			case CHAT_MSG_RAID:
			case CHAT_MSG_GUILD:
			case CHAT_MSG_OFFICER:
			case CHAT_MSG_AFK:
			case CHAT_MSG_DND:
			case CHAT_MSG_PARTY_LEADER:
				break;
			default:
				if (sender->GetTotalPlayedTime() < 2*HOUR)
				{
					SendNotification("Speaking is allowed after playing for at least 2 hours. You may use party and guild chat.");
					recvData.rfinish();
					return;
				}
		}

	// pussywizard:
	switch (type)
	{
		case CHAT_MSG_SAY:
		case CHAT_MSG_YELL:
		case CHAT_MSG_EMOTE:
		case CHAT_MSG_TEXT_EMOTE:
		case CHAT_MSG_AFK:
		case CHAT_MSG_DND:
            break;
	}

    // prevent talking at unknown language (cheating)
    LanguageDesc const* langDesc = GetLanguageDescByID(lang);
    if (!langDesc)
    {
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        recvData.rfinish();
        return;
    }
    if (langDesc->skill_id != 0 && !sender->HasSkill(langDesc->skill_id))
    {
        // also check SPELL_AURA_COMPREHEND_LANGUAGE (client offers option to speak in that language)
        Unit::AuraEffectList const& langAuras = sender->GetAuraEffectsByType(SPELL_AURA_COMPREHEND_LANGUAGE);
        bool foundAura = false;
        for (Unit::AuraEffectList::const_iterator i = langAuras.begin(); i != langAuras.end(); ++i)
        {
            if ((*i)->GetMiscValue() == int32(lang))
            {
                foundAura = true;
                break;
            }
        }
        if (!foundAura)
        {
            SendNotification(LANG_NOT_LEARNED_LANGUAGE);
            recvData.rfinish();
            return;
        }
    }

    if (lang == LANG_ADDON)
    {
        // LANG_ADDON is only valid for the following message types
        switch (type)
        {
            case CHAT_MSG_PARTY:
            case CHAT_MSG_RAID:
            case CHAT_MSG_GUILD:
            case CHAT_MSG_WHISPER:
                // check if addon messages are disabled
                if (!sWorld->getBoolConfig(CONFIG_ADDON_CHANNEL))
                {
                    recvData.rfinish();
                    return;
                }
                break;
            default:
                sLog->outError("Player %s (GUID: %u) sent a chatmessage with an invalid language/message type combination", 
                                                     GetPlayer()->GetName().c_str(), GetPlayer()->GetGUIDLow());

                recvData.rfinish();
                return;
        }
    }
    // LANG_ADDON should not be changed nor be affected by flood control
    else
    {
        uint32 specialMessageLimit = 0;
        // send in universal language if player in .gmon mode (ignore spell effects)
        if (sender->IsGameMaster())
            lang = LANG_UNIVERSAL;
        else
        {
            switch (type)
            {
                case CHAT_MSG_PARTY:
                case CHAT_MSG_PARTY_LEADER:
                case CHAT_MSG_RAID:
                    specialMessageLimit = 35;
                    break;
                case CHAT_MSG_GUILD:
                case CHAT_MSG_OFFICER:
                    specialMessageLimit = 15;
                    break;
                case CHAT_MSG_WHISPER:
                    if (sender->getLevel() >= 80)
                        specialMessageLimit = 15;
                    break;
            }

            // but overwrite it by SPELL_AURA_MOD_LANGUAGE auras (only single case used)
            Unit::AuraEffectList const& ModLangAuras = sender->GetAuraEffectsByType(SPELL_AURA_MOD_LANGUAGE);
            if (!ModLangAuras.empty())
                lang = ModLangAuras.front()->GetMiscValue();
        }

        if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
            sender->UpdateSpeakTime(specialMessageLimit);
    }

    // pussywizard: optimization
    /*if (sender->HasAura(1852) && type != CHAT_MSG_WHISPER)
    {
        SendNotification(GetTrinityString(LANG_GM_SILENCE), sender->GetName().c_str());
        recvData.rfinish();
        return;
    }*/

    std::string to, channel, msg;
    bool ignoreChecks = false;
    switch (type)
    {
        case CHAT_MSG_SAY:
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_YELL:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        case CHAT_MSG_GUILD:
        case CHAT_MSG_OFFICER:
        case CHAT_MSG_RAID:
            recvData >> msg;
            break;
        case CHAT_MSG_WHISPER:
            recvData >> to;
            recvData >> msg;
            break;
        case CHAT_MSG_CHANNEL:
            recvData >> channel;
            recvData >> msg;
            break;
        case CHAT_MSG_AFK:
        case CHAT_MSG_DND:
            recvData >> msg;
            ignoreChecks = true;
            break;
    }

    // Strip invisible characters for non-addon messages
    if (lang != LANG_ADDON && sWorld->getBoolConfig(CONFIG_CHAT_FAKE_MESSAGE_PREVENTING))
        stripLineInvisibleChars(msg);

	// pussywizard:
	if (lang != LANG_ADDON && msg.find("|0") != std::string::npos)
		return;

    if (!ignoreChecks)
    {
        if (msg.empty())
            return;

        if (ChatHandler(this).ParseCommands(msg.c_str()))
            return;

		if (!_player->CanSpeak())
		{
			std::string timeStr = secsToTimeString(m_muteTime - time(NULL));
			SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
			return;
		}

        if (lang != LANG_ADDON)
        {
            if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY) && !ChatHandler(this).isValidChatMessage(msg.c_str()))
            {
                //sLog->outError("Player %s (GUID: %u) sent a chatmessage with an invalid link: %s", GetPlayer()->GetName().c_str(),
                //    GetPlayer()->GetGUIDLow(), msg.c_str());

                if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
                    KickPlayer();

                return;
            }
        }
    }

	// exploit
	size_t found1 = msg.find("|Hquest");
	if (found1 != std::string::npos)
	{
		size_t found2 = msg.find(":", found1+8);
		size_t found3 = msg.find("|", found1+8);
		if (found3 != std::string::npos)
		{
			if (found2 == std::string::npos)
				return;
			if (found2 > found3)
				return;
		}
	}


    switch (type)
    {
        case CHAT_MSG_SAY:
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_YELL:
        {
            // Prevent cheating
            if (!sender->IsAlive())
                return;

            if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ));
                return;
            }

            if (type == CHAT_MSG_SAY)
                sender->Say(msg, lang);
            else if (type == CHAT_MSG_EMOTE)
                sender->TextEmote(msg);
            else if (type == CHAT_MSG_YELL)
                sender->Yell(msg, lang);
        } break;
        case CHAT_MSG_WHISPER:
        {
            if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_WHISPER_REQ), sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ));
                return;
            }

            if (!normalizePlayerName(to))
            {
                SendPlayerNotFoundNotice(to);
                break;
            }

            Player* receiver = ObjectAccessor::FindPlayerByName(to, false);
            bool senderIsPlayer = AccountMgr::IsPlayerAccount(GetSecurity());
            bool receiverIsPlayer = AccountMgr::IsPlayerAccount(receiver ? receiver->GetSession()->GetSecurity() : SEC_PLAYER);
            if (!receiver || (senderIsPlayer && !receiverIsPlayer && !receiver->isAcceptWhispers() && !receiver->IsInWhisperWhiteList(sender->GetGUID())))
            {
                SendPlayerNotFoundNotice(to);
                return;
            }

            if (senderIsPlayer && receiverIsPlayer)
                if (GetPlayer()->GetTeamId() != receiver->GetTeamId())
                {
                    SendWrongFactionNotice();
                    return;
                }

            // pussywizard: optimization
            /*if (GetPlayer()->HasAura(1852) && !receiver->IsGameMaster())
            {
                SendNotification(GetTrinityString(LANG_GM_SILENCE), GetPlayer()->GetName().c_str());
                return;
            }*/

            // If player is a Gamemaster and doesn't accept whisper, we auto-whitelist every player that the Gamemaster is talking to
            if (!senderIsPlayer && !sender->isAcceptWhispers() && !sender->IsInWhisperWhiteList(receiver->GetGUID()))
                sender->AddWhisperWhiteList(receiver->GetGUID());

            GetPlayer()->Whisper(msg, lang, receiver->GetGUID());
        } break;
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        {
            // if player is in battleground, he cannot say to battleground members by /p
            Group* group = sender->GetGroup();
            if (!group)
                return;

            if (type == CHAT_MSG_PARTY_LEADER && !group->IsLeader(sender->GetGUID()))
                return;

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, ChatMsg(type), Language(lang), sender, NULL, msg);
            group->BroadcastPacket(&data, false, group->GetMemberGroup(GetPlayer()->GetGUID()));
        } break;
        case CHAT_MSG_GUILD:
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    guild->BroadcastToGuild(this, false, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
        } break;
        case CHAT_MSG_OFFICER:
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    guild->BroadcastToGuild(this, true, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
        } break;
        case CHAT_MSG_RAID:
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetGroup();
            if (!group)
                return;

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, Language(lang), sender, NULL, msg);
            group->BroadcastPacket(&data, false);
        } break;
        case CHAT_MSG_CHANNEL:
        {
            if (AccountMgr::IsPlayerAccount(GetSecurity()))
            {
                if (sender->getLevel() < sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ))
                {
                    SendNotification(GetTrinityString(LANG_CHANNEL_REQ), sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ));
                    return;
                }
            }

            if (ChannelMgr* cMgr = ChannelMgr::forTeam(sender->GetTeamId()))
            {
                if (Channel* chn = cMgr->GetChannel(channel, sender))
                {
                    chn->Say(sender->GetGUID(), msg.c_str(), lang);
                }
            }
        } break;
        case CHAT_MSG_AFK:
        {
            if (!sender->IsInCombat())
            {
                if (sender->isAFK())                       // Already AFK
                {
                    if (msg.empty())
                        sender->ToggleAFK();               // Remove AFK
                    else
                        sender->autoReplyMsg = msg;        // Update message
                }
                else                                        // New AFK mode
                {
                    sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_AFK_DEFAULT) : msg;

                    if (sender->isDND())
                        sender->ToggleDND();

                    sender->ToggleAFK();
                }
            }
            break;
        }
        case CHAT_MSG_DND:
        {
            if (sender->isDND())                           // Already DND
            {
                if (msg.empty())
                    sender->ToggleDND();                   // Remove DND
                else
                    sender->autoReplyMsg = msg;            // Update message
            }
            else                                            // New DND mode
            {
                sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_DND_DEFAULT) : msg;

                if (sender->isAFK())
                    sender->ToggleAFK();

                sender->ToggleDND();
            }

            break;
        }
        default:
            sLog->outError("CHAT: unknown message type %u, lang: %u", type, lang);
            break;
    }
}

void WorldSession::HandleEmoteOpcode(WorldPacket & recvData)
{
    if (!GetPlayer()->IsAlive() || GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        return;

    uint32 emote;
    recvData >> emote;
    sScriptMgr->OnPlayerEmote(GetPlayer(), emote);
    GetPlayer()->HandleEmoteCommand(emote);
}

namespace Trinity
{
    class EmoteChatBuilder
    {
        public:
            EmoteChatBuilder(Player const& player, uint32 text_emote, uint32 emote_num, Unit const* target)
                : i_player(player), i_text_emote(text_emote), i_emote_num(emote_num), i_target(target) {}

            void operator()(WorldPacket& data)
            {
                std::string const name(i_target ? i_target->GetName() : "");
                uint32 namlen = name.size();

                data.Initialize(SMSG_TEXT_EMOTE, 20 + namlen);
                data << i_player.GetGUID();
                data << uint32(i_text_emote);
                data << uint32(i_emote_num);
                data << uint32(namlen);
                if (namlen > 1)
                    data << name;
                else
                    data << uint8(0x00);
            }

        private:
            Player const& i_player;
            uint32        i_text_emote;
            uint32        i_emote_num;
            Unit const*   i_target;
    };
}                                                           // namespace Trinity

void WorldSession::HandleTextEmoteOpcode(WorldPacket & recvData)
{
    if (!GetPlayer()->IsAlive())
        return;

	GetPlayer()->UpdateSpeakTime();

    if (!GetPlayer()->CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - time(NULL));
        SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        return;
    }

    uint32 text_emote, emoteNum;
    uint64 guid;

    recvData >> text_emote;
    recvData >> emoteNum;
    recvData >> guid;

    sScriptMgr->OnPlayerTextEmote(GetPlayer(), text_emote, emoteNum, guid);

    EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(text_emote);
    if (!em)
        return;

    uint32 emote_anim = em->textid;

    switch (emote_anim)
    {
        case EMOTE_STATE_SLEEP:
        case EMOTE_STATE_SIT:
        case EMOTE_STATE_KNEEL:
        case EMOTE_ONESHOT_NONE:
            break;
        default:
            // Only allow text-emotes for "dead" entities (feign death included)
            if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
                break;
             GetPlayer()->HandleEmoteCommand(emote_anim);
             break;
    }

    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);

    CellCoord p = Trinity::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    Cell cell(p);
    cell.SetNoCreate();

    Trinity::EmoteChatBuilder emote_builder(*GetPlayer(), text_emote, emoteNum, unit);
    Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > emote_do(emote_builder);
    Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > > emote_worker(GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE), emote_do);
    TypeContainerVisitor<Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder> >, WorldTypeMapContainer> message(emote_worker);
    cell.Visit(p, message, *GetPlayer()->GetMap(), *GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE));

    //Send scripted event call
    if (unit && unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->AI())
        ((Creature*)unit)->AI()->ReceiveEmote(GetPlayer(), text_emote);
}

void WorldSession::HandleChatIgnoredOpcode(WorldPacket& recvData)
{
    uint64 iguid;
    uint8 unk;
    //sLog->outDebug(LOG_FILTER_PACKETIO, "WORLD: Received CMSG_CHAT_IGNORED");

    recvData >> iguid;
    recvData >> unk;                                       // probably related to spam reporting

    Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(iguid);
    if (!player)
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_IGNORED, LANG_UNIVERSAL, _player, _player, GetPlayer()->GetName());
    player->GetSession()->SendPacket(&data);
}

void WorldSession::HandleChannelDeclineInvite(WorldPacket &recvPacket)
{
    ;//sLog->outDebug(LOG_FILTER_NETWORKIO, "Opcode %u", recvPacket.GetOpcode());
}

void WorldSession::SendPlayerNotFoundNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_NOT_FOUND, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendPlayerAmbiguousNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_AMBIGUOUS, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendWrongFactionNotice()
{
    WorldPacket data(SMSG_CHAT_WRONG_FACTION, 0);
    SendPacket(&data);
}

void WorldSession::SendChatRestrictedNotice(ChatRestrictionType restriction)
{
    WorldPacket data(SMSG_CHAT_RESTRICTED, 1);
    data << uint8(restriction);
    SendPacket(&data);
}
