#include "stdafx.h"
#include "furniture.h"
#include "player_message.h"
#include "tribe.h"
#include "fire.h"
#include "view_object.h"
#include "furniture_factory.h"
#include "creature.h"
#include "creature_attributes.h"
#include "sound.h"
#include "item_factory.h"
#include "item.h"
#include "game.h"
#include "game_event.h"
#include "level.h"
#include "furniture_usage.h"
#include "furniture_entry.h"
#include "furniture_dropped_items.h"
#include "furniture_click.h"
#include "furniture_tick.h"
#include "movement_set.h"
#include "fx_info.h"
#include "furniture_on_built.h"
#include "content_factory.h"
#include "item_list.h"

static string makePlural(const string& s) {
  if (s.empty())
    return "";
  if (s.back() == 'y')
    return s.substr(0, s.size() - 1) + "ies";
  if (s.back() == 'h')
    return s + "es";
  if (s.back() == 's')
    return s;
  if (endsWith(s, "shelf"))
    return s.substr(0, s.size() - 5) + "shelves";
  return s + "s";
}

/*Furniture::Furniture(const string& n, const optional<ViewObject>& o, FurnitureType t, TribeId id)
    : viewObject(o), name(n), pluralName(makePlural(name)), type(t), movementSet(id) {
  movementSet->addTrait(MovementTrait::WALK);
}*/

Furniture::Furniture(const Furniture&) = default;

Furniture::Furniture() {
  movementSet->addTrait(MovementTrait::WALK);
}

Furniture::~Furniture() {}

template<typename Archive>
void Furniture::serializeImpl(Archive& ar, const unsigned) {
  ar(SKIP(SUBCLASS(OwnedObject<Furniture>)), OPTION(viewObject), OPTION(removeNonFriendly), OPTION(canBuildOutsideOfTerritory));
  ar(NAMED(name), OPTION(pluralName), OPTION(type), OPTION(movementSet), OPTION(fire), OPTION(burntRemains), OPTION(destroyedRemains));
  ar(OPTION(destroyedInfo), OPTION(itemDrop), OPTION(wall), SKIP(creator), NAMED(createdTime), OPTION(canSilentlyReplace));
  ar(OPTION(blockVision), NAMED(usageType), NAMED(clickType), NAMED(tickType), OPTION(usageTime), OPTION(overrideMovement));
  ar(NAMED(constructMessage), OPTION(layer), OPTION(entryType), OPTION(lightEmission), OPTION(canHideHere), OPTION(warning));
  ar(NAMED(summonedElement), OPTION(droppedItems), OPTION(xForgetAfterBuilding), OPTION(requiredSupport), OPTION(builtOver));
  ar(OPTION(canBuildBridge), OPTION(noProjectiles), OPTION(clearFogOfWar), OPTION(removeWithCreaturePresent), OPTION(upgrade));
  ar(OPTION(luxury), OPTION(buildingSupport), NAMED(onBuilt), OPTION(burnsDownMessage), OPTION(maxTraining), OPTION(bridge));
  ar(OPTION(bedType), OPTION(requiresLight), OPTION(populationIncrease), OPTION(destroyFX), OPTION(tryDestroyFX), OPTION(walkOverFX));
  ar(OPTION(walkIntoFX), OPTION(usageFX), OPTION(hostileSpell), OPTION(lastingEffect), NAMED(meltInfo), NAMED(freezeTo));
  ar(OPTION(bloodCountdown), SKIP(bloodTime), NAMED(destroyedEffect));
}

template <class Archive>
void Furniture::serialize(Archive& ar1, const unsigned int v) {
  serializeImpl(ar1, v);
}

SERIALIZABLE(Furniture)

const heap_optional<ViewObject>& Furniture::getViewObject() const {
  return viewObject;
}

heap_optional<ViewObject>& Furniture::getViewObject() {
  return viewObject;
}

void Furniture::updateViewObject() {
  if (viewObject) {
    double minHealth = 1;
    for (auto action : ENUM_ALL(DestroyAction::Type))
      if (auto& info = destroyedInfo[action])
        minHealth = min(minHealth, info->health);
    if (minHealth < 1) {
      viewObject->setAttribute(ViewObjectAttribute::HEALTH, minHealth);
      if (isWall())
        viewObject->setModifier(ViewObjectModifier::FURNITURE_CRACKS);
    }
  }
}

const string& Furniture::getName(int count) const {
  if (count > 1)
    return pluralName;
  else
    return name;
}

FurnitureType Furniture::getType() const {
  return type;
}

bool Furniture::isVisibleTo(const Creature* c) const {
  PROFILE;
  if (entryType)
    return entryType->isVisibleTo(this, c);
  else
    return true;
}

const MovementSet& Furniture::getMovementSet() const {
  return *movementSet;
}

void Furniture::onEnter(Creature* c) const {
  if (entryType) {
    auto f = c->getPosition().modFurniture(layer);
    f->entryType->handle(f, c);
  }
}

void Furniture::destroy(Position pos, const DestroyAction& action) {
  if (!destroyedEffect)
    pos.globalMessage("The " + name + " " + action.getIsDestroyed());
  auto myLayer = layer;
  auto myType = type;
  if (itemDrop)
    pos.dropItems(itemDrop->random(pos.getGame()->getContentFactory()));
  if (usageType)
    FurnitureUsage::beforeRemoved(*usageType, pos);
  if (destroyFX)
    pos.getGame()->addEvent(EventInfo::FX{pos, *destroyFX});
  auto effect = destroyedEffect;
  pos.removeFurniture(this, destroyedRemains
      ? pos.getGame()->getContentFactory()->furniture.getFurniture(*destroyedRemains, getTribe()) : nullptr);
  pos.getGame()->addEvent(EventInfo::FurnitureDestroyed{pos, myType, myLayer});
  if (effect)
    effect->apply(pos);
}

void Furniture::tryToDestroyBy(Position pos, Creature* c, const DestroyAction& action) {
  if (auto& info = destroyedInfo[action.getType()]) {
    c->addSound(action.getSound());
    double damage = c->getAttr(AttrType::DAMAGE);
    if (auto skill = action.getDestroyingSkillMultiplier())
      damage = damage * c->getAttributes().getSkills().getValue(*skill);
    info->health -= damage / info->strength;
    updateViewObject();
    pos.setNeedsRenderAndMemoryUpdate(true);
    if (tryDestroyFX)
      pos.getGame()->addEvent(EventInfo::FX{pos, *tryDestroyFX});
    if (info->health <= 0)
      destroy(pos, action);
  }
}

TribeId Furniture::getTribe() const {
  return movementSet->getTribe();
}

void Furniture::setTribe(TribeId id) {
  movementSet->setTribe(id);
}

bool Furniture::hasRequiredSupport(Position pos) const {
  return requiredSupport.empty() || !!getSupportInfo(pos);
}

optional<ViewId> Furniture::getSupportViewId(Position pos) const {
  if (auto ret = getSupportInfo(pos))
    return ret->viewId;
  return none;
}

const Furniture::SupportInfo* Furniture::getSupportInfo(Position pos) const {
  auto hasSupport = [&](const vector<Dir>& dirs) {
    for (auto dir : dirs)
      if (!pos.plus(Vec2(dir)).isWall())
        return false;
    return true;
  };
  for (int i : All(requiredSupport)) {
    if (hasSupport(requiredSupport[i].dirs))
      return &requiredSupport[i];
  }
  return nullptr;
}

void Furniture::tick(Position pos) {
  PROFILE_BLOCK("Furniture::tick");
  if (fire && fire->isBurning()) {
    {
      auto otherF = pos.getFurniture(layer);
      CHECK(otherF == this) << EnumInfo<FurnitureLayer>::getString(layer) << " " << getName()
          << " " << (otherF ? (otherF->getName() + " " + EnumInfo<FurnitureLayer>::getString(otherF->getLayer())): "null"_s);
    }
    if (viewObject)
      viewObject->setModifier(ViewObject::Modifier::BURNING);
    INFO << getName() << " burning ";
    for (Position v : pos.neighbors8())
      v.fireDamage(0.02);
    pos.fireDamage(0.5);
    fire->tick();
    if (fire->isBurntOut()) {
      switch (burnsDownMessage) {
        case BurnsDownMessage::BURNS_DOWN:
          pos.globalMessage("The " + getName() + " burns down");
          break;
        case BurnsDownMessage::STOPS_BURNING:
          pos.globalMessage("The " + getName() + " stops burning");
          break;
      }
      pos.updateMovementDueToFire();
      pos.removeCreatureLight(false);
      auto myLayer = layer;
      auto myType = type;
      pos.removeFurniture(this, burntRemains ?
          pos.getGame()->getContentFactory()->furniture.getFurniture(*burntRemains, getTribe()) : nullptr);
      pos.getGame()->addEvent(EventInfo::FurnitureDestroyed{pos, myType, myLayer});
      return;
    }
  }
  if (tickType)
    FurnitureTick::handle(*tickType, pos, this); // this function can delete this
  if (bloodTime && *bloodTime <= pos.getModel()->getLocalTime())
    spreadBlood(pos);
}

bool Furniture::blocksAnyVision() const {
  return !blockVision.isEmpty();
}

bool Furniture::canSeeThru(VisionId id) const {
  return !blockVision.contains(id);
}

bool Furniture::stopsProjectiles(VisionId id) const {
  return !canSeeThru(id) || noProjectiles;
}

bool Furniture::overridesMovement() const {
  return overrideMovement;
}

void Furniture::click(Position pos) const {
  if (clickType) {
    FurnitureClick::handle(*clickType, pos, this);
    pos.setNeedsRenderAndMemoryUpdate(true);
  }
}

void Furniture::use(Position pos, Creature* c) const {
  if (usageType)
    FurnitureUsage::handle(*usageType, pos, this, c);
}

bool Furniture::canUse(const Creature* c) const {
  if (usageType)
    return FurnitureUsage::canHandle(*usageType, c);
  else
    return true;
}

optional<FurnitureUsageType> Furniture::getUsageType() const {
  return usageType;
}

TimeInterval Furniture::getUsageTime() const {
  return usageTime;
}

optional<FurnitureClickType> Furniture::getClickType() const {
  return clickType;
}

optional<FurnitureTickType> Furniture::getTickType() const {
  return tickType;
}

const heap_optional<FurnitureEntry>& Furniture::getEntryType() const {
  return entryType;
}

bool Furniture::isTicking() const {
  return !!tickType;
}

bool Furniture::isWall() const {
  return wall;
}

bool Furniture::isBuildingSupport() const {
  return buildingSupport;
}

void Furniture::onConstructedBy(Position pos, Creature* c) {
  if (c) {
    creator = c;
    createdTime = c->getLocalTime();
    if (constructMessage)
      switch (*constructMessage) {
        case ConstructMessage::BUILD:
          c->thirdPerson(c->getName().the() + " builds " + addAParticle(getName()));
          c->secondPerson("You build " + addAParticle(getName()));
          break;
        case ConstructMessage::FILL_UP:
          c->thirdPerson(c->getName().the() + " fills up the tunnel");
          c->secondPerson("You fill up the tunnel");
          break;
        case ConstructMessage::REINFORCE:
          c->thirdPerson(c->getName().the() + " reinforces the wall");
          c->secondPerson("You reinforce the wall");
          break;
        case ConstructMessage::SET_UP:
          c->thirdPerson(c->getName().the() + " sets up " + addAParticle(getName()));
          c->secondPerson("You set up " + addAParticle(getName()));
          break;
      }
  }
  if (onBuilt)
    handleOnBuilt(pos, this, *onBuilt);
}

FurnitureLayer Furniture::getLayer() const {
  return layer;
}

double Furniture::getLightEmission() const {
  if (fire && fire->isBurning())
    return Level::getCreatureLightRadius();
  else
    return lightEmission;
}

bool Furniture::canHide() const {
  return canHideHere;
}

bool Furniture::emitsWarning(const Creature*) const {
  return warning;
}

bool Furniture::canRemoveWithCreaturePresent() const {
  return removeWithCreaturePresent && !wall;
}

bool Furniture::canRemoveNonFriendly() const {
  return removeNonFriendly;
}

Creature* Furniture::getCreator() const {
  return creator.get();
}

optional<LocalTime> Furniture::getCreatedTime() const {
  return createdTime;
}

optional<CreatureId> Furniture::getSummonedElement() const {
  return summonedElement;
}

bool Furniture::isClearFogOfWar() const {
  return clearFogOfWar;
}

bool Furniture::forgetAfterBuilding() const {
  return xForgetAfterBuilding;
}

void Furniture::onCreatureWalkedOver(Position pos, Vec2 direction) const {
  if (walkOverFX)
    pos.getGame()->addEvent((EventInfo::FX{pos, *walkOverFX, direction}));
}

void Furniture::onCreatureWalkedInto(Position pos, Vec2 direction) const {
  if (walkIntoFX)
    pos.getGame()->addEvent((EventInfo::FX{pos, *walkIntoFX, direction}));
}

bool Furniture::onBloodNear(Position pos) {
  if (bloodCountdown)
    if (--*bloodCountdown == 0)
      return true;
  return false;
}

void Furniture::spreadBlood(Position pos) {
  if (!!bloodCountdown) {
    bloodCountdown = none;
    viewObject->setModifier(ViewObjectModifier::BLOODY);
    name = "bloody " + name;
    viewObject->setDescription(capitalFirst(name));
    for (auto v : pos.neighbors4())
      if (auto f = v.getFurniture(layer))
        if (!!f->bloodCountdown) {
          v.modFurniture(layer)->bloodTime = pos.getModel()->getLocalTime() + 1_visible;
          v.getLevel()->addTickingFurniture(v.getCoord());
        }
  }
}

int Furniture::getMaxTraining(ExperienceType t) const {
  return maxTraining[t];
}

optional<FurnitureType> Furniture::getUpgrade() const {
  return upgrade;
}

optional<FXVariantName> Furniture::getUsageFX() const {
  return usageFX;
}

vector<PItem> Furniture::dropItems(Position pos, vector<PItem> v) const {
  if (droppedItems) {
    return droppedItems->handle(pos, this, std::move(v));
  } else
    return v;
}

bool Furniture::canBuildBridgeOver() const {
  return canBuildBridge;
}

const LuxuryInfo&Furniture::getLuxuryInfo() const {
  return luxury;
}

const Furniture::PopulationInfo& Furniture::getPopulationIncrease() const {
  return populationIncrease;
}

optional<FurnitureType> Furniture::getBuiltOver() const {
  return builtOver;
}

bool Furniture::isBridge() const {
  return bridge;
}

bool Furniture::silentlyReplace() const {
  return canSilentlyReplace;
}

void Furniture::setType(FurnitureType t) {
  type = t;
}

bool Furniture::buildOutsideOfTerritory() const {
  return canBuildOutsideOfTerritory;
}

bool Furniture::isRequiresLight() const {
  return requiresLight;
}

bool Furniture::isHostileSpell() const {
  return hostileSpell;
}

optional<BedType> Furniture::getBedType() const {
  return bedType;
}

const optional<FurnitureEffectInfo>& Furniture::getLastingEffectInfo() const {
  return lastingEffect;
}

Furniture& Furniture::setBlocking() {
  movementSet->clearTraits();
  return *this;
}

Furniture& Furniture::setBlockingEnemies() {
  movementSet->addTrait(MovementTrait::WALK);
  movementSet->setBlockingEnemies();
  return *this;
}

const heap_optional<Fire>& Furniture::getFire() const {
  return fire;
}

bool Furniture::canDestroy(const MovementType& movement, const DestroyAction& action) const {
   return canDestroy(action) &&
       (!fire || !fire->isBurning()) &&
       (!movement.isCompatible(getTribe()) || action.canDestroyFriendly());
}

void Furniture::fireDamage(Position pos, bool withMessage) {
  if (meltInfo) {
    pos.globalMessage("The " + getName() + " melts");
    PFurniture replace;
    if (meltInfo->meltTo)
      replace = pos.getGame()->getContentFactory()->furniture.getFurniture(*meltInfo->meltTo, getTribe());
    pos.removeFurniture(this, std::move(replace));
  } else
  if (fire) {
    bool burning = fire->isBurning();
    fire->set();
    if (!burning && fire->isBurning()) {
      if (withMessage)
        pos.globalMessage("The " + getName() + " catches fire");
      if (viewObject)
        viewObject->setModifier(ViewObject::Modifier::BURNING);
      pos.updateMovementDueToFire();
      pos.getLevel()->addTickingFurniture(pos.getCoord());
      pos.addCreatureLight(false);
    }
  }
}

void Furniture::iceDamage(Position pos) {
  if (freezeTo) {
    pos.globalMessage("The " + getName() + " freezes");
    pos.removeFurniture(this, pos.getGame()->getContentFactory()->furniture.getFurniture(*freezeTo, getTribe()));
  }
}

Furniture& Furniture::setDestroyable(double s) {
  setDestroyable(s, DestroyAction::Type::BOULDER);
  setDestroyable(s, DestroyAction::Type::BASH);
  return *this;
}

Furniture& Furniture::setDestroyable(double s, DestroyAction::Type type) {
  destroyedInfo[type] = DestroyedInfo{ 1.0, s };
  return *this;
}

bool Furniture::canDestroy(const DestroyAction& action) const {
  return !!destroyedInfo[action.getType()];
}

optional<double> Furniture::getStrength(const DestroyAction& action) const {
  if (auto info = destroyedInfo[action.getType()]) {
    CHECK(info->health > 0) << info->health;
    CHECK(info->strength > 0) << info->health;
    return info->strength * info->health;
  }
  return none;
}

static ViewLayer getViewLayer(FurnitureLayer layer) {
  switch (layer) {
    case FurnitureLayer::FLOOR:
    case FurnitureLayer::GROUND:
      return ViewLayer::FLOOR_BACKGROUND;
    case FurnitureLayer::MIDDLE:
      return ViewLayer::FLOOR;
    case FurnitureLayer::CEILING:
      return ViewLayer::TORCH1;
  }
}

#include "pretty_archive.h"
template <>
void Furniture::serialize(PrettyInputArchive& ar, unsigned int v) {
  optional<ViewId> viewId;
  if (viewObject)
    viewId = viewObject->id();
  ar >> NAMED(viewId);
  optional<ViewLayer> viewLayer;
  if (viewObject)
    viewLayer = viewObject->layer();
  ar >> NAMED(viewLayer);
  PrettyFlag blockMovement;
  ar >> OPTION(blockMovement);
  optional_no_none<int> strength;
  ar >> NAMED(strength);
  optional_no_none<vector<pair<int, DestroyAction::Type>>> strength2;
  ar >> NAMED(strength2);
  optional_no_none<Dir> attachmentDir;
  if (viewObject)
    if (auto dir = viewObject->getAttachmentDir())
      attachmentDir = *dir;
  ar >> NAMED(attachmentDir);
  PrettyFlag blockingEnemies;
  ar >> OPTION(blockingEnemies);
  optional_no_none<double> waterDepth;
  if (viewObject)
    if (auto depth = viewObject->getAttribute(ViewObjectAttribute::WATER_DEPTH))
      waterDepth = *depth;
  ar >> NAMED(waterDepth);
  serializeImpl(ar, v);
  ar >> endInput();
  if (blockMovement.value)
    setBlocking();
  if (strength)
    setDestroyable(*strength);
  if (strength2)
    for (auto& elem : *strength2)
      setDestroyable(elem.first, elem.second);
  if (viewId)
    viewObject = ViewObject(*viewId, viewLayer.value_or(getViewLayer(layer)), capitalFirst(getName()));
  if (attachmentDir)
    viewObject->setAttachmentDir(*attachmentDir);
  if (waterDepth)
    viewObject->setAttribute(ViewObjectAttribute::WATER_DEPTH, *waterDepth);
  if (blockingEnemies.value)
    setBlockingEnemies();
  if (pluralName.empty())
    pluralName = makePlural(name);
}
