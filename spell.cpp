#include "stdafx.h"
#include "spell.h"
#include "effect.h"
#include "creature.h"
#include "player_message.h"
#include "creature_name.h"
#include "creature_factory.h"
#include "sound.h"
#include "lasting_effect.h"
#include "effect.h"
#include "furniture_type.h"
#include "attr_type.h"
#include "attack_type.h"
#include "draw_line.h"
#include "game.h"
#include "game_event.h"
#include "view_id.h"
#include "move_info.h"
#include "fx_name.h"

SERIALIZE_DEF(Spell, NAMED(id), NAMED(upgrade), NAMED(symbol), NAMED(effect), NAMED(cooldown), OPTION(castMessageType), NAMED(sound), OPTION(range), NAMED(fx), OPTION(endOnly), OPTION(targetSelf))
SERIALIZATION_CONSTRUCTOR_IMPL(Spell)
STRUCT_IMPL(Spell)

const string& Spell::getSymbol() const {
  return symbol;
}

const Effect& Spell::getEffect() const {
  return *effect;
}

int Spell::getCooldown() const {
  return cooldown;
}

optional<SoundId> Spell::getSound() const {
  return sound;
}

bool Spell::canTargetSelf() const {
  return targetSelf || range == 0;
}

string Spell::getDescription() const {
  return effect->getDescription();
}

void Spell::addMessage(Creature* c) const {
  switch (castMessageType) {
    case CastMessageType::STANDARD:
      c->verb("cast", "casts", "a spell");
      break;
    case CastMessageType::AIR_BLAST:
      c->verb("create", "creates", "an air blast!");
      break;
    case CastMessageType::BREATHE_FIRE:
      c->verb("breathe", "breathes", "fire!");
      break;
    case CastMessageType::ABILITY:
      c->verb("use", "uses", "an ability");
      break;
  }
}

void Spell::apply(Creature* c, Position target) const {
  if (target == c->getPosition()) {
    if (canTargetSelf())
      effect->apply(target, c);
    return;
  }
  auto thisFx = effect->getProjectileFX();
  if (fx)
    thisFx = FXInfo{*fx};
  if (endOnly) {
    c->getGame()->addEvent(
        EventInfo::Projectile{std::move(thisFx), effect->getProjectile(), c->getPosition(), target, none});
    effect->apply(target, c);
    return;
  }
  vector<Position> trajectory;
  auto origin = c->getPosition().getCoord();
  for (auto& v : drawLine(origin, target.getCoord()))
    if (v != origin && v.dist8(origin) <= range) {
      trajectory.push_back(Position(v, target.getLevel()));
      if (trajectory.back().isDirEffectBlocked())
        break;
    }
  c->getGame()->addEvent(
      EventInfo::Projectile{std::move(thisFx), effect->getProjectile(), c->getPosition(), trajectory.back(), none});
  for (auto& pos : trajectory)
    effect->apply(pos, c);
}

int Spell::getRange() const {
  return range;
}

bool Spell::isEndOnly() const {
  return endOnly;
}

SpellId Spell::getId() const {
  return id;
}

const char* Spell::getName() const {
  return id.data();
}

optional<SpellId> Spell::getUpgrade() const {
  return upgrade;
}

bool Spell::checkTrajectory(const Creature* c, Position to) const {
  Position from = c->getPosition();
  for (auto& v : drawLine(from, to))
    if (v != from && (v.isDirEffectBlocked() || (effect->shouldAIApply(c, v) == EffectAIIntent::UNWANTED && !endOnly)))
      return false;
  return true;
}

MoveInfo Spell::getAIMove(const Creature* c) const {
  if (c->isReady(this))
    for (auto pos : c->getPosition().getRectangle(Rectangle::centered(range)))
      if ((pos == c->getPosition() && canTargetSelf()) || (c->canSee(pos) && pos != c->getPosition() && checkTrajectory(c, pos)))
        if (effect->shouldAIApply(c, pos) == EffectAIIntent::WANTED)
          return c->castSpell(this, pos);
  return NoMove;
}


#include "pretty_archive.h"
template void Spell::serialize(PrettyInputArchive&, unsigned);
