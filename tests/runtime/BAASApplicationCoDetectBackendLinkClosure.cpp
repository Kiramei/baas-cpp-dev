#include "runtime/procedure/BAASConnectionCoDetectPort.h"

#include <stdexcept>

int main()
{
    // Calling the production factory forces its archive member, and therefore
    // every real legacy BAAS method it uses, into this final executable link.
    // A null application is rejected before any config or resource can be read.
    try {
        static_cast<void>(
            baas::runtime::procedure::make_baas_application_co_detect_backend({}));
    } catch (const std::invalid_argument&) {
        return 0;
    }
    return 1;
}
