/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#pragma once

#include "util.h"
#include "position.h"

class Level;
class Creature;
class Item;
class Tribe;
class CreatureGroup;

class EffectType;

class Effect {
  public:

  Effect(const EffectType&);
  Effect(const Effect&);
  Effect(Effect&&);
  Effect();
  Effect& operator = (const Effect&);
  Effect& operator = (Effect&&);
  ~Effect();

  bool operator == (const Effect&) const;
  bool operator != (const Effect&) const;
  template <class Archive>
  void serialize(Archive&, const unsigned int);

  void apply(Position, Creature* attacker = nullptr) const;
  string getName() const;
  string getDescription() const;
  bool isConsideredHostile() const;
  optional<MinionEquipmentType> getMinionEquipmentType() const;
  bool canAutoAssignMinionEquipment() const;
  optional<FXInfo> getProjectileFX() const;
  optional<ViewId> getProjectile() const;

  EffectAIIntent shouldAIApply(const Creature* caster, Position) const;

  static vector<Creature*> summon(Creature*, CreatureId, int num, optional<TimeInterval> ttl, TimeInterval delay = 0_visible);
  static vector<Creature*> summon(Position, CreatureGroup&, int num, optional<TimeInterval> ttl, TimeInterval delay = 0_visible);
  static vector<Creature*> summonCreatures(Position, int radius, vector<PCreature>, TimeInterval delay = 0_visible);
  static void emitPoisonGas(Position, double amount, bool msg);
  static vector<Effect> getWishedForEffects();

  HeapAllocatedSerializationWorkaround<EffectType> SERIAL(effect);

  private:
  EffectAIIntent shouldAIApply(const Creature* victim, bool isEnemy) const;
};
