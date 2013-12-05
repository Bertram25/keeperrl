#include "stdafx.h"

using namespace std;

Creature* Creature::getDefault() {
  static PCreature defaultCreature = CreatureFactory::fromId(CreatureId::GNOME, Tribe::monster,
      MonsterAIFactory::idle());
  return defaultCreature.get();
}

Creature::Creature(ViewObject o, Tribe* t, const CreatureAttributes& attr, ControllerFactory f) : CreatureAttributes(attr), viewObject(o), time(0), tribe(t), dead(false), lastTick(0), controller(f.get(this)) {
  static int cnt = 1;
  uniqueId = ++cnt;
  for (Skill* skill : skills)
    skill->onTeach(this);
}

ViewIndex Creature::getViewIndex(Vec2 pos) const {
  return level->getSquare(pos)->getViewIndex(this);
}

void Creature::pushController(Controller* ctrl) {
  viewObject.setPlayer(true);
  controllerStack.push(std::move(controller));
  controller.reset(ctrl);
}

void Creature::popController() {
  viewObject.setPlayer(false);
  CHECK(canPopController());
  controller = std::move(controllerStack.top());
  controllerStack.pop();
}

bool Creature::canPopController() {
  return !controllerStack.empty();
}

bool Creature::isDead() const {
  return dead;
}

void Creature::spendTime(double t) {
  time += 100.0 * t / (double) getAttr(AttrType::SPEED);
  hidden = false;
}

bool Creature::canMove(Vec2 direction) const {
  if (holding) {
    privateMessage("You can't break free!");
    return false;
  }
  return (direction.length8() == 1 && level->canMoveCreature(this, direction)) || canSwapPosition(direction);
}

void Creature::move(Vec2 direction) {
  stationary = false;
  Debug() << getTheName() << " moving " << direction;
  CHECK(canMove(direction));
  if (level->canMoveCreature(this, direction))
    level->moveCreature(this, direction);
  else
    swapPosition(direction);
  if (collapsed) {
    you(MsgType::CRAWL, getConstSquare()->getName());
    spendTime(3);
  } else
    spendTime(1);
}

int Creature::getDebt(const Creature* debtor) const {
  return controller->getDebt(debtor);
}

bool Creature::wantsItems(const Creature* from, vector<Item*> items) const {
  return controller->wantsItems(from, items);
}

void Creature::takeItems(const Creature* from, vector<PItem> items) {
  return controller->takeItems(from, std::move(items));
}

void Creature::you(MsgType type, const string& param) const {
  controller->you(type, param);
}

void Creature::you(const string& param) const {
  controller->you(param);
}

void Creature::privateMessage(const string& message) const {
  controller->privateMessage(message);
}

void Creature::onItemsAppeared(vector<Item*> items) {
  controller->onItemsAppeared(items);
}

void Creature::grantIdentify(int numItems) {
  controller->grantIdentify(numItems);
}

Controller* Creature::getController() {
  return controller.get();
}

const MapMemory& Creature::getMemory(const Level* l) const {
  return controller->getMemory(l);
}

bool Creature::canSwapPosition(Vec2 direction) const {
  const Creature* c = getConstSquare(direction)->getCreature();
  if (!c)
    return false;
  if (c->sleeping) {
    privateMessage(c->getTheName() + " is sleeping.");
    return false;
  }
  return (!swapPositionCooldown || isPlayer()) && !c->stationary &&
      direction.length8() == 1 && !c->isPlayer() && !c->isEnemy(this) &&
      getConstSquare(direction)->canEnterEmpty(this) && getConstSquare()->canEnterEmpty(c);
}

void Creature::swapPosition(Vec2 direction) {
  CHECK(canSwapPosition(direction));
  swapPositionCooldown = 4;
  getConstSquare(direction)->getCreature()->privateMessage("Excuse me!");
  privateMessage("Excuse me!");
  level->swapCreatures(this, getSquare(direction)->getCreature());
}

void Creature::makeMove() {
  CHECK(!isDead());
  if (holding && holding->isDead())
    holding = nullptr;
  if (sleeping) {
    spendTime(1);
    return;
  }
  updateVisibleEnemies();
  if (swapPositionCooldown)
    --swapPositionCooldown;
  MEASURE(controller->makeMove(), "creature move time");
  CHECK(!inEquipChain) << "Someone forgot to finishEquipChain()";
  if (!hidden)
    viewObject.setHidden(false);
  unknownAttacker.clear();
  if (!getSquare()->isCovered())
    shineLight();
}

Square* Creature::getSquare() {
  return level->getSquare(position);
}

Square* Creature::getSquare(Vec2 direction) {
  return level->getSquare(position + direction);
}

const Square* Creature::getConstSquare() const {
  return getLevel()->getSquare(position);
}

const Square* Creature::getConstSquare(Vec2 direction) const {
  return getLevel()->getSquare(position + direction);
}

void Creature::wait() {
  Debug() << getTheName() << " waiting";
  bool keepHiding = hidden;
  spendTime(1);
  hidden = keepHiding;
}

int Creature::getUniqueId() const {
  return uniqueId;
}

const Equipment& Creature::getEquipment() const {
  return equipment;
}

vector<PItem> Creature::steal(const vector<Item*> items) {
  return equipment.removeItems(items);
}

Item* Creature::getAmmo() const {
  for (Item* item : equipment.getItems())
    if (item->getType() == ItemType::AMMO)
      return item;
  return nullptr;
}

const Level* Creature::getLevel() const {
  return level;
}

Level* Creature::getLevel() {
  return level;
}

Vec2 Creature::getPosition() const {
  return position;
}

void Creature::globalMessage(const string& playerCanSee, const string& cant) const {
  if (const Creature* player = level->getPlayer()) {
    if (player->canSee(this))
      player->privateMessage(playerCanSee);
    else
      player->privateMessage(cant);
  }
}

const vector<const Creature*>& Creature::getVisibleEnemies() const {
  return visibleEnemies;
}

void Creature::updateVisibleEnemies() {
  visibleEnemies.clear();
  for (const Creature* c : level->getAllCreatures())
    if (isEnemy(c) && canSee(c))
      visibleEnemies.push_back(c);
}

vector<const Creature*> Creature::getVisibleCreatures() const {
  vector<const Creature*> res;
  for (Creature* c : level->getAllCreatures())
    if (canSee(c))
      res.push_back(c);
  for (const Creature* c : getUnknownAttacker())
    if (!contains(res, c))
      res.push_back(c);
  return res;
}

void Creature::addSkill(Skill* skill) {
  skills.insert(skill);
  skill->onTeach(this);
  privateMessage(skill->getHelpText());
}

bool Creature::hasSkill(Skill* skill) const {
  return skills.count(skill);
}

bool Creature::hasSkillToUseWeapon(const Item* it) const {
  return !it->isWieldedTwoHanded() || hasSkill(Skill::twoHandedWeapon);
}

vector<Item*> Creature::getPickUpOptions() const {
  if (!isHumanoid())
    return vector<Item*>();
  else
    return level->getSquare(getPosition())->getItems();
}

bool Creature::canPickUp(const vector<Item*>& items) const {
  if (!isHumanoid())
    return false;
  double weight = getInventoryWeight();
  for (Item* it : items)
    weight += it->getWeight();
  if (weight > 2 * getAttr(AttrType::INV_LIMIT)) {
    privateMessage("You are carrying too much to pick this up.");
    return false;
  }
  return true;
}

void Creature::pickUp(const vector<Item*>& items) {
  CHECK(canPickUp(items));
  Debug() << getTheName() << " pickup ";
  for (auto item : items) {
    equipment.addItem(level->getSquare(getPosition())->removeItem(item));
  }
  if (getInventoryWeight() > getAttr(AttrType::INV_LIMIT))
    privateMessage("You are overloaded.");
  EventListener::addPickupEvent(this, items);
  spendTime(1);
}

void Creature::drop(const vector<Item*>& items) {
  CHECK(isHumanoid());
  Debug() << getTheName() << " drop";
  for (auto item : items) {
    level->getSquare(getPosition())->dropItem(equipment.removeItem(item));
  }
  EventListener::addDropEvent(this, items);
  spendTime(1);
}

void Creature::drop(vector<PItem> items) {
  Debug() << getTheName() << " drop";
  getSquare()->dropItems(std::move(items));
}

void Creature::startEquipChain() {
  inEquipChain = true;
}

void Creature::finishEquipChain() {
  inEquipChain = false;
  if (numEquipActions > 0)
    spendTime(1);
  numEquipActions = 0;
}

bool Creature::canEquip(const Item* item) const {
  if (!isHumanoid())
    return false;
  if (numGoodArms() == 0) {
    privateMessage("You don't have hands!");
    return false;
  }
  if (!hasSkill(Skill::twoHandedWeapon) && item->isWieldedTwoHanded()) {
    privateMessage("You don't have the skill to use two-handed weapons.");
    return false;
  }
  if (!hasSkill(Skill::archery) && item->getType() == ItemType::RANGED_WEAPON) {
    privateMessage("You don't have the skill to shoot a bow.");
    return false;
  }
  if (numGoodArms() == 1 && item->isWieldedTwoHanded()) {
    privateMessage("You need two hands to wield " + item->getAName() + "!");
    return false;
  }
  return item->canEquip() && equipment.getItem(item->getEquipmentSlot()) == nullptr;
}

bool Creature::canUnequip(const Item* item) const {
  if (!isHumanoid())
    return false;
  if (numGoodArms() == 0) {
    privateMessage("You don't have hands!");
    return false;
  } else
    return true;
}

void Creature::equip(Item* item) {
  CHECK(canEquip(item));
  Debug() << getTheName() << " equip " << item->getName();
  EquipmentSlot slot = item->getEquipmentSlot();
  equipment.equip(item, slot);
  item->onEquip(this);
  if (!inEquipChain)
    spendTime(1);
  else
    ++numEquipActions;
}

void Creature::unequip(Item* item) {
  CHECK(canUnequip(item));
  Debug() << getTheName() << " unequip";
  EquipmentSlot slot = item->getEquipmentSlot();
  CHECK(equipment.getItem(slot) == item) << "Item not equiped.";
  equipment.unequip(slot);
  item->onUnequip(this);
  if (!inEquipChain)
    spendTime(1);
  else
    ++numEquipActions;
}

bool Creature::canHeal(Vec2 direction) const {
  Creature* other = level->getSquare(position + direction)->getCreature();
  return healer && other && other->getHealth() < 1;
}

void Creature::heal(Vec2 direction) {
  CHECK(canHeal(direction));
  Creature* other = level->getSquare(position + direction)->getCreature();
  other->you(MsgType::ARE, "healed by " + getTheName());
  other->heal();
  spendTime(1);
}

bool Creature::canBumpInto(Vec2 direction) const {
  return level->getSquare(getPosition() + direction)->getCreature();
}

void Creature::bumpInto(Vec2 direction) {
  CHECK(canBumpInto(direction));
  level->getSquare(getPosition() + direction)->getCreature()->controller->onBump(this);
  spendTime(1);
}

void Creature::applySquare() {
  Debug() << getTheName() << " applying " << getSquare()->getName();;
  getSquare()->onApply(this);
  spendTime(1);
}

bool Creature::canHide() const {
  return skills.count(Skill::ambush) && getConstSquare()->canHide();
}

void Creature::hide() {
  knownHiding.clear();
  viewObject.setHidden(true);
  for (const Creature* c : getLevel()->getAllCreatures())
    if (c->canSee(this) && c->isEnemy(this)) {
      knownHiding.insert(c);
      if (!isBlind())
        you(MsgType::CAN_SEE_HIDING, c->getTheName());
    }
  spendTime(1);
  hidden = true;
}

bool Creature::canChatTo(Vec2 direction) const {
  return getConstSquare(direction)->getCreature();
}

void Creature::chatTo(Vec2 direction) {
  CHECK(canChatTo(direction));
  Creature* c = getSquare(direction)->getCreature();
  c->onChat(this);
  spendTime(1);
}

void Creature::onChat(Creature* from) {
  if (isEnemy(from) && chatReactionHostile) {
    if (chatReactionHostile->front() == '\"')
      from->privateMessage(*chatReactionHostile);
    else
      from->privateMessage(getTheName() + " " + *chatReactionHostile);
  }
  if (!isEnemy(from) && chatReactionFriendly) {
    if (chatReactionFriendly->front() == '\"')
      from->privateMessage(*chatReactionFriendly);
    else
      from->privateMessage(getTheName() + " " + *chatReactionFriendly);
  }
}

void Creature::stealFrom(Vec2 direction, const vector<Item*>& items) {
  Creature* c = NOTNULL(getSquare(direction)->getCreature());
  equipment.addItems(c->steal(items));
}

bool Creature::isHidden() const {
  return hidden;
}

bool Creature::knowsHiding(const Creature* c) const {
  return knownHiding.count(c) == 1;
}

void Creature::panic(double time) {
  if (sleeping)
    return;
  enraged.unset();
  if (!panicking)
    you(MsgType::PANIC, "");
  panicking.set(getTime() + time);
}

void Creature::hallucinate(double time) {
  if (!isBlind())
    privateMessage("The world explodes into colors!");
  hallucinating.set(getTime() + time);
}

bool Creature::isHallucinating() const {
  return hallucinating;
}

void Creature::blind(double time) {
  if (permanentlyBlind)
    return;
  if (!blinded) 
    you(MsgType::ARE, "blind!");
  viewObject.setBlind(true);
  blinded.set(getTime() + time);
}

bool Creature::isBlind() const {
  return blinded || permanentlyBlind;
}

void Creature::makeInvisible(double time) {
  if (!isBlind())
    you(MsgType::TURN_INVISIBLE, "");
  viewObject.setInvisible(true);
  invisible.set(getTime() + time);
}

bool Creature::isInvisible() const {
  return invisible;
}

void Creature::rage(double time) {
  if (sleeping)
    return;
  panicking.unset();
  if (!enraged)
    you(MsgType::RAGE, "");
  enraged.set(getTime() + time);
}

void Creature::giveStrBonus(double time) {
  if (!strBonus)
    you(MsgType::FEEL, "stronger");
  strBonus.set(getTime() + time);
}

void Creature::giveDexBonus(double time) {
  if (!dexBonus)
    you(MsgType::FEEL, "more agile");
  dexBonus.set(getTime() + time);
}

bool Creature::isPanicking() const {
  return panicking;
}

int Creature::getAttrVal(AttrType type) const {
  switch (type) {
    case AttrType::SPEED: return *speed + expLevel * 4;
    case AttrType::DEXTERITY: return *dexterity + expLevel / 2;
    case AttrType::STRENGTH: return *strength + (expLevel - 1) / 2;
    default: return 0;
  }
}

int attrBonus = 3;

int dexPenNoArm = 2;
int dexPenNoLeg = 10;
int dexPenNoWing = 5;

int strPenNoArm = 1;
int strPenNoLeg = 3;
int strPenNoWing = 2;

int Creature::getAttr(AttrType type) const {
  int def = getAttrVal(type);
  for (Item* item : equipment.getItems())
    if (equipment.isEquiped(item))
      def += item->getModifier(type);
  switch (type) {
    case AttrType::STRENGTH:
        def *= 0.666 + health / 3;
        if (sleeping)
          def *= 0.66;
        if (strBonus)
          def += attrBonus;
        def -= injuredArms * strPenNoArm + injuredLegs * strPenNoLeg + injuredWings * strPenNoWing;
        break;
    case AttrType::DEXTERITY:
        def *= 0.666 + health / 3;
        if (sleeping)
          def = 0;
        if (dexBonus)
          def += attrBonus;
        def -= injuredArms * dexPenNoArm + injuredLegs * dexPenNoLeg + injuredWings * dexPenNoWing;
        break;
    case AttrType::THROWN_DAMAGE: 
    case AttrType::DAMAGE: 
        def += getAttr(AttrType::STRENGTH);
        if (!getWeapon())
          def += barehandedDamage;
        if (panicking)
          def -= attrBonus;
        if (enraged)
          def += attrBonus;
        break;
    case AttrType::DEFENSE: 
        def += getAttr(AttrType::STRENGTH);
        if (panicking)
          def += attrBonus;
        if (enraged)
          def -= attrBonus;
        break;
    case AttrType::THROWN_TO_HIT: 
    case AttrType::TO_HIT: 
        def += getAttr(AttrType::DEXTERITY);
        break;
    case AttrType::SPEED: {
        double totWeight = getInventoryWeight();
        if (totWeight > getAttr(AttrType::STRENGTH))
          def -= 20.0 * totWeight / def;
        if (slowed)
          def /= 2;
        if (speeding)
          def *= 2;
        break;}
    case AttrType::INV_LIMIT:
        return getAttr(AttrType::STRENGTH) * 2 * carryingMultiplier;
 //   default:
 //       break;
  }
  return max(0, def);
}

double Creature::getInventoryWeight() const {
  double ret = 0;
  for (Item* item : getEquipment().getItems())
    ret += item->getWeight();
  return ret;
}

Tribe* Creature::getTribe() const {
  return tribe;
}

bool Creature::isFriend(const Creature* c) const {
  return !isEnemy(c);
}

pair<double, double> Creature::getStanding(const Creature* c) const {
  double bestWeight = 0;
  double standing = getTribe()->getStanding(c);
  if (contains(privateEnemies, c)) {
    standing = -1;
    bestWeight = 1;
  }
  for (EnemyCheck* enemyCheck : enemyChecks)
    if (enemyCheck->hasStanding(c) && enemyCheck->getWeight() > bestWeight) {
      standing = enemyCheck->getStanding(c);
      bestWeight = enemyCheck->getWeight();
    }
  return make_pair(standing, bestWeight);
}

void Creature::addEnemyCheck(EnemyCheck* c) {
  enemyChecks.push_back(c);
}

void Creature::removeEnemyCheck(EnemyCheck* c) {
  removeElement(enemyChecks, c);
}

bool Creature::isEnemy(const Creature* c) const {
  pair<double, double> myStanding = getStanding(c);
  pair<double, double> hisStanding = c->getStanding(this);
  double standing = 0;
  if (myStanding.second > hisStanding.second)
    standing = myStanding.first;
  if (myStanding.second < hisStanding.second)
    standing = hisStanding.first;
  if (myStanding.second == hisStanding.second)
    standing = min(myStanding.first, hisStanding.first);
  return c != this && standing < 0;
}

vector<Item*> Creature::getGold(int num) const {
  vector<Item*> ret;
  for (Item* item : equipment.getItems([](Item* it) { return it->getType() == ItemType::GOLD; })) {
    ret.push_back(item);
    if (ret.size() == num)
      return ret;
  }
  return ret;
}

void Creature::setPosition(Vec2 pos) {
  position = pos;
}

void Creature::setLevel(Level* l) {
  level = l;
}

void Creature::slowDown(double duration) {
  you(MsgType::ARE, "moving more slowly");
  speeding.unset();
  slowed.set(getTime() + duration);
}

void Creature::speedUp(double duration) {
  you(MsgType::ARE, "moving faster");
  slowed.unset();
  speeding.set(getTime() + duration);
}

double Creature::getTime() const {
  return time;
}

void Creature::setTime(double t) {
  time = t;
}

void Creature::tick(double realTime) {
  for (Item* item : equipment.getItems()) {
    item->tick(time, level, position);
    if (item->isDiscarded())
      equipment.removeItem(item);
  }
  if (slowed.isFinished(realTime))
    you(MsgType::ARE, "moving faster again");
  if (sleeping.isFinished(realTime))
    you(MsgType::WAKE_UP, "");
  if (speeding.isFinished(realTime))
    you(MsgType::ARE, "moving more slowly again");
  if (strBonus.isFinished(realTime))
    you(MsgType::ARE, "weaker again");
  if (dexBonus.isFinished(realTime))
    you(MsgType::FEEL, "less agile again");
  if (panicking.isFinished(realTime) || enraged.isFinished(realTime) || hallucinating.isFinished(realTime)) {
    if (!hallucinating)
      privateMessage("Your mind is clear again");
    else
      privateMessage("Your brain is hurting a bit less.");
  }
  if (blinded.isFinished(realTime)) {
    you("can see again");
    viewObject.setBlind(false);
  }
  if (invisible.isFinished(realTime)) {
    you(MsgType::TURN_VISIBLE, "");
    viewObject.setInvisible(false);
  }
  double delta = realTime - lastTick;
  lastTick = realTime;
  updateViewObject();
  if (undead && numGoodArms() + numGoodLegs() + numGoodHeads() <= 2) {
    you(MsgType::FALL_APART, "");
    die(lastAttacker);
    return;
  }
  if (health < 0.5)
    health -= delta / 40;
  if (health <= 0) {
    you(MsgType::DIE_OF_BLEEDING, "");
    die(lastAttacker);
  }

}

BodyPart Creature::armOrWing() const {
  if (arms == 0)
    return BodyPart::WING;
  if (wings == 0)
    return BodyPart::ARM;
  return chooseRandom({ BodyPart::WING, BodyPart::ARM }, {1, 1});
}

BodyPart Creature::getBodyPart(AttackLevel attack) const {
  if (flyer)
    return chooseRandom({BodyPart::TORSO, BodyPart::HEAD, BodyPart::LEG, BodyPart::WING, BodyPart::ARM}, {1, 1, 1, 2, 1});
  switch (attack) {
    case AttackLevel::HIGH: 
       return BodyPart::HEAD;
    case AttackLevel::MIDDLE:
       if (size == CreatureSize::SMALL || size == CreatureSize::MEDIUM || collapsed)
         return BodyPart::HEAD;
       else
         return chooseRandom({BodyPart::TORSO, armOrWing()}, {1, 1});
    case AttackLevel::LOW:
       if (size == CreatureSize::SMALL || collapsed)
         return chooseRandom({BodyPart::TORSO, armOrWing(), BodyPart::HEAD, BodyPart::LEG}, {1, 1, 1, 1});
       if (size == CreatureSize::MEDIUM)
         return chooseRandom({BodyPart::TORSO, armOrWing(), BodyPart::LEG}, {1, 1, 3});
       else
         return BodyPart::LEG;
  }
  return BodyPart::ARM;
}

string getAttackParam(AttackType type) {
  switch (type) {
    case AttackType::CUT: return "cut";
    case AttackType::STAB: return "stab";
    case AttackType::CRUSH: return "crush";
    case AttackType::PUNCH: return "punch";
    case AttackType::BITE: return "bite";
    case AttackType::HIT: return "hit";
    case AttackType::SHOOT: return "shot";
  }
  return "";
}

void Creature::injureLeg(bool drop) {
  if (legs == 0)
    return;
  if (drop) {
    --legs;
    ++lostLegs;
    if (injuredLegs > legs)
      --injuredLegs;
  }
  else if (injuredLegs < legs)
    ++injuredLegs;
  if (!collapsed)
    you(MsgType::COLLAPSE, "");
  collapsed = true;
  if (drop) {
    getSquare()->dropItem(ItemFactory::corpse(*name + " leg", "bone", *weight / 8,
          isFood ? ItemType::FOOD : ItemType::CORPSE));
  }
}

void Creature::injureArm(bool dropArm) {
  if (dropArm) {
    --arms;
    ++lostArms;
    if (injuredArms > arms)
      --injuredArms;
  }
  else if (injuredArms < arms)
    ++injuredArms;
  string itemName = isPlayer() ? ("your arm") : (*name + " arm");
  if (getWeapon()) {
    you(MsgType::DROP_WEAPON, getWeapon()->getName());
    level->getSquare(getPosition())->dropItem(equipment.removeItem(getWeapon()));
  }
  if (dropArm)
    getSquare()->dropItem(ItemFactory::corpse(*name + " arm", "bone", *weight / 12,
          isFood ? ItemType::FOOD : ItemType::CORPSE));
}

void Creature::injureWing(bool drop) {
  if (drop) {
    --wings;
    ++lostWings;
    if (injuredWings > wings)
      --injuredWings;
  }
  else if (injuredWings < wings)
    ++injuredWings;
  if (flyer) {
    you(MsgType::FALL, getSquare()->getName());
    flyer = false;
  }
  if ((legs < 2 || injuredLegs > 0) && !collapsed) {
    collapsed = true;
  }
  string itemName = isPlayer() ? ("your wing") : (*name + " wing");
  if (drop)
    getSquare()->dropItem(ItemFactory::corpse(*name + " wing", "bone", *weight / 12,
          isFood ? ItemType::FOOD : ItemType::CORPSE));
}

void Creature::injureHead(bool drop) {
  if (drop) {
    --heads;
    if (injuredHeads > heads)
      --injuredHeads;
  }
  else if (injuredHeads < heads)
    ++injuredHeads;
  if (drop)
    getSquare()->dropItem(ItemFactory::corpse(*name +" head", *name + " skull", *weight / 12,
          isFood ? ItemType::FOOD : ItemType::CORPSE));
}

void Creature::attack(const Creature* c1) {
  Creature* c = const_cast<Creature*>(c1);
  int toHitVariance = 9;
  int attackVariance = 6;
  CHECK((c->getPosition() - getPosition()).length8() == 1)
      << "Bad attack direction " << c->getPosition() - getPosition();
  CHECK(canAttack(c));
  Debug() << getTheName() << " attacking " << c->getName();
  int toHit = Random.getRandom(-toHitVariance, toHitVariance) + getAttr(AttrType::TO_HIT);
  int damage = Random.getRandom(-attackVariance, attackVariance) + getAttr(AttrType::DAMAGE);
  bool backstab = false;
  string enemyName = getLevel()->playerCanSee(c) ? c->getTheName() : "something";
  if (c->isPlayer())
    enemyName = "";
  if (!c->canSee(this) && canSee(c)) {
    if (getWeapon() && getWeapon()->getAttackType() == AttackType::STAB) {
      damage += 15;
      backstab = true;
    }
    you(MsgType::ATTACK_SURPRISE, enemyName);
  }
  Attack attack(this, getRandomAttackLevel(), getAttackType(), toHit, damage, backstab);
  if (!c->dodgeAttack(attack)) {
    if (getWeapon()) {
      you(getWeapon()->getAttackType() == AttackType::STAB ?
          MsgType::THRUST_WEAPON : MsgType::SWING_WEAPON, getWeapon()->getName());
      if (!canSee(c))
        privateMessage("You hit something.");
    }
    else {
      if (isHumanoid()) {
        you(attack.getLevel() == AttackLevel::LOW ? MsgType::KICK : MsgType::PUNCH, enemyName);
      }
      else
        you(MsgType::BITE, enemyName);
    }
    c->takeDamage(attack);
  }
  else
    you(MsgType::MISS_ATTACK, enemyName);
  spendTime(1);
}

bool Creature::dodgeAttack(Attack attack) {
  Debug() << getTheName() << " dodging " << attack.getAttacker()->getName() << " to hit " << attack.getToHit() << " dodge " << getAttr(AttrType::TO_HIT);
  if (const Creature* c = attack.getAttacker()) {
    if (!canSee(c))
      unknownAttacker.push_back(c);
    EventListener::addAttackEvent(this, c);
    if (!contains(privateEnemies, c))
      privateEnemies.push_back(c);
  }
  return canSee(attack.getAttacker()) && attack.getToHit() <= getAttr(AttrType::TO_HIT);
}

bool Creature::takeDamage(Attack attack) {
  if (sleeping)
    wakeUp();
  int defense = getAttr(AttrType::DEFENSE);
  Debug() << getTheName() << " attacked by " << attack.getAttacker()->getName() << " damage " << attack.getStrength() << " defense " << defense;
  if (attack.getStrength() > defense) {
    lastAttacker = attack.getAttacker();
    double dam = (defense == 0) ? 1 : double(attack.getStrength() - defense) / defense;
    dam *= damageMultiplier;
    if (!undead)
      bleed(dam);
    if (!noBody) {
      BodyPart part = attack.inTheBack() ? BodyPart::BACK : getBodyPart(attack.getLevel());
      switch (part) {
        case BodyPart::BACK:
          youHit(part, attack.getType());
          break;
        case BodyPart::WING:
          if (dam >= 0.3 && wings > injuredWings) {
            youHit(BodyPart::WING, attack.getType()); 
            injureWing(attack.getType() == AttackType::CUT || attack.getType() == AttackType::BITE);
            if (health <= 0)
              health = 0.01;
            return false;
          }
        case BodyPart::ARM:
          if (dam >= 0.5 && arms > injuredArms) {
            youHit(BodyPart::ARM, attack.getType()); 
            injureArm(attack.getType() == AttackType::CUT || attack.getType() == AttackType::BITE);
            if (health <= 0)
              health = 0.01;
            return false;
          }
        case BodyPart::LEG:
          if (dam >= 0.8 && legs > injuredLegs) {
            youHit(BodyPart::LEG, attack.getType()); 
            injureLeg(attack.getType() == AttackType::CUT || attack.getType() == AttackType::BITE);
            if (health <= 0)
              health = 0.01;
            return false;
          }
        case BodyPart::HEAD:
          if (dam >= 0.8 && heads > injuredHeads) {
            youHit(BodyPart::HEAD, attack.getType()); 
            injureHead(attack.getType() == AttackType::CUT || attack.getType() == AttackType::BITE);
            if (!undead) {
              you(MsgType::DIE, "");
              die(attack.getAttacker());
            }
            return true;
          }
        case BodyPart::TORSO:
          if (dam >= 1.5) {
            youHit(BodyPart::TORSO, attack.getType());
            if (!undead)
              you(MsgType::DIE, "");
            die(attack.getAttacker());
            return true;
          }
          break;
      }
    }
    if (health <= 0) {
      you(MsgType::ARE, "critically wounded");
      you(MsgType::DIE, "");
      die(attack.getAttacker());
      return true;
    } else
    if (health < 0.5)
      you(MsgType::ARE, "critically wounded");
    else
      you(MsgType::ARE, "wounded");
  } else
    you(MsgType::GET_HIT_NODAMAGE, getAttackParam(attack.getType()));
  const Creature* c = attack.getAttacker();
  return false;
}

void Creature::updateViewObject() {
  if (const Creature* c = getLevel()->getPlayer())
    if (isEnemy(c))
      viewObject.setHostile(true);
  viewObject.setBleeding(1 - health);
}

double Creature::getHealth() const {
  return health;
}

double Creature::getWeight() const {
  return *weight;
}

string sizeStr(CreatureSize s) {
  switch (s) {
    case CreatureSize::SMALL: return "small";
    case CreatureSize::MEDIUM: return "medium";
    case CreatureSize::LARGE: return "large";
    case CreatureSize::HUGE: return "huge";
  }
  return 0;
}


string limbsStr(int arms, int legs, int wings) {
  vector<string> ret;
  if (arms)
    ret.push_back("arms");
  if (legs)
    ret.push_back("legs");
  if (wings)
    ret.push_back("wings");
  if (ret.size() > 0)
    return " with " + combine(ret);
  else
    return "";
}

string attrStr(bool strong, bool agile, bool fast) {
  vector<string> good;
  vector<string> bad;
  if (strong)
    good.push_back("strong");
  else
    bad.push_back("weak");
  if (agile)
    good.push_back("agile");
  else
    bad.push_back("clumsy");
  if (fast)
    good.push_back("fast");
  else
    bad.push_back("slow");
  string p1 = combine(good);
  string p2 = combine(bad);
  if (p1.size() > 0 && p2.size() > 0)
    p1.append(", but ");
  p1.append(p2);
  return p1;
}

bool Creature::isSpecialMonster() const {
  return specialMonster;
}

string Creature::getDescription() const {
  string weapon;
  /*if (Item* item = getEquipment().getItem(EquipmentSlot::WEAPON))
    weapon = " It's wielding " + item->getAName() + ".";
  else
  if (Item* item = getEquipment().getItem(EquipmentSlot::RANGED_WEAPON))
    weapon = " It's wielding " + item->getAName() + ".";*/
  return getTheName() + " is a " + sizeStr(*size) + (isHumanoid() ? " humanoid creature" : " beast") +
      (!isHumanoid() ? limbsStr(arms, legs, wings) : (wings ? " with wings" : "")) + ". " +
     "It is " + attrStr(*strength > 16, *dexterity > 16, *speed > 100) + "." + weapon;
}

void Creature::setSpeed(double value) {
  speed = value;
}

double Creature::getSpeed() const {
  return *speed;
}
void Creature::heal(double amount, bool replaceLimbs) {
  Debug() << getTheName() << " heal";
  if (health < 1) {
    health = min(1., health + amount);
    if (health >= 0.5) {
      if (injuredArms > 0) {
        you(MsgType::YOUR, string(injuredArms > 1 ? "arms are" : "arm is") + " in better shape");
        injuredArms = 0;
      }
      if (lostArms > 0 && replaceLimbs) {
        you(MsgType::YOUR, string(lostArms > 1 ? "arms grow back!" : "arm grows back!"));
        arms += lostArms;
        lostArms = 0;
      }
      if (injuredWings > 0) {
        you(MsgType::YOUR, string(injuredArms > 1 ? "wings are" : "wing is") + " in better shape");
        injuredWings = 0;
      }
      if (lostWings > 0 && replaceLimbs) {
        you(MsgType::YOUR, string(lostArms > 1 ? "wings grow back!" : "wing grows back!"));
        wings += lostWings;
        lostWings = 0;
        flyer = true;
      }
     if (injuredLegs > 0) {
        you(MsgType::YOUR, string(injuredLegs > 1 ? "legs are" : "leg is") + " in better shape");
        injuredLegs = 0;
        if (legs == 2 && collapsed) {
          collapsed = false;
          you(MsgType::STAND_UP, "");
        }
      }
      if (lostLegs > 0 && replaceLimbs) {
        you(MsgType::YOUR, string(lostLegs > 1 ? "legs grow back!" : "leg grows back!"));
        legs += lostLegs;
        lostLegs = 0;
      }
    }
    if (health == 1) {
      you(MsgType::BLEEDING_STOPS, "");
      health = 1;
      lastAttacker = nullptr;
    }
    updateViewObject();
  }
}

void Creature::bleed(double severity) {
  updateViewObject();
  health -= severity;
  updateViewObject();
  Debug() << getTheName() << " health " << health;
}

void Creature::setOnFire(double amount) {
  if (!fireResistant) {
    you(MsgType::ARE, "burnt by the fire");
    bleed(6. * amount / double(getAttr(AttrType::STRENGTH)));
  }
}

void Creature::poisonWithGas(double amount) {
  if (breathing && !undead) {
    you(MsgType::ARE, "poisoned by the gas");
    bleed(amount / double(getAttr(AttrType::STRENGTH)));
  }
}

void Creature::shineLight() {
  if (undead) {
    if (Random.roll(10)) {
      you(MsgType::YOUR, "body crumbles to dust");
      die(nullptr);
    } else
      you(MsgType::ARE, "burnt by the sun");
  }
}

void Creature::setHeld(const Creature* c) {
  holding = c;
}

bool Creature::isHeld() const {
  return holding != nullptr;
}

void Creature::sleep(int time) {
  if (!noSleep)
    sleeping.set(getTime() + time);
}

bool Creature::isSleeping() const {
  return sleeping;
}

void Creature::wakeUp() {
  you(MsgType::WAKE_UP, "");
  sleeping.unset();
}

void Creature::take(vector<PItem> items) {
  for (PItem& elem : items)
    take(std::move(elem));
}

void Creature::take(PItem item) {
 /* item->identify();
  Debug() << (specialMonster ? "special monster " : "") + getTheName() << " takes " << item->getNameAndModifiers();*/
  if (item->isWieldedTwoHanded())
    addSkill(Skill::twoHandedWeapon);
  if (item->getType() == ItemType::RANGED_WEAPON)
    addSkill(Skill::archery);
  Item* ref = item.get();
  equipment.addItem(std::move(item));
  if (canEquip(ref))
    equip(ref);
}

void Creature::dropCorpse() {
  getSquare()->dropItem(ItemFactory::corpse(*name + " corpse", *name + " skeleton", *weight,
        isFood ? ItemType::FOOD : ItemType::CORPSE));
}

void Creature::die(const Creature* attacker, bool dropInventory) {
  Debug() << getTheName() << " dies.";
  controller->onKilled(attacker);
  if (dropInventory)
    for (PItem& item : equipment.removeAllItems()) {
      level->getSquare(position)->dropItem(std::move(item));
    }
  dead = true;
  if (dropInventory && !noBody)
    dropCorpse();
  level->killCreature(this);
  EventListener::addKillEvent(this, attacker);
}

bool Creature::canFlyAway() const {
  return canFly() && !getConstSquare()->isCovered();
}

void Creature::flyAway() {
  Debug() << getTheName() << " fly away";
  CHECK(canFlyAway());
  globalMessage(getTheName() + " flies away.");
  dead = true;
  level->killCreature(this);
}

void Creature::give(const Creature* whom, vector<Item*> items) {
  CHECK(whom->wantsItems(this, items));
  getLevel()->getSquare(whom->getPosition())->getCreature()->takeItems(this, equipment.removeItems(items));
}

bool Creature::canFire(Vec2 direction) const {
  CHECK(direction.length8() == 1);
  if (!getEquipment().getItem(EquipmentSlot::RANGED_WEAPON))
    return false;
  if (!hasSkill(Skill::archery)) {
    privateMessage("You don't have the skill to shoot a bow.");
    return false;
  }
  if (numGoodArms() < 2) {
    privateMessage("You need two hands to shoot a bow.");
    return false;
  }
  if (!getAmmo()) {
    privateMessage("Out of ammunition");
    return false;
  }
  return true;
}

void Creature::fire(Vec2 direction) {
  CHECK(canFire(direction));
  PItem ammo = equipment.removeItem(NOTNULL(getAmmo()));
  RangedWeapon* weapon = NOTNULL(dynamic_cast<RangedWeapon*>(getEquipment().getItem(EquipmentSlot::RANGED_WEAPON)));
  weapon->fire(this, level, std::move(ammo), direction);
  spendTime(1);
}

void Creature::squash(Vec2 direction) {
  if (canDestroy(direction))
    destroy(direction);
  Creature* c = getSquare(direction)->getCreature();
  if (c) {
    c->you(MsgType::KILLED_BY, getTheName());
    c->die();
  }
}

void Creature::construct(Vec2 direction, SquareType type) {
  getSquare(direction)->construct(type);
  spendTime(1);
}

bool Creature::canConstruct(Vec2 direction, SquareType type) const {
  return getConstSquare(direction)->canConstruct(type) && canConstruct(type);
}

bool Creature::canConstruct(SquareType type) const {
  return hasSkill(Skill::construction);
}

void Creature::eat(Item* item) {
  getSquare()->removeItem(item);
  spendTime(3);
}

bool Creature::canDestroy(Vec2 direction) const {
  return getConstSquare(direction)->canDestroy();
}

void Creature::destroy(Vec2 direction) {
  getSquare(direction)->destroy(getAttr(AttrType::STRENGTH));
  spendTime(1);
}

bool Creature::canAttack(const Creature* c) const {
  Vec2 direction = c->getPosition() - getPosition();
  return direction.length8() == 1;
}

AttackLevel Creature::getRandomAttackLevel() const {
  if (isHumanoid() && injuredArms == arms)
    return AttackLevel::LOW;
  switch (*size) {
    case CreatureSize::SMALL: return AttackLevel::LOW;
    case CreatureSize::MEDIUM: return chooseRandom({AttackLevel::LOW, AttackLevel::MIDDLE}, {1,1});
    case CreatureSize::LARGE: return chooseRandom({AttackLevel::LOW, AttackLevel::MIDDLE, AttackLevel::HIGH},{1,2,2});
    case CreatureSize::HUGE: return chooseRandom({AttackLevel::MIDDLE, AttackLevel::HIGH}, {1,3});
  }
  return AttackLevel::LOW;
}

Item* Creature::getWeapon() const {
  return equipment.getItem(EquipmentSlot::WEAPON);
}

AttackType Creature::getAttackType() const {
  if (getWeapon())
    return getWeapon()->getAttackType();
  else
    return isHumanoid() ? AttackType::PUNCH : AttackType::BITE;
}

void Creature::applyItem(Item* item) {
  Debug() << getTheName() << " applying " << item->getAName();
  CHECK(canApplyItem(item));
  double time = item->getApplyTime();
  item->apply(this, level);
  if (item->isDiscarded()) {
    equipment.removeItem(item);
  }
  spendTime(time);
}

bool Creature::canApplyItem(Item* item) const {
  if (!isHumanoid())
    return false;
  if (!numGoodArms()) {
    privateMessage("You don't have hands!");
    return false;
  } else
    return true; 
}

bool Creature::canThrowItem(Item* item) {
  if (injuredArms == arms || !isHumanoid()) {
    privateMessage("You can't throw anything!");
    return false;
  } else if (item->getWeight() > 20) {
    privateMessage(item->getTheName() + " is too heavy!");
    return false;
  }
    return true;
}

void Creature::throwItem(Item* item, Vec2 direction) {
  Debug() << getTheName() << " throwing " << item->getAName();
  CHECK(canThrowItem(item));
  int dist = 0;
  int toHitVariance = 10;
  int attackVariance = 7;
  int str = getAttr(AttrType::STRENGTH);
  if (item->getWeight() <= 0.5)
    dist = 10 * str / 15;
  else if (item->getWeight() <= 5)
    dist = 5 * str / 15;
  else if (item->getWeight() <= 20)
    dist = 2 * str / 15;
  else 
    Debug(FATAL) << "Item too heavy.";
  int toHit = Random.getRandom(-toHitVariance, toHitVariance) +
      getAttr(AttrType::THROWN_TO_HIT) + item->getModifier(AttrType::THROWN_TO_HIT);
  int damage = Random.getRandom(-attackVariance, attackVariance) +
      getAttr(AttrType::THROWN_DAMAGE) + item->getModifier(AttrType::THROWN_DAMAGE);
  if (hasSkill(Skill::knifeThrowing) && item->getAttackType() == AttackType::STAB) {
    damage += 7;
    toHit += 4;
  }
  Attack attack(this, getRandomAttackLevel(), item->getAttackType(), toHit, damage);
  level->throwItem(equipment.removeItem(item), attack, dist, getPosition(), direction);
  spendTime(1);
}

const ViewObject& Creature::getViewObject() const {
  return viewObject;
}

void Creature::setViewObject(const ViewObject& obj) {
  viewObject = obj;
}

bool Creature::canSee(const Creature* c) const {
  return !isBlind() && !c->isInvisible() &&
         (!c->isHidden() || c->knowsHiding(this)) && 
         getLevel()->canSee(position, c->getPosition());
}

bool Creature::canSee(Vec2 pos) const {
  return !isBlind() && 
      getLevel()->canSee(position, pos);
}
 
bool Creature::isPlayer() const {
  return controller->isPlayer();
}
  
string Creature::getTheName() const {
  if (islower((*name)[0]))
    return "the " + *name;
  return *name;
}
 
string Creature::getAName() const {
  if (islower((*name)[0]))
    return "a " + *name;
  return *name;
}

Optional<string> Creature::getFirstName() const {
  return firstName;
}

string Creature::getName() const {
  return *name;
}

bool Creature::isHumanoid() const {
  return *humanoid;
}

bool Creature::isAnimal() const {
  return animal;
}

bool Creature::isStationary() const {
  return stationary;
}

void Creature::setStationary() {
  stationary = true;
}

bool Creature::isInvincible() const {
  return invincible;
}

bool Creature::isUndead() const {
  return undead;
}

bool Creature::canSwim() const {
  return contains(skills, Skill::swimming);
}

bool Creature::canFly() const {
  return flyer;
}

bool Creature::canWalk() const {
  return walker;
}

int Creature::numArms() const {
  return arms;
}

int Creature::numLegs() const {
  return legs;
}

int Creature::numWings() const {
  return wings;
}

bool Creature::lostLimbs() const {
  return lostWings > 0 || lostArms > 0 || lostLegs > 0;
}

int Creature::numGoodArms() const {
  return arms - injuredArms;
}

int Creature::numGoodLegs() const {
  return legs - injuredLegs;
}

int Creature::numGoodHeads() const {
  return heads - injuredHeads;
}

double Creature::getCourage() const {
  return courage;
}

void Creature::increaseExpLevel() {
  if (expLevel < maxLevel) {
    ++expLevel;
    viewObject.setSizeIncrease(0.3);
    if (skillGain.count(expLevel))
      addSkill(skillGain.at(expLevel));
  }
}

int Creature::getExpLevel() const {
  return expLevel;
}

Optional<Vec2> Creature::getMoveTowards(Vec2 pos, bool away) {
  Debug() << "" << getPosition() << (away ? "Moving away from" : " Moving toward ") << pos;
  bool newPath = false;
  if (!shortestPath || shortestPath->getTarget() != pos || shortestPath->isReversed() != away) {
    newPath = true;
    if (!away)
      shortestPath = ShortestPath(getLevel(), this, pos, getPosition());
    else
      shortestPath = ShortestPath(getLevel(), this, pos, getPosition(), -1.5);
  }
  CHECK(shortestPath);
  if (shortestPath->isReachable(getPosition())) {
    Vec2 pos2 = shortestPath->getNextMove(getPosition());
    if (canMove(pos2 - getPosition())) {
      return pos2 - getPosition();
    }
  }
  if (newPath)
    return Nothing();
  Debug() << "Reconstructing shortest path.";
  if (!away)
    shortestPath = ShortestPath(getLevel(), this, pos, getPosition());
  else
    shortestPath = ShortestPath(getLevel(), this, pos, getPosition(), -1.5);
  if (shortestPath->isReachable(getPosition())) {
    Vec2 pos2 = shortestPath->getNextMove(getPosition());
    if (canMove(pos2 - getPosition())) {
      return pos2 - getPosition();
    } else
      return Nothing();
  } else {
    Debug() << "Cannot move toward " << pos;
    return Nothing();
  }
}

Optional<Vec2> Creature::getMoveAway(Vec2 pos, bool pathfinding) {
  if ((pos - getPosition()).length8() <= 5 && pathfinding) {
    Optional<Vec2> move = getMoveTowards(pos, true);
    if (move)
      return move;
  }
  pair<Vec2, Vec2> dirs = (getPosition() - pos).approxL1();
  vector<Vec2> moves;
  if (canMove(dirs.first))
    moves.push_back(dirs.first);
  if (canMove(dirs.second))
    moves.push_back(dirs.second);
  if (moves.size() > 0)
    return moves[Random.getRandom(moves.size())];
  return Nothing();
}

bool Creature::atTarget() const {
  return shortestPath && getPosition() == shortestPath->getTarget();
}

void Creature::youHit(BodyPart part, AttackType type) const {
  switch (part) {
    case BodyPart::BACK:
        switch (type) {
          case AttackType::SHOOT: you(MsgType::ARE, "shot in the spine!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "head is bitten off!"); break;
          case AttackType::CUT: you(MsgType::YOUR, "head is choped off!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "skull is shattered!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "neck is broken!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the back of the head!"); break;
          case AttackType::STAB: you(MsgType::ARE, "stabbed in the " + 
                                     chooseRandom<string>({"back", "neck"})); break;
        }
        break;
    case BodyPart::HEAD: 
        switch (type) {
          case AttackType::SHOOT: you(MsgType::YOUR, "shot in the " +
                                      chooseRandom<string>({"eye", "neck", "forehead"}) + "!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "head is bitten off!"); break;
          case AttackType::CUT: you(MsgType::YOUR, "head is choped off!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "skull is shattered!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "neck is broken!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the head!"); break;
          case AttackType::STAB: you(MsgType::ARE, "stabbed in the eye!"); break;
        }
        break;
    case BodyPart::TORSO:
        switch (type) {
          case AttackType::SHOOT: you(MsgType::YOUR, "shot in the heart!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "internal organs are ripped out!"); break;
          case AttackType::CUT: you(MsgType::ARE, "cut in half!"); break;
          case AttackType::STAB: you(MsgType::ARE, "stabbed in the " +
                                     chooseRandom<string>({"stomach", "heart"}, {1, 1}) + "!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "ribs and internal organs are crushed!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the chest!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "stomach receives a deadly blow!"); break;
        }
        break;
    case BodyPart::ARM:
        switch (type) {
          case AttackType::SHOOT: you(MsgType::YOUR, "shot in the arm!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "arm is bitten off!"); break;
          case AttackType::CUT: you(MsgType::YOUR, "arm is choped off!"); break;
          case AttackType::STAB: you(MsgType::ARE, "stabbed in the arm!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "arm is smashed!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the arm!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "arm is broken!"); break;
        }
        break;
    case BodyPart::WING:
        switch (type) {
          case AttackType::SHOOT: you(MsgType::YOUR, "shot in the wing!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "wing is bitten off!"); break;
          case AttackType::CUT: you(MsgType::YOUR, "wing is choped off!"); break;
          case AttackType::STAB: you(MsgType::ARE, "stabbed in the wing!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "wing is smashed!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the wing!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "wing is broken!"); break;
        }
        break;
    case BodyPart::LEG:
        switch (type) {
          case AttackType::SHOOT: you(MsgType::YOUR, "shot in the leg!"); break;
          case AttackType::BITE: you(MsgType::YOUR, "leg is bitten off!"); break;
          case AttackType::CUT: you(MsgType::YOUR, "leg is cut off!"); break;
          case AttackType::STAB: you(MsgType::YOUR, "stabbed in the leg!"); break;
          case AttackType::CRUSH: you(MsgType::YOUR, "knee is crushed!"); break;
          case AttackType::HIT: you(MsgType::ARE, "hit in the leg!"); break;
          case AttackType::PUNCH: you(MsgType::YOUR, "leg is broken!"); break;
        }
        break;
  }
}

vector<const Creature*> Creature::getUnknownAttacker() const {
  return unknownAttacker;
}

void Creature::refreshGameInfo(View::GameInfo& gameInfo) const {
    gameInfo.infoType = View::GameInfo::InfoType::PLAYER;
    View::GameInfo::PlayerInfo& info = gameInfo.playerInfo;
    info.speed = getAttr(AttrType::SPEED);
    if (firstName)
      info.playerName = *firstName;
    info.title = *name;
    info.adjectives.clear();
    if (isBlind())
      info.adjectives.push_back("blind");
    if (isInvisible())
      info.adjectives.push_back("invisible");
    if (numArms() == 1)
      info.adjectives.push_back("one-armed");
    if (numArms() == 0)
      info.adjectives.push_back("armless");
    if (numLegs() == 1)
      info.adjectives.push_back("one-legged");
    if (numLegs() == 0)
      info.adjectives.push_back("legless");
    if (isHallucinating())
      info.adjectives.push_back("tripped");
    Item* weapon = getEquipment().getItem(EquipmentSlot::WEAPON);
    info.weaponName = weapon ? weapon->getAName() : "";
    const Location* location = getLevel()->getLocation(getPosition());
    info.levelName = location && location->hasName() 
      ? capitalFirst(location->getName()) : getLevel()->getName();
    info.defense = getAttr(AttrType::DEFENSE);
    info.bleeding = getHealth() < 1;
    info.attack = getAttr(AttrType::DAMAGE);
    info.strength = getAttr(AttrType::STRENGTH);
    info.dexterity = getAttr(AttrType::DEXTERITY);
    info.time = getTime();
    info.numGold = getGold(100000000).size();
    info.elfStanding = Tribe::elven->getStanding(this);
    info.dwarfStanding = Tribe::dwarven->getStanding(this);
    info.goblinStanding = Tribe::goblin->getStanding(this);
  }
