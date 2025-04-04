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

#include "level_maker.h"
#include "item_factory.h"
#include "square.h"
#include "collective_builder.h"
#include "collective.h"
#include "shortest_path.h"
#include "creature.h"
#include "level_builder.h"
#include "model.h"
#include "monster_ai.h"
#include "item.h"
#include "view_id.h"
#include "furniture_type.h"
#include "furniture_factory.h"
#include "furniture.h"
#include "progress.h"
#include "file_path.h"
#include "movement_set.h"
#include "container_range.h"
#include "settlement_info.h"
#include "task.h"
#include "equipment.h"
#include "creature_group.h"
#include "resource_counts.h"
#include "content_factory.h"
#include "item_list.h"
#include "biome_id.h"
#include "build_info.h"

namespace {

void failGen() {
  throw LevelGenException();
}

void checkGen(bool b) {
  if (!b)
    failGen();
}

class Predicate {
  public:
  bool apply(LevelBuilder* builder, Vec2 pos) const {
    return predFun(builder, pos);
  }

  Vec2 getRandomPosition(LevelBuilder* builder, Rectangle area) {
    vector<Vec2> good;
    for (Vec2 v : area)
      if (apply(builder, v))
        good.push_back(v);
    if (good.empty())
      failGen();
    return builder->getRandom().choose(good);
  }

  static Predicate attrib(SquareAttrib attr) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return builder->hasAttrib(pos, attr);});
  }

  Predicate operator !() const {
    PredFun self(predFun);
    return Predicate([self] (LevelBuilder* builder, Vec2 pos) { return !self(builder, pos);});
  }

  Predicate operator && (const Predicate& p1) const {
    PredFun self(predFun);
    return Predicate([self, p1] (LevelBuilder* builder, Vec2 pos) {
        return p1.apply(builder, pos) && self(builder, pos);});
  }

  Predicate operator || (const Predicate& p1) const {
    PredFun self(predFun);
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
        return p1.apply(builder, pos) || self(builder, pos);});
  }

  static Predicate type(FurnitureType t) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
      return builder->isFurnitureType(pos, t);});
  }

  static Predicate inRectangle(Rectangle r) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
      return pos.inRectangle(r);});
  }

  static Predicate alwaysTrue() {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return true;});
  }

  static Predicate alwaysFalse() {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return false;});
  }

  static Predicate canEnter(MovementType m) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return builder->canNavigate(pos, m);});
  }

  static Predicate near8AtLeast(FurnitureType type, int count) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
      int cnt = count;
      for (auto v : pos.neighbors8())
        if (builder->isFurnitureType(v, type))
          --cnt;
      return cnt <= 0;
    });
  }

  static Predicate near4AtLeast(FurnitureType type, int count) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
      int cnt = count;
      for (auto v : pos.neighbors4())
        if (builder->isFurnitureType(v, type))
          --cnt;
      return cnt <= 0;
    });
  }

  static Predicate near4Equals(FurnitureType type, int count) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
      int cnt = count;
      for (auto v : pos.neighbors4())
        if (builder->isFurnitureType(v, type))
          --cnt;
      return cnt == 0;
    });
  }

  private:
  typedef function<bool(LevelBuilder*, Vec2)> PredFun;
  Predicate(PredFun fun) : predFun(fun) {}
  PredFun predFun;
};

class SquareChange {
  public:
  static SquareChange none() {
    return SquareChange([](LevelBuilder*, Vec2) {});
  }

  SquareChange& add(SquareChange added, double prob = 1.0) {
    auto funCopy = changeFun; // copy just the function because storing "this" leads to a crash
    changeFun = [prob, added, funCopy] (LevelBuilder* builder, Vec2 pos) {
      funCopy(builder, pos);
      if (builder->getRandom().chance(prob))
        added.changeFun(builder, pos);
    };
    return *this;
  }

  SquareChange(optional<FurnitureType> type, optional<SquareAttrib> attrib = ::none)
      : changeFun([=](LevelBuilder* builder, Vec2 pos) {
    if (type)
      builder->putFurniture(pos, *type);
    if (attrib)
      builder->addAttrib(pos, *attrib);
  }) {}

  SquareChange(FurnitureType type, optional<SquareAttrib> attrib = ::none)
      : changeFun([=](LevelBuilder* builder, Vec2 pos) {
    builder->putFurniture(pos, type);
    if (attrib)
      builder->addAttrib(pos, *attrib);
  }) {}

  SquareChange(FurnitureParams f, optional<SquareAttrib> attrib = ::none)
      : changeFun([=](LevelBuilder* builder, Vec2 pos) {
    builder->putFurniture(pos, f);
    if (attrib)
      builder->addAttrib(pos, *attrib);
  }) {}

  SquareChange(SquareAttrib attrib)
      : changeFun([=](LevelBuilder* builder, Vec2 pos) {
    builder->addAttrib(pos, attrib);
  }) {}

  SquareChange(FurnitureType f1, FurnitureType f2)
      : changeFun([=](LevelBuilder* builder, Vec2 pos) {
    builder->putFurniture(pos, f1);
    builder->putFurniture(pos, f2); }) {}

  static SquareChange reset(optional<FurnitureType> f1, optional<SquareAttrib> attrib = ::none) {
    return SquareChange([=](LevelBuilder* builder, Vec2 pos) {
      if (f1)
        builder->resetFurniture(pos, *f1);
      if (attrib)
        builder->addAttrib(pos, *attrib);
    });
  }

  static SquareChange resetOrRemove(optional<FurnitureType> f1, FurnitureLayer layer, optional<SquareAttrib> attrib = ::none) {
    return SquareChange([=](LevelBuilder* builder, Vec2 pos) {
      if (f1)
        builder->resetFurniture(pos, *f1);
      else
        builder->removeFurniture(pos, layer);
      if (attrib)
        builder->addAttrib(pos, *attrib);
    });
  }

  static SquareChange remove(FurnitureLayer layer) {
    return SquareChange([=](LevelBuilder* builder, Vec2 pos) {
      builder->removeFurniture(pos, layer);
    });
  }

  static SquareChange reset(FurnitureParams params, optional<SquareAttrib> attrib = ::none) {
    return SquareChange([=](LevelBuilder* builder, Vec2 pos) {
      builder->resetFurniture(pos, params, attrib);
    });
  }

  static SquareChange addTerritory(CollectiveBuilder* collective) {
    return SquareChange([=](LevelBuilder* builder, Vec2 pos) {
      collective->addArea(builder->toGlobalCoordinates(vector<Vec2>({pos})));
    });
  }

  void apply(LevelBuilder* builder, Vec2 pos) {
    changeFun(builder, pos);
  }

  private:
  typedef function<void(LevelBuilder*, Vec2)> ChangeFun;
  SquareChange(ChangeFun fun) : changeFun(fun) {}
  ChangeFun changeFun;

};

class Empty : public LevelMaker {
  public:
  Empty(SquareChange s) : square(s) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      square.apply(builder, v);
  }

  private:
  SquareChange square;
};

class RoomMaker : public LevelMaker {
  public:
  RoomMaker(int _numRooms,
            int _minSize, int _maxSize, 
            SquareChange wall = SquareChange::none(),
            optional<FurnitureType> _onType = none,
            PLevelMaker _roomContents = unique<Empty>(FurnitureType("FLOOR")),
            vector<PLevelMaker> _insideMakers = {},
            bool _diggableCorners = false) : 
      numRooms(_numRooms),
      minSize(_minSize),
      maxSize(_maxSize),
      wallChange(wall.add(SquareChange(SquareAttrib::ROOM_WALL))),
      onType(_onType),
      roomContents(std::move(_roomContents)),
      insideMakers(std::move(_insideMakers)),
      diggableCorners(_diggableCorners) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int spaceBetween = 0;
    Table<int> taken(area.right(), area.bottom());
    for (Vec2 v : area)
      taken[v] = onType && !builder->isFurnitureType(v, *onType);
    for (int i : Range(numRooms)) {
      Vec2 p, k;
      bool good;
      int cnt = 100;
      do {
        k = Vec2(builder->getRandom().get(minSize, maxSize), builder->getRandom().get(minSize, maxSize));
        p = Vec2(area.left() + spaceBetween + builder->getRandom().get(area.width() - k.x - 2 * spaceBetween),
                 area.top() + spaceBetween + builder->getRandom().get(area.height() - k.y - 2 * spaceBetween));
        good = true;
        for (Vec2 v : Rectangle(k.x + 2 * spaceBetween, k.y + 2 * spaceBetween))
          if (taken[p + v - Vec2(spaceBetween,spaceBetween)]) {
            good = false;
            break;
          }
      } while (!good && --cnt > 0);
      if (cnt == 0) {
        INFO << "Placed only " << i << " rooms out of " << numRooms;
        break;
      }
      for (Vec2 v : Rectangle(k))
        taken[v + p] = 1;
      for (Vec2 v : Rectangle(k - Vec2(2, 2)))
        builder->resetFurniture(p + v + Vec2(1, 1), FurnitureType("FLOOR"), SquareAttrib::ROOM);
      for (int i : Range(p.x, p.x + k.x)) {
        wallChange.apply(builder, Vec2(i, p.y));
        wallChange.apply(builder, Vec2(i, p.y + k.y - 1));
        if ((i == p.x || i == p.x + k.x - 1) && !diggableCorners) {
          builder->addAttrib(Vec2(i, p.y), SquareAttrib::NO_DIG);
          builder->addAttrib(Vec2(i, p.y + k.y - 1), SquareAttrib::NO_DIG);
        }
      }
      for (int i : Range(p.y + 1, p.y + k.y - 1)) {
        wallChange.apply(builder, Vec2(p.x, i));
        wallChange.apply(builder, Vec2(p.x + k.x - 1, i));
      }
      Rectangle inside(p.x + 1, p.y + 1, p.x + k.x - 1, p.y + k.y - 1);
      roomContents->make(builder, inside);
      if (i < insideMakers.size())
        insideMakers[i]->make(builder, inside);
      else
        for (Vec2 v : inside)
          builder->addAttrib(v, SquareAttrib::EMPTY_ROOM);
    }
  }

  private:
  int numRooms;
  int minSize;
  int maxSize;
  SquareChange wallChange;
  optional<FurnitureType> onType;
  PLevelMaker roomContents;
  vector<PLevelMaker> insideMakers;
  bool diggableCorners;
};

class Connector : public LevelMaker {
  public:
  Connector(optional<BuildingInfo::DoorInfo> door, TribeId tribe, double _diggingCost = 3,
        Predicate pred = Predicate::canEnter({MovementTrait::WALK}), optional<SquareAttrib> setAttr = none)
      : door(door), tribe(tribe), diggingCost(_diggingCost), connectPred(pred), setAttrib(setAttr) {
    CHECK(diggingCost > 0);
  }
  double getValue(LevelBuilder* builder, Vec2 pos, Rectangle area) {
    if (builder->canNavigate(pos, {MovementTrait::WALK}))
      return 1;
    if (builder->hasAttrib(pos, SquareAttrib::NO_DIG))
      return ShortestPath::infinity;
    if (builder->hasAttrib(pos, SquareAttrib::LAKE))
      return 15;
    if (builder->hasAttrib(pos, SquareAttrib::RIVER))
      return 15;
    int numCorners = 0;
    int numTotal = 0;
    for (Vec2 v : Vec2::directions8())
      if ((pos + v).inRectangle(area) && builder->canNavigate(pos + v, MovementTrait::WALK)) {
        if (abs(v.x) == abs(v.y))
          ++numCorners;
        ++numTotal;
      }
    if (numCorners == 1)
      return 1000;
    if (numTotal - numCorners > 1)
      return diggingCost + 5;
    return diggingCost;
  }

  void connect(LevelBuilder* builder, Vec2 p1, Vec2 p2, Rectangle area) {
    ShortestPath path(area,
        [builder, this, &area](Vec2 pos) { return getValue(builder, pos, area); }, 
        [p2] (Vec2 to) { return p2.dist4(to); },
        Vec2::directions4(builder->getRandom()), p1, p2);
    for (Vec2 v = p2; v != p1; v = path.getNextMove(v)) {
      if (!builder->canNavigate(v, {MovementTrait::WALK})) {
        if (auto furniture = builder->getFurniture(v, FurnitureLayer::MIDDLE)) {
          bool placeDoor = furniture->isWall() && builder->hasAttrib(v, SquareAttrib::ROOM_WALL);
          if (!furniture->getMovementSet().canEnter({MovementTrait::WALK}))
            builder->removeFurniture(v, FurnitureLayer::MIDDLE);
          if (placeDoor && door && builder->getRandom().chance(door->prob)) {
            builder->putFurniture(v, door->type, tribe);
          }
        }
        if (!builder->canNavigate(v, {MovementTrait::WALK}))
          if (builder->getFurniture(v, FurnitureLayer::GROUND)->canBuildBridgeOver())
            builder->putFurniture(v, FurnitureType("BRIDGE"));
        CHECK(builder->canNavigate(v, {MovementTrait::WALK}));
      }
      if (!path.isReachable(v))
        failGen();
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Vec2 p1, p2;
    vector<Vec2> points = area.getAllSquares().filter([&] (Vec2 v) { return connectPred.apply(builder, v);});
    if (points.size() < 2)
      return;
    for (int i : Range(30)) {
      p1 = builder->getRandom().choose(points);
      p2 = builder->getRandom().choose(points);
      if (p1 != p2)
        connect(builder, p1, p2, area);
    }
    auto dijkstraFun = [&] (Vec2 pos) {
      if (builder->canNavigate(pos, MovementTrait::WALK))
        return 1.;
      else
        return ShortestPath::infinity;};
    Table<bool> connected(area, false);
    while (1) {
      Dijkstra dijkstra(area, {p1}, 10000, dijkstraFun);
      for (Vec2 v : area)
        if (dijkstra.isReachable(v))
          connected[v] = true;
      bool found = false;
      for (Vec2 v : area)
        if (connectPred.apply(builder, v) && !connected[v]) {
          connect(builder, p1, v, area);
          p1 = v;
          found = true;
          break;
         }
      if (!found)
        break;
    }
  }
  
  private:
  optional<BuildingInfo::DoorInfo> door;
  TribeId tribe;
  double diggingCost;
  Predicate connectPred;
  optional<SquareAttrib> setAttrib;
};

namespace {
class Furnitures : public LevelMaker {
  public:
  Furnitures(Predicate pred, double _density, FurnitureListId furnitureListId, TribeId tribe, optional<SquareAttrib> setAttr = none):
      furnitureListId(furnitureListId), tribe(tribe), density(_density), predicate(pred), attr(setAttr) {
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    auto furnitureList = builder->getContentFactory()->furniture.getFurnitureList(furnitureListId);
    vector<Vec2> available;
    for (Vec2 v : area)
      if (predicate.apply(builder, v) && builder->canPutFurniture(v, FurnitureLayer::MIDDLE))
        available.push_back(v);
    for (int i : Range(max<int>(furnitureList.numUnique(), available.size() * density))) {
      checkGen(!available.empty());
      Vec2 pos = builder->getRandom().choose(available);
      builder->putFurniture(pos, furnitureList, tribe);
      if (attr)
        builder->addAttrib(pos, *attr);
      available.removeElement(pos);
    }
  }

  private:
  FurnitureListId furnitureListId;
  TribeId tribe;
  double density;
  Predicate predicate;
  optional<SquareAttrib> attr;
};
}

class Inhabitants : public LevelMaker {
  public:

  Inhabitants(InhabitantsInfo inhab, CollectiveBuilder* col, Predicate pred = Predicate::alwaysTrue()) :
      inhabitants(inhab), actorFactory(MonsterAIFactory::monster()), onPred(pred), collective(col) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (!actorFactory)
      actorFactory = MonsterAIFactory::stayInLocation(builder->toGlobalCoordinates(area));
    Table<char> taken(area.right(), area.bottom());
    auto creatures = inhabitants.generateCreatures(builder->getRandom(), &builder->getContentFactory()->getCreatures(), collective->getTribe(),
        *actorFactory);
    for (auto& minion : creatures) {
      PCreature& creature = minion.first;
      vector<Vec2> positions;
      for (auto v : area)
        if (builder->canPutCreature(v, creature.get()) && onPred.apply(builder, v))
          positions.push_back(v);
      checkGen(!positions.empty());
      auto pos = builder->getRandom().choose(positions);
      if (collective) {
        collective->addCreature(creature.get(), minion.second);
        builder->addCollective(collective);
      }
      builder->putCreature(pos, std::move(creature));
      taken[pos] = 1;
    }
  }

  private:
  InhabitantsInfo inhabitants;
  optional<MonsterAIFactory> actorFactory;
  Predicate onPred;
  CollectiveBuilder* collective = nullptr;
};

class Corpses : public LevelMaker {
  public:

  Corpses(InhabitantsInfo inhab, Predicate pred = Predicate::alwaysTrue()) : inhabitants(inhab), onPred(pred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<char> taken(area.right(), area.bottom());
    auto creatures = inhabitants.generateCreatures(builder->getRandom(), &builder->getContentFactory()->getCreatures(),
        TribeId::getMonster(), MonsterAIFactory::monster());
    for (auto& minion : creatures) {
      PCreature& creature = minion.first;
      Vec2 pos;
      int numTries = 100;
      do {
        pos = Vec2(builder->getRandom().get(area.left(), area.right()),
            builder->getRandom().get(area.top(), area.bottom()));
      } while (--numTries > 0 && (!builder->canPutItems(pos) || (!onPred.apply(builder, pos))));
      checkGen(numTries > 0);
      if (builder->getRandom().roll(10))
        builder->putItems(pos, creature->getEquipment().removeAllItems(creature.get()));
      builder->putItems(pos, creature->generateCorpse(builder->getContentFactory(), true));
      taken[pos] = 1;
    }
  }

  private:
  InhabitantsInfo inhabitants;
  Predicate onPred;
};

class Creatures : public LevelMaker {
  public:
  Creatures(CreatureGroup f, int num, MonsterAIFactory actorF, Predicate pred = Predicate::alwaysTrue()) :
      creatures(f), numCreatures(num), actorFactory(actorF), onPred(pred) {}

  Creatures(CreatureList f, TribeId t, MonsterAIFactory actorF, Predicate pred = Predicate::alwaysTrue()) :
      creatures(f), tribe(t), actorFactory(actorF), onPred(pred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (!actorFactory)
      actorFactory = MonsterAIFactory::stayInLocation(builder->toGlobalCoordinates(area));
    Table<char> taken(area.right(), area.bottom());
    vector<PCreature> c = creatures.visit(
        [&](CreatureGroup c){
          vector<PCreature> ret;
          for (int i : Range(numCreatures))
            ret.push_back(c.random(&builder->getContentFactory()->getCreatures(), *actorFactory));
          return ret;
        },
        [&](const CreatureList& c){
          return c.generate(builder->getRandom(), &builder->getContentFactory()->getCreatures(), tribe, *actorFactory);
        }
    );
    for (auto& creature : c) {
      Vec2 pos;
      int numTries = 100;
      do {
        pos = Vec2(builder->getRandom().get(area.left(), area.right()),
            builder->getRandom().get(area.top(), area.bottom()));
      } while (--numTries > 0 && (!builder->canPutCreature(pos, creature.get()) || (!onPred.apply(builder, pos))));
      checkGen(numTries > 0);
      builder->putCreature(pos, std::move(creature));
      taken[pos] = 1;
    }
  }

  private:
  variant<CreatureGroup, CreatureList> creatures;
  int numCreatures;
  TribeId tribe;
  optional<MonsterAIFactory> actorFactory;
  Predicate onPred;
};

class Items : public LevelMaker {
  public:
  Items(variant<ItemListId, ItemType> items, int minc, int maxc, Predicate pred = Predicate::alwaysTrue(),
      bool _placeOnFurniture = false) :
      items(items), minItem(minc), maxItem(maxc), predicate(pred), placeOnFurniture(_placeOnFurniture) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int numItem = builder->getRandom().get(minItem, maxItem);
    vector<Vec2> available;
    for (auto v : area)
      if (predicate.apply(builder, v) && builder->canNavigate(v, MovementTrait::WALK) &&
          (placeOnFurniture || !builder->getFurniture(v, FurnitureLayer::MIDDLE)))
        available.push_back(v);
    checkGen(!available.empty());
    auto itemList = getItems(builder);
    for (int i : Range(numItem))
      builder->putItems(builder->getRandom().choose(available), itemList.random(builder->getContentFactory()));
  }

  ItemList getItems(const LevelBuilder* builder) {
    return items.visit(
        [&](const ItemListId list) {
          return builder->getContentFactory()->itemFactory.get(list);
        },
        [&](const ItemType& item) {
          return ItemList({item});
        }
    );
  }

  private:
  variant<ItemListId, ItemType> items;
  int minItem;
  int maxItem;
  Predicate predicate;
  bool placeOnFurniture;
};

class River : public LevelMaker {
  public:
  River(int _width, FurnitureType type) : width(_width), furnitureType(type){}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int wind = 5;
    int middle = (area.left() + area.right()) / 2;
    int px = builder->getRandom().get(middle - wind, middle + width);
    int kx = px + builder->getRandom().get(-wind, wind); // builder->getRandom().get(area.left(), area.right()) - width;
    if (kx < 0)
      kx = 0;
    if (kx >= area.right() - width)
      kx = area.right() - width - 1;
    int tot = 5;
    for (int h : Range(tot)) {
      int height = area.top() * (tot - h) / tot + area.bottom() * h / tot;
      int height2 = area.top() * (tot - h - 1) / tot + area.bottom() * (h + 1) / tot;
      vector<Vec2> line = straightLine(px, height, kx, (h == tot - 1) ? area.bottom() : height2);
      for (Vec2 v : line)
        for (int i : Range(width)) {
          Vec2 pos = v + Vec2(i, 0);
          builder->resetFurniture(pos, furnitureType, SquareAttrib::RIVER);
        }
      px = kx;
      kx = px + builder->getRandom().get(-wind, wind);
      if (kx < 0)
        kx = 0;
      if (kx >= area.right() - width)
        kx = area.right() - width - 1;
    }
  }

  private:

  vector<Vec2> straightLine(int x0, int y0, int x1, int y1){
    INFO << "Line " << x1 << " " << y0 << " " << x1 << " " << y1;
    int dx = x1 - x0;
    int dy = y1 - y0;
    vector<Vec2> ret{ Vec2(x0, y0)};
    if (abs(dx) > abs(dy)) {          // slope < 1
      double m = (double) dy / (double) dx;      // compute slope
      double b = y0 - m*x0;
      dx = (dx < 0) ? -1 : 1;
      while (x0+dx != x1) {
        x0 += dx;
        ret.push_back(Vec2(x0,(int)roundf(m*x0+b)));
      }
    } else
      if (dy != 0) {                              // slope >= 1
        double m = (double) dx / (double) dy;      // compute slope
        double b = x0 - m*y0;
        dy = (dy < 0) ? -1 : 1;
        while (y0+dy != y1) {
          y0 += dy;
          ret.push_back(Vec2((int)round(m*y0+b),y0));
        }
      }
    return ret;
  }

  int width;
  FurnitureType furnitureType;
};

class MountainRiver : public LevelMaker {
  public:
  MountainRiver(int num, Predicate startPred, optional<FurnitureType> waterType)
    : number(num), startPredicate(startPred), waterType(waterType) {}

  optional<Vec2> fillLake(LevelBuilder* builder, set<Vec2>& waterTiles, Rectangle area, Vec2 pos) {
    vector<Vec2> ret;
    double height = builder->getHeightMap(pos);
    queue<Vec2> q;
    set<Vec2> visited {pos};
    map<Vec2, Vec2> predecessor {{ pos, pos}};
    q.push(pos);
    while (!q.empty()) {
      pos = q.front();
      q.pop();
      for (Vec2 v : pos.neighbors8(builder->getRandom()))
        if (v.inRectangle(area) && !visited.count(v)) {
          visited.insert(v);
          predecessor[v] = pos;
          if (fabs(builder->getHeightMap(v) - height) < 0.000001)
            q.push(v);
          else
          if (builder->getHeightMap(v) < height)
            ret.push_back(v);
        }
    }
    if (builder->getRandom().roll(predecessor.size()) || ret.empty()) {
      for (auto& elem : predecessor)
        if (!ret.contains(elem.first))
          waterTiles.insert(elem.first);
      if (ret.empty())
        return none;
      else
        return builder->getRandom().choose(ret);
    } else {
      Vec2 end = builder->getRandom().choose(ret);
      for (Vec2 v = predecessor.at(end);; v = predecessor.at(v)) {
        waterTiles.insert(v);
        if (v == predecessor.at(v))
          break;
      }
      return end;
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    set<Vec2> allWaterTiles;
    for (int i : Range(number)) {
      set<Vec2> waterTiles;
      Vec2 pos = startPredicate.getRandomPosition(builder, area);
      int width = builder->getRandom().get(3, 6);
      while (1) {
        if (builder->hasAttrib(pos, SquareAttrib::RIVER))
          break;
        if (auto next = fillLake(builder, waterTiles, area, pos))
          pos = *next;
        else
          break;
      }
      for (Vec2 v : waterTiles)
        for (Vec2 v2 : Rectangle(v, v + Vec2(width, width)))
          allWaterTiles.insert(v2);
    }
    for (auto layer : Iter(Vec2::calculateLayers(allWaterTiles))) {
      for (Vec2 v : *layer)
        if (v.inRectangle(area) && !builder->hasAttrib(v, SquareAttrib::RIVER)) {
          builder->addAttrib(v, SquareAttrib::RIVER);
          builder->resetFurniture(v, getWaterType(builder, v, layer.index()));
        }
    }
  }

  FurnitureType getWaterType(LevelBuilder* builder, Vec2 pos, int numLayer) {
    if (waterType)
      return *waterType;
    if (builder->hasAttrib(pos, SquareAttrib::MOUNTAIN))
      return builder->getContentFactory()->furniture.getWaterType(100);
    else if (numLayer == 0)
      return FurnitureType("SAND");
    else
      return builder->getContentFactory()->furniture.getWaterType(1.1 * (numLayer - 1));
  }

  private:
  int number;
  Predicate startPredicate;
  optional<FurnitureType> waterType;
};

class Blob : public LevelMaker {
  public:
  Blob(double _insideRatio = 0.333) : insideRatio(_insideRatio) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) = 0;

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> squares;
    Table<char> isInside(area, 0);
    Vec2 center = area.middle();
    squares.push_back(center);
    isInside[center] = 1;
    for (int i : Range(area.width() * area.height() * insideRatio)) {
      vector<Vec2> nextPos;
      for (auto pos : squares)
        for (Vec2 next : pos.neighbors4())
          if (next.inRectangle(area) && !squares.contains(next))
            nextPos.push_back(next);
      vector<double> probs = nextPos.transform([&](Vec2 v) {
          double px = std::abs(v.x - center.x);
          double py = std::abs(v.y - center.y);
          py *= area.width();
          py /= area.height();
          double coeff = -1.0 + 1.0 / (sqrt(px * px + py * py) / sqrt(2 * area.width() * area.width()));
          CHECK(coeff > 0.0);
          return coeff;
        });
      Vec2 chosen = builder->getRandom().choose(nextPos, probs);
      isInside[chosen] = 1;
      squares.push_back(chosen);
    }
    queue<Vec2> q;
    int inf = 10000;
    Table<int> distance(area, inf);
    for (Vec2 v : isInside.getBounds())
      if (!isInside[v]) {
        distance[v] = 0;
        q.push(v);
      }
    while (!q.empty()) {
      Vec2 pos = q.front();
      q.pop();
      for (Vec2 v : pos.neighbors8())
        if (v.inRectangle(area) && distance[v] == inf) {
          distance[v] = distance[pos] + 1;
          q.push(v);
          addSquare(builder, v, distance[v]);
        }
    }
  }

  private:
  double insideRatio;
};

class UniformBlob : public Blob {
  public:
  UniformBlob(SquareChange insideSquare, optional<SquareChange> borderSquare = none,
      double insideRatio = 0.3333) : Blob(insideRatio),
      inside(insideSquare), border(borderSquare) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    if (edgeDist == 1 && border)
      border->apply(builder, pos);
    else
      inside.apply(builder, pos);
  }

  private:
  SquareChange inside;
  optional<SquareChange> border;
};

class FurnitureBlob : public Blob {
  public:
  FurnitureBlob(SquareChange in, double insideRatio = 0.3333) : Blob(insideRatio), inside(in) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    inside.apply(builder, pos);
  }

  private:
  SquareChange inside;
};

class Lake : public Blob {
  public:
  Lake(optional<SquareChange> sandType) : sandType(sandType) {}
  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    builder->addAttrib(pos, SquareAttrib::LAKE);
    if (sandType && edgeDist == 1/* && !builder->isFurnitureType(pos, waterType)*/)
      sandType->apply(builder, pos);
    else {
      if (!!sandType)
        --edgeDist;
      builder->resetFurniture(pos, builder->getContentFactory()->furniture.getWaterType(double(edgeDist) / 2));
    }
  }

  private:
  optional<SquareChange> sandType;
};

class RemoveFurniture : public LevelMaker {
  public:
  RemoveFurniture(FurnitureLayer l) : layer(l) {
    CHECK(layer != FurnitureLayer::GROUND);
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      builder->removeFurniture(v, layer);
  }

  private:
  FurnitureLayer layer;
};

class Buildings : public LevelMaker {
  public:
  Buildings(int minBuildings, int maxBuildings,
      int minSize, int maxSize,
      BuildingInfo building,
      TribeId tribe,
      bool align,
      vector<PLevelMaker> insideMakers,
      bool roadConnection = true) :
    minBuildings(minBuildings),
    maxBuildings(maxBuildings),
    minSize(minSize), maxSize(maxSize),
    align(align),
    building(building),
    tribe(tribe),
    insideMakers(std::move(insideMakers)),
    roadConnection(roadConnection) {
      CHECK(insideMakers.size() <= minBuildings);
    }

  Buildings(int minBuildings, int maxBuildings,
      int minSize, int maxSize,
      BuildingInfo building,
      TribeId tribe,
      bool align,
      PLevelMaker insideMaker,
      bool roadConnection = true) : Buildings(minBuildings, maxBuildings, minSize, maxSize, building, tribe, align,
        insideMaker ? makeVec<PLevelMaker>(std::move(insideMaker)) : vector<PLevelMaker>(), roadConnection) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<bool> filled(area);
    int width = area.width();
    int height = area.height();
    for (Vec2 v : area)
      filled[v] =  0;
    int spaceBetween = 1;
    int alignHeight = 0;
    if (align) {
      alignHeight = height / 2 - 2 + builder->getRandom().get(5);
    }
    int nextw = -1;
    int numBuildings = builder->getRandom().get(minBuildings, maxBuildings);
    for (int i = 0; i < numBuildings; ++i) {
      bool spaceOk = true;
      int w, h, px, py;
      int cnt = 10000;
      bool buildingRow;
      do {
        buildingRow = builder->getRandom().get(2);
        spaceOk = true;
        w = builder->getRandom().get(minSize, maxSize);
        h = builder->getRandom().get(minSize, maxSize);
        if (nextw > -1 && nextw + w < area.right()) {
          px = nextw;
          nextw = -1;
        } else
          px = area.left() + builder->getRandom().get(width - w - 2 * spaceBetween + 1) + spaceBetween;
        if (!align)
          py = area.top() + builder->getRandom().get(height - h - 2 * spaceBetween + 1) + spaceBetween;
        else {
          py = area.top() + (buildingRow == 1 ? alignHeight - h - 1 : alignHeight + 2);
          if (py + h >= area.bottom() || py < area.top()) {
            spaceOk = false;
            continue;
          }
        }
        Vec2 tmp(px - spaceBetween, py - spaceBetween);
        for (Vec2 v : Rectangle(w + spaceBetween * 2 + 1, h + spaceBetween * 2 + 1))
          if (!(tmp + v).inRectangle(area) || filled[px + v.x - spaceBetween][py + v.y - spaceBetween]) {
            spaceOk = false;
            break;
          }
      } while (!spaceOk && --cnt > 0);
      if (cnt == 0) {
        if (i < minBuildings)
          failGen(); // "Failed to add " << minBuildings - i << " buildings out of " << minBuildings;
        else
          break;
      }
      if (builder->getRandom().roll(1))
        nextw = px + w;
      for (Vec2 v : Rectangle(w + 1, h + 1)) {
        filled[Vec2(px, py) + v] = true;
        if (v.x == w || v.x == 0 || v.y == h || v.y == 0)
          builder->putFurniture(Vec2(px, py) + v, building.wall);
        builder->setBuilding(Vec2(px, py) + v, true);
      }
      for (Vec2 v : Rectangle(w - 1, h - 1)) {
        auto pos = Vec2(px + 1, py + 1) + v;
        if (building.floorInside)
          builder->resetFurniture(pos, *building.floorInside);
        builder->addAttrib(pos, SquareAttrib::ROOM);
      }
      Vec2 doorLoc = align ? 
            // if the building is large enough, don't place door near the corner
          Vec2(px + (w >= 4 ? builder->getRandom().get(2, w - 1) : builder->getRandom().get(1, w)),
               py + (buildingRow * h)) :
          getRandomExit(Random, Rectangle(px, py, px + w + 1, py + h + 1), (w >= 4 && h >= 4) ? 2 : 1);
      if (building.floorInside)
        builder->resetFurniture(doorLoc, *building.floorInside);
      if (building.door)
        builder->putFurniture(doorLoc, building.door->type, tribe);
      Rectangle inside(px + 1, py + 1, px + w, py + h);
      if (i < insideMakers.size()) 
        insideMakers[i]->make(builder, inside);
      else
        for (Vec2 v : inside)
          builder->addAttrib(v, SquareAttrib::EMPTY_ROOM);
    }
    if (align)
      for (Vec2 v : Rectangle(area.left() + area.width() / 3, area.top() + alignHeight,
            area.right() - area.width() / 3, area.top() + alignHeight + 2))
        builder->addAttrib(v, SquareAttrib::BUILDINGS_CENTER);
    if (roadConnection) {
      Vec2 pos = Vec2((area.left() + area.right()) / 2, area.top() + alignHeight);
      builder->removeFurniture(pos, FurnitureLayer::MIDDLE);
      builder->putFurniture(pos, FurnitureParams{FurnitureType("ROAD"), TribeId::getMonster()});
      builder->addAttrib(pos, SquareAttrib::CONNECT_ROAD);
    }
  }

  private:
  int minBuildings;
  int maxBuildings;
  int minSize;
  int maxSize;
  bool align;
  BuildingInfo building;
  TribeId tribe;
  vector<PLevelMaker> insideMakers;
  bool roadConnection;
};

DEF_UNIQUE_PTR(MakerQueue);

class MakerQueue : public LevelMaker {
  public:
  MakerQueue() = default;
  MakerQueue(vector<PLevelMaker> _makers) : makers(std::move(_makers)) {}

  template <typename T1, typename... Args>
  MakerQueue(T1&& t1, Args&&... args) : MakerQueue(makeVec<PLevelMaker>(std::move(t1), std::move(args)...)) {}

  void addMaker(PLevelMaker maker) {
    makers.push_back(std::move(maker));
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (auto& maker : makers)
      maker->make(builder, area);
  }

  private:
  vector<PLevelMaker> makers;
};

class PredicatePrecalc {
  public:
  PredicatePrecalc(const Predicate& predicate, LevelBuilder* builder, Rectangle area)
      : counts(Rectangle(area.topLeft(), area.bottomRight() + Vec2(1, 1))) {
    int px = counts.getBounds().left();
    int py = counts.getBounds().top();
    for (int x : Range(px, counts.getBounds().right()))
      counts[x][py] = 0;
    for (int y : Range(py, counts.getBounds().bottom()))
      counts[px][y] = 0;
    for (Vec2 v : Rectangle(area.topLeft() + Vec2(1, 1), counts.getBounds().bottomRight()))
      counts[v] = (predicate.apply(builder, v - Vec2(1, 1)) ? 1 : 0) +
        counts[v.x - 1][v.y] + counts[v.x][v.y - 1] -counts[v.x - 1][v.y - 1];
  }

  int getCount(Rectangle area) const {
    return counts[area.bottomRight()] + counts[area.topLeft()]
      -counts[area.bottomLeft()] - counts[area.topRight()];
  }

  private:
  Table<int> counts;
};

class RandomLocations : public LevelMaker {
  public:
  RandomLocations(vector<PLevelMaker> _insideMakers, const vector<pair<int, int>>& _sizes, Predicate pred)
      : insideMakers(std::move(_insideMakers)), sizes(_sizes), predicate(sizes.size(), pred) {
    CHECK(insideMakers.size() == sizes.size());
    CHECK(predicate.size() == sizes.size());
  }

  class LocationPredicate {
    public:
    LocationPredicate(Predicate main, Predicate sec, int minSec, int maxSec)
      // main and sec must be mutually exclusive!!!
        : predicate(main), second(sec), minSecond(minSec), maxSecond(maxSec) {
    }

    LocationPredicate(Predicate p) : predicate(p) {}

    class Precomputed {
      public:
      Precomputed(LevelBuilder* builder, Rectangle area, Predicate p1, Predicate p2, int minSec, int maxSec)
        : pred1(p1, builder, area), pred2(p2, builder, area), minSecond(minSec), maxSecond(maxSec) {
      }

      bool apply(Rectangle rect) const {
        int numFirst = pred1.getCount(rect);
        int numSecond = pred2.getCount(rect);
        return numSecond >= minSecond && numSecond < maxSecond && numSecond + numFirst == rect.width() * rect.height();
      }

      private:
      PredicatePrecalc pred1;
      PredicatePrecalc pred2;
      int minSecond;
      int maxSecond;
    };

    Precomputed precompute(LevelBuilder* builder, Rectangle area) {
      return Precomputed(builder, area, predicate, second, minSecond, maxSecond);
    }

    private:
    Predicate predicate;
    Predicate second = Predicate::alwaysFalse();
    int minSecond = 0;
    int maxSecond = 1;
  };

  RandomLocations() {}

  void add(PLevelMaker maker, Vec2 size, LocationPredicate pred) {
    insideMakers.push_back(std::move(maker));
    sizes.push_back({size.x, size.y});
    predicate.push_back(pred);
  }

  void setMinDistance(LevelMaker* m1, LevelMaker* m2, double dist) {
    minDistance[{m1, m2}] = dist;
    minDistance[{m2, m1}] = dist;
  }

  void setMaxDistance(LevelMaker* m1, LevelMaker* m2, double dist) {
    maxDistance[{m1, m2}] = dist;
    maxDistance[{m2, m1}] = dist;
  }

  void setMinMargin(LevelMaker *m1, int margin) {
    minMargin[m1] = margin;
  }

  void setMinDistanceLast(LevelMaker* m, double dist) {
    minDistance[{m, insideMakers.back().get()}]  = dist;
    minDistance[{insideMakers.back().get(), m}]  = dist;
  }

  void setMaxDistanceLast(LevelMaker* m, double dist) {
    maxDistance[{m, insideMakers.back().get()}] = dist;
    maxDistance[{insideMakers.back().get(), m}] = dist;
  }

  void setCanOverlap(LevelMaker* m) {
    overlapping.insert(m);
  }

  LevelMaker* getLast() {
    return insideMakers.back().get();
  }


  virtual void make(LevelBuilder* builder, Rectangle area) override {
    PROFILE;
    checkConsistency();
    vector<vector<Vec2>> allowedPositions;
    vector<LevelBuilder::Rot> rotations;
    {
      PROFILE_BLOCK("precomputing");
      for (int i : All(insideMakers)) {
        rotations.push_back(builder->getRandom().choose(
              LevelBuilder::CW0, LevelBuilder::CW1, LevelBuilder::CW2, LevelBuilder::CW3));
        auto maker = insideMakers[i].get();
        auto precomputed = predicate[i].precompute(builder, area);
        vector<Vec2> pos;
        const int margin = getValueMaybe(minMargin, maker).value_or(0);
        int width = sizes[i].first;
        int height = sizes[i].second;
        if (contains({LevelBuilder::CW1, LevelBuilder::CW3}, rotations[i]))
          std::swap(width, height);
        for (int x : Range(area.left() + margin, area.right() - margin - width))
          for (int y : Range(area.top() + margin, area.bottom() - margin - height))
            if (precomputed.apply(Rectangle(x, y, x + width, y + height)))
              pos.push_back(Vec2(x, y));
        allowedPositions.push_back(pos);
      }
    }
    {
      PROFILE_BLOCK("generating positions");
      for (int i : Range(300))
        if (tryMake(builder, allowedPositions, rotations))
          return;
      failGen(); // "Failed to find free space for " << (int)sizes.size() << " areas";
    }
  }

  bool checkDistances(int makerIndex, Rectangle area, const vector<Rectangle>& occupied,
      const vector<optional<double>>& minDist, const vector<optional<double>>& maxDist) {
    for (int j : Range(makerIndex)) {
      auto distance = area.getDistance(occupied[j]);
      if ((maxDist[j] && *maxDist[j] < distance) || (minDist[j] && *minDist[j] > distance))
        return false;
    }
    return true;
  }

  bool checkIntersections(Rectangle area, const vector<Rectangle>& occupied) {
    for (Rectangle r : occupied)
      if (r.intersects(area))
        return false;
    return true;
  }

  bool tryMake(LevelBuilder* builder, const vector<vector<Vec2>>& allowedPositions,
      const vector<LevelBuilder::Rot>& rotations) {
    PROFILE;
    vector<Rectangle> occupied;
    vector<Rectangle> makerBounds;
    for (int makerIndex : All(insideMakers)) {
      PROFILE_BLOCK("maker");
      auto maker = insideMakers[makerIndex].get();
      bool canOverlap = overlapping.count(maker);
      int width = sizes[makerIndex].first;
      int height = sizes[makerIndex].second;
      if (contains({LevelBuilder::CW1, LevelBuilder::CW3}, rotations[makerIndex]))
        std::swap(width, height);
      vector<optional<double>> maxDist(makerIndex);
      vector<optional<double>> minDist(makerIndex);
      for (int j : Range(makerIndex)) {
        maxDist[j] = getValueMaybe(maxDistance, make_pair(insideMakers[j].get(), maker));
        minDist[j] = getValueMaybe(minDistance, make_pair(insideMakers[j].get(), maker));
      }
      auto findGoodPosition = [&] () -> optional<Vec2> {
        for (auto& pos : builder->getRandom().permutation(allowedPositions[makerIndex])) {
          Progress::checkIfInterrupted();
          Rectangle area(pos, pos + Vec2(width, height));
          if ((canOverlap || checkIntersections(area, occupied)) &&
              checkDistances(makerIndex, area, occupied, minDist, maxDist)) {
            return pos;
          }
        }
        return none;
      };
      if (auto pos = findGoodPosition()) {
        occupied.push_back(Rectangle(*pos, *pos + Vec2(width, height)));
        makerBounds.push_back(Rectangle(*pos, *pos + Vec2(sizes[makerIndex].first, sizes[makerIndex].second)));
      } else
        return false;
    }
    CHECK(insideMakers.size() == occupied.size());
    for (int i : All(insideMakers)) {
      PROFILE_BLOCK("insider makers");
      builder->pushMap(makerBounds[i], rotations[i]);
      insideMakers[i]->make(builder, makerBounds[i]);
      builder->popMap();
    }
    return true;
  }

  void checkConsistency() const {
    auto check = [&] (LevelMaker* maker) {
      for (auto& elem : insideMakers)
        if (elem.get() == maker)
          return;
      FATAL << "LevelMaker not found";
    };
    for (auto& elem : overlapping)
      check(elem);
    for (auto& elem : minDistance) {
      check(elem.first.first);
      check(elem.first.second);
    }
    for (auto& elem : maxDistance) {
      check(elem.first.first);
      check(elem.first.second);
    }
    for (auto& elem : minMargin)
      check(elem.first);
  }

  private:
  vector<PLevelMaker> insideMakers;
  vector<pair<int, int>> sizes;
  vector<LocationPredicate> predicate;
  set<LevelMaker*> overlapping;
  map<pair<LevelMaker*, LevelMaker*>, double> minDistance;
  map<pair<LevelMaker*, LevelMaker*>, double> maxDistance;
  map<LevelMaker*, int> minMargin;
};

class Margin : public LevelMaker {
  public:
  Margin(int s, PLevelMaker in) : left(s), top(s), right(s), bottom(s), inside(std::move(in)) {}
  Margin(int _left, int _top, int _right, int _bottom, PLevelMaker in)
      :left(_left) ,top(_top), right(_right), bottom(_bottom), inside(std::move(in)) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    CHECK(area.width() > left + right && area.height() > top + bottom);
    inside->make(builder, Rectangle(
          area.left() + left,
          area.top() + top,
          area.right() - right,
          area.bottom() - bottom));
  }

  private:
  int left, top, right, bottom;
  PLevelMaker inside;
};

void addAvg(int x, int y, const Table<double>& wys, double& avg, int& num) {
  Vec2 pos(x, y);
  if (pos.inRectangle(wys.getBounds())) {
    avg += wys[pos];
    ++num;
  }
}

struct NoiseInit {
  int topLeft;
  int topRight;
  int bottomRight;
  int bottomLeft;
  int middle;
};

Table<double> genNoiseMap(RandomGen& random, Rectangle area, NoiseInit init, double varianceMult) {
  int width = 1;
  while (width < area.width() - 1 || width < area.height() - 1)
    width *= 2;
  width /= 2;
  ++width;
  Table<double> wys(width, width);
  wys[0][0] = init.topLeft;
  wys[width - 1][0] = init.topRight;
  wys[width - 1][width - 1] = init.bottomRight;
  wys[0][width - 1] = init.bottomLeft;
  wys[(width - 1) / 2][(width - 1) / 2] = init.middle;

  double variance = 0.5;
  double heightDiff = 0.1;
  for (int a = width - 1; a >= 2; a /= 2) {
    if (a < width - 1)
      for (Vec2 pos1 : Rectangle((width - 1) / a, (width - 1) / a)) {
        Vec2 pos = pos1 * a;
        double avg = (wys[pos] + wys[pos.x + a][pos.y] + wys[pos.x][pos.y + a] + wys[pos.x + a][pos.y + a]) / 4;
        wys[pos.x + a / 2][pos.y + a / 2] =
            avg + variance * (random.getDouble() * 2 - 1);
      }
    for (Vec2 pos1 : Rectangle((width - 1) / a, (width - 1) / a + 1)) {
      Vec2 pos = pos1 * a;
      double avg = 0;
      int num = 0;
      addAvg(pos.x + a / 2, pos.y - a / 2, wys, avg, num);
      addAvg(pos.x, pos.y, wys, avg, num);
      addAvg(pos.x + a, pos.y, wys, avg, num);
      addAvg(pos.x + a / 2, pos.y + a / 2, wys, avg, num);
      wys[pos.x + a / 2][pos.y] =
          avg / num + variance * (random.getDouble() * 2 - 1);
    }
    for (Vec2 pos1 : Rectangle((width - 1) / a + 1, (width - 1) / a)) {
      Vec2 pos = pos1 * a;
      double avg = 0;
      int num = 0;
      addAvg(pos.x - a / 2, pos.y + a / 2, wys, avg, num);
      addAvg(pos.x, pos.y, wys, avg, num);
      addAvg(pos.x, pos.y + a , wys, avg, num);
      addAvg(pos.x + a / 2, pos.y + a / 2, wys, avg, num);
      wys[pos.x][pos.y + a / 2] =
          avg / num + variance * (random.getDouble() * 2 - 1);
    }
    variance *= varianceMult;
  }
  Table<double> ret(area);
  Vec2 offset(area.left(), area.top());
  for (Vec2 v : area) {
    Vec2 lv((v.x - offset.x) * width / area.width(), (v.y - offset.y) * width / area.height());
    ret[v] = wys[lv];
  }
  return ret;
}

void raiseLocalMinima(Table<double>& t) {
  Vec2 minPos = t.getBounds().topLeft();
  for (Vec2 v : t.getBounds())
    if (t[v] < t[minPos])
      minPos = v;
  Table<bool> visited(t.getBounds(), false);
  auto comparator = [&](const Vec2& v1, const Vec2& v2) { return t[v1] > t[v2];};
  priority_queue<Vec2, vector<Vec2>, decltype(comparator)> q(comparator);
  q.push(minPos);
  visited[minPos] = true;
  while (!q.empty()) {
    Vec2 pos = q.top();
    q.pop();
    for (Vec2 v : pos.neighbors4())
      if (v.inRectangle(t.getBounds()) && !visited[v]) {
        if (t[v] < t[pos])
          t[v] = t[pos];
        q.push(v);
        visited[v] = true;
      }
  }
}

vector<double> sortedValues(const Table<double>& t) {
  vector<double> values;
  for (Vec2 v : t.getBounds()) {
    values.push_back(t[v]);
  }
  std::sort(values.begin(), values.end());
  return values;
}

class SetSunlight : public LevelMaker {
  public:
  SetSunlight(double a, Predicate p) : amount(a), pred(p) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      if (pred.apply(builder, v))
        builder->setSunlight(v, amount);
  }

  private:
  double amount;
  Predicate pred;
};

static void removeEdge(Table<bool>& values, int thickness) {
  auto bounds = values.getBounds();
  Table<int> distance(bounds, 1000000);
  queue<Vec2> q;
  for (auto v : bounds)
    if (!values[v]) {
      distance[v] = 0;
      q.push(v);
    }
  while (!q.empty()) {
    auto v = q.front();
    q.pop();
    if (distance[v] <= thickness)
      values[v] = false;
    else
      break;
    for (auto neighbor : v.neighbors4())
      if (neighbor.inRectangle(bounds) && distance[neighbor] > distance[v] + 1) {
        distance[neighbor] = distance[v] + 1;
        q.push(neighbor);
      }
  }
}

class Mountains : public LevelMaker {
  public:
  static constexpr double varianceM = 0.45;
  Mountains(double lowland, double hill, NoiseInit init, TribeId tribe, FurnitureType hillType, FurnitureType grassType,
      FurnitureType mountainType, FurnitureType mountainFloorType)
      : ratioLowland(lowland), ratioHill(hill), noiseInit(init), varianceMult(varianceM), tribe(tribe), hillType(hillType),
        grassType(grassType), mountainType(mountainType), mountainFloorType(mountainFloorType) {
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<double> wys = genNoiseMap(builder->getRandom(), area, noiseInit, varianceMult);
    raiseLocalMinima(wys);
    vector<double> values = sortedValues(wys);
    double cutOffLowland = values[(int)(ratioLowland * double(values.size() - 1))];
    double cutOffHill = values[(int)((ratioHill + ratioLowland) * double(values.size() - 1))];
    double cutOffDarkness = values[(int)((ratioHill + ratioLowland + 1.0) * 0.5 * double(values.size() - 1))];
    int dCnt = 0, mCnt = 0, hCnt = 0, lCnt = 0;
    Table<bool> isMountain(area, false);
    for (Vec2 v : area) {
      builder->setHeightMap(v, wys[v]);
      if (wys[v] >= cutOffHill) {
        isMountain[v] = true;
        builder->putFurniture(v, mountainFloorType);
        builder->putFurniture(v, {mountainType, tribe}, SquareAttrib::MOUNTAIN);
        builder->setSunlight(v, max(0.0, 1. - (wys[v] - cutOffHill) / (cutOffDarkness - cutOffHill)));
        builder->setCovered(v, true);
        ++mCnt;
      }
      else if (wys[v] >= cutOffLowland) {
        builder->putFurniture(v, hillType, SquareAttrib::HILL);
        ++hCnt;
      }
      else {
        builder->putFurniture(v, grassType, SquareAttrib::LOWLAND);
        ++lCnt;
      }
    }
    // Remove the MOUNTAIN2 tiles that are to close to the edge of the mountain
    removeEdge(isMountain, 20);
    for (auto v : area)
      if (isMountain[v])
        builder->putFurniture(v, {FurnitureType("MOUNTAIN2"), tribe}, SquareAttrib::MOUNTAIN);
    INFO << "Terrain distribution " << dCnt << " darkness, " << mCnt << " mountain, " << hCnt << " hill, " << lCnt << " lowland";
  }

  private:
  double ratioLowland;
  double ratioHill;
  NoiseInit noiseInit;
  double varianceMult;
  TribeId tribe;
  FurnitureType hillType;
  FurnitureType grassType;
  FurnitureType mountainType;
  FurnitureType mountainFloorType;
};

class Roads : public LevelMaker {
  public:
  Roads() {}

  bool makeBridge(LevelBuilder* builder, Vec2 pos) {
    return !builder->canNavigate(pos, {MovementTrait::WALK}) && builder->canNavigate(pos, {MovementTrait::SWIM});
  }

  double getValue(LevelBuilder* builder, Vec2 pos) {
    if ((!builder->canNavigate(pos, MovementType({MovementTrait::WALK, MovementTrait::SWIM})) &&
         !builder->hasAttrib(pos, SquareAttrib::ROAD_CUT_THRU)) ||
        builder->hasAttrib(pos, SquareAttrib::NO_ROAD))
      return ShortestPath::infinity;
    if (makeBridge(builder, pos))
      return 10;
    if (builder->isFurnitureType(pos, FurnitureType("ROAD")) || builder->isFurnitureType(pos, FurnitureType("BRIDGE")))
      return 1;
    return 1 + pow(1 + builder->getHeightMap(pos), 2);
  }

  FurnitureType getRoadType(LevelBuilder* builder, Vec2 pos) {
    if (makeBridge(builder, pos))
      return FurnitureType("BRIDGE");
    else
      return FurnitureType("ROAD");
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> points;
    for (Vec2 v : area)
      if (builder->hasAttrib(v, SquareAttrib::CONNECT_ROAD)) {
        points.push_back(v);
        INFO << "Connecting point " << v;
      }
    for (int ind : Range(1, points.size())) {
      Vec2 p1 = points[ind];
      Vec2 p2 = points[ind - 1];
      ShortestPath path(area,
          [=](Vec2 pos) { return (pos == p1 || pos == p2) ? 1 : getValue(builder, pos); },
          [p2] (Vec2 to) { return p2.dist4(to); },
          Vec2::directions4(builder->getRandom()), p1, p2);
      for (Vec2 v = p2; v != p1; v = path.getNextMove(v)) {
        if (!path.isReachable(v))
          failGen();
        auto roadType = getRoadType(builder, v);
        if (v != p2 && v != p1 && !builder->isFurnitureType(v, roadType))
          builder->putFurniture(v, roadType);
      }
    }
  }
};

class StartingPos : public LevelMaker {
  public:

  StartingPos(Predicate pred, StairKey key) : predicate(pred), stairKey(key) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 pos : area)
      if (predicate.apply(builder, pos))
        builder->modSquare(pos)->setLandingLink(stairKey);
  }

  private:
  Predicate predicate;
  StairKey stairKey;
};

class TransferPos : public LevelMaker {
  public:

  TransferPos(Predicate pred, StairKey key, int w) : predicate(pred), stairKey(key), width(w) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    bool found = false;
    for (Vec2 pos : area)
      if (((pos.x - area.left() < width) || (pos.y - area.top() < width) ||
          (area.right() - pos.x <= width) || (area.bottom() - pos.y <= width)) &&
          predicate.apply(builder, pos)) {
        builder->modSquare(pos)->setLandingLink(stairKey);
        found = true;
      }
    checkGen(found);
  }

  private:
  Predicate predicate;
  StairKey stairKey;
  int width;
};

class Forrest : public LevelMaker {
  public:
  Forrest(double _ratio, double _density, Predicate pred, FurnitureListId furnitureListId, TribeId tribe)
      : ratio(_ratio), density(_density), furnitureListId(furnitureListId), tribe(tribe), pred(pred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    auto furnitureList = builder->getContentFactory()->furniture.getFurnitureList(furnitureListId);
    Table<double> wys = genNoiseMap(builder->getRandom(), area, {0, 0, 0, 0, 0}, 0.65);
    vector<double> values = sortedValues(wys);
    double cutoff = values[values.size() * ratio];
    for (Vec2 v : area)
      if (pred.apply(builder, v) && builder->canNavigate(v, {MovementTrait::WALK}) && wys[v] < cutoff) {
        if (builder->getRandom().getDouble() <= density)
          builder->putFurniture(v, furnitureList, tribe);
        builder->addAttrib(v, SquareAttrib::FORREST);
      }
  }

  private:
  double ratio;
  double density;
  FurnitureListId furnitureListId;
  TribeId tribe;
  Predicate pred;
};

class PlaceCollective : public LevelMaker {
  public:
  PlaceCollective(CollectiveBuilder* c, Predicate pred = Predicate::alwaysTrue())
      : collective(NOTNULL(c)), predicate(pred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (!collective->hasCentralPoint())
      collective->setCentralPoint(builder->toGlobalCoordinates(area).middle());
    collective->addArea(builder->toGlobalCoordinates(area.getAllSquares()
        .filter([&](Vec2 pos) { return predicate.apply(builder, pos); })));
    builder->addCollective(collective);
  }

  private:
  CollectiveBuilder* collective;
  Predicate predicate;
};

class ForEachSquare : public LevelMaker {
  public:
  ForEachSquare(function<void(LevelBuilder*, Vec2 pos)> f, Predicate _onPred = Predicate::alwaysTrue())
    : fun(f), onPred(_onPred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      if (onPred.apply(builder, v))
        fun(builder, v);
  }

  protected:
  function<void(LevelBuilder*, Vec2 pos)> fun;
  Predicate onPred;
};

class AddAttrib : public ForEachSquare {
  public:
  AddAttrib(SquareAttrib attr, Predicate onPred = Predicate::alwaysTrue())
      : ForEachSquare([attr](LevelBuilder* b, Vec2 pos) { b->addAttrib(pos, attr); }, onPred) {}
};

class RemoveAttrib : public ForEachSquare {
  public:
  RemoveAttrib(SquareAttrib attr, Predicate onPred = Predicate::alwaysTrue())
    : ForEachSquare([attr](LevelBuilder* b, Vec2 pos) { b->removeAttrib(pos, attr); }, onPred) {}
};

enum class StairDirection {
  UP, DOWN
};

class Stairs : public LevelMaker {
  public:
  Stairs(StairDirection dir, StairKey k, BuildingInfo building, Predicate onPred, optional<SquareAttrib> _setAttr = none)
    : direction(dir), key(k), onPredicate(onPred), setAttr(_setAttr), building(std::move(building)) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    auto type = direction == StairDirection::DOWN
        ? building.downStairs.value_or(FurnitureType("DOWN_STAIRS"))
        : building.upStairs.value_or(FurnitureType("UP_STAIRS"));
    vector<Vec2> allPos;
    for (Vec2 v : area)
      if (onPredicate.apply(builder, v) && builder->canPutFurniture(v, builder->getContentFactory()->furniture.getData(type).getLayer()))
        allPos.push_back(v);
    checkGen(allPos.size() > 0);
    Vec2 pos = allPos[builder->getRandom().get(allPos.size())];
    builder->putFurniture(pos, FurnitureParams{type, TribeId::getHostile()});
    builder->setLandingLink(pos, key);
  }

  private:
  StairDirection direction;
  StairKey key;
  Predicate onPredicate;
  optional<SquareAttrib> setAttr;
  BuildingInfo building;
};

class ShopMaker : public LevelMaker {
  public:
  ShopMaker(const SettlementInfo& info, int _numItems)
      : shopItems(*info.shopItems), tribe(info.tribe), numItems(_numItems),
        building(info.buildingInfo), shopkeeperDead(info.shopkeeperDead)  {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    PCreature shopkeeper = builder->getContentFactory()->getCreatures().fromId(CreatureId("SHOPKEEPER"), tribe, MonsterAIFactory::idle());
    shopkeeper->setController(CreatureFactory::getShopkeeper(builder->toGlobalCoordinates(area), shopkeeper.get()));
    vector<Vec2> pos;
    for (Vec2 v : area)
      if (builder->canNavigate(v, MovementTrait::WALK) && builder->hasAttrib(v, SquareAttrib::ROOM))
        pos.push_back(v);
    Vec2 shopkeeperPos = pos[builder->getRandom().get(pos.size())];
    if (!shopkeeperDead)
      builder->putCreature(shopkeeperPos, std::move(shopkeeper));
    else {
      builder->putItems(shopkeeperPos, shopkeeper->getEquipment().removeAllItems(shopkeeper.get()));
      builder->putItems(shopkeeperPos, shopkeeper->generateCorpse(builder->getContentFactory(), true));
    }
    builder->putFurniture(pos[builder->getRandom().get(pos.size())], FurnitureParams{FurnitureType("GROUND_TORCH"), tribe});
    auto itemList = builder->getContentFactory()->itemFactory.get(shopItems);
    for (int i : Range(numItems)) {
      Vec2 v = pos[builder->getRandom().get(pos.size())];
      builder->putItems(v, itemList.random(builder->getContentFactory()));
    }
  }

  private:
  ItemListId shopItems;
  TribeId tribe;
  int numItems;
  BuildingInfo building;
  bool shopkeeperDead;
};

class LevelExit : public LevelMaker {
  public:
  LevelExit(SquareChange exit, int _minCornerDist = 1)
      : exit(std::move(exit)), minCornerDist(_minCornerDist) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Vec2 pos = getRandomExit(builder->getRandom(), area, minCornerDist);
    exit.apply(builder, pos);
  }

  private:
  SquareChange exit;
  optional<SquareAttrib> attrib;
  int minCornerDist;
};

class Division : public LevelMaker {
  public:
  Division(double _vRatio, double _hRatio,
      PLevelMaker _upperLeft, PLevelMaker _upperRight, PLevelMaker _lowerLeft, PLevelMaker _lowerRight,
      optional<SquareChange> _wall = none) : vRatio(_vRatio), hRatio(_hRatio),
      upperLeft(std::move(_upperLeft)), upperRight(std::move(_upperRight)), lowerLeft(std::move(_lowerLeft)),
      lowerRight(std::move(_lowerRight)), wall(_wall) {}

  Division(double _hRatio, PLevelMaker _left, PLevelMaker _right, optional<SquareChange> _wall = none)
      : vRatio(-1), hRatio(_hRatio), upperLeft(std::move(_left)), upperRight(std::move(_right)), wall(_wall) {}

  Division(bool, double _vRatio, PLevelMaker _top, PLevelMaker _bottom, optional<SquareChange> _wall = none)
      : vRatio(_vRatio), hRatio(-1), upperLeft(std::move(_top)), lowerLeft(std::move(_bottom)), wall(_wall) {}

  void makeHorizDiv(LevelBuilder* builder, Rectangle area) {
    int hDiv = area.left() + min(area.width() - 1, max(1, (int) (hRatio * area.width())));
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), hDiv, area.bottom()));
    if (upperRight)
      upperRight->make(builder, Rectangle(hDiv + (wall ? 1 : 0), area.top(), area.right(), area.bottom()));
    if (wall)
      for (int i : Range(area.top(), area.bottom()))
        wall->apply(builder, Vec2(hDiv, i));
  }

  void makeVertDiv(LevelBuilder* builder, Rectangle area) {
    int vDiv = area.top() + min(area.height() - 1, max(1, (int) (vRatio * area.height())));
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), area.right(), vDiv));
    if (lowerLeft)
      lowerLeft->make(builder, Rectangle(area.left(), vDiv + (wall ? 1 : 0), area.right(), area.bottom()));
    if (wall)
      for (int i : Range(area.left(), area.right()))
        wall->apply(builder, Vec2(i, vDiv));
  }

  void makeDiv(LevelBuilder* builder, Rectangle area) {
    int vDiv = area.top() + min(area.height() - 1, max(1, (int) (vRatio * area.height())));
    int hDiv = area.left() + min(area.width() - 1, max(1, (int) (hRatio * area.width())));
    int wallSpace = wall ? 1 : 0;
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), hDiv, vDiv));
    if (upperRight)
      upperRight->make(builder, Rectangle(hDiv + wallSpace, area.top(), area.right(), vDiv));
    if (lowerLeft)
      lowerLeft->make(builder, Rectangle(area.left(), vDiv + wallSpace, hDiv, area.bottom()));
    if (lowerRight)
      lowerRight->make(builder, Rectangle(hDiv + wallSpace, vDiv + wallSpace, area.right(), area.bottom()));
    if (wall) {
      for (int i : Range(area.top(), area.bottom()))
        wall->apply(builder, Vec2(hDiv, i));
      for (int i : Range(area.left(), area.right()))
        wall->apply(builder, Vec2(i, vDiv));
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (vRatio < 0)
      makeHorizDiv(builder, area);
    else if (hRatio < 0)
      makeVertDiv(builder, area);
    else
      makeDiv(builder, area);
  }

  private:
  double vRatio, hRatio;
  PLevelMaker upperLeft;
  PLevelMaker upperRight;
  PLevelMaker lowerLeft;
  PLevelMaker lowerRight;
  optional<SquareChange> wall;
};

class AreaCorners : public LevelMaker {
  public:
  AreaCorners(PLevelMaker _maker, Vec2 _size, vector<PLevelMaker> _insideMakers = {})
      : maker(std::move(_maker)), size(_size), insideMakers(std::move(_insideMakers)) {}

  vector<Rectangle> getCorners(Rectangle area) {
    return {
      Rectangle(area.topLeft(), area.topLeft() + size),
      Rectangle(area.topRight() - Vec2(size.x, 0), area.topRight() + Vec2(0, size.y)),
      Rectangle(area.bottomLeft() - Vec2(0, size.y), area.bottomLeft() + Vec2(size.x, 0)),
      Rectangle(area.bottomRight() - size, area.bottomRight())};
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Rectangle> corners = builder->getRandom().permutation(getCorners(area));
    for (int i : All(corners)) {
      maker->make(builder, corners[i]);
      if (i < insideMakers.size())
        insideMakers[i]->make(builder, corners[i]);
    }
  }

  private:
  PLevelMaker maker;
  Vec2 size;
  vector<PLevelMaker> insideMakers;
};

class CastleExit : public LevelMaker {
  public:
  CastleExit(SettlementInfo settlement) : settlement(std::move(settlement)) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    auto& building = settlement.buildingInfo;
    Vec2 loc(area.right() - 1, area.middle().y - 1);
    for (int i = 0; i < 2; ++i) {
      if (building.floorInside)
        builder->resetFurniture(loc + Vec2(2, i), *building.floorInside);
      if (building.gate)
        builder->putFurniture(loc + Vec2(2, i), building.gate->type, settlement.tribe);
      else
        builder->removeFurniture(loc + Vec2(2, i), FurnitureLayer::MIDDLE);
      settlement.collective->addArea(builder->toGlobalCoordinates(makeVec(loc + Vec2(2, i))));
    }
    if (!settlement.dontBuildRoad)
      builder->addAttrib(loc + Vec2(2, 0), SquareAttrib::CONNECT_ROAD);
    vector<Vec2> walls { Vec2(1, -2), Vec2(2, -2), Vec2(2, -1), Vec2(2, 2), Vec2(2, 3), Vec2(1, 3)};
    for (Vec2 v : walls) {
      builder->putFurniture(loc + v, building.wall);
      settlement.collective->addArea(builder->toGlobalCoordinates(makeVec(loc + v)));
    }
    vector<Vec2> floor { Vec2(1, -1), Vec2(1, 0), Vec2(1, 1), Vec2(1, 2), Vec2(0, -1), Vec2(0, 0), Vec2(0, 1), Vec2(0, 2) };
    for (Vec2 v : floor) {
      settlement.collective->addArea(builder->toGlobalCoordinates(makeVec(loc + v)));
      if (building.floorInside)
        builder->resetFurniture(loc + v, *building.floorInside);
      else
        builder->removeFurniture(loc + v, FurnitureLayer::MIDDLE);
    }
    vector<Vec2> guardPos { Vec2(1, 2), Vec2(1, -1) };
    for (Vec2 pos : guardPos) {
      auto fighters = settlement.inhabitants.fighters.generate(builder->getRandom(), &builder->getContentFactory()->getCreatures(),
          settlement.tribe, MonsterAIFactory::stayInLocation(
              builder->toGlobalCoordinates(Rectangle(loc + pos, loc + pos + Vec2(1, 1))), false), true);
      if (!fighters.empty())
        builder->putCreature(loc + pos, std::move(fighters[0]));
    }
  }

  private:
  SettlementInfo settlement;
};

class AddMapBorder : public LevelMaker {
  public:
  AddMapBorder(int w) : width(w) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      if (!v.inRectangle(area.minusMargin(width)))
        builder->setUnavailable(v);
  }

  private:
  int width;
};

class BorderGuard : public LevelMaker {
  public:

  BorderGuard(PLevelMaker inside, SquareChange c)
      : change(c), insideMaker(std::move(inside)) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (int i : Range(area.left(), area.right())) {
      change.apply(builder, Vec2(i, area.top()));
      change.apply(builder, Vec2(i, area.bottom() - 1));
    }
    for (int i : Range(area.top(), area.bottom())) {
      change.apply(builder, Vec2(area.left(), i));
      change.apply(builder, Vec2(area.right() - 1, i));
    }
    insideMaker->make(builder, Rectangle(area.left() + 1, area.top() + 1, area.right() - 1, area.bottom() - 1));
  }

  private:
  SquareChange change;
  PLevelMaker insideMaker;

};

class DestroyRandomly : public LevelMaker {
  public:
  DestroyRandomly(FurnitureType type, double prob) : type(type), prob(prob) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (auto v : area)
      if (builder->getRandom().chance(prob) && builder->isFurnitureType(v, type))
        builder->removeFurniture(v, builder->getContentFactory()->furniture.getData(type).getLayer());
  }

  private:
  FurnitureType type;
  double prob;
};

}

static PMakerQueue stockpileMaker(StockpileInfo info) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(info.furniture));
  if (info.furniture)
    queue->addMaker(unique<Empty>(SquareChange(*info.furniture)));
  queue->addMaker(unique<Items>(info.items, info.count, info.count + 1, Predicate::alwaysTrue(), !!info.furniture));
  return queue;
}

PLevelMaker LevelMaker::mazeLevel(RandomGen& random, SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  auto& building = info.buildingInfo;
  auto floor = building.floorOutside.value_or(FurnitureType("FLOOR"));
  queue->addMaker(unique<Empty>(SquareChange(floor, building.wall)));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<RoomMaker>(random.get(8, 15), 3, 5, SquareChange::none(), none, unique<Empty>(floor)));
  queue->addMaker(unique<Connector>(building.door, info.tribe));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, furniture, info.tribe));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::type(floor)));
  for (StairKey key : info.upStairs)
    queue->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::type(floor)));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  if (info.shopItems)
    queue->addMaker(unique<Items>(*info.shopItems, 5, 10));
  return unique<BorderGuard>(std::move(queue), SquareChange(floor, building.wall));
}

static PMakerQueue getElderRoom(SettlementInfo info) {
  auto& building = info.buildingInfo;
  PMakerQueue elderRoom = unique<MakerQueue>();
  if (info.lootItem)
    elderRoom->addMaker(unique<Items>(*info.lootItem, 1, 2, Predicate::alwaysTrue(), true));
  return elderRoom;
}

static PMakerQueue village2(RandomGen& random, SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<PlaceCollective>(info.collective));
  vector<PLevelMaker> insideMakers = makeVec<PLevelMaker>(getElderRoom(info));
  for (auto& elem : info.stockpiles)
    insideMakers.push_back(stockpileMaker(elem));
  if (info.shopItems)
    insideMakers.push_back(unique<ShopMaker>(info, random.get(8, 16)));
  queue->addMaker(unique<Buildings>(6, 10, 3, 4, building, info.tribe, false, std::move(insideMakers)));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, furniture, info.tribe));
  if (info.outsideFeatures)
    queue->addMaker(unique<Furnitures>(!Predicate::attrib(SquareAttrib::FLOOR_OUTSIDE), 0.01, *info.outsideFeatures, info.tribe));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue village(RandomGen& random, SettlementInfo info, int minRooms, int maxRooms) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<UniformBlob>(building.floorOutside, none, 0.6));
  vector<PLevelMaker> insideMakers = makeVec<PLevelMaker>(
 //     hatchery(CreatureGroup::singleType(info.tribe, "PIG"), random.get(2, 5)),
      getElderRoom(info));
  if (info.shopItems)
    insideMakers.push_back(unique<ShopMaker>(info, random.get(8, 16)));
  for (auto& elem : info.stockpiles)
    insideMakers.push_back(stockpileMaker(elem));
  queue->addMaker(unique<Buildings>(minRooms, maxRooms, 3, 7, building, info.tribe, true, std::move(insideMakers), !info.dontBuildRoad));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, furniture, info.tribe));
  if (info.outsideFeatures)
    queue->addMaker(unique<Furnitures>(
        !Predicate::attrib(SquareAttrib::ROOM) &&
        Predicate::attrib(SquareAttrib::BUILDINGS_CENTER), 0.2, *info.outsideFeatures, info.tribe, SquareAttrib::NO_ROAD));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::EMPTY_ROOM)));
  for (StairKey key : info.upStairs)
    queue->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::attrib(SquareAttrib::EMPTY_ROOM)));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue cottage(SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  if (building.floorOutside)
    queue->addMaker(unique<Empty>(*building.floorOutside));
  auto room = getElderRoom(info);
  for (StairKey key : info.upStairs)
    room->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (StairKey key : info.downStairs)
    room->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (auto& furniture : info.furniture)
    room->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::ROOM), 0.3, furniture, info.tribe));
  if (building.prettyFloor)
    room->addMaker(unique<Empty>(SquareChange(*building.prettyFloor)));
  queue->addMaker(unique<Buildings>(1, 2, 5, 7, building, info.tribe, false, std::move(room), false));
  if (info.outsideFeatures)
    queue->addMaker(unique<Furnitures>(!Predicate::attrib(SquareAttrib::ROOM), 0.1, *info.outsideFeatures, info.tribe));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue temple(RandomGen& random, SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  auto room = getElderRoom(info);
  for (StairKey key : info.upStairs)
    room->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (StairKey key : info.downStairs)
    room->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (auto& furniture : info.furniture)
    room->addMaker(unique<Margin>(1, unique<Furnitures>(Predicate::attrib(SquareAttrib::ROOM), 0.3, furniture, info.tribe)));
  auto torchPred = Predicate::attrib(SquareAttrib::ROOM) && random.choose(
      Predicate::near8AtLeast(building.wall, 4),
      Predicate::near8AtLeast(building.wall, 5),
      Predicate::near4AtLeast(building.wall, 1),
      Predicate::near4Equals(building.wall, 1)
  );
  //room->addMaker(unique<Furnitures>(torchPred, 1.0, FurnitureFactory(info.tribe, {{FurnitureType("GROUND_TORCH"), 1}})));
  if (info.outsideFeatures)
    room->addMaker(unique<Furnitures>(!Predicate::attrib(SquareAttrib::ROOM), 0.1, *info.outsideFeatures, info.tribe));
  if (building.prettyFloor)
    room->addMaker(unique<Empty>(SquareChange(*building.prettyFloor)));
  queue->addMaker(unique<Buildings>(1, 2, 4, 5, building, info.tribe, false, std::move(room), false));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue forrestCottage(SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  auto room = getElderRoom(info);
  for (StairKey key : info.upStairs)
    room->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (StairKey key : info.downStairs)
    room->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::ROOM), none));
  for (auto& furniture : info.furniture)
    room->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::ROOM), 0.3, furniture, info.tribe));
  if (info.outsideFeatures)
    room->addMaker(unique<Furnitures>(!Predicate::attrib(SquareAttrib::ROOM), 0.1, *info.outsideFeatures, info.tribe));
  queue->addMaker(unique<Buildings>(1, 3, 3, 4, building, info.tribe, false, std::move(room), false));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue castle(RandomGen& random, SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto castleRoom = [&] { return unique<BorderGuard>(unique<Empty>(SquareChange::resetOrRemove(building.floorInside, FurnitureLayer::MIDDLE, SquareAttrib::EMPTY_ROOM)),
      SquareChange(building.wall, SquareAttrib::ROOM_WALL)); };
  auto leftSide = unique<MakerQueue>();
  leftSide->addMaker(unique<Division>(true, random.getDouble(0.5, 0.5),
      unique<Margin>(1, -1, -1, 1, castleRoom()), unique<Margin>(1, 1, -1, -1, castleRoom())));
  leftSide->addMaker(getElderRoom(info));
  auto inside = unique<MakerQueue>();
  vector<PLevelMaker> insideMakers;
  if (info.shopItems)
    insideMakers.push_back(unique<ShopMaker>(info, random.get(8, 16)));
  inside->addMaker(unique<Division>(random.getDouble(0.25, 0.4), std::move(leftSide),
        unique<Buildings>(1, 3, 3, 6, building, info.tribe, false, std::move(insideMakers), false),
            SquareChange(building.wall, SquareAttrib::ROOM_WALL)));
  auto insidePlusWall = unique<MakerQueue>();
  if (info.outsideFeatures)
    inside->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::FLOOR_OUTSIDE), 0.18, *info.outsideFeatures, info.tribe));
  for (auto& furniture : info.furniture)
    inside->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.35, furniture, info.tribe));
  insidePlusWall->addMaker(unique<Empty>(SquareChange::resetOrRemove(building.floorOutside, FurnitureLayer::MIDDLE, SquareAttrib::FLOOR_OUTSIDE)));
  insidePlusWall->addMaker(unique<BorderGuard>(std::move(inside), building.wall));
  auto queue = unique<MakerQueue>();
  int insideMargin = 2;
  queue->addMaker(unique<Margin>(insideMargin, unique<PlaceCollective>(info.collective)));
  queue->addMaker(unique<Margin>(insideMargin, std::move(insidePlusWall)));
  vector<PLevelMaker> cornerMakers;
  for (auto& elem : info.stockpiles)
    cornerMakers.push_back(unique<Margin>(1, stockpileMaker(elem)));
  queue->addMaker(unique<AreaCorners>(
      unique<MakerQueue>(makeVec<PLevelMaker>(
          unique<BorderGuard>(unique<Empty>(
              SquareChange::resetOrRemove(building.floorInside, FurnitureLayer::MIDDLE, SquareAttrib::CASTLE_CORNER)),
              SquareChange(building.wall, SquareAttrib::ROOM_WALL)),
          unique<Empty>(SquareChange::addTerritory(info.collective))
      )),
      Vec2(5, 5),
      std::move(cornerMakers)));
  queue->addMaker(unique<Margin>(insideMargin, unique<Connector>(building.door, info.tribe, 18)));
  queue->addMaker(unique<Margin>(insideMargin, unique<CastleExit>(info)));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building,
          Predicate::attrib(SquareAttrib::CASTLE_CORNER), none));
  queue->addMaker(unique<StartingPos>(Predicate::attrib(SquareAttrib::FLOOR_OUTSIDE)
      && !Predicate::attrib(SquareAttrib::CASTLE_CORNER), StairKey::heroSpawn()));
  queue->addMaker(unique<AddAttrib>(SquareAttrib::NO_DIG, Predicate::type(building.wall)));
  return queue;
}

static PMakerQueue castle2(RandomGen& random, SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto inside = unique<MakerQueue>();
  auto insideMaker = unique<MakerQueue>();
  if (!info.stockpiles.empty())
    insideMaker->addMaker(stockpileMaker(info.stockpiles.getOnlyElement()));
  inside->addMaker(unique<Buildings>(1, 2, 3, 4, building, info.tribe, false, std::move(insideMaker), false));
  for (StairKey key : info.downStairs)
    inside->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::alwaysTrue(), none));
  auto insidePlusWall = unique<MakerQueue>();
  insidePlusWall->addMaker(unique<Empty>(SquareChange(building.floorOutside, SquareAttrib::FLOOR_OUTSIDE)));
  insidePlusWall->addMaker(unique<BorderGuard>(std::move(inside), building.wall));
  insidePlusWall->addMaker(unique<PlaceCollective>(info.collective));
  insidePlusWall->addMaker(unique<CastleExit>(info));
  insidePlusWall->addMaker(unique<Connector>(building.door, info.tribe, 18));
  if (info.outsideFeatures)
    insidePlusWall->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::FLOOR_OUTSIDE), 0.05, *info.outsideFeatures, info.tribe));
  insidePlusWall->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  insidePlusWall->addMaker(unique<AddAttrib>(SquareAttrib::NO_DIG, Predicate::type(building.wall)));
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Margin>(0, 0, 2, 0, std::move(insidePlusWall)));
  return queue;
}

static PMakerQueue tower(RandomGen& random, SettlementInfo info, bool withExit) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(SquareChange(FurnitureType("FLOOR"), building.wall)));
  if (withExit) {
    if (building.door)
      queue->addMaker(unique<LevelExit>(SquareChange(FurnitureParams{building.door->type, info.tribe}), 2));
    else
      queue->addMaker(unique<LevelExit>(SquareChange::remove(FurnitureLayer::MIDDLE), 2));
  }
  queue->addMaker(unique<Margin>(1, unique<Empty>(
      SquareChange::resetOrRemove(building.floorInside, FurnitureLayer::MIDDLE, SquareAttrib::ROOM))));
  queue->addMaker(unique<RemoveAttrib>(SquareAttrib::ROAD_CUT_THRU));
  if (info.collective)
    queue->addMaker(unique<PlaceCollective>(info.collective));
  PLevelMaker downStairs;
  for (StairKey key : info.downStairs)
    downStairs = unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::ROOM));
  PLevelMaker upStairs;
  for (StairKey key : info.upStairs)
    upStairs = unique<Stairs>(StairDirection::UP, key, building, Predicate::attrib(SquareAttrib::ROOM));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective, Predicate::attrib(SquareAttrib::ROOM)));
  queue->addMaker(unique<Division>(0.5, 0.5, std::move(upStairs), nullptr, nullptr, std::move(downStairs)));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::ROOM), 0.5, furniture, info.tribe));
  if (info.corpses)
    queue->addMaker(unique<Corpses>(*info.corpses));
  return queue;
}

PLevelMaker LevelMaker::towerLevel(RandomGen& random, SettlementInfo info) {
  return PLevelMaker(tower(random, info, false));
}

Vec2 getSize(RandomGen& random, SettlementType type) {
  switch (type) {
    case SettlementType::CEMETERY:
    case SettlementType::MOUNTAIN_LAKE:
    case SettlementType::SMALL_VILLAGE: return {15, 15};
    case SettlementType::SWAMP: return {random.get(12, 16), random.get(12, 16)};
    case SettlementType::TEMPLE:
    case SettlementType::COTTAGE: return {random.get(8, 10), random.get(8, 10)};
    case SettlementType::FORREST_COTTAGE: return {15, 15};
    case SettlementType::FOREST: return {18, 13};
    case SettlementType::FORREST_VILLAGE: return {20, 20};
    case SettlementType::VILLAGE:
    case SettlementType::ANT_NEST:  return {20, 20};
    case SettlementType::CASTLE: return {30, 20};
    case SettlementType::CASTLE2: return {15, 14};
    case SettlementType::MINETOWN: return {30, 20};
    case SettlementType::SMALL_MINETOWN: return {15, 15};
    case SettlementType::CAVE: return {12, 12};
    case SettlementType::SPIDER_CAVE: return {12, 12};
    case SettlementType::VAULT: return {10, 10};
    case SettlementType::TOWER: return {5, 5};
    case SettlementType::ISLAND_VAULT_DOOR:
    case SettlementType::ISLAND_VAULT: return {6, 6};
  }
}

RandomLocations::LocationPredicate getSettlementPredicate(const SettlementInfo& info) {
  switch (info.type) {
    case SettlementType::FOREST:
    case SettlementType::FORREST_COTTAGE:
    case SettlementType::FORREST_VILLAGE:
      return !Predicate::attrib(SquareAttrib::RIVER) && Predicate::attrib(SquareAttrib::FORREST);
    case SettlementType::CAVE:
      return RandomLocations::LocationPredicate(
          Predicate::attrib(SquareAttrib::MOUNTAIN), Predicate::attrib(SquareAttrib::HILL), 5, 15);
    case SettlementType::VAULT:
    case SettlementType::ANT_NEST:
    case SettlementType::SMALL_MINETOWN:
    case SettlementType::MINETOWN:
      return Predicate::attrib(SquareAttrib::MOUNTAIN);
    case SettlementType::SPIDER_CAVE:
      return RandomLocations::LocationPredicate(
          Predicate::attrib(SquareAttrib::MOUNTAIN),
          Predicate::attrib(SquareAttrib::CONNECTOR), 1, 2);
    case SettlementType::MOUNTAIN_LAKE:
    case SettlementType::ISLAND_VAULT:
      return Predicate::attrib(SquareAttrib::MOUNTAIN);
    case SettlementType::ISLAND_VAULT_DOOR:
      return RandomLocations::LocationPredicate(
            Predicate::attrib(SquareAttrib::MOUNTAIN) && !Predicate::attrib(SquareAttrib::RIVER),
            Predicate::attrib(SquareAttrib::RIVER), 10, 30);
    default:
      return Predicate::canEnter({MovementTrait::WALK});
  }
}

static PMakerQueue genericMineTownMaker(RandomGen& random, SettlementInfo info, int numCavern, int maxCavernSize,
    int numRooms, int minRoomSize, int maxRoomSize) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>();
  auto caverns = unique<RandomLocations>();
  vector<PLevelMaker> vCavern;
  vector<pair<int, int>> sizes;
  for (int i : Range(numCavern)) {
    caverns->add(unique<UniformBlob>(building.floorInside),
        Vec2(random.get(5, maxCavernSize), random.get(5, maxCavernSize)),
        Predicate::alwaysTrue());
    caverns->setCanOverlap(caverns->getLast());
  }
  queue->addMaker(std::move(caverns));
  vector<PLevelMaker> roomInsides;
  if (info.shopItems)
    roomInsides.push_back(unique<ShopMaker>(info, random.get(8, 16)));
  for (auto& elem : info.stockpiles)
    roomInsides.push_back(stockpileMaker(elem));
  queue->addMaker(unique<RoomMaker>(numRooms, minRoomSize, maxRoomSize, SquareChange::none(), none,
      unique<Empty>(SquareChange(building.floorInside, ifTrue(!info.dontConnectCave, SquareAttrib::CONNECT_CORRIDOR))),
      std::move(roomInsides), true));
  queue->addMaker(unique<Connector>(none, info.tribe));
  Predicate featurePred = Predicate::attrib(SquareAttrib::EMPTY_ROOM);
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, featurePred));
  for (StairKey key : info.upStairs)
    queue->addMaker(unique<Stairs>(StairDirection::UP, key, building, featurePred));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(featurePred, 0.3, furniture, info.tribe));
  if (info.outsideFeatures)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::ROOM), 0.09, *info.outsideFeatures, info.tribe));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  queue->addMaker(unique<PlaceCollective>(info.collective, Predicate::canEnter(MovementTrait::WALK)));
  return queue;
}

static PMakerQueue mineTownMaker(RandomGen& random, SettlementInfo info) {
  return genericMineTownMaker(random, info, 10, 12, random.get(5, 7), 6, 8);
}

static PMakerQueue antNestMaker(RandomGen& random, SettlementInfo info) {
  auto ret = genericMineTownMaker(random, info, 4, 6, random.get(5, 7), 3, 4);
  if (info.dontConnectCave)
    ret->addMaker(unique<AddAttrib>(SquareAttrib::NO_DIG));
  return ret;
}

static PMakerQueue smallMineTownMaker(RandomGen& random, SettlementInfo info) {
  auto ret = genericMineTownMaker(random, info, 2, 7, random.get(3, 5), 5, 7);
  if (info.dontConnectCave)
    ret->addMaker(unique<AddAttrib>(SquareAttrib::NO_DIG));
  return ret;
}

static PMakerQueue vaultMaker(SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  auto& building = info.buildingInfo;
  auto change = SquareChange::resetOrRemove(
      building.floorOutside, FurnitureLayer::MIDDLE, SquareAttrib::FLOOR_OUTSIDE);
  if (!info.dontConnectCave)
    change.add(SquareAttrib::CONNECT_CORRIDOR);
  queue->addMaker(unique<UniformBlob>(std::move(change)));
  auto insidePredicate = Predicate::attrib(SquareAttrib::FLOOR_OUTSIDE) && Predicate::canEnter(MovementTrait::WALK);
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective, insidePredicate));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, insidePredicate));
  for (StairKey key : info.upStairs)
    queue->addMaker(unique<Stairs>(StairDirection::UP, key, building, insidePredicate));
  if (info.shopItems)
    queue->addMaker(unique<Items>(*info.shopItems, 16, 20, insidePredicate));
  queue->addMaker(unique<PlaceCollective>(info.collective, insidePredicate));
  if (info.dontConnectCave)
    queue->addMaker(unique<AddAttrib>(SquareAttrib::NO_DIG));
  return queue;
}

static PMakerQueue spiderCaveMaker(SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  auto& building = info.buildingInfo;
  auto inside = unique<MakerQueue>();
  inside->addMaker(unique<UniformBlob>(SquareChange::resetOrRemove(
      building.floorOutside, FurnitureLayer::MIDDLE, SquareAttrib::CONNECT_CORRIDOR), none));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  if (info.shopItems)
    inside->addMaker(unique<Items>(*info.shopItems, 5, 10));
  queue->addMaker(unique<Margin>(3, std::move(inside)));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Connector>(none, info.tribe, 0));
  return queue;
}

static PMakerQueue islandVaultMaker(RandomGen& random, SettlementInfo info, bool door) {
  auto& building = info.buildingInfo;
  auto inside = unique<MakerQueue>();
  inside->addMaker(unique<PlaceCollective>(info.collective));
  Predicate featurePred = Predicate::attrib(SquareAttrib::ROOM);
  if (!info.stockpiles.empty())
    inside->addMaker(stockpileMaker(info.stockpiles.getOnlyElement()));
  else
    inside->addMaker(unique<Empty>(SquareChange::reset(building.floorInside, SquareAttrib::ROOM)));
  for (StairKey key : info.downStairs)
    inside->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, featurePred));
  for (StairKey key : info.upStairs)
    inside->addMaker(unique<Stairs>(StairDirection::UP, key, building, featurePred));
  auto buildingMaker = unique<MakerQueue>(
      unique<Empty>(SquareChange(building.wall)),
      unique<AddAttrib>(SquareAttrib::NO_DIG),
      unique<RemoveAttrib>(SquareAttrib::CONNECT_CORRIDOR),
      unique<Margin>(1, std::move(inside))
      );
  if (door)
    buildingMaker->addMaker(unique<LevelExit>(FurnitureType("WOOD_DOOR")));
  return unique<MakerQueue>(
        unique<Empty>(SquareChange::reset(FurnitureType("WATER"))),
        unique<Margin>(1, std::move(buildingMaker)));
}

PLevelMaker LevelMaker::mineTownLevel(RandomGen& random, SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(SquareChange(FurnitureType("FLOOR"), FurnitureType("MOUNTAIN"))));
  queue->addMaker(mineTownMaker(random, info));
  return unique<BorderGuard>(std::move(queue), SquareChange(FurnitureType("FLOOR"), FurnitureType("MOUNTAIN")));
}

static PMakerQueue cemetery(SettlementInfo info) {
  auto& building = info.buildingInfo;
  auto queue = unique<MakerQueue>(
          unique<PlaceCollective>(info.collective),
          unique<Margin>(1, unique<Buildings>(1, 2, 2, 3, building, info.tribe, false, nullptr, false))
  );
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::type(FurnitureType("GRASS")), 0.15, furniture, info.tribe));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::attrib(SquareAttrib::ROOM)));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue emptyCollective(SettlementInfo info) {
  return unique<MakerQueue>(
      unique<PlaceCollective>(info.collective),
      unique<Inhabitants>(info.inhabitants, info.collective));
}

static PMakerQueue swamp(SettlementInfo info) {
  auto queue = unique<MakerQueue>(
      unique<Lake>(none),
      unique<PlaceCollective>(info.collective)
  );
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PMakerQueue mountainLake(SettlementInfo info) {
  auto queue = unique<MakerQueue>(
      unique<UniformBlob>(SquareChange::reset(FurnitureType("WATER"), SquareAttrib::LAKE), none),
      unique<PlaceCollective>(info.collective)
  );
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  return queue;
}

static PLevelMaker getMountains(BiomeId id, TribeId tribe) {
  switch (id) {
    case BiomeId::SNOW:
      return unique<Mountains>(0.45, 0.02, NoiseInit{0, 1, 0, 0, 0}, tribe,
          FurnitureType("SNOW"), FurnitureType("SNOW"), FurnitureType("GLACIER"), FurnitureType("SNOW"));
    case BiomeId::DESERT:
      return unique<Mountains>(0.45, 0.02, NoiseInit{0, 1, 0, 0, 0}, tribe,
          FurnitureType("SAND"), FurnitureType("SAND"), FurnitureType("SANDSTONE"), FurnitureType("SAND"));
    case BiomeId::GRASSLAND:
    case BiomeId::FORREST:
      return unique<Mountains>(0.45, 0.06, NoiseInit{0, 1, 0, 0, 0}, tribe,
          FurnitureType("HILL"), FurnitureType("GRASS"), FurnitureType("MOUNTAIN"), FurnitureType("FLOOR"));
    case BiomeId::MOUNTAIN:
      return unique<Mountains>(0.25, 0.1, NoiseInit{0, 1, 0, 0, 0}, tribe,
          FurnitureType("HILL"), FurnitureType("GRASS"), FurnitureType("MOUNTAIN"), FurnitureType("FLOOR"));
  }
}

static PLevelMaker getForrest(BiomeId id) {
  switch (id) {
    case BiomeId::MOUNTAIN:
      return unique<MakerQueue>(
          unique<Forrest>(0.2, 0.5, Predicate::type(FurnitureType("GRASS")), FurnitureListId("vegetationLow"), TribeId::getHostile()),
          unique<Forrest>(0.8, 0.5, Predicate::type(FurnitureType("HILL")), FurnitureListId("vegetationHigh"), TribeId::getHostile()));
    case BiomeId::GRASSLAND:
      return unique<MakerQueue>(
          unique<Forrest>(0.3, 0.25, Predicate::type(FurnitureType("GRASS")), FurnitureListId("vegetationLow"), TribeId::getHostile()),
          unique<Forrest>(0.8, 0.25, Predicate::type(FurnitureType("HILL")), FurnitureListId("vegetationHigh"), TribeId::getHostile()));
    case BiomeId::FORREST:
      return unique<MakerQueue>(
          unique<Forrest>(0.8, 0.5, Predicate::type(FurnitureType("GRASS")), FurnitureListId("vegetationLow"), TribeId::getHostile()),
          unique<Forrest>(0.8, 0.5, Predicate::type(FurnitureType("HILL")), FurnitureListId("vegetationHigh"), TribeId::getHostile()));
    case BiomeId::DESERT:
      return unique<MakerQueue>(
          unique<Forrest>(0.8, 0.015, Predicate::type(FurnitureType("SAND")), FurnitureListId("vegetationDesert"), TribeId::getHostile())
      );
    case BiomeId::SNOW:
      return unique<MakerQueue>(
      );
  }
}

static PLevelMaker getForrestCreatures(CreatureGroup factory, int levelWidth, BiomeId biome) {
  int div;
  switch (biome) {
    case BiomeId::SNOW:
    case BiomeId::DESERT:
    case BiomeId::FORREST: div = 2000; break;
    case BiomeId::GRASSLAND:
    case BiomeId::MOUNTAIN: div = 7000; break;
  }
  return unique<Creatures>(factory, levelWidth * levelWidth / div, MonsterAIFactory::wildlifeNonPredator());
}

static auto getItems(BiomeId id, int mapWidth) {
  switch (id) {
    case BiomeId::SNOW:
      return unique<Items>(ItemListId("snowItems"), 1, 4);
    case BiomeId::DESERT:
      return unique<Items>(ItemListId("desertItems"), mapWidth / 30, mapWidth / 15);
    default:
      return unique<Items>(ItemListId("wildernessItems"), mapWidth / 10, mapWidth / 5);
  }
}

struct SurroundWithResourcesInfo {
  LevelMaker* maker;
  SettlementInfo info;
};

static void generateResources(RandomGen& random, ResourceCounts resourceCounts, LevelMaker* startingPos,
    RandomLocations* locations, const vector<SurroundWithResourcesInfo>& surroundWithResources, int mapWidth, TribeId tribe) {
  auto addResources = [&](int count, Range size, int maxDist, FurnitureType type, LevelMaker* center,
      CollectiveBuilder* collective) {
    for (int i : Range(count)) {
      SquareChange change(FurnitureParams{type, tribe}, SquareAttrib::NO_DIG);
      if (collective)
        change.add(SquareChange::addTerritory(collective));
      auto queue = unique<MakerQueue>(unique<FurnitureBlob>(std::move(change)));
      locations->add(std::move(queue), {random.get(size), random.get(size)},
          Predicate::type(FurnitureType("MOUNTAIN2")));
      locations->setMaxDistanceLast(center, maxDist);
    }
  };
  const int closeDist = 0;
  for (auto& info : resourceCounts.elems)
    addResources(info.countStartingPos, Range(5, 10), 20, info.type, startingPos, nullptr);
  for (auto enemy : surroundWithResources)
    for (int i : Range(enemy.info.surroundWithResources))
      if (auto type = enemy.info.extraResources)
        addResources(1, Range(5, 10), closeDist, *type, enemy.maker, enemy.info.collective);
      else {
        auto& info = resourceCounts.elems[i % resourceCounts.elems.size()];
        if (info.countFurther > 0) {
          addResources(1, Range(5, 10), closeDist, info.type, enemy.maker, enemy.info.collective);
          --info.countFurther;
      }
    }
  for (auto& info : resourceCounts.elems)
    if (info.countFurther > 0)
      addResources(info.countFurther, Range(5, 10), mapWidth / 3, info.type, startingPos, nullptr);
}

static PMakerQueue getSettlementMaker(RandomGen& random, const SettlementInfo& settlement) {
  switch (settlement.type) {
    case SettlementType::SMALL_VILLAGE:
      return village(random, settlement, 3, 4);
    case SettlementType::VILLAGE:
      return village(random, settlement, 4, 8);
    case SettlementType::FORREST_VILLAGE:
      return village2(random, settlement);
    case SettlementType::CASTLE:
      return castle(random, settlement);
    case SettlementType::CASTLE2:
      return castle2(random, settlement);
    case SettlementType::COTTAGE:
      return cottage(settlement);
    case SettlementType::FORREST_COTTAGE:
      return forrestCottage(settlement);
    case SettlementType::TOWER:
      return tower(random, settlement, true);
    case SettlementType::TEMPLE:
      return temple(random, settlement);
    case SettlementType::FOREST:
      return emptyCollective(settlement);
    case SettlementType::MINETOWN:
      return mineTownMaker(random, settlement);
    case SettlementType::ANT_NEST:
      return antNestMaker(random, settlement);
    case SettlementType::SMALL_MINETOWN:
      return smallMineTownMaker(random, settlement);
    case SettlementType::ISLAND_VAULT:
      return islandVaultMaker(random, settlement, false);
    case SettlementType::ISLAND_VAULT_DOOR:
      return islandVaultMaker(random, settlement, true);
    case SettlementType::VAULT:
    case SettlementType::CAVE:
      return vaultMaker(settlement);
    case SettlementType::SPIDER_CAVE:
      return spiderCaveMaker(settlement);
    case SettlementType::CEMETERY:
      return cemetery(settlement);
    case SettlementType::MOUNTAIN_LAKE:
      return mountainLake(settlement);
    case SettlementType::SWAMP:
      return swamp(settlement);
  }
}

PLevelMaker LevelMaker::topLevel(RandomGen& random, optional<CreatureGroup> forrestCreatures,
    vector<SettlementInfo> settlements, int mapWidth, optional<TribeId> keeperTribe, BiomeId biomeId,
    ResourceCounts resourceCounts) {
  auto queue = unique<MakerQueue>();
  auto locations = unique<RandomLocations>();
  auto locations2 = unique<RandomLocations>();
  LevelMaker* startingPos = nullptr;
  int locationMargin = 10;
  if (keeperTribe) {
    auto startingPosMaker = unique<StartingPos>(Predicate::alwaysTrue(), StairKey::keeperSpawn());
    startingPos = startingPosMaker.get();
    locations->add(std::move(startingPosMaker), Vec2(4, 4), RandomLocations::LocationPredicate(
        Predicate::attrib(SquareAttrib::HILL) && Predicate::canEnter({MovementTrait::WALK}),
        Predicate::attrib(SquareAttrib::MOUNTAIN), 1, 8));
    int minMargin = 50;
    locations->setMinMargin(startingPos, minMargin - locationMargin);
  }
  struct CottageInfo {
    LevelMaker* maker;
    CollectiveBuilder* collective;
    TribeId tribe;
    int maxDistance;
  };
  vector<CottageInfo> cottages;
  vector<SurroundWithResourcesInfo> surroundWithResources;
  for (SettlementInfo settlement : settlements) {
    auto queue = getSettlementMaker(random, settlement);
    if (settlement.cropsDistance)
      cottages.push_back({queue.get(), settlement.collective, settlement.tribe, *settlement.cropsDistance});
    if (settlement.corpses)
      queue->addMaker(unique<Corpses>(*settlement.corpses));
    if (settlement.surroundWithResources > 0)
      surroundWithResources.push_back({queue.get(), settlement});
    if (settlement.type == SettlementType::SPIDER_CAVE)
      locations2->add(std::move(queue), getSize(random, settlement.type), getSettlementPredicate(settlement));
    else {
      if (keeperTribe && !settlement.anyPlayerDistance) {
        if (settlement.closeToPlayer) {
          locations->setMinDistance(startingPos, queue.get(), 40);
          locations->setMaxDistance(startingPos, queue.get(), 55);
        } else
          locations->setMinDistance(startingPos, queue.get(), 70);
      }
      locations->add(std::move(queue), getSize(random, settlement.type), getSettlementPredicate(settlement));
    }
  }
  Predicate lowlandPred = Predicate::attrib(SquareAttrib::LOWLAND) && !Predicate::attrib(SquareAttrib::RIVER);
  for (auto& cottage : cottages)
    for (int i : Range(random.get(1, 3))) {
      locations->add(unique<MakerQueue>(
            unique<RemoveFurniture>(FurnitureLayer::MIDDLE),
            unique<FurnitureBlob>(SquareChange(FurnitureParams{FurnitureType("CROPS"), cottage.tribe})),
            unique<PlaceCollective>(cottage.collective)),
          {random.get(7, 12), random.get(7, 12)},
          lowlandPred);
      locations->setMaxDistanceLast(cottage.maker, cottage.maxDistance);
    }
  if (biomeId == BiomeId::GRASSLAND || biomeId == BiomeId::FORREST)
    for (int i : Range(random.get(0, 3)))
      locations->add(unique<Lake>(none), {random.get(20, 30), random.get(20, 30)}, Predicate::attrib(SquareAttrib::LOWLAND));
  if (biomeId == BiomeId::DESERT)
    for (int i : Range(random.get(1, 3)))
      locations->add(unique<Lake>(SquareChange::reset(FurnitureType("GRASS")).add(FurnitureType("PALM_TREE"), 0.5)),
          {random.get(7, 12), random.get(7, 12)}, Predicate::attrib(SquareAttrib::LOWLAND));
  if (biomeId == BiomeId::MOUNTAIN)
    for (int i : Range(random.get(3, 6))) {
      locations->add(unique<UniformBlob>(SquareChange::reset(FurnitureType("WATER"), SquareAttrib::LAKE), none),
          {random.get(10, 30), random.get(10, 30)}, Predicate::attrib(SquareAttrib::MOUNTAIN));
    //  locations->setMaxDistanceLast(startingPos, i == 0 ? 25 : 60);
  }
/*  for (int i : Range(random.get(3, 5))) {
    locations->add(unique<UniformBlob>(FurnitureType("FLOOR"), none),
        {random.get(5, 12), random.get(5, 12)}, Predicate::type(SquareId::MOUNTAIN));
 //   locations->setMaxDistanceLast(startingPos, i == 0 ? 25 : 40);
  }*/
  if (keeperTribe) {
    generateResources(random, resourceCounts, startingPos, locations.get(), surroundWithResources, mapWidth, *keeperTribe);
  }
  int mapBorder = 2;
  queue->addMaker(unique<Empty>(FurnitureType("WATER")));
  queue->addMaker(getMountains(biomeId, keeperTribe.value_or(TribeId::getHostile())));
  optional<FurnitureType> waterType;
  if (biomeId == BiomeId::SNOW)
    waterType = FurnitureType("ICE");
  if (biomeId != BiomeId::DESERT)
    queue->addMaker(unique<MountainRiver>(1, Predicate::attrib(SquareAttrib::MOUNTAIN), waterType));
  queue->addMaker(unique<AddAttrib>(SquareAttrib::CONNECT_CORRIDOR, Predicate::attrib(SquareAttrib::LOWLAND)));
  queue->addMaker(unique<AddAttrib>(SquareAttrib::CONNECT_CORRIDOR, Predicate::attrib(SquareAttrib::HILL)));
  queue->addMaker(getForrest(biomeId));
  queue->addMaker(unique<Furnitures>(Predicate::canEnter(MovementTrait::WALK), 0.003, FurnitureListId("randomTerrain"), TribeId::getHostile()));
  queue->addMaker(unique<Margin>(mapBorder + locationMargin, std::move(locations)));
  queue->addMaker(unique<Margin>(mapBorder, unique<Roads>()));
  queue->addMaker(unique<Margin>(mapBorder,
        unique<TransferPos>(Predicate::canEnter(MovementTrait::WALK), StairKey::transferLanding(), 2)));
  queue->addMaker(unique<Margin>(mapBorder, unique<DestroyRandomly>(FurnitureType("RUIN_WALL"), 0.3)));
  queue->addMaker(unique<Margin>(mapBorder, unique<Connector>(none, TribeId::getMonster(), 5,
          Predicate::canEnter({MovementTrait::WALK}) &&
          Predicate::attrib(SquareAttrib::CONNECT_CORRIDOR),
      SquareAttrib::CONNECTOR)));
  queue->addMaker(unique<Margin>(mapBorder + locationMargin, std::move(locations2)));
  queue->addMaker(getItems(biomeId, mapWidth));
  queue->addMaker(unique<AddMapBorder>(mapBorder));
  if (forrestCreatures)
    queue->addMaker(unique<Margin>(mapBorder, getForrestCreatures(*forrestCreatures, mapWidth - 2 * mapBorder, biomeId)));
  return std::move(queue);
}

static PLevelMaker underground(RandomGen& random, vector<WaterType> waterTypes = { WaterType::LAVA, WaterType::WATER }) {
  auto waterType = random.choose(waterTypes);
  auto water = [&] {
    switch (waterType) {
      case WaterType::ICE:
        return FurnitureType("ICE");
      case WaterType::WATER:
        return FurnitureType("WATER");
      case WaterType::LAVA:
        return FurnitureType("MAGMA");
    }
  }();
  auto creatureGroup = [&] {
    switch (waterType) {
      case WaterType::ICE:
        return CreatureGroup::iceCreatures(TribeId::getMonster());
      case WaterType::WATER:
        return CreatureGroup::waterCreatures(TribeId::getMonster());
      case WaterType::LAVA:
        return CreatureGroup::lavaCreatures(TribeId::getMonster());
    }
  }();
  auto queue = unique<MakerQueue>();
  if (random.roll(1)) {
    auto caverns = unique<RandomLocations>();
    int minSize = random.get(5, 15);
    int maxSize = minSize + random.get(3, 10);
    for (int i : Range(sqrt(random.get(4, 100)))) {
      int size = random.get(minSize, maxSize);
      caverns->add(unique<UniformBlob>(SquareChange::reset(FurnitureType("FLOOR"))), Vec2(size, size), Predicate::alwaysTrue());
      caverns->setCanOverlap(caverns->getLast());
    }
    queue->addMaker(std::move(caverns));
  }
  switch (random.get(1, 3)) {
    case 1:
      queue->addMaker(unique<River>(3, water));
      break;
    case 2: {
      int numLakes = sqrt(random.get(1, 100));
      auto caverns = unique<RandomLocations>();
      for (int i : Range(numLakes)) {
        int size = random.get(6, 20);
        caverns->add(unique<UniformBlob>(SquareChange::reset(water, SquareAttrib::LAKE), none), Vec2(size, size), Predicate::alwaysTrue());
        caverns->setCanOverlap(caverns->getLast());
      }
      queue->addMaker(std::move(caverns));
      queue->addMaker(unique<Creatures>(creatureGroup, random.get(1, 4), MonsterAIFactory::monster(), Predicate::type(water)));
      break;
    }
    default: break;
  }
  return std::move(queue);
}

PLevelMaker LevelMaker::getFullZLevel(RandomGen& random, optional<SettlementInfo> settlement, ResourceCounts resourceCounts,
    int mapWidth, TribeId keeperTribe, StairKey landingLink) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(SquareChange(FurnitureType("FLOOR"))
      .add(FurnitureParams{FurnitureType("MOUNTAIN2"), keeperTribe})));
  queue->addMaker(underground(random));
  auto locations = unique<RandomLocations>();
  auto startingPosMaker = unique<MakerQueue>(
      unique<Empty>(SquareChange(FurnitureType("FLOOR"))),
      unique<StartingPos>(Predicate::alwaysTrue(), landingLink));
  LevelMaker* startingPos = startingPosMaker.get();
  vector<SurroundWithResourcesInfo> surroundWithResources;
  if (settlement) {
    auto maker = getSettlementMaker(random, *settlement);
    if (settlement->corpses)
      maker->addMaker(unique<Corpses>(*settlement->corpses));
    maker->addMaker(unique<RandomLocations>(makeVec<PLevelMaker>(std::move(startingPosMaker)), makeVec<pair<int, int>>({1, 1}),
        Predicate::canEnter(MovementTrait::WALK)));
    if (settlement->corpses)
      queue->addMaker(unique<Corpses>(*settlement->corpses));
    if (settlement->surroundWithResources > 0)
      surroundWithResources.push_back({maker.get(), *settlement});
    // assign the whole settlement maker to startingPos, otherwise resource distance constraint doesn't work
    startingPos = maker.get();
    locations->add(std::move(maker), getSize(random, settlement->type),
        RandomLocations::LocationPredicate(Predicate::alwaysTrue()));
  } else {
    locations->add(std::move(startingPosMaker), Vec2(1, 1),
        RandomLocations::LocationPredicate(Predicate::alwaysTrue()));
  }
  locations->setMinMargin(startingPos, mapWidth / 3);
  generateResources(random, resourceCounts, startingPos, locations.get(), surroundWithResources, mapWidth, keeperTribe);
  queue->addMaker(std::move(locations));
  return std::move(queue);
}

PLevelMaker LevelMaker::getWaterZLevel(RandomGen& random, FurnitureType waterType, int mapWidth, CreatureList enemies, StairKey landingLink) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(SquareChange(waterType)));
  auto locations = unique<RandomLocations>();
  LevelMaker* startingPos = nullptr;
  auto startingPosMaker = unique<MakerQueue>(
      unique<Empty>(SquareChange(FurnitureType("FLOOR"))),
      unique<StartingPos>(Predicate::alwaysTrue(), landingLink));
  startingPos = startingPosMaker.get();
  locations->add(std::move(startingPosMaker), Vec2(1, 1),
      RandomLocations::LocationPredicate(Predicate::alwaysTrue()));
  for (int i : Range(5))
    locations->add(unique<UniformBlob>(SquareChange(FurnitureType("FLOOR"))), Vec2(Random.get(5, 10), Random.get(5, 10)),
        RandomLocations::LocationPredicate(Predicate::alwaysTrue()));
  locations->setMinMargin(startingPos, mapWidth / 3);
  queue->addMaker(std::move(locations));
  queue->addMaker(unique<Creatures>(std::move(enemies), TribeId::getMonster(), MonsterAIFactory::monster()));
  return std::move(queue);
}

Vec2 LevelMaker::getRandomExit(RandomGen& random, Rectangle rect, int minCornerDist) {
  CHECK(rect.width() > 2 * minCornerDist && rect.height() > 2 * minCornerDist);
  int w1 = random.get(2);
  int w2 = random.get(2);
  int d1 = random.get(minCornerDist, rect.width() - minCornerDist);
  int d2 = random.get(minCornerDist, rect.height() - minCornerDist);
  return Vec2(
        rect.left() + d1 * w1 + (1 - w1) * w2 * (rect.width() - 1),
        rect.top() + d2 * (1 - w1) + w1 * w2 * (rect.height() - 1));
}

class SpecificArea : public LevelMaker {
  public:
  SpecificArea(Rectangle a, PLevelMaker m) : area(a), maker(std::move(m)) {}

  virtual void make(LevelBuilder* builder, Rectangle) override {
    maker->make(builder, area);
  }

  private:
  Rectangle area;
  PLevelMaker maker;
};

PLevelMaker LevelMaker::splashLevel(CreatureGroup heroLeader, CreatureGroup heroes, CreatureGroup monsters,
    CreatureGroup imps, const FilePath& splashPath) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<Empty>(FurnitureType("BLACK_FLOOR")));
  Rectangle leaderSpawn(
          Level::getSplashVisibleBounds().right() + 1, Level::getSplashVisibleBounds().middle().y,
          Level::getSplashVisibleBounds().right() + 2, Level::getSplashVisibleBounds().middle().y + 1);
  Rectangle heroSpawn(
          Level::getSplashVisibleBounds().right() + 2, Level::getSplashVisibleBounds().middle().y - 1,
          Level::getSplashBounds().right(), Level::getSplashVisibleBounds().middle().y + 2);
  Rectangle monsterSpawn1(
          Level::getSplashVisibleBounds().left(), 0,
          Level::getSplashVisibleBounds().right(), Level::getSplashVisibleBounds().top() - 1);
  Rectangle monsterSpawn2(
          Level::getSplashVisibleBounds().left(), Level::getSplashVisibleBounds().bottom() + 2,
          Level::getSplashVisibleBounds().right(), Level::getSplashBounds().bottom());
  queue->addMaker(unique<SpecificArea>(leaderSpawn, unique<Creatures>(heroLeader, 1,MonsterAIFactory::splashHeroes(true))));
  queue->addMaker(unique<SpecificArea>(heroSpawn, unique<Creatures>(heroes, 22, MonsterAIFactory::splashHeroes(false))));
  queue->addMaker(unique<SpecificArea>(monsterSpawn1, unique<Creatures>(monsters, 17, MonsterAIFactory::splashMonsters())));
  queue->addMaker(unique<SpecificArea>(monsterSpawn2, unique<Creatures>(monsters, 17, MonsterAIFactory::splashMonsters())));
  queue->addMaker(unique<SpecificArea>(monsterSpawn1, unique<Creatures>(imps, 15,
          MonsterAIFactory::splashImps(splashPath))));
  queue->addMaker(unique<SpecificArea>(monsterSpawn2, unique<Creatures>(imps, 15,
          MonsterAIFactory::splashImps(splashPath))));
  queue->addMaker(unique<SetSunlight>(0.0, !Predicate::inRectangle(Level::getSplashVisibleBounds())));
  return std::move(queue);
}

PLevelMaker LevelMaker::roomLevel(RandomGen& random, SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  auto& building = info.buildingInfo;
  SquareChange wall(FurnitureType("FLOOR"), building.wall);
  queue->addMaker(unique<Empty>(wall));
  queue->addMaker(underground(random, building.water));
  queue->addMaker(unique<RoomMaker>(random.get(8, 15), 4, 7));
  queue->addMaker(unique<Connector>(building.door, info.tribe));
  for (auto& furniture : info.furniture)
    queue->addMaker(unique<Furnitures>(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.05, furniture, info.tribe));
  for (StairKey key : info.downStairs)
    queue->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::type(FurnitureType("FLOOR"))));
  for (StairKey key : info.upStairs)
    queue->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::type(FurnitureType("FLOOR"))));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective));
  queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Items>(ItemListId("dungeon"), 5, 10));
  return unique<BorderGuard>(std::move(queue), wall);
}

PLevelMaker LevelMaker::adoxieTemple(RandomGen&, SettlementInfo info) {
  auto queue = unique<MakerQueue>();
  auto& building = info.buildingInfo;
  queue->addMaker(unique<Empty>(SquareChange(FurnitureType("FLOOR"))
      .add(FurnitureType("MOUNTAIN2"))));
  queue->addMaker(unique<Margin>(5, unique<UniformBlob>(SquareChange::reset(FurnitureType("MAGMA")))));
  auto locations = unique<RandomLocations>();
  const int templeRoomSize = 5;
  const int templeRoomMargin = 5;
  auto templeRoom = unique<MakerQueue>();
  auto floor = building.floorInside.value_or(FurnitureType("FLOOR"));
  templeRoom->addMaker(unique<Empty>(SquareChange::reset(FurnitureType("FLOOR"))
      .add(building.prettyFloor.value_or(FurnitureType("FLOOR")))));
  templeRoom->addMaker(unique<PlaceCollective>(info.collective));
  templeRoom->addMaker(unique<AreaCorners>(unique<Empty>(building.wall), Vec2(1, 1)));
  for (auto& furniture : info.furniture)
    templeRoom->addMaker(unique<Margin>((templeRoomSize - 1) / 2,
        unique<Furnitures>(Predicate::alwaysTrue(), 0.05, furniture, info.tribe)));
  for (StairKey key : info.downStairs)
    templeRoom->addMaker(unique<Margin>((templeRoomSize - 1) / 2,
        unique<StartingPos>(Predicate::type(FurnitureType("FLOOR")), key)));
  templeRoom->addMaker(unique<Inhabitants>(info.inhabitants, info.collective, Predicate::near4AtLeast(FurnitureType("MAGMA"), 1)));
  locations->add(unique<Margin>(templeRoomMargin, std::move(templeRoom)),
      Vec2(templeRoomSize + 2 * templeRoomMargin, templeRoomSize + 2 * templeRoomMargin),
      RandomLocations::LocationPredicate(Predicate::type(FurnitureType("MAGMA"))));
  auto entryRoom = unique<MakerQueue>();
  entryRoom->addMaker(unique<Empty>(SquareChange::reset(floor)));
  for (StairKey key : info.downStairs)
    entryRoom->addMaker(unique<Stairs>(StairDirection::DOWN, key, building, Predicate::type(floor)));
  for (StairKey key : info.upStairs)
    entryRoom->addMaker(unique<Stairs>(StairDirection::UP, key, building, Predicate::type(floor)));
  locations->add(unique<Margin>(1, std::move(entryRoom)), Vec2(7, 7),
      RandomLocations::LocationPredicate(Predicate::type(FurnitureType("MOUNTAIN2"))));
  queue->addMaker(std::move(locations));
  queue->addMaker(unique<Connector>(none, info.tribe));
  return PLevelMaker(queue.release());
}

namespace {

class SokobanFromFile : public LevelMaker {
  public:
  SokobanFromFile(Table<char> f, StairKey hole) : file(f), holeKey(hole) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    CHECK(area == file.getBounds()) << "Bad size of sokoban input.";
    builder->setNoDiagonalPassing();
    for (Vec2 v : area) {
      builder->resetFurniture(v, FurnitureType("FLOOR"));
      switch (file[v]) {
        case '.':
          break;
        case '#':
          builder->putFurniture(v, FurnitureType("DUNGEON_WALL"));
          break;
        case '^':
          builder->putFurniture(v, FurnitureType("SOKOBAN_HOLE"));
          break;
        case '$':
          builder->addAttrib(v, SquareAttrib::SOKOBAN_PRIZE);
          break;
        case '@':
          builder->addAttrib(v, SquareAttrib::SOKOBAN_ENTRY);
          break;
        case '+':
          builder->putFurniture(v, FurnitureParams{FurnitureType("IRON_DOOR"), TribeId::getHostile()});
          break;
        case '0':
          builder->putCreature(v, builder->getContentFactory()->getCreatures().fromId(CreatureId("SOKOBAN_BOULDER"),
              TribeId::getPeaceful()));
          break;
        default: FATAL << "Unknown symbol in sokoban data: " << file[v];
      }
    }
  }

  Table<char> file;
  StairKey holeKey;
};

}

PLevelMaker LevelMaker::sokobanFromFile(RandomGen& random, SettlementInfo info, Table<char> file) {
  auto queue = unique<MakerQueue>();
  queue->addMaker(unique<SokobanFromFile>(file, info.downStairs.getOnlyElement()));
  queue->addMaker(unique<Stairs>(StairDirection::DOWN, info.downStairs.getOnlyElement(), info.buildingInfo,
        Predicate::attrib(SquareAttrib::SOKOBAN_ENTRY)));
  //queue->addMaker(unique<PlaceCollective>(info.collective));
  queue->addMaker(unique<Inhabitants>(info.inhabitants, info.collective, Predicate::attrib(SquareAttrib::SOKOBAN_PRIZE)));
  return std::move(queue);
}

namespace {

class BattleFromFile : public LevelMaker {
  public:
  BattleFromFile(Table<char> f, CreatureList a, CreatureList e)
      : level(f), allies(a), enemies(e) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    CHECK(area == level.getBounds()) << "Bad size of battle level input.";
    auto alliesList = allies.generate(builder->getRandom(), &builder->getContentFactory()->getCreatures(), TribeId::getDarkKeeper(),
        MonsterAIFactory::guard());
    int allyIndex = 0;
    auto enemyList = enemies.generate(builder->getRandom(), &builder->getContentFactory()->getCreatures(), TribeId::getHuman(),
        MonsterAIFactory::monster());
    int enemyIndex = 0;
    for (Vec2 v : area) {
      builder->resetFurniture(v, FurnitureType("FLOOR"));
      switch (level[v]) {
        case '.':
          break;
        case '#':
          builder->putFurniture(v, FurnitureType("MOUNTAIN"));
          break;
        case 'w':
          builder->putFurniture(v, FurnitureType("WATER"));
          break;
        case 'a':
          if (allyIndex < alliesList.size()) {
            builder->putCreature(v, std::move(alliesList[allyIndex]));
            ++allyIndex;
          }
          break;
        case 'e':
          if (enemyIndex < enemyList.size()) {
            builder->putCreature(v, std::move(enemyList[enemyIndex]));
            ++enemyIndex;
          }
          break;
        default: FATAL << "Unknown symbol in battle test data: " << level[v];
      }
    }
  }

  Table<char> level;
  CreatureList allies;
  CreatureList enemies;
};

}

PLevelMaker LevelMaker::battleLevel(Table<char> level, CreatureList allies, CreatureList enemies) {
  return unique<BattleFromFile>(level, allies, enemies);
}

PLevelMaker LevelMaker::emptyLevel(FurnitureType t, bool withFloor) {
  auto queue = unique<MakerQueue>();
  SquareChange change(t);
  if (withFloor)
    change = SquareChange(FurnitureType("FLOOR"), t);
  queue->addMaker(unique<Empty>(change));
  return std::move(queue);
}
