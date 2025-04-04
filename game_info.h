#pragma once

#include "unique_entity.h"
#include "item_action.h"
#include "village_action.h"
#include "view_id.h"
#include "player_message.h"
#include "experience_type.h"
#include "entity_set.h"
#include "immigrant_auto_state.h"
#include "tutorial_highlight.h"
#include "hashing.h"
#include "experience_type.h"
#include "attr_type.h"
#include "best_attack.h"
#include "villain_type.h"
#include "game_time.h"
#include "intrinsic_attack.h"
#include "team_member_action.h"
#include "team_order.h"
#include "view_object_modifier.h"
#include "creature_experience_info.h"
#include "tech_id.h"

class PlayerMessage;
class SpecialTrait;

struct CreatureInfo {
  CreatureInfo(const Creature*);
  ViewId HASH(viewId);
  UniqueEntity<Creature>::Id HASH(uniqueId);
  string HASH(name);
  string HASH(stackName);
  BestAttack HASH(bestAttack);
  double HASH(morale);
  HASH_ALL(viewId, uniqueId, name, stackName, bestAttack, morale)
};


struct ItemInfo {
  static ItemInfo get(const Creature*, const vector<Item*>&);
  string HASH(name);
  string HASH(fullName);
  vector<string> HASH(description);
  int HASH(number);
  ViewId HASH(viewId);
  EnumSet<ViewObjectModifier> HASH(viewIdModifiers);
  EntitySet<Item> HASH(ids);
  vector<ItemAction> HASH(actions);
  bool HASH(equiped);
  bool HASH(locked);
  bool HASH(pending);
  bool HASH(unavailable);
  optional<IntrinsicAttack::Active> HASH(intrinsicState);
  string HASH(unavailableReason);
  optional<EquipmentSlot> HASH(slot);
  optional<CreatureInfo> HASH(owner);
  enum Type {EQUIPMENT, CONSUMABLE, OTHER} HASH(type);
  optional<pair<ViewId, int>> HASH(price);
  optional<double> HASH(weight);
  bool HASH(tutorialHighlight);
  bool HASH(hidden);
  HASH_ALL(name, fullName, description, number, viewId, ids, actions, equiped, locked, pending, unavailable, slot, owner, type, price, unavailableReason, weight, tutorialHighlight, intrinsicState, viewIdModifiers, hidden)
};

struct AttributeInfo {
  static vector<AttributeInfo> fromCreature(const Creature*);
  string HASH(name);
  AttrType HASH(attr);
  int HASH(value);
  int HASH(bonus);
  const char* HASH(help);
  HASH_ALL(name, attr, value, bonus, help)
};

struct AvatarLevelInfo {
  int HASH(level);
  double HASH(progress);
  ViewId HASH(viewId);
  string HASH(title);
  int HASH(numAvailable);
  HASH_ALL(level, progress, viewId, title, numAvailable)
};

class PlayerInfo {
  public:
  PlayerInfo(const Creature*);
  string getFirstName() const;
  string getTitle() const;
  vector<AttributeInfo> HASH(attributes);
  optional<AvatarLevelInfo> HASH(avatarLevelInfo);
  BestAttack HASH(bestAttack);
  struct SkillInfo {
    string HASH(name);
    string HASH(help);
    HASH_ALL(name, help)
  };
  vector<SkillInfo> HASH(skills);
  string HASH(firstName);
  string HASH(name);
  string HASH(title);
  CreatureExperienceInfo HASH(experienceInfo);
  vector<string> HASH(spellSchools);
  string description;
  int HASH(positionHash);
  struct Effect {
    string HASH(name);
    string HASH(help);
    bool HASH(bad);
    HASH_ALL(name, help, bad)
  };
  vector<Effect> HASH(effects);
  struct Spell {
    string HASH(name);
    string HASH(symbol);
    vector<string> HASH(help);
    optional<TimeInterval> HASH(timeout);
    HASH_ALL(name, symbol, help, timeout)
  };
  vector<Spell> HASH(spells);
  vector<ItemInfo> HASH(lyingItems);
  vector<ItemInfo> HASH(inventory);
  vector<ItemInfo> HASH(intrinsicAttacks);
  int HASH(debt);
  vector<PlayerInfo> HASH(teamInfos);
  struct CommandInfo {
    string HASH(name);
    optional<char> HASH(keybinding);
    string HASH(description);
    bool HASH(active);
    bool HASH(tutorialHighlight) = false;
    HASH_ALL(name, keybinding, description, active, tutorialHighlight)
  };
  vector<CommandInfo> HASH(commands);
  struct MinionActivityInfo {
    MinionActivity HASH(task);
    bool HASH(inactive);
    bool HASH(current);
    optional<bool> HASH(locked);
    HASH_ALL(task, inactive, current, locked)
  };
  vector<MinionActivityInfo> HASH(minionTasks);
  UniqueEntity<Creature>::Id HASH(creatureId);
  int HASH(moveCounter);
  double HASH(morale);
  ViewId HASH(viewId);
  bool HASH(isPlayerControlled);
  enum ControlMode {
    FULL,
    LEADER
  };
  ControlMode HASH(controlMode);
  enum Action {
    CONTROL,
    RENAME,
    BANISH,
    CONSUME,
    LOCATE
  };
  optional<EnumSet<TeamOrder>> HASH(teamOrders);
  vector<Action> HASH(actions);
  vector<TeamMemberAction> HASH(teamMemberActions);
  optional<double> HASH(carryLimit);
  optional<ViewId> HASH(quarters);
  bool HASH(canAssignQuarters);
  HASH_ALL(attributes, skills, firstName, name, title, experienceInfo, positionHash, effects, spells, lyingItems, inventory, minionTasks, creatureId, morale, viewId, actions, commands, debt, bestAttack, carryLimit, intrinsicAttacks, teamInfos, moveCounter, isPlayerControlled, controlMode, teamMemberActions, quarters, canAssignQuarters, teamOrders, avatarLevelInfo, spellSchools)
};

struct ImmigrantDataInfo {
  ImmigrantDataInfo();
  STRUCT_DECLARATIONS(ImmigrantDataInfo)
  vector<string> HASH(requirements);
  vector<string> HASH(info);
  vector<SpecialTrait> HASH(specialTraits);
  optional<pair<ViewId, int>> HASH(cost);
  string HASH(name);
  ViewId HASH(viewId);
  vector<AttributeInfo> HASH(attributes);
  optional<int> HASH(count);
  optional<TimeInterval> HASH(timeLeft);
  int HASH(id);
  enum AutoState { AUTO_REJECT, AUTO_ACCEPT};
  optional<ImmigrantAutoState> HASH(autoState);
  optional<milliseconds> HASH(generatedTime);
  optional<Keybinding> HASH(keybinding);
  optional<TutorialHighlight> HASH(tutorialHighlight);
  size_t getHash() const;
};

class CollectiveInfo {
  public:
  CollectiveInfo() {}
  CollectiveInfo(const CollectiveInfo&) = delete;
  CollectiveInfo(CollectiveInfo&&) = default;
  CollectiveInfo& operator = (CollectiveInfo&&) = default;
  CollectiveInfo& operator = (const CollectiveInfo&) = delete;
  string HASH(warning);
  struct Button {
    ViewId HASH(viewId);
    string HASH(name);
    optional<pair<ViewId, int>> HASH(cost);
    string HASH(count);
    enum { ACTIVE, GRAY_CLICKABLE, INACTIVE} HASH(state);
    string HASH(help);
    char HASH(hotkey);
    string HASH(groupName);
    bool HASH(hotkeyOpensGroup);
    optional<TutorialHighlight> HASH(tutorialHighlight);
    HASH_ALL(viewId, name, cost, count, state, help, hotkey, groupName, hotkeyOpensGroup, tutorialHighlight)
  };
  vector<Button> HASH(buildings);
  int HASH(minionCount);
  int HASH(minionLimit);
  string HASH(monsterHeader);
  vector<CreatureInfo> HASH(minions);
  struct CreatureGroup {
    UniqueEntity<Creature>::Id HASH(creatureId);
    string HASH(name);
    ViewId HASH(viewId);
    int HASH(count);
    bool HASH(highlight);
    HASH_ALL(creatureId, name, viewId, count, highlight)
  };
  vector<CreatureGroup> HASH(minionGroups);
  vector<CreatureGroup> HASH(enemyGroups);
  struct ChosenCreatureInfo {
    UniqueEntity<Creature>::Id HASH(chosenId);
    vector<PlayerInfo> HASH(creatures);
    optional<TeamId> HASH(teamId);
    HASH_ALL(chosenId, creatures, teamId)
  };
  optional<ChosenCreatureInfo> HASH(chosenCreature);
  vector<ImmigrantDataInfo> HASH(immigration);
  vector<ImmigrantDataInfo> HASH(allImmigration);
  struct WorkshopButton {
    string HASH(name);
    ViewId HASH(viewId);
    bool HASH(active);
    bool HASH(unavailable);
    HASH_ALL(name, viewId, active, unavailable)
  };
  vector<WorkshopButton> HASH(workshopButtons);
  struct QueuedItemInfo {
    double HASH(productionState);
    ItemInfo HASH(itemInfo);
    struct UpgradeInfo {
      ViewId HASH(viewId);
      string HASH(name);
      int HASH(count);
      vector<string> HASH(description);
      HASH_ALL(viewId, name, count, description)
    };
    vector<UpgradeInfo> HASH(available);
    vector<UpgradeInfo> HASH(added);
    int HASH(maxUpgrades);
    HASH_ALL(productionState, itemInfo, available, added, maxUpgrades)
  };
  struct ChosenWorkshopInfo {
    vector<ItemInfo> HASH(options);
    vector<QueuedItemInfo> HASH(queued);
    int HASH(index);
    HASH_ALL(index, options, queued)
  };
  optional<ChosenWorkshopInfo> HASH(chosenWorkshop);
  struct LibraryInfo {
    struct TechInfo {
      TechId HASH(id);
      string HASH(description);
      bool HASH(active);
      optional<TutorialHighlight> HASH(tutorialHighlight);
      HASH_ALL(id, active, description, tutorialHighlight)
    };
    int HASH(currentProgress);
    int HASH(totalProgress);
    optional<string> HASH(warning);
    vector<TechInfo> HASH(available);
    vector<TechInfo> HASH(researched);
    HASH_ALL(currentProgress, totalProgress, warning, available, researched)
  };
  optional<LibraryInfo> HASH(libraryInfo);
  struct Resource {
    ViewId HASH(viewId);
    int HASH(count);
    string HASH(name);
    optional<TutorialHighlight> HASH(tutorialHighlight);
    HASH_ALL(viewId, count, name, tutorialHighlight)
  };
  vector<Resource> HASH(numResource);
  AvatarLevelInfo HASH(avatarLevelInfo);
  struct Team {
    TeamId HASH(id);
    vector<UniqueEntity<Creature>::Id> HASH(members);
    bool HASH(active);
    bool HASH(highlight);
    HASH_ALL(id, members, active, highlight)
  };
  vector<Team> HASH(teams);
  const CreatureInfo* getMinion(UniqueEntity<Creature>::Id) const;
  bool hasMinion(UniqueEntity<Creature>::Id);
  int HASH(nextPayout);
  int HASH(payoutTimeRemaining);

  struct Task {
    string HASH(name);
    optional<UniqueEntity<Creature>::Id> HASH(creature);
    bool HASH(priority);
    HASH_ALL(name, creature, priority)
  };
  vector<Task> HASH(taskMap);

  struct Ransom {
    pair<ViewId, int> HASH(amount);
    string HASH(attacker);
    bool HASH(canAfford);
    HASH_ALL(amount, attacker, canAfford)
  };
  optional<Ransom> HASH(ransom);

  struct NextWave {
    ViewId HASH(viewId);
    string HASH(attacker);
    TimeInterval HASH(numTurns);
    HASH_ALL(viewId, attacker, numTurns)
  };
  optional<NextWave> HASH(nextWave);
  enum class RebellionChance {
    LOW,
    MEDIUM,
    HIGH,
  };
  optional<RebellionChance> HASH(rebellionChance);
  vector<ViewId> HASH(allQuarters);
  HASH_ALL(warning, buildings, minionCount, minionLimit, monsterHeader, minions, minionGroups, enemyGroups, chosenCreature, numResource, teams, nextPayout, payoutTimeRemaining, taskMap, ransom, nextWave, chosenWorkshop, workshopButtons, immigration, allImmigration, libraryInfo, allQuarters, rebellionChance, avatarLevelInfo)
};

class VillageInfo {
  public:
  struct Village {
    optional<string> HASH(name);
    string HASH(tribeName);
    ViewId HASH(viewId);
    VillainType HASH(type);
    enum Access { ACTIVE, INACTIVE, LOCATION, NO_LOCATION };
    Access HASH(access);
    bool HASH(isConquered);
    struct ActionInfo {
      VillageAction HASH(action);
      optional<string> HASH(disabledReason);
      HASH_ALL(action, disabledReason)
    };
    struct TriggerInfo {
      string HASH(name);
      double HASH(value);
      HASH_ALL(name, value)
    };
    vector<ActionInfo> HASH(actions);
    vector<TriggerInfo> HASH(triggers);
    UniqueEntity<Collective>::Id HASH(id);
    bool HASH(attacking);
    HASH_ALL(name, tribeName, access, isConquered, actions, triggers, id, viewId, type, attacking)
  };
  int HASH(numMainVillains);
  int HASH(numConqueredMainVillains);
  set<pair<UniqueEntity<Collective>::Id, string>> HASH(dismissedInfos);
  vector<Village> HASH(villages);
  HASH_ALL(villages, numMainVillains, numConqueredMainVillains, dismissedInfos)
};

class GameSunlightInfo {
  public:
  string HASH(description);
  TimeInterval HASH(timeRemaining);
  HASH_ALL(description, timeRemaining)
};

class TutorialInfo {
  public:
  string HASH(message);
  optional<string> HASH(warning);
  bool HASH(canContinue);
  bool HASH(canGoBack);
  EnumSet<TutorialHighlight> HASH(highlights);
  vector<Vec2> HASH(highlightedSquaresHigh);
  vector<Vec2> HASH(highlightedSquaresLow);
  HASH_ALL(message, warning, canContinue, canGoBack, highlights, highlightedSquaresHigh, highlightedSquaresLow)
};

struct CurrentLevelInfo {
  string HASH(levelName);
  bool HASH(canScrollUp);
  bool HASH(canScrollDown);
  HASH_ALL(levelName, canScrollUp, canScrollDown)
};

/** Represents all the game information displayed around the map window.*/
class GameInfo {
  public:
  enum class InfoType { PLAYER, BAND, SPECTATOR} HASH(infoType) = InfoType::PLAYER;
  GlobalTime HASH(time);
  int HASH(modifiedSquares);
  int HASH(totalSquares);

  bool HASH(singleModel);
  GameInfo() {}
  GameInfo(const GameInfo&) = delete;
  GameInfo(GameInfo&&) = default;
  GameInfo& operator = (const GameInfo&) = delete;
  GameInfo& operator = (GameInfo&&) = default;

  variant<CollectiveInfo, PlayerInfo> HASH(playerInfo);
  VillageInfo HASH(villageInfo);
  GameSunlightInfo HASH(sunlightInfo);
  optional<TutorialInfo> HASH(tutorial);
  optional<CurrentLevelInfo> HASH(currentLevel);

  vector<PlayerMessage> HASH(messageBuffer);
  bool HASH(takingScreenshot) = false;
  HASH_ALL(infoType, time, playerInfo, villageInfo, sunlightInfo, messageBuffer, singleModel, modifiedSquares, totalSquares, tutorial, currentLevel, takingScreenshot)
};
