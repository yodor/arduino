#pragma once
#include "MenuTypes.hpp"

// The application's actual root menu content (Colors, Calibration,
// Stereo/Mono, Starting Screen), separated from main.cpp's orchestration
// logic (switching in/out of the menu, routing input to it). MenuScreen
// itself stays a purely content-agnostic navigation engine -- this is
// where the specific items/callbacks/labels for THIS project's menu live.
const MenuItem* getRootMenuItems();
size_t getRootMenuItemCount();