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

#ifndef SCRIPTEDCREATURE_H_
#define SCRIPTEDCREATURE_H_

#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureAIImpl.h"

#define CAST_AI(a, b)   (dynamic_cast<a*>(b))

class SummonList
{
public:
    typedef std::list<uint64> StorageType;
    typedef StorageType::iterator iterator;
    typedef StorageType::const_iterator const_iterator;
    typedef StorageType::size_type size_type;
    typedef StorageType::value_type value_type;

    explicit SummonList(Creature* creature)
        : me(creature)
    { }

    // And here we see a problem of original inheritance approach. People started
    // to exploit presence of std::list members, so I have to provide wrappers

    iterator begin()
    {
        return storage_.begin();
    }

    const_iterator begin() const
    {
        return storage_.begin();
    }

    iterator end()
    {
        return storage_.end();
    }

    const_iterator end() const
    {
        return storage_.end();
    }

    iterator erase(iterator i)
    {
        return storage_.erase(i);
    }

    bool empty() const
    {
        return storage_.empty();
    }

    size_type size() const
    {
        return storage_.size();
    }

	void clear()
	{
		storage_.clear();
	}

    void Summon(Creature const* summon) { storage_.push_back(summon->GetGUID()); }
    void Despawn(Creature const* summon) { storage_.remove(summon->GetGUID()); }
    void DespawnEntry(uint32 entry);
    void DespawnAll();

    template <typename T>
    void DespawnIf(T const &predicate)
    {
        storage_.remove_if(predicate);
    }

    void DoAction(int32 info, uint16 max = 0)
    {
        if (max)
            RemoveNotExisting(); // pussywizard: when max is set, non existing can be chosen and nothing will happen

		StorageType listCopy = storage_;
        for (StorageType::const_iterator i = listCopy.begin(); i != listCopy.end(); ++i)
        {
            if (Creature* summon = ObjectAccessor::GetCreature(*me, *i))
                if (summon->IsAIEnabled)
                    summon->AI()->DoAction(info);
        }
    }

    template <class Predicate>
    void DoAction(int32 info, Predicate& predicate, uint16 max = 0)
    {
        if (max)
            RemoveNotExisting(); // pussywizard: when max is set, non existing can be chosen and nothing will happen

        // We need to use a copy of SummonList here, otherwise original SummonList would be modified
        StorageType listCopy = storage_;
        Trinity::Containers::RandomResizeList<uint64, Predicate>(listCopy, predicate, max);
        for (StorageType::iterator i = listCopy.begin(); i != listCopy.end(); ++i)
        {
            Creature* summon = ObjectAccessor::GetCreature(*me, *i);
            if (summon)
            {
                if (summon->IsAIEnabled)
                    summon->AI()->DoAction(info);
            }
            else
                storage_.remove(*i);
        }
    }

    void RemoveNotExisting();
    bool HasEntry(uint32 entry) const;
    uint32 GetEntryCount(uint32 entry) const;
	void Respawn();
	Creature* GetCreatureWithEntry(uint32 entry) const;

private:
    Creature* me;
    StorageType storage_;
};

class EntryCheckPredicate
{
    public:
        EntryCheckPredicate(uint32 entry) : _entry(entry) {}
        bool operator()(uint64 guid) { return GUID_ENPART(guid) == _entry; }

    private:
        uint32 _entry;
};

class PlayerOrPetCheck
{
    public:
        bool operator() (WorldObject* unit) const
        {
            if (unit->GetTypeId() != TYPEID_PLAYER)
				if (!IS_PLAYER_GUID(unit->ToUnit()->GetOwnerGUID()))
                    return true;

            return false;
        }
};

struct ScriptedAI : public CreatureAI
{
    explicit ScriptedAI(Creature* creature);
    virtual ~ScriptedAI() {}

    // *************
    //CreatureAI Functions
    // *************

    void AttackStartNoMove(Unit* target);

    // Called at any Damage from any attacker (before damage apply)
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damagetype*/, SpellSchoolMask /*damageSchoolMask*/) {}

    //Called at World update tick
    virtual void UpdateAI(uint32 diff);

    //Called at creature death
    void JustDied(Unit* /*killer*/) {}

    //Called at creature killing another unit
    void KilledUnit(Unit* /*victim*/) {}

    // Called when the creature summon successfully other creature
    void JustSummoned(Creature* /*summon*/) {}

    // Called when a summoned creature is despawned
    void SummonedCreatureDespawn(Creature* /*summon*/) {}

    // Called when hit by a spell
    void SpellHit(Unit* /*caster*/, SpellInfo const* /*spell*/) {}

    // Called when spell hits a target
    void SpellHitTarget(Unit* /*target*/, SpellInfo const* /*spell*/) {}

    //Called at waypoint reached or PointMovement end
    void MovementInform(uint32 /*type*/, uint32 /*id*/) {}

    // Called when AI is temporarily replaced or put back when possess is applied or removed
    void OnPossess(bool /*apply*/) {}

    // *************
    // Variables
    // *************

    //Pointer to creature we are manipulating
    Creature* me;

    //For fleeing
    bool IsFleeing;

    // *************
    //Pure virtual functions
    // *************

    //Called at creature reset either by death or evade
    void Reset() {}

    //Called at creature aggro either by MoveInLOS or Attack Start
    void EnterCombat(Unit* /*victim*/) {}

    // Called before EnterCombat even before the creature is in combat.
    void AttackStart(Unit* /*target*/);

    // *************
    //AI Helper Functions
    // *************

    //Start movement toward victim
    void DoStartMovement(Unit* target, float distance = 0.0f, float angle = 0.0f);

    //Start no movement on victim
    void DoStartNoMovement(Unit* target);

    //Stop attack of current victim
    void DoStopAttack();

    //Cast spell by spell info
    void DoCastSpell(Unit* target, SpellInfo const* spellInfo, bool triggered = false);

    //Drops all threat to 0%. Does not remove players from the threat list
    void DoResetThreat();

    float DoGetThreat(Unit* unit);
    void DoModifyThreatPercent(Unit* unit, int32 pct);

    //Teleports a player without dropping threat (only teleports to same map)
    void DoTeleportPlayer(Unit* unit, float x, float y, float z, float o);

    //Returns friendly unit with the most amount of hp missing from max hp
    Unit* DoSelectLowestHpFriendly(float range, uint32 minHPDiff = 1);

    //Returns a list of friendly CC'd units within range
    std::list<Creature*> DoFindFriendlyCC(float range);

    //Returns a list of all friendly units missing a specific buff within range
    std::list<Creature*> DoFindFriendlyMissingBuff(float range, uint32 spellId);

    //Return a player with at least minimumRange from me
    Player* GetPlayerAtMinimumRange(float minRange);

    //Spawns a creature relative to me
    Creature* DoSpawnCreature(uint32 entry, float offsetX, float offsetY, float offsetZ, float angle, uint32 type, uint32 despawntime);

    bool HealthBelowPct(uint32 pct) const { return me->HealthBelowPct(pct); }
    bool HealthAbovePct(uint32 pct) const { return me->HealthAbovePct(pct); }

    //Returns spells that meet the specified criteria from the creatures spell list
    SpellInfo const* SelectSpell(Unit* target, uint32 school, uint32 mechanic, SelectTargetType targets, uint32 powerCostMin, uint32 powerCostMax, float rangeMin, float rangeMax, SelectEffect effect);

    void SetEquipmentSlots(bool loadDefault, int32 mainHand = EQUIP_NO_CHANGE, int32 offHand = EQUIP_NO_CHANGE, int32 ranged = EQUIP_NO_CHANGE);

    // Used to control if MoveChase() is to be used or not in AttackStart(). Some creatures does not chase victims
    // NOTE: If you use SetCombatMovement while the creature is in combat, it will do NOTHING - This only affects AttackStart
    //       You should make the necessary to make it happen so.
    //       Remember that if you modified _isCombatMovementAllowed (e.g: using SetCombatMovement) it will not be reset at Reset().
    //       It will keep the last value you set.
    void SetCombatMovement(bool allowMovement);
    bool IsCombatMovementAllowed() const { return _isCombatMovementAllowed; }

    bool EnterEvadeIfOutOfCombatArea();
	virtual bool CheckEvadeIfOutOfCombatArea() const { return false; }

    // return the dungeon or raid difficulty
    Difficulty GetDifficulty() const { return _difficulty; }

	Player* SelectTargetFromPlayerList(float maxdist, uint32 excludeAura = 0, bool mustBeInLOS = false) const;

    private:
        Difficulty _difficulty;
        uint32 _evadeCheckCooldown;
        bool _isCombatMovementAllowed;
};

// SD2 grid searchers.
Creature* GetClosestCreatureWithEntry(WorldObject* source, uint32 entry, float maxSearchRange, bool alive = true);
GameObject* GetClosestGameObjectWithEntry(WorldObject* source, uint32 entry, float maxSearchRange);
void GetCreatureListWithEntryInGrid(std::list<Creature*>& list, WorldObject* source, uint32 entry, float maxSearchRange);
void GetGameObjectListWithEntryInGrid(std::list<GameObject*>& list, WorldObject* source, uint32 entry, float maxSearchRange);

#endif // SCRIPTEDCREATURE_H_
