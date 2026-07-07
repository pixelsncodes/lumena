#include "Lumena.h"

#ifndef LUMENA_VERSION
#define LUMENA_VERSION "0.0.0"
#endif

namespace lumen {

std::string version() {
    return LUMENA_VERSION;
}

} // namespace lumen
