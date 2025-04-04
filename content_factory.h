#pragma once

#include "furniture_factory.h"
#include "creature_factory.h"
#include "tile_paths.h"
#include "technology.h"
#include "workshop_array.h"
#include "campaign_builder.h"
#include "custom_item_id.h"
#include "item_factory.h"

class KeyVerifier;
class BuildInfo;
class ExternalEnemy;
class ResourceDistribution;
class EnemyInfo;
class ImmigrantInfo;
class ZLevelInfo;
class BuildingInfo;

class ContentFactory {
  public:
  optional<string> readData(const GameConfig*);
  FurnitureFactory SERIAL(furniture);
  array<vector<ZLevelInfo>, 3> SERIAL(zLevels);
  vector<ResourceDistribution> SERIAL(resources);
  TilePaths SERIAL(tilePaths);
  map<EnemyId, EnemyInfo> SERIAL(enemies);
  vector<ExternalEnemy> SERIAL(externalEnemies);
  ItemFactory SERIAL(itemFactory);
  Technology SERIAL(technology);
  vector<pair<string, WorkshopArray>> SERIAL(workshopGroups);
  map<string, vector<ImmigrantInfo>> SERIAL(immigrantsData);
  vector<pair<string, vector<BuildInfo>>> SERIAL(buildInfo);
  VillainsTuple SERIAL(villains);
  GameIntros SERIAL(gameIntros);
  PlayerCreaturesInfo SERIAL(playerCreatures);
  map<CustomItemId, ItemAttributes> SERIAL(items);
  map<BuildingId, BuildingInfo> SERIAL(buildingInfo);
  void merge(ContentFactory);

  CreatureFactory& getCreatures();
  const CreatureFactory& getCreatures() const;

  ContentFactory();
  ~ContentFactory();
  ContentFactory(ContentFactory&&);

  template <class Archive>
  void serialize(Archive& ar, const unsigned int);

  private:
  CreatureFactory SERIAL(creatures);
  optional<string> readCreatureFactory(const GameConfig*, KeyVerifier*);
  optional<string> readFurnitureFactory(const GameConfig*, KeyVerifier*);
  optional<string> readVillainsTuple(const GameConfig*, KeyVerifier*);
  optional<string> readPlayerCreatures(const GameConfig*, KeyVerifier*);
  optional<string> readItems(const GameConfig*, KeyVerifier*);
  optional<string> readBuildingInfo(const GameConfig*, KeyVerifier*);
};
