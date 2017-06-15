#ifndef MENU_H
#define MENU_H
#include <Arduino.h>

class Menu;
class MenuProcessor;

class MenuItem {
public:
  MenuItem();
  ~MenuItem();
  const __FlashStringHelper *name;
  Menu *menu;
};

class Menu
{
public:
  Menu();
  ~Menu();

  int index;
  MenuProcessor *processor;

  Menu *parent;
  static Menu *Current;

  void allocateItems(unsigned int num_items);
  MenuItem *itemAt(unsigned int pos);
  MenuItem *indexItem();
  unsigned int itemCount();

protected:
  MenuItem *items;
  unsigned int item_count;
};


#endif

