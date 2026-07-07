#include "Lumena.h"

#ifndef LUMENA_VERSION
#define LUMENA_VERSION "0.0.0"
#endif

namespace lumena {

std::string version() {
    return LUMENA_VERSION;
}

} // namespace lumena
