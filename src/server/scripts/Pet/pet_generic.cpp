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

/*
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_gen_".
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Group.h"
#include "CreatureTextMgr.h"
#include "PetAI.h"
#include "PassiveAI.h"
#include "SpellScript.h"
#include "SpellAuras.h"

enum Mojo
{
    SAY_MOJO                = 0,

    SPELL_FEELING_FROGGY    = 43906,
    SPELL_SEDUCTION_VISUAL  = 43919
};

class npc_pet_gen_mojo : public CreatureScript
{
    public:
        npc_pet_gen_mojo() : CreatureScript("npc_pet_gen_mojo") { }

        struct npc_pet_gen_mojoAI : public ScriptedAI
        {
            npc_pet_gen_mojoAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset()
            {
                _victimGUID = 0;

                if (Unit* owner = me->GetOwner())
                    me->GetMotionMaster()->MoveFollow(owner, 0.0f, 0.0f);
            }

            void EnterCombat(Unit* /*who*/) { }
            void UpdateAI(uint32 /*diff*/) { }

            void ReceiveEmote(Player* player, uint32 emote)
            {
                me->HandleEmoteCommand(emote);
                Unit* owner = me->GetOwner();
                if (emote != TEXT_EMOTE_KISS || !owner || owner->GetTypeId() != TYPEID_PLAYER ||
                    owner->ToPlayer()->GetTeamId() != player->GetTeamId())
                {
                    return;
                }

                Talk(SAY_MOJO, player);

                if (_victimGUID)
                    if (Player* victim = ObjectAccessor::GetPlayer(*me, _victimGUID))
                        victim->RemoveAura(SPELL_FEELING_FROGGY);

                _victimGUID = player->GetGUID();

                DoCast(player, SPELL_FEELING_FROGGY, true);
                DoCast(me, SPELL_SEDUCTION_VISUAL, true);
                me->GetMotionMaster()->MoveFollow(player, 0.0f, 0.0f);
            }

        private:
            uint64 _victimGUID;
        };

        CreatureAI* GetAI(Creature* creature) const
        {
            return new npc_pet_gen_mojoAI(creature);
        }
};

enum soulTrader
{
	SPELL_STEAL_ESSENCE_VISUAL			= 50101,
	SPELL_CREATE_TOKEN					= 50063,
	SPELL_PROC_TRIGGER_ON_KILL_AURA		= 50051,
	SPELL_OWNER_KILLED_INFORM			= 50050,
	SPELL_EMOTE_STATE_SWIM_RUN			= 47127,

	EVENT_INITIAL_TALK					= 1,
	EVENT_ADD_TOKEN						= 2
};

class npc_pet_gen_soul_trader_beacon : public CreatureScript
{
public:
    npc_pet_gen_soul_trader_beacon() : CreatureScript("npc_pet_gen_soul_trader_beacon") { }

	struct npc_pet_gen_soul_trader_beaconAI : public ScriptedAI
	{
		uint64 ownerGUID;
		EventMap events;
		npc_pet_gen_soul_trader_beaconAI(Creature *c) : ScriptedAI(c) 
		{
			events.Reset();
			events.ScheduleEvent(EVENT_INITIAL_TALK, 0);
			if (me->ToTempSummon())
				if (Unit* owner = me->ToTempSummon()->GetOwner())
				{
					owner->CastSpell(owner, SPELL_PROC_TRIGGER_ON_KILL_AURA, true);
					ownerGUID = owner->GetGUID();
				}
		}

		Player* GetOwner() const { return ObjectAccessor::GetPlayer(*me, ownerGUID); }

		void SpellHitTarget(Unit* target, const SpellInfo* spellInfo)
		{
			if (spellInfo->Id == SPELL_STEAL_ESSENCE_VISUAL && target == me)
			{
				Talk(1);
				events.ScheduleEvent(EVENT_ADD_TOKEN, 3000);
				me->CastSpell(me, SPELL_EMOTE_STATE_SWIM_RUN, true);
			}
		}

		void UpdateAI(uint32 diff)
		{
			events.Update(diff);
			switch (events.ExecuteEvent())
			{
				case EVENT_INITIAL_TALK:
					Talk(0);
					break;
				case EVENT_ADD_TOKEN:
					me->RemoveAurasDueToSpell(SPELL_EMOTE_STATE_SWIM_RUN);
					me->CastSpell(me, SPELL_CREATE_TOKEN, true);
					Talk(2);
					break;
			}
		}
	};

	CreatureAI* GetAI(Creature* creature) const
	{
		return new npc_pet_gen_soul_trader_beaconAI (creature);
	}
};

enum eTargetFollowingBomb
{
	NPC_EXPLOSIVE_SHEEP				= 2675,
	SPELL_EXPLOSIVE_SHEEP			= 4050,

	NPC_GOBLIN_BOMB					= 8937,
	SPELL_EXPLOSIVE_GOBLIN			= 13259,

	NPC_HIGH_EXPLOSIVE_SHEEP		= 24715,
    SPELL_HIGH_EXPLOSIVE_SHEEP		= 44279,
};

class npc_pet_gen_target_following_bomb : public CreatureScript
{
public:
    npc_pet_gen_target_following_bomb() : CreatureScript("npc_pet_gen_target_following_bomb") { }

	struct npc_pet_gen_target_following_bombAI : public NullCreatureAI
	{
		npc_pet_gen_target_following_bombAI(Creature *c) : NullCreatureAI(c) 
		{
			checkTimer = 0;
			bombSpellId = 0;

			switch (me->GetEntry())
			{
				case NPC_EXPLOSIVE_SHEEP:		bombSpellId = SPELL_EXPLOSIVE_SHEEP;  break;
				case NPC_GOBLIN_BOMB:			bombSpellId = SPELL_EXPLOSIVE_GOBLIN; break;
				case NPC_HIGH_EXPLOSIVE_SHEEP:	bombSpellId = SPELL_HIGH_EXPLOSIVE_SHEEP; break;
			}
		}

		uint32 bombSpellId;
		uint32 checkTimer;

		void UpdateAI(uint32 diff)
		{
			checkTimer += diff;
			if (checkTimer >= 1000)
			{
				checkTimer = 0;
				if (Unit* target = me->SelectNearestTarget(30.0f))
				{
					me->GetMotionMaster()->MoveChase(target);
					if (me->GetDistance(target) < 3.0f)
					{
						me->CastSpell(me, bombSpellId, false);
						me->DespawnOrUnsummon(500);
					}
				}
				else if (!me->HasUnitState(UNIT_STATE_FOLLOW))
				{
					if (Unit* owner = me->GetCharmerOrOwner())
					{
						me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
					}
				}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_target_following_bombAI (pCreature);
	}
};

class npc_pet_gen_gnomish_flame_turret : public CreatureScript
{
public:
    npc_pet_gen_gnomish_flame_turret() : CreatureScript("npc_pet_gen_gnomish_flame_turret") { }

	struct npc_pet_gen_gnomish_flame_turretAI : public ScriptedAI
	{
		npc_pet_gen_gnomish_flame_turretAI(Creature *c) : ScriptedAI(c) 
		{
			checkTimer = 0;
		}

		uint32 checkTimer;

		void Reset()
		{
			me->GetMotionMaster()->Clear(false);
		}

		void AttackStart(Unit* who)
		{
			if (!who)
				return;

			if (me->Attack(who, false))
				DoStartNoMovement(who);
		}

		void UpdateAI(uint32 diff)
		{
			if (!me->GetVictim())
				return;

			if (Unit* target = me->SelectVictim())
			{
				AttackStart(target);
				DoSpellAttackIfReady(me->m_spells[0]);
			}
		}
	};

	CreatureAI* GetAI(Creature* creature) const
	{
		return new npc_pet_gen_gnomish_flame_turretAI (creature);
	}
};

class npc_pet_gen_valkyr_guardian : public CreatureScript
{
public:
    npc_pet_gen_valkyr_guardian() : CreatureScript("npc_pet_gen_valkyr_guardian") { }

	struct npc_pet_gen_valkyr_guardianAI : public ScriptedAI
	{
		npc_pet_gen_valkyr_guardianAI(Creature *c) : ScriptedAI(c)
		{
			me->SetReactState(REACT_DEFENSIVE);
			me->SetDisableGravity(true);
			me->AddUnitState(UNIT_STATE_NO_ENVIRONMENT_UPD);
			targetCheck = 0;
		}

		uint32 targetCheck;

		void InitializeAI()
		{
			if (Player* owner = me->GetCharmerOrOwnerPlayerOrPlayerItself())
				if (Unit* target = owner->GetSelectedUnit())
					if (!owner->IsFriendlyTo(target))
						AttackStart(target);
		}

		void OwnerAttacked(Unit* target)
		{
			if (!target || (me->GetVictim() && me->GetVictim()->IsAlive() && !me->GetVictim()->HasBreakableByDamageCrowdControlAura()))
				return;

			AttackStart(target);
		}

		void UpdateAI(uint32 diff)
		{
			if (!UpdateVictim())
			{
				targetCheck += diff;
				if (targetCheck > 1000)
				{
					targetCheck = 0;
					if (Unit* owner = me->GetCharmerOrOwner())
						if (Unit* ownerVictim = owner->GetVictim())
							if (!ownerVictim->HasBreakableByDamageCrowdControlAura())
								AttackStart(ownerVictim);
				}
				return;
			}

			if (me->isAttackReady() && !me->GetVictim()->HasBreakableByDamageCrowdControlAura())
				DoSpellAttackIfReady(me->GetCreatureTemplate()->spells[0]);
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_valkyr_guardianAI (pCreature);
	}
};

class spell_pet_gen_valkyr_guardian_smite : public SpellScriptLoader
{
    public:
        spell_pet_gen_valkyr_guardian_smite() : SpellScriptLoader("spell_pet_gen_valkyr_guardian_smite") { }

        class spell_pet_gen_valkyr_guardian_smite_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_pet_gen_valkyr_guardian_smite_SpellScript);

            void RecalculateDamage()
            {
				if (GetHitUnit() != GetCaster())
				{
					std::list<Spell::TargetInfo>* targetsInfo = GetSpell()->GetUniqueTargetInfo();
					for (std::list<Spell::TargetInfo>::iterator ihit = targetsInfo->begin(); ihit != targetsInfo->end(); ++ihit)
						if (ihit->targetGUID == GetCaster()->GetGUID())
							ihit->damage = -int32(GetHitDamage()*0.25f);
				}
            }

            void Register()
            {
                OnHit += SpellHitFn(spell_pet_gen_valkyr_guardian_smite_SpellScript::RecalculateDamage);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_pet_gen_valkyr_guardian_smite_SpellScript();
        }
};

class npc_pet_gen_imp_in_a_bottle : public CreatureScript
{
public:
    npc_pet_gen_imp_in_a_bottle() : CreatureScript("npc_pet_gen_imp_in_a_bottle") { }

	struct npc_pet_gen_imp_in_a_bottleAI : public NullCreatureAI
	{
		npc_pet_gen_imp_in_a_bottleAI(Creature *c) : NullCreatureAI(c)
		{
			_talkTimer = 0;
			_ownerGUID = 0;
			_hasParty = false;
		}

		WorldPacket _data;
		uint32 _talkTimer;
		uint64 _ownerGUID;
		bool _hasParty;

		void InitializeAI()
		{
			NullCreatureAI::InitializeAI();

			if (TempSummon* summon = me->ToTempSummon())
				if (Unit* owner = summon->GetSummoner())
					if (owner->GetTypeId() == TYPEID_PLAYER)
					{
						_ownerGUID = owner->GetGUID();
						if (owner->ToPlayer()->GetGroup())
						{
							_hasParty = true;
							std::string const& text = sCreatureTextMgr->GetChatString(me->GetEntry(), 0 /*text group*/, urand(0, 60) /*text id*/);

							_data.Initialize(SMSG_MESSAGECHAT, 200);                // guess size
							_data << uint8(CHAT_MSG_MONSTER_PARTY);
							_data << uint32(LANG_UNIVERSAL);
							_data << uint64(me->GetGUID());
							_data << uint32(0);
                            _data << uint32(me->GetName().size() + 1);
							_data << me->GetName();
							_data << uint64(0);
							_data << uint32(text.size()+1);
							_data << text.c_str();
							_data << uint8(0);
						}
					}
		}

		void UpdateAI(uint32 diff)
		{
			_talkTimer += diff;
			if (_talkTimer >= 5000)
			{
				_talkTimer = 0;
				me->DespawnOrUnsummon(1);
				if (!_hasParty)
					Talk(0, ObjectAccessor::GetPlayer(*me, _ownerGUID));
				else if (Player* player = ObjectAccessor::GetPlayer(*me, _ownerGUID))
				{
					uint8 limit = 0;
					if (player->GetGroup())
						for (GroupReference* itr = player->GetGroup()->GetFirstMember(); itr != NULL && limit < 4; itr = itr->next(), ++limit)
							if (Player* groupPlayer = itr->GetSource())
								if (groupPlayer != player)
									groupPlayer->GetSession()->SendPacket(&_data);

					player->GetSession()->SendPacket(&_data);
				}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_imp_in_a_bottleAI (pCreature);
	}
};

class npc_pet_gen_wind_rider_cub : public CreatureScript
{
public:
    npc_pet_gen_wind_rider_cub() : CreatureScript("npc_pet_gen_wind_rider_cub") { }

	struct npc_pet_gen_wind_rider_cubAI : public NullCreatureAI
	{
		npc_pet_gen_wind_rider_cubAI(Creature *c) : NullCreatureAI(c)
		{
			allowMove = true;
			isFlying = true;
			checkTimer = 0;
			checkTimer2 = 2000;
			me->AddUnitState(UNIT_STATE_NO_ENVIRONMENT_UPD);
		}

		bool isFlying;
		bool allowMove;
		uint32 checkTimer;
		uint32 checkTimer2;

		void MovementInform(uint32 type, uint32 id)
		{
			if (type == POINT_MOTION_TYPE && id == 1)
				allowMove = true;
		}

		void UpdateAI(uint32 diff)
		{
			checkTimer2 += diff;
			if (checkTimer2 > 2000)
			{
				checkTimer2 = 0;
				if (Unit* owner = me->GetOwner())
				{
					if (owner->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
					{
						isFlying = true;
						me->SetCanFly(true);
						me->SetDisableGravity(true);
					}
					else if (isFlying)
					{
						isFlying = false;
						me->SetCanFly(false);
						me->SetDisableGravity(false);
						me->GetMotionMaster()->MoveFall();
					}
				}
			}

			checkTimer += diff;
			if (allowMove || checkTimer > 2000)
			{
				allowMove = false;
				checkTimer = 0;
				if (Unit* owner = me->GetOwner())
				{
					if (me->GetMotionMaster()->GetMotionSlotType(MOTION_SLOT_ACTIVE) == POINT_MOTION_TYPE ||
						me->GetDistance(owner) < 1.0f)
						return;
					float x, y, z;
					owner->GetNearPoint2D(x, y, 0.5f, owner->GetOrientation()+PET_FOLLOW_ANGLE);
					z = owner->GetPositionZ() + (isFlying ? 2.5f : 0.0f);

					me->GetMotionMaster()->MovePoint(1, x, y, z);
				}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_wind_rider_cubAI (pCreature);
	}
};

enum turkey
{
	GO_BASIC_CAMPFIRE			= 29784,
	SPELL_TURKEY_STARTS_TO_BURN	= 61768,
};

class npc_pet_gen_plump_turkey : public CreatureScript
{
public:
    npc_pet_gen_plump_turkey() : CreatureScript("npc_pet_gen_plump_turkey") { }

	struct npc_pet_gen_plump_turkeyAI : public PassiveAI
	{
		npc_pet_gen_plump_turkeyAI(Creature *c) : PassiveAI(c)
		{
			goGUID = 0;
			jumpTimer = 0;
			checkTimer = 0;
			jumping = false;
		}

		uint64 goGUID;
		uint32 jumpTimer;
		uint32 checkTimer;
		bool jumping;

		void MovementInform(uint32 type, uint32 id)
		{
			if (type == EFFECT_MOTION_TYPE && id == 1)
			{
				Unit::Kill(me, me);
				me->AddAura(SPELL_TURKEY_STARTS_TO_BURN, me);
			}
		}

		void UpdateAI(uint32 diff)
		{
			if (jumping)
				return;

			if (jumpTimer)
			{
				jumpTimer += diff;
				if (jumpTimer >= 2000)
				{
					if (GameObject* go = me->GetMap()->GetGameObject(goGUID))
						me->GetMotionMaster()->MoveJump(*go, 5.0f, 10.0f, 1);
					jumping = true;
				}
				return;
			}

			checkTimer += diff;
			if (checkTimer > 3000)
			{
				checkTimer = 0;
				if (GameObject* go = me->FindNearestGameObject(GO_BASIC_CAMPFIRE, 7.0f))
				{
					goGUID = go->GetGUID();
					me->StopMoving();
					me->GetMotionMaster()->Clear(false);
					me->SetFacingTo(me->GetAngle(go));
					Talk(0);
					jumpTimer = 1;
				}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_plump_turkeyAI (pCreature);
	}
};

class npc_pet_gen_toxic_wasteling : public CreatureScript
{
public:
    npc_pet_gen_toxic_wasteling() : CreatureScript("npc_pet_gen_toxic_wasteling") { }

	struct npc_pet_gen_toxic_wastelingAI : public PassiveAI
	{
		npc_pet_gen_toxic_wastelingAI(Creature *c) : PassiveAI(c)
		{
		}

		uint32 checkTimer;

		void Reset() { checkTimer = 3000; }

		void EnterEvadeMode()
		{
		}

		void MovementInform(uint32 type, uint32 id)
		{
			if (type == EFFECT_MOTION_TYPE && id == 1)
				checkTimer = 1;
		}

		void UpdateAI(uint32 diff)
		{
			if (checkTimer)
			{
				if (checkTimer == 1)
					me->GetMotionMaster()->MovementExpired(false);
				checkTimer += diff;
				if (checkTimer >= 3000)
				{
					if (Unit* owner = me->GetCharmerOrOwner())
					{
						me->GetMotionMaster()->Clear(false);
						me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, me->GetFollowAngle(), MOTION_SLOT_ACTIVE);
					}
					me->AddAura(71854, me); // Growth
					checkTimer = 0;
				}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_toxic_wastelingAI (pCreature);
	}
};

class npc_pet_gen_fetch_ball : public CreatureScript
{
public:
    npc_pet_gen_fetch_ball() : CreatureScript("npc_pet_gen_fetch_ball") { }

	struct npc_pet_gen_fetch_ballAI : public NullCreatureAI
	{
		npc_pet_gen_fetch_ballAI(Creature *c) : NullCreatureAI(c)
		{
		}

		uint32 checkTimer;
		uint64 targetGUID;

		void IsSummonedBy(Unit* summoner)
		{
			if (!summoner)
				return;

			me->SetOwnerGUID(summoner->GetGUID());
			checkTimer = 0;
			targetGUID = 0;
			me->CastSpell(me, 48649 /*SPELL_PET_TOY_FETCH_BALL_COME_HERE*/, true);
		}

		void SpellHitTarget(Unit* target, const SpellInfo* spellInfo)
		{
			if (spellInfo->Id == 48649 /*SPELL_PET_TOY_FETCH_BALL_COME_HERE*/)
			{
				target->GetMotionMaster()->MovePoint(50, me->GetHomePosition());
				targetGUID = target->GetGUID();
			}
		}

		void UpdateAI(uint32 diff)
		{
			checkTimer += diff;
			if (checkTimer >= 1000)
			{
				checkTimer = 0;
				if (Creature* target = ObjectAccessor::GetCreature(*me, targetGUID))
					if (me->GetDistance2d(target) < 2.0f)
					{
						target->AI()->EnterEvadeMode();
						target->CastSpell(target, 48708 /*SPELL_PET_TOY_FETCH_BALL_HAS_BALL*/, true);
						me->DespawnOrUnsummon(1);
					}
			}
		}
	};

	CreatureAI* GetAI(Creature* pCreature) const
	{
		return new npc_pet_gen_fetch_ballAI (pCreature);
	}
};

void AddSC_generic_pet_scripts()
{
    new npc_pet_gen_mojo();
	new npc_pet_gen_soul_trader_beacon();
	new npc_pet_gen_target_following_bomb();
	new npc_pet_gen_gnomish_flame_turret();
	new npc_pet_gen_valkyr_guardian();
	new spell_pet_gen_valkyr_guardian_smite();
	new npc_pet_gen_imp_in_a_bottle();
	new npc_pet_gen_wind_rider_cub();
	new npc_pet_gen_plump_turkey();
	new npc_pet_gen_toxic_wasteling();
	new npc_pet_gen_fetch_ball();
}
