#include "Menu.h"

MenuItem::MenuItem() : name(0), menu(0)
{

}
MenuItem::~MenuItem()
{

}

Menu* Menu::Current = 0;

Menu::Menu() : index(0), processor(0), parent(0), items(0)
{}
Menu::~Menu()
{}
void Menu::allocateItems(unsigned int num_items)
{
  //no reallocations
  if (items) {
    delete [] items;
  }
  items = new MenuItem[num_items];
  item_count = num_items;
  
}
MenuItem* Menu::itemAt(unsigned int pos)
{
  if (pos<item_count) {
    return &items[pos];
  }
  else return 0;
}
MenuItem* Menu::indexItem()
{

  if (index>=0 && index<item_count) {
      return &items[index];
  }
  return 0;
}
unsigned int Menu::itemCount()
{
  return item_count;
}
