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

#include "stdafx.h"

#include "creature_factory.h"
#include "monster.h"
#include "level.h"
#include "entity_set.h"
#include "effect.h"
#include "item_factory.h"
#include "creature_attributes.h"
#include "view_object.h"
#include "view_id.h"
#include "creature.h"
#include "game.h"
#include "name_generator.h"
#include "player_message.h"
#include "equipment.h"
#include "minion_activity_map.h"
#include "spell_map.h"
#include "tribe.h"
#include "monster_ai.h"
#include "sound.h"
#include "player.h"
#include "map_memory.h"
#include "body.h"
#include "attack_type.h"
#include "attack_level.h"
#include "attack.h"
#include "spell_map.h"
#include "item_type.h"
#include "item.h"
#include "furniture.h"
#include "experience_type.h"
#include "creature_debt.h"
#include "effect.h"
#include "game_event.h"
#include "game_config.h"
#include "creature_inventory.h"
#include "effect_type.h"
#include "item_types.h"

SERIALIZE_DEF(CreatureFactory, nameGenerator, attributes, inventory, spellSchools, spells)
SERIALIZATION_CONSTRUCTOR_IMPL(CreatureFactory)

class BoulderController : public Monster {
  public:
  BoulderController(Creature* c, Vec2 dir) : Monster(c, MonsterAIFactory::idle()), direction(dir) {
    CHECK(direction.length4() == 1);
  }

  virtual void makeMove() override {
    Position nextPos = creature->getPosition().plus(direction);
    if (Creature* c = nextPos.getCreature()) {
      if (!c->getBody().isKilledByBoulder()) {
        if (nextPos.canEnterEmpty(creature)) {
          creature->swapPosition(direction);
          return;
        }
      } else {
        health -= c->getBody().getBoulderDamage();
        if (health <= 0) {
          nextPos.globalMessage(creature->getName().the() + " crashes on " + c->getName().the());
          nextPos.unseenMessage("You hear a crash");
          creature->dieNoReason();
          //c->takeDamage(Attack(creature, AttackLevel::MIDDLE, AttackType::HIT, 1000, AttrType::DAMAGE));
          return;
        } else {
          c->you(MsgType::KILLED_BY, creature->getName().the());
          c->dieWithAttacker(creature);
        }
      }
    }
    if (auto furniture = nextPos.getFurniture(FurnitureLayer::MIDDLE))
      if (furniture->canDestroy(creature->getMovementType(), DestroyAction::Type::BOULDER) &&
          *furniture->getStrength(DestroyAction::Type::BOULDER) <
          health * creature->getAttr(AttrType::DAMAGE)) {
        health -= *furniture->getStrength(DestroyAction::Type::BOULDER) /
            (double) creature->getAttr(AttrType::DAMAGE);
        creature->destroyImpl(direction, DestroyAction::Type::BOULDER);
      }
    if (auto action = creature->move(direction))
      action.perform(creature);
    else {
      nextPos.globalMessage(creature->getName().the() + " crashes on the " + nextPos.getName());
      nextPos.unseenMessage("You hear a crash");
      creature->dieNoReason();
      return;
    }
    health -= 0.2;
    if (health <= 0 && !creature->isDead())
      creature->dieNoReason();
  }

  virtual MessageGenerator& getMessageGenerator() const override {
    static MessageGenerator g(MessageGenerator::BOULDER);
    return g;
  }

  SERIALIZE_ALL(SUBCLASS(Monster), direction)
  SERIALIZATION_CONSTRUCTOR(BoulderController)

  private:
  Vec2 SERIAL(direction);
  double health = 1;
};

PCreature CreatureFactory::getRollingBoulder(TribeId tribe, Vec2 direction) {
  ViewObject viewObject(ViewId("boulder"), ViewLayer::CREATURE);
  viewObject.setModifier(ViewObjectModifier::NO_UP_MOVEMENT);
  auto ret = makeOwner<Creature>(viewObject, tribe, CATTR(
            c.viewId = ViewId("boulder");
            c.attr[AttrType::DAMAGE] = 250;
            c.attr[AttrType::DEFENSE] = 250;
            c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::HUGE);
            c.body->setDeathSound(none);
            c.permanentEffects[LastingEffect::BLIND] = 1;
            c.boulder = true;
            c.name = "boulder";
            ), SpellMap{});
  ret->setController(makeOwner<BoulderController>(ret.get(), direction));
  return ret;
}

class SokobanController : public Monster {
  public:
  SokobanController(Creature* c) : Monster(c, MonsterAIFactory::idle()) {}

  virtual MessageGenerator& getMessageGenerator() const override {
    static MessageGenerator g(MessageGenerator::BOULDER);
    return g;
  }

  SERIALIZE_ALL(SUBCLASS(Monster))
  SERIALIZATION_CONSTRUCTOR(SokobanController)

  private:
};

PCreature CreatureFactory::getSokobanBoulder(TribeId tribe) {
  ViewObject viewObject(ViewId("boulder"), ViewLayer::CREATURE);
  viewObject.setModifier(ViewObjectModifier::NO_UP_MOVEMENT).setModifier(ViewObjectModifier::REMEMBER);
  auto ret = makeOwner<Creature>(viewObject, tribe, CATTR(
            c.viewId = ViewId("boulder");
            c.attr[AttrType::DAMAGE] = 250;
            c.attr[AttrType::DEFENSE] = 250;
            c.body = Body::nonHumanoid(Body::Material::ROCK, Body::Size::HUGE);
            c.body->setDeathSound(none);
            c.body->setMinPushSize(Body::Size::LARGE);
            c.permanentEffects[LastingEffect::BLIND] = 1;
            c.boulder = true;
            c.name = "boulder";), SpellMap{});
  ret->setController(makeOwner<SokobanController>(ret.get()));
  return ret;
}

CreatureAttributes CreatureFactory::getKrakenAttributes(ViewId id, const char* name) {
  return CATTR(
      c.viewId = id;
      c.body = Body::nonHumanoid(Body::Size::LARGE);
      c.body->setDeathSound(none);
      c.attr[AttrType::DAMAGE] = 28;
      c.attr[AttrType::DEFENSE] = 28;
      c.permanentEffects[LastingEffect::POISON_RESISTANT] = 1;
      c.permanentEffects[LastingEffect::NIGHT_VISION] = 1;
      c.permanentEffects[LastingEffect::SWIMMING_SKILL] = 1;
      c.name = name;);
}

ViewId CreatureFactory::getViewId(CreatureId id) {
  if (!idMap.count(id)) {
    auto c = fromId(id, TribeId::getMonster());
    idMap[id] = c->getViewObject().id();
  }
  return idMap.at(id);
}

vector<CreatureId> CreatureFactory::getAllCreatures() const {
  return getKeys(attributes);
}

NameGenerator* CreatureFactory::getNameGenerator() {
  return &*nameGenerator;
}

const map<SpellSchoolId, SpellSchool> CreatureFactory::getSpellSchools() const {
  return spellSchools;
}

const vector<Spell>& CreatureFactory::getSpells() const {
  return spells;
}

CreatureFactory::CreatureFactory(NameGenerator n, map<CreatureId, CreatureAttributes> a, map<CreatureId, CreatureInventory> i,
    map<SpellSchoolId, SpellSchool> s, vector<Spell> sp)
  : nameGenerator(std::move(n)), attributes(a), inventory(i), spellSchools(s), spells(sp) {}

CreatureFactory::~CreatureFactory() {
}

void CreatureFactory::merge(CreatureFactory f) {
  mergeMap(std::move(f.attributes), attributes);
  mergeMap(std::move(f.inventory), inventory);
  mergeMap(std::move(f.spellSchools), spellSchools);
  append(spells, std::move(f.spells));
}

void CreatureFactory::setContentFactory(const ContentFactory* f) const {
  contentFactory = f;
}

CreatureFactory::CreatureFactory(CreatureFactory&&) = default;
CreatureFactory& CreatureFactory::operator =(CreatureFactory&&) = default;

constexpr int maxKrakenLength = 15;

class KrakenController : public Monster {
  public:
  KrakenController(Creature* c) : Monster(c, MonsterAIFactory::monster()) {
  }

  KrakenController(Creature* c, WeakPointer<KrakenController> father, int length)
      : Monster(c, MonsterAIFactory::monster()), length(length), father(father) {
  }

  virtual bool dontReplaceInCollective() override {
    return true;
  }

  int getMaxSpawns() {
    if (father)
      return 1;
    else
      return 7;
  }

  virtual void onKilled(const Creature* attacker) override {
    if (attacker) {
      if (father)
        attacker->secondPerson("You cut the kraken's tentacle");
      else
        attacker->secondPerson("You kill the kraken!");
    }
    for (Creature* c : spawns)
      if (!c->isDead())
        c->dieNoReason();
  }

  virtual MessageGenerator& getMessageGenerator() const override {
    static MessageGenerator kraken(MessageGenerator::KRAKEN);
    static MessageGenerator third(MessageGenerator::THIRD_PERSON);
    if (father)
      return kraken;
    else
      return third;
  }

  void pullEnemy(Creature* held) {
    if (Random.roll(3)) {
      held->you(MsgType::HAPPENS_TO, creature->getName().the() + " pulls");
      if (father) {
        held->setHeld(father->creature);
        Vec2 pullDir = held->getPosition().getDir(creature->getPosition());
        creature->dieNoReason(Creature::DropType::NOTHING);
        held->displace(pullDir);
      } else {
        held->you(MsgType::ARE, "eaten by " + creature->getName().the());
        held->dieNoReason();
      }
    }
  }

  Creature* getHeld() {
    for (auto pos : creature->getPosition().neighbors8())
      if (auto other = pos.getCreature())
        if (other->getHoldingCreature() == creature)
          return other;
    return nullptr;
  }

  Creature* getVisibleEnemy() {
    const int radius = 10;
    Creature* ret = nullptr;
    auto myPos = creature->getPosition();
    for (Position pos : creature->getPosition().getRectangle(Rectangle::centered(Vec2(0, 0), radius)))
      if (Creature* c = pos.getCreature())
        if (c->getAttributes().getCreatureId() != creature->getAttributes().getCreatureId() &&
            (!ret || *ret->getPosition().dist8(myPos) > *c->getPosition().dist8(myPos)) &&
            creature->canSee(c) && creature->isEnemy(c) && !c->getHoldingCreature())
          ret = c;
    return ret;
  }

  void considerAttacking(Creature* c) {
    auto pos = c->getPosition();
    Vec2 v = creature->getPosition().getDir(pos);
    if (v.length8() == 1) {
      c->you(MsgType::HAPPENS_TO, creature->getName().the() + " swings itself around");
      c->setHeld(creature);
    } else if (length < maxKrakenLength && Random.roll(2)) {
      pair<Vec2, Vec2> dirs = v.approxL1();
      vector<Vec2> moves;
      if (creature->getPosition().plus(dirs.first).canEnter(
            {{MovementTrait::WALK, MovementTrait::SWIM}}))
        moves.push_back(dirs.first);
      if (creature->getPosition().plus(dirs.second).canEnter(
            {{MovementTrait::WALK, MovementTrait::SWIM}}))
        moves.push_back(dirs.second);
      if (!moves.empty()) {
        Vec2 move = Random.choose(moves);
        ViewId viewId = creature->getPosition().plus(move).canEnter({MovementTrait::SWIM})
          ? ViewId("kraken_water") : ViewId("kraken_land");
        auto spawn = makeOwner<Creature>(creature->getTribeId(),
            CreatureFactory::getKrakenAttributes(viewId, "kraken tentacle"), SpellMap{});
        spawn->setController(makeOwner<KrakenController>(spawn.get(), getThis().dynamicCast<KrakenController>(),
            length + 1));
        spawns.push_back(spawn.get());
        creature->getPosition().plus(move).addCreature(std::move(spawn));
      }
    }
  }

  virtual void makeMove() override {
    for (Creature* c : spawns)
      if (c->isDead()) {
        spawns.removeElement(c);
        break;
      }
    if (spawns.empty()) {
      if (auto held = getHeld()) {
        pullEnemy(held);
      } else if (auto c = getVisibleEnemy()) {
        considerAttacking(c);
      } else if (father && Random.roll(5)) {
        creature->dieNoReason(Creature::DropType::NOTHING);
        return;
      }
    }
    creature->wait().perform(creature);
  }

  SERIALIZE_ALL(SUBCLASS(Monster), ready, spawns, father, length);
  SERIALIZATION_CONSTRUCTOR(KrakenController);

  private:
  int SERIAL(length) = 0;
  bool SERIAL(ready) = false;
  vector<Creature*> SERIAL(spawns);
  WeakPointer<KrakenController> SERIAL(father);
};

class ShopkeeperController : public Monster, public EventListener<ShopkeeperController> {
  public:
  ShopkeeperController(Creature* c, Rectangle area)
      : Monster(c, MonsterAIFactory::stayInLocation(area)), shopArea(area) {
  }

  vector<Position> getAllShopPositions() const {
    return shopArea.getAllSquares().transform([this](Vec2 v){ return Position(v, myLevel); });
  }

  bool isShopPosition(const Position& pos) {
    return pos.isSameLevel(myLevel) && pos.getCoord().inRectangle(shopArea);
  }

  virtual void makeMove() override {
    if (firstMove) {
      myLevel = creature->getLevel();
      subscribeTo(creature->getPosition().getModel());
      for (Position v : getAllShopPositions()) {
        for (Item* item : v.getItems())
          item->setShopkeeper(creature);
        v.clearItemIndex(ItemIndex::FOR_SALE);
      }
      firstMove = false;
    }
    if (!creature->getPosition().isSameLevel(myLevel)) {
      Monster::makeMove();
      return;
    }
    vector<Creature::Id> creatures;
    for (Position v : getAllShopPositions())
      if (Creature* c = v.getCreature()) {
        creatures.push_back(c->getUniqueId());
        if (!prevCreatures.contains(c) && !thieves.contains(c) && !creature->isEnemy(c)) {
          if (!debtors.contains(c))
            c->secondPerson("\"Welcome to " + creature->getName().firstOrBare() + "'s shop!\"");
          else {
            c->secondPerson("\"Pay your debt or... !\"");
            thiefCount.erase(c);
          }
        }
      }
    for (auto debtorId : copyOf(debtors))
      if (!creatures.contains(debtorId))
        for (auto pos : creature->getPosition().getRectangle(Rectangle::centered(Vec2(0, 0), 30)))
          if (auto debtor = pos.getCreature())
            if (debtor->getUniqueId() == debtorId) {
              debtor->privateMessage("\"Come back, you owe me " + toString(debtor->getDebt().getAmountOwed(creature)) +
                  " gold!\"");
              if (++thiefCount.getOrInit(debtor) == 4) {
                debtor->privateMessage("\"Thief! Thief!\"");
                creature->getTribe()->onItemsStolen(debtor);
                thiefCount.erase(debtor);
                debtors.erase(debtor);
                thieves.insert(debtor);
                for (Item* item : debtor->getEquipment().getItems())
                  item->setShopkeeper(nullptr);
                break;
              }
            }
    prevCreatures.clear();
    for (auto c : creatures)
      prevCreatures.insert(c);
    Monster::makeMove();
  }

  virtual void onItemsGiven(vector<Item*> items, Creature* from) override {
    int paid = items.filter(Item::classPredicate(ItemClass::GOLD)).size();
    from->getDebt().add(creature, -paid);
    if (from->getDebt().getAmountOwed(creature) <= 0)
      debtors.erase(from);
  }
  
  void onEvent(const GameEvent& event) {
    using namespace EventInfo;
    event.visit(
        [&](const ItemsAppeared& info) {
          if (isShopPosition(info.position)) {
            for (auto& it : info.items) {
              it->setShopkeeper(creature);
              info.position.clearItemIndex(ItemIndex::FOR_SALE);
            }
          }
        },
        [&](const ItemsPickedUp& info) {
          if (isShopPosition(info.creature->getPosition())) {
            for (auto& item : info.items)
              if (item->isShopkeeper(creature)) {
                info.creature->getDebt().add(creature, item->getPrice());
                debtors.insert(info.creature);
              }
          }
        },
        [&](const ItemsDropped& info) {
          if (isShopPosition(info.creature->getPosition())) {
            for (auto& item : info.items)
              if (item->isShopkeeper(creature)) {
                info.creature->getDebt().add(creature, -item->getPrice());
                if (info.creature->getDebt().getAmountOwed(creature) == 0)
                  debtors.erase(info.creature);
              }
          }
        },
        [&](const auto&) {}
    );
  }

  template <class Archive>
  void serialize(Archive& ar, const unsigned int) {
    ar & SUBCLASS(Monster) & SUBCLASS(EventListener);
    ar(prevCreatures, debtors, thiefCount, thieves, shopArea, myLevel, firstMove);
  }

  SERIALIZATION_CONSTRUCTOR(ShopkeeperController)

  private:
  EntitySet<Creature> SERIAL(prevCreatures);
  EntitySet<Creature> SERIAL(debtors);
  EntityMap<Creature, int> SERIAL(thiefCount);
  EntitySet<Creature> SERIAL(thieves);
  Rectangle SERIAL(shopArea);
  WLevel SERIAL(myLevel) = nullptr;
  bool SERIAL(firstMove) = true;
};

void CreatureFactory::addInventory(Creature* c, const vector<ItemType>& items) {
  for (ItemType item : items)
    c->take(item.get(contentFactory));
}

PController CreatureFactory::getShopkeeper(Rectangle shopArea, Creature* c) {
  return makeOwner<ShopkeeperController>(c, shopArea);
}

class IllusionController : public DoNothingController {
  public:
  IllusionController(Creature* c, GlobalTime deathT) : DoNothingController(c), deathTime(deathT) {}

  virtual void onKilled(const Creature* attacker) override {
    if (attacker)
      attacker->message("It was just an illusion!");
  }

  virtual void makeMove() override {
    if (*creature->getGlobalTime() >= deathTime) {
      creature->message("The illusion disappears.");
      creature->dieNoReason();
    } else
      creature->wait().perform(creature);
  }

  SERIALIZE_ALL(SUBCLASS(DoNothingController), deathTime)
  SERIALIZATION_CONSTRUCTOR(IllusionController)

  private:
  GlobalTime SERIAL(deathTime);
};

PCreature CreatureFactory::getIllusion(Creature* creature) {
  ViewObject viewObject(creature->getViewObject().id(), ViewLayer::CREATURE, "Illusion");
  viewObject.setModifier(ViewObject::Modifier::ILLUSION);
  auto ret = makeOwner<Creature>(viewObject, creature->getTribeId(), CATTR(
          c.viewId = ViewId("rock"); //overriden anyway
          c.illusionViewObject = creature->getViewObject();
          c.illusionViewObject->setModifier(ViewObject::Modifier::INVISIBLE, false);
          c.body = Body::nonHumanoidSpirit(Body::Size::LARGE);
          c.body->setDeathSound(SoundId::MISSED_ATTACK);
          c.attr[AttrType::DAMAGE] = 20; // just so it's not ignored by creatures
          c.attr[AttrType::DEFENSE] = 1;
          c.permanentEffects[LastingEffect::FLYING] = 1;
          c.noAttackSound = true;
          c.canJoinCollective = false;
          c.name = creature->getName();), SpellMap{});
  ret->setController(makeOwner<IllusionController>(ret.get(), *creature->getGlobalTime()
      + TimeInterval(Random.get(5, 10))));
  return ret;
}

REGISTER_TYPE(BoulderController)
REGISTER_TYPE(SokobanController)
REGISTER_TYPE(KrakenController)
REGISTER_TYPE(ShopkeeperController)
REGISTER_TYPE(IllusionController)
REGISTER_TYPE(ListenerTemplate<ShopkeeperController>)

PCreature CreatureFactory::get(CreatureAttributes attr, TribeId tribe, const ControllerFactory& factory, SpellMap spells) {
  auto ret = makeOwner<Creature>(tribe, std::move(attr), std::move(spells));
  ret->setController(factory.get(ret.get()));
  return ret;
}

static ViewId getSpecialViewId(bool humanoid, bool large, bool body, bool wings) {
  static vector<ViewId> specialViewIds {
    ViewId("special_blbn"),
    ViewId("special_blbw"),
    ViewId("special_blgn"),
    ViewId("special_blgw"),
    ViewId("special_bmbn"),
    ViewId("special_bmbw"),
    ViewId("special_bmgn"),
    ViewId("special_bmgw"),
    ViewId("special_hlbn"),
    ViewId("special_hlbw"),
    ViewId("special_hlgn"),
    ViewId("special_hlgw"),
    ViewId("special_hmbn"),
    ViewId("special_hmbw"),
    ViewId("special_hmgn"),
    ViewId("special_hmgw"),
  };
  return specialViewIds[humanoid * 8 + (!large) * 4 + (!body) * 2 + wings];
}

static string getSpeciesName(bool humanoid, bool large, bool living, bool wings) {
  static vector<string> names {
    "devitablex",
    "owlbeast",
    "hellar dra",
    "marilisk",
    "gelaticorn",
    "mant eatur",
    "phanticore",
    "yeth horro",
    "yeth amon",
    "mantic dra",
    "unic cread",
    "under hulk",
    "nightshasa",
    "manananggal",
    "dire spawn",
    "shamander",
  };
  return names[humanoid * 8 + (!large) * 4 + (!living) * 2 + wings];
}

static optional<ItemType> getSpecialBeastAttack(bool large, bool living, bool wings) {
  static vector<optional<ItemType>> attacks {
    ItemType(ItemType::fangs(7)),
    ItemType(ItemType::fangs(7, VictimEffect{0.7, EffectType(Effects::Fire{})})),
    ItemType(ItemType::fangs(7, VictimEffect{0.7, EffectType(Effects::Fire{})})),
    ItemType(ItemType::fists(7)),
    ItemType(ItemType::fangs(7, VictimEffect{0.3, EffectType(Effects::Lasting{LastingEffect::POISON})})),
    ItemType(ItemType::fangs(7)),
    ItemType(ItemType::fangs(7, VictimEffect{0.3, EffectType(Effects::Lasting{LastingEffect::POISON})})),
    ItemType(ItemType::fists(7)),
  };
  return attacks[(!large) * 4 + (!living) * 2 + wings];
}

static EnumMap<BodyPart, int> getSpecialBeastBody(bool large, bool living, bool wings) {
  static vector<EnumMap<BodyPart, int>> parts {
    {
      { BodyPart::LEG, 2}},
    {
      { BodyPart::ARM, 2},
      { BodyPart::LEG, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::LEG, 4},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::ARM, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {},
    { 
      { BodyPart::LEG, 2},
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
    {
      { BodyPart::LEG, 8},
      { BodyPart::HEAD, 1}},
    { 
      { BodyPart::WING, 2},
      { BodyPart::HEAD, 1}},
  };
  return parts[(!large) * 4 + (!living) * 2 + wings];
}

static vector<LastingEffect> getResistanceAndVulnerability(RandomGen& random) {
  vector<LastingEffect> resistances {
      LastingEffect::MAGIC_RESISTANCE,
      LastingEffect::MELEE_RESISTANCE,
      LastingEffect::RANGED_RESISTANCE
  };
  vector<LastingEffect> vulnerabilities {
      LastingEffect::MAGIC_VULNERABILITY,
      LastingEffect::MELEE_VULNERABILITY,
      LastingEffect::RANGED_VULNERABILITY
  };
  vector<LastingEffect> ret;
  ret.push_back(Random.choose(resistances));
  vulnerabilities.removeIndex(*resistances.findElement(ret[0]));
  ret.push_back(Random.choose(vulnerabilities));
  return ret;
}

PCreature CreatureFactory::getSpecial(CreatureId id, TribeId tribe, SpecialParams p, const ControllerFactory& factory) {
  Body body = Body(p.humanoid, p.living ? Body::Material::FLESH : Body::Material::SPIRIT,
      p.large ? Body::Size::LARGE : Body::Size::MEDIUM);
  if (p.wings)
    body.addWithoutUpdatingPermanentEffects(BodyPart::WING, 2);
  string name = getSpeciesName(p.humanoid, p.large, p.living, p.wings);
  auto attributes = CATTR(
        c.viewId = getSpecialViewId(p.humanoid, p.large, p.living, p.wings);
        c.isSpecial = true;
        c.body = std::move(body);
        c.attr[AttrType::DAMAGE] = Random.get(28, 34);
        c.attr[AttrType::DEFENSE] = Random.get(28, 34);
        c.attr[AttrType::SPELL_DAMAGE] = Random.get(28, 34);
        for (auto effect : getResistanceAndVulnerability(Random))
          c.permanentEffects[effect] = 1;
        if (p.large) {
          c.attr[AttrType::DAMAGE] += 6;
          c.attr[AttrType::DEFENSE] += 2;
          c.attr[AttrType::SPELL_DAMAGE] -= 6;
        }
        if (p.humanoid) {
          c.skills.setValue(SkillId::WORKSHOP, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::FORGE, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::LABORATORY, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::JEWELER, Random.getDouble(0, 1));
          c.skills.setValue(SkillId::FURNACE, Random.getDouble(0, 1));
          c.maxLevelIncrease[ExperienceType::MELEE] = 10;
          c.maxLevelIncrease[ExperienceType::SPELL] = 10;
          c.spellSchools = LIST(SpellSchoolId("mage"));
        }
        if (p.humanoid) {
          c.chatReactionFriendly = "\"I am the mighty " + name + "\"";
          c.chatReactionHostile = "\"I am the mighty " + name + ". Die!\"";
        } else {
          c.chatReactionFriendly = c.chatReactionHostile = c.petReaction = "snarls."_s;
        }
        c.name = name;
        c.name.setStack(p.humanoid ? "legendary humanoid" : "legendary beast");
        c.name.setFirst(nameGenerator->getNext(NameGeneratorId("DEMON")));
        if (!p.humanoid) {
          c.body->setBodyParts(getSpecialBeastBody(p.large, p.living, p.wings));
          c.attr[AttrType::DAMAGE] += 5;
          c.attr[AttrType::DEFENSE] += 5;
          if (auto attack = getSpecialBeastAttack(p.large, p.living, p.wings))
            c.body->setIntrinsicAttack(BodyPart::HEAD, *attack);
        }
        if (Random.roll(3))
          c.permanentEffects[LastingEffect::SWIMMING_SKILL] = 1;
        );
  initializeAttributes(id, attributes);
  auto spells = getSpellMap(attributes);
  PCreature c = get(std::move(attributes), tribe, factory, std::move(spells));
  if (body.isHumanoid()) {
    if (Random.roll(4))
      c->take(ItemType(CustomItemId("Bow")).get(contentFactory));
    c->take(Random.choose(
          ItemType(CustomItemId("Sword")).setPrefixChance(1),
          ItemType(CustomItemId("BattleAxe")).setPrefixChance(1),
          ItemType(CustomItemId("WarHammer")).setPrefixChance(1))
        .get(contentFactory));
  }
  return c;
}

void CreatureFactory::initializeAttributes(CreatureId id, CreatureAttributes& attr) {
  attr.setCreatureId(id);
  attr.randomize();
  auto& attacks = attr.getBody().getIntrinsicAttacks();
  for (auto bodyPart : ENUM_ALL(BodyPart))
    if (auto& attack = attacks[bodyPart])
      attack->initializeItem(contentFactory);
}

CreatureAttributes CreatureFactory::getAttributesFromId(CreatureId id) {
  auto ret = [this, id] {
    if (auto ret = getValueMaybe(attributes, id)) {
      ret->name.generateFirst(&*nameGenerator);
      return std::move(*ret);
    } else if (id == "KRAKEN")
      return getKrakenAttributes(ViewId("kraken_head"), "kraken");
    FATAL << "Unrecognized creature type: \"" << id << "\"";
    fail();
  }();
  initializeAttributes(id, ret);
  return ret;
}

ControllerFactory getController(CreatureId id, MonsterAIFactory normalFactory) {
  if (id == "KRAKEN")
    return ControllerFactory([=](Creature* c) {
        return makeOwner<KrakenController>(c);
        });
  else
    return Monster::getFactory(normalFactory);
}

const map<CreatureId, CreatureFactory::SpecialParams>& CreatureFactory::getSpecialParams() {
  static map<CreatureId, CreatureFactory::SpecialParams> ret = {
    { CreatureId("SPECIAL_BLBN"), {false, true, true, false}},
    { CreatureId("SPECIAL_BLBW"), {false, true, true, true}},
    { CreatureId("SPECIAL_BLGN"), {false, true, false, false}},
    { CreatureId("SPECIAL_BLGW"), {false, true, false, true}},
    { CreatureId("SPECIAL_BMBN"), {false, false, true, false}},
    { CreatureId("SPECIAL_BMBW"), {false, false, true, true}},
    { CreatureId("SPECIAL_BMGN"), {false, false, false, false}},
    { CreatureId("SPECIAL_BMGW"), {false, false, false, true}},
    { CreatureId("SPECIAL_HLBN"), {true, true, true, false}},
    { CreatureId("SPECIAL_HLBW"), {true, true, true, true}},
    { CreatureId("SPECIAL_HLGN"), {true, true, false, false}},
    { CreatureId("SPECIAL_HLGW"), {true, true, false, true}},
    { CreatureId("SPECIAL_HMBN"), {true, false, true, false}},
    { CreatureId("SPECIAL_HMBW"), {true, false, true, true}},
    { CreatureId("SPECIAL_HMGN"), {true, false, false, false}},
    { CreatureId("SPECIAL_HMGW"), {true, false, false, true}},
  };
  return ret;
}

SpellMap CreatureFactory::getSpellMap(const CreatureAttributes& attr) {
  SpellMap spellMap;
  for (auto& schoolName : attr.spellSchools) {
    auto& school = spellSchools.at(schoolName);
    for (auto& spell : school.spells)
      spellMap.add(*getSpell(spell.first), school.expType, spell.second);
  }
  for (auto& spell : attr.spells)
    spellMap.add(*getSpell(spell), ExperienceType::SPELL, 0);
  return spellMap;
}

PCreature CreatureFactory::get(CreatureId id, TribeId tribe, MonsterAIFactory aiFactory) {
  ControllerFactory factory = Monster::getFactory(aiFactory);
  auto& special = getSpecialParams();
  if (special.count(id))
    return getSpecial(id, tribe, special.at(id), factory);
  else if (id == "SOKOBAN_BOULDER")
    return getSokobanBoulder(tribe);
  else {
    auto attr = getAttributesFromId(id);
    auto spells = getSpellMap(attr);
    return get(std::move(attr), tribe, getController(id, aiFactory), std::move(spells));
  }
}

const Spell* CreatureFactory::getSpell(SpellId id) const {
  for (auto& spell : spells)
    if (spell.getId() == id)
      return &spell;
  return nullptr;
}

PCreature CreatureFactory::getGhost(Creature* creature) {
  ViewObject viewObject(creature->getViewObject().id(), ViewLayer::CREATURE, "Ghost");
  viewObject.setModifier(ViewObject::Modifier::ILLUSION);
  auto ret = makeOwner<Creature>(viewObject, creature->getTribeId(), getAttributesFromId(CreatureId("LOST_SOUL")), SpellMap{});
  ret->setController(Monster::getFactory(MonsterAIFactory::monster()).get(ret.get()));
  return ret;
}

vector<ItemType> CreatureFactory::getDefaultInventory(CreatureId id) const {
  if (inventory.count(id)) {
    auto& inventoryGen = inventory.at(id);
    vector<ItemType> items;
    for (auto& elem : inventoryGen.elems)
      if (Random.chance(elem.chance))
        for (int i : Range(Random.get(elem.countMin, elem.countMax + 1)))
          items.push_back(ItemType(elem.type).setPrefixChance(elem.prefixChance));
    return items;
  } else
    return {};
}

PCreature CreatureFactory::fromId(CreatureId id, TribeId t) {
  return fromId(id, t, MonsterAIFactory::monster());
}


PCreature CreatureFactory::fromId(CreatureId id, TribeId t, const MonsterAIFactory& f) {
  return fromId(id, t, f, {});
}

PCreature CreatureFactory::fromId(CreatureId id, TribeId t, const MonsterAIFactory& factory, const vector<ItemType>& inventory) {
  auto ret = get(id, t, factory);
  addInventory(ret.get(), inventory);
  addInventory(ret.get(), getDefaultInventory(id));
  return ret;
}

PCreature CreatureFactory::getHumanForTests() {
  auto attributes = CATTR(
      c.viewId = ViewId("keeper1");
      c.attr[AttrType::DAMAGE] = 12;
      c.attr[AttrType::DEFENSE] = 12;
      c.body = Body::humanoid(Body::Size::LARGE);
      c.name = "wizard";
      c.viewIdUpgrades = LIST(ViewId("keeper2"), ViewId("keeper3"), ViewId("keeper4"));
      c.name.setFirst("keeper"_s);
      c.name.useFullTitle();
      c.skills.setValue(SkillId::LABORATORY, 0.2);
      c.maxLevelIncrease[ExperienceType::MELEE] = 7;
      c.maxLevelIncrease[ExperienceType::SPELL] = 12;
      //c.spells->add(SpellId::HEAL_SELF);
  );
  return get(std::move(attributes), TribeId::getMonster(), Monster::getFactory(MonsterAIFactory::idle()), SpellMap{});
}
