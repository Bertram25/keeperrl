#ifndef _COLLECTIVE_ACTION_H
#define _COLLECTIVE_ACTION_H

#include "util.h"

class CollectiveAction {
  public:
  enum Type { IDLE, GO_TO, POSSESS, BUTTON_RELEASE, ROOM_BUTTON, CREATURE_BUTTON,
      CREATURE_DESCRIPTION, GATHER_TEAM, CANCEL_TEAM, MARKET, TECHNOLOGY, DRAW_LEVEL_MAP };

  CollectiveAction(Type, Vec2 pos);
  CollectiveAction(Type, int);
  CollectiveAction(Type, const Creature*);
  CollectiveAction(Type);

  Type getType();
  Vec2 getPosition();
  int getNum();
  const Creature* getCreature();

  private:
  Type type;
  Vec2 pos;
  int num;
  const Creature* creature;
};

#endif
