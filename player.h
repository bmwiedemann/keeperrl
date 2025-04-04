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

#include "creature_action.h"
#include "controller.h"
#include "user_input.h"
#include "creature_view.h"
#include "map_memory.h"
#include "position.h"
#include "event_listener.h"
#include "game_info.h"

class View;
class Model;
class Creature;
class Item;
class ListElem;
struct ItemInfo;
class Game;
class VisibilityMap;
class Tutorial;
class MessageBuffer;
class UnknownLocations;
class DungeonLevel;

class Player : public Controller, public CreatureView, public EventListener<Player> {
  public:
  virtual ~Player() override;

  Player(Creature*, bool adventurer, SMapMemory, SMessageBuffer, SVisibilityMap, SUnknownLocations,
      STutorial = nullptr);

  void onEvent(const GameEvent&);

  SERIALIZATION_DECL(Player)

  protected:

  virtual void moveAction(Vec2 direction);

  // from CreatureView
  virtual void getViewIndex(Vec2 pos, ViewIndex&) const override;
  virtual const MapMemory& getMemory() const override;
  virtual void refreshGameInfo(GameInfo&) const override;
  virtual Vec2 getScrollCoord() const override;
  virtual Level* getCreatureViewLevel() const override;
  virtual vector<Vec2> getVisibleEnemies() const override;
  virtual double getAnimationTime() const override;
  virtual CenterType getCenterType() const override;
  virtual const vector<Vec2>& getUnknownLocations(WConstLevel) const override;

  // from Controller
  virtual void onKilled(const Creature* attacker) override;
  virtual void makeMove() override;
  virtual void sleeping() override;
  virtual bool isPlayer() const override;
  virtual void privateMessage(const PlayerMessage& message) override;
  virtual MessageGenerator& getMessageGenerator() const override;
  virtual void grantWish(const string& message) override;

  // overridden by subclasses
  struct CommandInfo {
    PlayerInfo::CommandInfo commandInfo;
    function<void(Player*)> perform;
    bool actionKillsController;
  };
  virtual vector<CommandInfo> getCommands() const;
  virtual void onFellAsleep();
  virtual vector<Creature*> getTeam() const;
  virtual bool isTravelEnabled() const;
  virtual bool handleUserInput(UserInput);
  virtual void updateUnknownLocations();

  struct OtherCreatureCommand {
    int priority;
    ViewObjectAction name;
    bool allowAuto;
    function<void(Player*)> perform;
  };
  virtual vector<OtherCreatureCommand> getOtherCreatureCommands(Creature*) const;

  optional<Vec2> chooseDirection(const string& question);
  optional<Position> chooseTarget(Table<PassableInfo>, TargetType, const string& question);

  SMapMemory SERIAL(levelMemory);
  void showHistory();
  Game* getGame() const;
  WModel getModel() const;
  View* getView() const;

  bool tryToPerform(CreatureAction);

  bool canTravel() const;
  private:

  WLevel getLevel() const;
  void considerAdventurerMusic();
  void considerKeeperModeTravelMusic();
  void creatureClickAction(Position, bool extended);
  void pickUpItemAction(int item, bool multi = false);
  void equipmentAction();
  void applyItem(vector<Item*> item);
  void throwItem(Item* item, optional<Position> target = none);
  void takeOffAction();
  void hideAction();
  void displayInventory();
  void handleItems(const EntitySet<Item>&, ItemAction);
  void handleIntrinsicAttacks(const EntitySet<Item>&, ItemAction);
  bool interruptedByEnemy();
  void travelAction();
  void targetAction();
  void payForAllItemsAction();
  void payForItemAction(const vector<Item*>&);
  void chatAction(optional<Vec2> dir = none);
  void giveAction(vector<Item*>);
  void spellAction(int);
  void fireAction();
  void getItemNames(vector<Item*> it, vector<ListElem>& names, vector<vector<Item*> >& groups,
      ItemPredicate = alwaysTrue<const Item*>());
  string getInventoryItemName(const Item*, bool plural) const;
  string getPluralName(Item* item, int num);
  bool SERIAL(travelling) = false;
  Vec2 SERIAL(travelDir);
  optional<Position> SERIAL(target);
  bool SERIAL(adventurer);
  bool SERIAL(displayGreeting);
  bool updateView = true;
  void retireMessages();
  SMessageBuffer SERIAL(messageBuffer);
  string getRemainingString(LastingEffect) const;
  ItemInfo getFurnitureUsageInfo(const string& question, ViewId viewId) const;
  optional<FurnitureUsageType> getUsableUsageType() const;
  SVisibilityMap SERIAL(visibilityMap);
  STutorial SERIAL(tutorial);
  vector<TeamMemberAction> getTeamMemberActions(const Creature*) const;
  optional<GlobalTime> lastEnemyInterruption;
  SUnknownLocations SERIAL(unknownLocations);
  void updateSquareMemory(Position);
  HeapAllocated<DungeonLevel> SERIAL(avatarLevel);
  void fillDungeonLevel(PlayerInfo&) const;
  vector<unordered_set<ViewId, CustomHash<ViewId>>> halluIds;
  void generateHalluIds();
  ViewId shuffleViewId(const ViewId&) const;
};

