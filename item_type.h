#pragma once

#include "util.h"

class ItemAttributes;
class ContentFactory;
class ItemTypeVariant;
class VictimEffect;

class ItemType {
  public:
  static ItemType legs(int damage);
  static ItemType claws(int damage);
  static ItemType beak(int damage);
  static ItemType fists(int damage);
  static ItemType fangs(int damage);
  static ItemType fangs(int damage, VictimEffect);
  static ItemType spellHit(int damage);

  ItemType(const ItemTypeVariant&);
  ItemType();
  STRUCT_DECLARATIONS(ItemType)
  bool operator == (const ItemType&) const;
  bool operator != (const ItemType&) const;

  ItemType& setPrefixChance(double chance);

  template <class Archive>
  void serialize(Archive&, const unsigned int);

  PItem get(const ContentFactory*) const;
  vector<PItem> get(int, const ContentFactory*) const;

  HeapAllocatedSerializationWorkaround<ItemTypeVariant> SERIAL(type);

  private:
  ItemAttributes getAttributes(const ContentFactory*) const;
  double SERIAL(prefixChance) = 0.0;
};
