#include "DisplaySettings.hpp"

DisplaySettings& DisplaySettings::instance() {
    static DisplaySettings inst;
    return inst;
}