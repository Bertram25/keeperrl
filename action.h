#ifndef _ACTION_H
#define _ACTION_H

#include <iostream>

enum class ActionId {
     MOVE,
     TRAVEL,
     FIRE,
     PICK_UP,
     EXT_PICK_UP,
     DROP,
     EXT_DROP,
     SHOW_INVENTORY,
     APPLY_ITEM,
     EQUIPMENT,
     THROW,
     THROW_DIR,
     SHOW_HISTORY,
     HIDE,
     PAY_DEBT,
     CHAT,
     WAIT,
     UNPOSSESS,
     IDLE
};

class Action {
  public:
  Action();
  Action(ActionId);
  Action(ActionId, Vec2 direction);
  ActionId getId();
  Vec2 getDirection();

  private:
  ActionId id;
  Vec2 direction;
};

std::ostream& operator << (std::ostream&, Action);
std::istream& operator >> (std::istream&, Action&);

#endif