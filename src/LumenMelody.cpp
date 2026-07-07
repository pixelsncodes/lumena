#include "LumenMelody.h"

#ifndef LUMENMELODY_VERSION
#define LUMENMELODY_VERSION "0.0.0"
#endif

namespace lumen {

std::string version() {
    return LUMENMELODY_VERSION;
}

} // namespace lumen
