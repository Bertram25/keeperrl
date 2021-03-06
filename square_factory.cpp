#include "stdafx.h"

#include "square_factory.h"
#include "square.h"
#include "creature.h"
#include "level.h"
#include "item_factory.h"
#include "creature_factory.h"

using namespace std;

class Staircase : public Square {
  public:
  Staircase(const ViewObject& obj, const string& name, StairDirection dir, StairKey key)
      : Square(obj, name, true, true, 10000) {
    setLandingLink(dir, key);
  }

  virtual void onEnterSpecial(Creature* c) override {
    c->privateMessage("There are " + getName() + " here.");
  }

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override {
    switch (getLandingLink()->first) {
      case StairDirection::DOWN: return SquareApplyType::DESCEND;
      case StairDirection::UP: return SquareApplyType::ASCEND;
    }
    return Nothing();
  }

  virtual void onApply(Creature* c) override {
    auto link = getLandingLink();
    getLevel()->changeLevel(link->first, link->second, c);
  }
};

class SecretPassage : public Square {
  public:
  SecretPassage(const ViewObject& obj, const ViewObject& sec) : Square(obj, "secret door", false), secondary(sec), uncovered(false) {
  }

  void uncover(Vec2 pos) {
    uncovered = true;
    setName("floor");
    setViewObject(secondary);
    face.clear();
    setCanSeeThru(true);
    getLevel()->updateVisibility(pos);
  }

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void destroy(int strength) override {
    if (uncovered)
      return;
    if (getLevel()->playerCanSee(getPosition())) {
      getLevel()->globalMessage(getPosition(), "A secret passage is destroyed!");
      uncover(getPosition());
    }
  }

  virtual void onEnterSpecial(Creature* c) override {
    if (uncovered)
      return;
    if (c->isPlayer()) {
      c->privateMessage("You found a secret passage!");
      uncover(c->getPosition());
    } else 
    if (getLevel()->playerCanSee(c->getPosition())) {
      getLevel()->globalMessage(getPosition(), c->getTheName() + " uncovers a secret passage!");
      uncover(c->getPosition());
    }
  }

  private:
  ViewObject secondary;
  bool uncovered;
};

class Magma : public Square {
  public:
  Magma(const ViewObject& object, const string& name, const string& itemMsg, const string& noSee)
      : Square(object, name, true, false, 0, 0, {{SquareType::BRIDGE, 20}}), itemMessage(itemMsg), noSeeMsg(noSee) {}

  virtual bool canEnterSpecial(const Creature* c) const override {
    return c->canFly() || c->isBlind() || c->isHeld();
  }

  virtual void onEnterSpecial(Creature* c) override {
    if (!c->canFly()) {
      c->you(MsgType::BURN, getName());
      c->die(nullptr, false);
    }
  }

  virtual void dropItem(PItem item) override {
    getLevel()->globalMessage(getPosition(), item->getTheName() + " " + itemMessage, noSeeMsg);
  }

  virtual bool itemBounces(Item*) const override {
    return false;
  }

  private:
  string itemMessage;
  string noSeeMsg;
};

class Water : public Square {
  public:
  Water(ViewObject object, const string& name, const string& itemMsg, const string& noSee, double _depth)
      : Square(object.setWaterDepth(_depth), name, true, false, 0, 0, {{SquareType::BRIDGE, 20}}),
        itemMessage(itemMsg), noSeeMsg(noSee), depth(_depth) {}

  bool canWalk(const Creature* c) const {
    switch (c->getSize()) {
      case CreatureSize::HUGE: return depth < 3;
      case CreatureSize::LARGE: return depth < 1.5;
      case CreatureSize::MEDIUM: return depth < 1;
      case CreatureSize::SMALL: return depth < 0.3;
    }
    return false;
  }

  virtual bool canEnterSpecial(const Creature* c) const override {
    bool can = canWalk(c) || c->canSwim() || c->canFly() || c->isBlind() || c->isHeld();
 /*   if (!can)
      c->privateMessage("The water is too deep.");*/
    return can;
  }

  virtual void onEnterSpecial(Creature* c) override {
    if (!c->canFly() && !c->canSwim() && !canWalk(c)) {
      c->you(MsgType::DROWN, getName());
      c->die(nullptr, false);
    }
  }

  virtual void dropItem(PItem item) override {
    getLevel()->globalMessage(getPosition(), item->getTheName() + " " + itemMessage, noSeeMsg);
  }

  virtual bool itemBounces(Item*) const override {
    return false;
  }

  private:
  string itemMessage;
  string noSeeMsg;
  double depth;
};

class Chest : public Square {
  public:
  Chest(const ViewObject& object, const ViewObject& opened, const string& name, CreatureId id, int minC, int maxC,
        const string& _msgItem, const string& _msgMonster, const string& _msgGold,
        ItemFactory _itemFactory) : 
      Square(object, name, true, true, 30, 0.5), creatureId(id), minCreatures(minC), maxCreatures(maxC), msgItem(_msgItem), msgMonster(_msgMonster), msgGold(_msgGold), itemFactory(_itemFactory), openedObject(opened) {}

  virtual void onEnterSpecial(Creature* c) override {
    c->privateMessage(string("There is a ") + (opened ? " opened " : "") + getName() + " here");
  }

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void onConstructNewSquare(Square* s) override {
    if (opened)
      return;
    vector<PItem> item;
    if (!Random.roll(10))
      append(item, itemFactory.random());
    else {
      for (int i : Range(Random.getRandom(minCreatures, maxCreatures)))
        item.push_back(ItemFactory::corpse(creatureId));
    }
    s->dropItems(std::move(item));
  }

  virtual Optional<SquareApplyType> getApplyType(const Creature* c) const override { 
    if (opened || !c->isHumanoid()) 
      return Nothing();
    else
      return SquareApplyType::USE_CHEST;
  }

  virtual void onApply(Creature* c) override {
    CHECK(!opened);
    c->privateMessage("You open the " + getName());
    opened = true;
    setViewObject(openedObject);
    if (!Random.roll(5)) {
      c->privateMessage(msgItem);
      vector<PItem> items = itemFactory.random();
      EventListener::addItemsAppeared(getLevel(), getPosition(), Item::extractRefs(items));
      c->takeItems(std::move(items), nullptr);
    } else {
      c->privateMessage(msgMonster);
      int numR = Random.getRandom(minCreatures, maxCreatures);
      for (Vec2 v : getPosition().neighbors8(true)) {
        PCreature rat = CreatureFactory::fromId(creatureId, Tribe::pest);
        if (getLevel()->getSquare(v)->canEnter(rat.get())) {
          getLevel()->addCreature(v, std::move(rat));
          if (--numR == 0)
            break;
        }
      }
    }
  }
  private:
  CreatureId creatureId;
  int minCreatures, maxCreatures;
  string msgItem, msgMonster, msgGold;
  bool opened = false;
  ItemFactory itemFactory;
  ViewObject openedObject;
};

class Fountain : public Square {
  public:
  Fountain(const ViewObject& object) : Square(object, "fountain", true, true, 100) {}

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override { 
    return SquareApplyType::DRINK;
  }

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void onEnterSpecial(Creature* c) override {
    c->privateMessage("There is a " + getName() + " here");
  }

  virtual void onApply(Creature* c) override {
    c->privateMessage("You drink from the fountain.");
    PItem potion = getOnlyElement(ItemFactory::potions().random(seed));
    potion->apply(c, getLevel());
  }

  private:
  int seed = Random.getRandom(123456);
};

class Tree : public Square {
  public:
  Tree(const ViewObject& object, const string& name, bool noObstruct, int _numWood, map<SquareType, int> construct)
      : Square(object, name, noObstruct, true, 100, 0.5, construct), numWood(_numWood), bounces(!noObstruct) {}

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void destroy(int strength) override {
    if (destroyed)
      return;
    getLevel()->globalMessage(getPosition(), "The tree falls.");
    destroyed = true;
    setCanSeeThru(true);
    getLevel()->updateVisibility(getPosition());
    setViewObject(ViewObject(ViewId::FALLEN_TREE, ViewLayer::FLOOR, "Fallen tree"));
  }

  virtual void onConstructNewSquare(Square* s) override {
    s->dropItems(ItemFactory::fromId(ItemId::WOOD_PLANK, numWood));
  }

  virtual void burnOut() override {
    setCanSeeThru(true);
    getLevel()->updateVisibility(getPosition());
    setViewObject(ViewObject(ViewId::BURNT_TREE, ViewLayer::FLOOR, "Burnt tree"));
  }

  virtual bool itemBounces(Item* item) const {
    return bounces || Random.roll(2);
  }

  virtual void onEnterSpecial(Creature* c) override {
 /*   c->privateMessage(isBurnt() ? "There is a burnt tree here." : 
        destroyed ? "There is fallen tree here." : "You pass beneath a tree");*/
  }

  private:
  bool destroyed = false;
  int numWood;
  bool bounces;
};

class TrapSquare : public Square {
  public:
  TrapSquare(const ViewObject& object, EffectType e) : Square(object, "floor", true), effect(e) {
  }

  virtual void onEnterSpecial(Creature* c) override {
    if (active && c->isPlayer()) {
      c->you(MsgType::TRIGGER_TRAP, "");
      Effect::applyToCreature(c, effect, EffectStrength::NORMAL);
      active = false;
    }
  }

  private:
  bool active = true;
  EffectType effect;
  Tribe* tribe;
};

class Door : public Square {
  public:
  Door(const ViewObject& object) : Square(object, "door", false, true, 100, 1) {}

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void onEnterSpecial(Creature* c) override {
    c->privateMessage("You open the door.");
  }
};

class TribeDoor : public Door {
  public:
  TribeDoor(const ViewObject& object, int destStrength) : Door(object), destructionStrength(destStrength) {}

  virtual void destroy(int strength) override {
    destructionStrength -= strength;
    if (destructionStrength <= 0) {
      EventListener::addSquareReplacedEvent(getLevel(), getPosition());
      getLevel()->replaceSquare(getPosition(), PSquare(SquareFactory::get(SquareType::FLOOR)));
    }
  }

  virtual bool canEnterSpecial(const Creature* c) const override {
    return (c->canWalk() && c->getTribe() == Tribe::player);
  }

  private:
  int destructionStrength;
};

class Furniture : public Square {
  public:
  Furniture(const ViewObject& object, const string& name, double flamability) 
      : Square(object, name, true , true, 100, flamability) {}

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void onEnterSpecial(Creature* c) override {
   // c->privateMessage("There is a " + getName() + " here.");
  }
};

class Bed : public Furniture {
  public:
  Bed(const ViewObject& object, const string& name) : Furniture(object, name, 1) {}

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override { 
    return SquareApplyType::SLEEP;
  }

  virtual void onApply(Creature* c) override {
    Effect::applyToCreature(c, EffectType::SLEEP, EffectStrength::STRONG);
    getLevel()->addTickingSquare(getPosition());
  }

  virtual void tickSpecial(double time) override {
    if (getCreature() && getCreature()->isSleeping())
      getCreature()->heal(0.005);
  }
};

class Grave : public Bed {
  public:
  Grave(const ViewObject& object, const string& name) : Bed(object, name) {}

  virtual Optional<SquareApplyType> getApplyType(const Creature* c) const override { 
    if (c->isUndead())
      return SquareApplyType::SLEEP;
    else
      return Nothing();
  }

  virtual void onApply(Creature* c) override {
    if (c->getName() != "vampire")
      return;
    Bed::onApply(c);
  }
};

class Altar : public Square {
  public:
  Altar(const ViewObject& object, Deity* d)
      : Square(object, "shrine to " + d->getName(), true , true, 100, 0), deity(d) {
  }

  virtual bool canDestroy() const override {
    return true;
  }

  virtual void onEnterSpecial(Creature* c) override {
    c->privateMessage("This is a shrine to " + deity->getName());
    c->privateMessage((deity->getGender() == Gender::MALE ? "He lives in " : "She lives in ")
        + deity->getHabitatString());
    c->privateMessage((deity->getGender() == Gender::MALE ? "He is the god of " : "She is the goddess of ")
        + deity->getEpithets());
  }

  virtual Optional<SquareApplyType> getApplyType(const Creature* c) const override { 
    if (c->isHumanoid())
      return SquareApplyType::PRAY;
    else
      return Nothing();
  }

  virtual void onApply(Creature* c) override {
    c->privateMessage("You pray to " + deity->getName());
    deity->onPrayer(c);
  }

  private:
  Deity* deity;
};

class ConstructionDropItems : public SolidSquare {
  public:
  ConstructionDropItems(const ViewObject& object, const string& name,
      map<SquareType, int> constructions, vector<PItem> _items)
      : SolidSquare(object, name, false, constructions), items(std::move(_items)) {}

  virtual void onConstructNewSquare(Square* s) override {
    s->dropItems(std::move(items));
  }

  private:
  vector<PItem> items;
};

class TrainingDummy : public Furniture {
  public:
  TrainingDummy(const ViewObject& object, const string& name) : Furniture(object, name, 1) {}

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override { 
    return SquareApplyType::TRAIN;
  }

  virtual void onApply(Creature* c) override {
    if (Random.roll(50)) {
      c->increaseExpLevel(1);
    }
  }
};

class Library : public TrainingDummy {
  public:
  Library(const ViewObject& object, const string& name) : TrainingDummy(object, name) {
    spell = chooseRandom({SpellId::HEALING, SpellId::TELEPORT, SpellId::INVISIBILITY, SpellId::WORD_OF_POWER});
  }

  virtual void onApply(Creature* c) override {
 /*   if (Random.roll(50)) {
      c->addSpell(spell);
    }*/
  }

  private:
  SpellId spell;
};

class Workshop : public Furniture {
  public:
  using Furniture::Furniture;

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override { 
    return SquareApplyType::WORKSHOP;
  }

  virtual void onApply(Creature* c) override {
  }
};

class Hatchery : public Square {
  public:
  Hatchery(const ViewObject& object, const string& name) : Square(object, name, true, false, 0, 0, {}, true) {}

  virtual void tickSpecial(double time) override {
    if (getCreature() || !Random.roll(10))
      return;
    for (Vec2 v : getPosition().neighbors8())
      if (Creature* c = getLevel()->getSquare(v)->getCreature())
        if (c->getName() == "chicken")
          return;
    getLevel()->addCreature(getPosition(), CreatureFactory::fromId(CreatureId::CHICKEN, Tribe::peaceful,
          MonsterAIFactory::moveRandomly()));
  }

  virtual bool canEnterSpecial(const Creature* c) const override {
    return c->canWalk() || c->getName() == "chicken" || c->getName() == "pig";
  }

};

class Throne : public Furniture {
  public:
  Throne(const ViewObject& object, const string& name) : Furniture(object, name, 1) {}

  virtual Optional<SquareApplyType> getApplyType(const Creature*) const override { 
    return SquareApplyType::WORKSHOP;
  }

  virtual void onApply(Creature* c) override {
    c->privateMessage("You sit on the throne.");
  }
};

class Laboratory : public Workshop {
  public:
  using Workshop::Workshop;

  virtual void onApply(Creature* c) override {
    c->privateMessage("You mix the concoction.");
  }
};

Square* SquareFactory::getAltar(Deity* deity) {
  return new Altar(ViewObject(ViewId::ALTAR, ViewLayer::FLOOR, "Shrine"), deity);
}

Square* SquareFactory::get(SquareType s) {
  switch (s) {
    case SquareType::PATH:
    case SquareType::FLOOR:
        return new Square(ViewObject(ViewId::PATH, ViewLayer::FLOOR_BACKGROUND, "Floor"), "floor", true, false, 0, 0, 
            {{SquareType::TREASURE_CHEST, 10}, {SquareType::BED, 10}, {SquareType::TRIBE_DOOR, 10},
            {SquareType::TRAINING_DUMMY, 10}, {SquareType::LIBRARY, 10}, {SquareType::STOCKPILE, 1},
            {SquareType::GRAVE, 10}, {SquareType::WORKSHOP, 10},
            {SquareType::LABORATORY, 10}});
    case SquareType::BRIDGE:
        return new Square(ViewObject(ViewId::BRIDGE, ViewLayer::FLOOR_BACKGROUND,"Rope bridge"), "rope bridge", true);
    case SquareType::GRASS:
        return new Square(ViewObject(ViewId::GRASS, ViewLayer::FLOOR_BACKGROUND, "Grass"), "grass", true, false, 0, 0, {{SquareType::ANIMAL_TRAP, 10}});
    case SquareType::CROPS:
        return new Square(ViewObject(ViewId::CROPS, ViewLayer::FLOOR_BACKGROUND, "Potatoes"),
            "potatoes", true, false, 0, 0);
    case SquareType::MUD:
        return new Square(ViewObject(ViewId::MUD, ViewLayer::FLOOR_BACKGROUND, "Mud"), "mud", true);
    case SquareType::ROAD:
        return new Square(ViewObject(ViewId::ROAD, ViewLayer::FLOOR, "Road"), "road", true);
    case SquareType::ROCK_WALL:
        return new SolidSquare(ViewObject(ViewId::WALL, ViewLayer::FLOOR, "Wall", true), "wall", false,
            {{SquareType::FLOOR, Random.getRandom(3, 8)}});
    case SquareType::GOLD_ORE:
        return new ConstructionDropItems(ViewObject(ViewId::GOLD_ORE, ViewLayer::FLOOR, "Gold ore", true), "gold ore",
            {{SquareType::FLOOR, Random.getRandom(30, 80)}},
            ItemFactory::fromId(ItemId::GOLD_PIECE, Random.getRandom(30, 60)));
    case SquareType::IRON_ORE:
        return new ConstructionDropItems(ViewObject(ViewId::IRON_ORE, ViewLayer::FLOOR, "Iron ore", true), "iron ore",
            {{SquareType::FLOOR, Random.getRandom(30, 80)}},
            ItemFactory::fromId(ItemId::IRON_ORE, Random.getRandom(5, 20)));
    case SquareType::STONE:
        return new ConstructionDropItems(ViewObject(ViewId::STONE, ViewLayer::FLOOR, "Stone", true), "stone",
            {{SquareType::FLOOR, Random.getRandom(30, 80)}},
            ItemFactory::fromId(ItemId::ROCK, Random.getRandom(5, 20)));
    case SquareType::LOW_ROCK_WALL:
        return new SolidSquare(ViewObject(ViewId::LOW_ROCK_WALL, ViewLayer::FLOOR, "Wall"), "wall", false);
    case SquareType::WOOD_WALL:
        return new SolidSquare(ViewObject(ViewId::WOOD_WALL, ViewLayer::FLOOR, "Wooden wall", true), "wall", false,
            {}, false, 1);
    case SquareType::BLACK_WALL:
        return new SolidSquare(ViewObject(ViewId::BLACK_WALL, ViewLayer::FLOOR, "Wall", true), "wall", false);
    case SquareType::YELLOW_WALL:
        return new SolidSquare(ViewObject(ViewId::YELLOW_WALL, ViewLayer::FLOOR, "Wall", true), "wall", false);
    case SquareType::HELL_WALL:
        return new SolidSquare(ViewObject(ViewId::HELL_WALL, ViewLayer::FLOOR, "Wall", true), "wall", false);
    case SquareType::CASTLE_WALL:
        return new SolidSquare(ViewObject(ViewId::CASTLE_WALL, ViewLayer::FLOOR, "Wall", true), "wall", false);
    case SquareType::MUD_WALL:
        return new SolidSquare(ViewObject(ViewId::MUD_WALL, ViewLayer::FLOOR, "Wall", true), "wall", false);
    case SquareType::MOUNTAIN:
        return new SolidSquare(ViewObject(ViewId::MOUNTAIN, ViewLayer::FLOOR, "Mountain"), "mountain", true);
    case SquareType::MOUNTAIN2:
        return new SolidSquare(ViewObject(ViewId::MOUNTAIN2, ViewLayer::FLOOR, "Mountain"), "mountain", false,
            {{SquareType::FLOOR, Random.getRandom(3, 8)}});
    case SquareType::GLACIER:
        return new SolidSquare(ViewObject(ViewId::SNOW, ViewLayer::FLOOR, "Mountain"), "mountain", true);
    case SquareType::HILL:
        return new Square(ViewObject(ViewId::HILL, ViewLayer::FLOOR_BACKGROUND, "Hill"), "hill", true);
    case SquareType::SECRET_PASS:
        return new SecretPassage(ViewObject(ViewId::SECRETPASS, ViewLayer::FLOOR, "Wall"),
                                 ViewObject(ViewId::FLOOR, ViewLayer::FLOOR, "Floor"));
    case SquareType::WATER:
        return new Water(ViewObject(ViewId::WATER, ViewLayer::FLOOR, "Water"), "water",
            "sinks in the water", "You hear a splash", 100);
    case SquareType::MAGMA: 
        return new Magma(ViewObject(ViewId::MAGMA, ViewLayer::FLOOR, "Magma"),
            "magma", "burns in the magma", "");
    case SquareType::ABYSS: 
        FAIL << "Unimplemented";
    case SquareType::SAND: return new Square(ViewObject(ViewId::SAND, ViewLayer::FLOOR_BACKGROUND, "Sand"),
                               "sand", true);
    case SquareType::CANIF_TREE: return new Tree(ViewObject(ViewId::CANIF_TREE, ViewLayer::FLOOR, "Tree"), "tree", 
                                     false, Random.getRandom(15, 30), {{SquareType::TREE_TRUNK, 20}});
    case SquareType::DECID_TREE: return new Tree(ViewObject(ViewId::DECID_TREE, ViewLayer::FLOOR, "Tree"), "tree",
                                     false, Random.getRandom(15, 30), {{SquareType::TREE_TRUNK, 20}});
    case SquareType::BUSH: return new Tree(ViewObject(ViewId::BUSH, ViewLayer::FLOOR, "Bush"), "bush",
                                     true, Random.getRandom(5, 10), {{SquareType::TREE_TRUNK, 10}});
    case SquareType::TREE_TRUNK: return new Furniture(ViewObject(ViewId::TREE_TRUNK, ViewLayer::FLOOR, "tree trunk"),
                                   "tree trunk", 0);
    case SquareType::BED: return new Bed(ViewObject(ViewId::BED, ViewLayer::FLOOR, "Bed"), "bed");
    case SquareType::STOCKPILE:
        return new Furniture(ViewObject(ViewId::STOCKPILE, ViewLayer::FLOOR_BACKGROUND, "Floor"), "floor", 0);
    case SquareType::TORTURE_TABLE:
        return new Furniture(ViewObject(ViewId::TORTURE_TABLE, ViewLayer::FLOOR, "Torture table"), 
            "torture table", 0.3);
    case SquareType::ANIMAL_TRAP:
        return new Furniture(ViewObject(ViewId::ANIMAL_TRAP, ViewLayer::FLOOR, "Animal trap"), 
            "animal trap", 0.3);
    case SquareType::TRAINING_DUMMY:
        return new TrainingDummy(ViewObject(ViewId::TRAINING_DUMMY, ViewLayer::FLOOR, "Training post"), 
            "training post");
    case SquareType::LIBRARY:
        return new Library(ViewObject(ViewId::LIBRARY, ViewLayer::FLOOR, "Book shelf"), 
            "book shelf");
    case SquareType::LABORATORY: return new Laboratory(ViewObject(ViewId::LABORATORY, ViewLayer::FLOOR, "cauldron"),
                                   "cauldron", 0);
    case SquareType::WORKSHOP:
        return new Workshop(ViewObject(ViewId::WORKSHOP, ViewLayer::FLOOR, "Workshop stand"), 
            "workshop stand", 1);
    case SquareType::HATCHERY:
        return new Hatchery(ViewObject(ViewId::MUD, ViewLayer::FLOOR_BACKGROUND, "Hatchery"), "hatchery");
    case SquareType::KEEPER_THRONE:
        return new Throne(ViewObject(ViewId::THRONE, ViewLayer::FLOOR, "Throne"), "throne");
    case SquareType::ALTAR:
        FAIL << "Altars are not handled by this method.";
    case SquareType::ROLLING_BOULDER: return new TrapSquare(ViewObject(ViewId::FLOOR, ViewLayer::FLOOR, "floor"),
                                          EffectType::ROLLING_BOULDER);
    case SquareType::POISON_GAS: return new TrapSquare(ViewObject(ViewId::FLOOR, ViewLayer::FLOOR, "floor"),
                                          EffectType::EMIT_POISON_GAS);
    case SquareType::FOUNTAIN:
        return new Fountain(ViewObject(ViewId::FOUNTAIN, ViewLayer::FLOOR, "Fountain"));
    case SquareType::CHEST:
        return new Chest(ViewObject(ViewId::CHEST, ViewLayer::FLOOR, "Chest"), ViewObject(ViewId::OPENED_CHEST, ViewLayer::FLOOR, "Opened chest"), "chest", CreatureId::RAT, 3, 6, "There is an item inside", "It's full of rats!", "There is gold inside", ItemFactory::chest());
    case SquareType::TREASURE_CHEST:
        return new Furniture(ViewObject(ViewId::CHEST, ViewLayer::FLOOR, "Chest"), "chest", 1);
    case SquareType::COFFIN:
        return new Chest(ViewObject(ViewId::COFFIN, ViewLayer::FLOOR, "Coffin"), ViewObject(ViewId::OPENED_COFFIN, ViewLayer::FLOOR, "Coffin"),"coffin", CreatureId::VAMPIRE, 1, 2, "There is a rotting corpse inside. You find an item.", "There is a rotting corpse inside. The corpse is alive!", "There is a rotting corpse inside. You find some gold.", ItemFactory::chest());
    case SquareType::GRAVE:
        return new Grave(ViewObject(ViewId::GRAVE, ViewLayer::FLOOR, "Grave"), "grave");
    case SquareType::IRON_BARS:
        FAIL << "Unimplemented";
    case SquareType::DOOR: return new Door(ViewObject(ViewId::DOOR, ViewLayer::FLOOR, "Door"));
    case SquareType::TRIBE_DOOR: return new TribeDoor(ViewObject(ViewId::DOOR, ViewLayer::LARGE_ITEM, "Door"), 100);
    case SquareType::BORDER_GUARD:
        return new SolidSquare(ViewObject(ViewId::BORDER_GUARD, ViewLayer::FLOOR, "Wall"), "wall", false);
    case SquareType::DOWN_STAIRS:
    case SquareType::UP_STAIRS: FAIL << "Stairs are not handled by this method.";
  }
  return 0;
}

Square* SquareFactory::getStairs(StairDirection direction, StairKey key, StairLook look) {
  ViewId id1 = ViewId(0), id2 = ViewId(0);
  switch (look) {
    case StairLook::NORMAL: id1 = ViewId::UP_STAIRCASE; id2 = ViewId::DOWN_STAIRCASE; break;
    case StairLook::HELL: id1 = ViewId::UP_STAIRCASE_HELL; id2 = ViewId::DOWN_STAIRCASE_HELL; break;
    case StairLook::CELLAR: id1 = ViewId::UP_STAIRCASE_CELLAR; id2 = ViewId::DOWN_STAIRCASE_CELLAR; break;
    case StairLook::PYRAMID: id1 = ViewId::UP_STAIRCASE_PYR; id2 = ViewId::DOWN_STAIRCASE_PYR; break;
    case StairLook::DUNGEON_ENTRANCE: id1 = id2 = ViewId::DUNGEON_ENTRANCE; break;
    case StairLook::DUNGEON_ENTRANCE_MUD: id1 = id2 = ViewId::DUNGEON_ENTRANCE_MUD; break;
  }
  switch (direction) {
    case StairDirection::UP:
        return new Staircase(ViewObject(id1, ViewLayer::FLOOR, "Stairs leading up"),
            "stairs leading up", direction, key);
    case StairDirection::DOWN:
        return new Staircase(ViewObject(id2, ViewLayer::FLOOR, "Stairs leading down"),
            "stairs leading down", direction, key);
  }
  return nullptr;
}
  
Square* SquareFactory::getWater(double depth) {
  return new Water(ViewObject(ViewId::WATER, ViewLayer::FLOOR, "Water"), "water",
      "sinks in the water", "You hear a splash", depth);
}
