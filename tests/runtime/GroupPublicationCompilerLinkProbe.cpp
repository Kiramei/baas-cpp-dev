#include "runtime/publisher/GroupPublicationCompiler.h"

#include <string_view>

int main()
{
    try {
        static_cast<void>(baas::runtime::publisher::parse_group_publication_lock(
            std::string_view{"{}"}));
    } catch (const baas::runtime::publisher::PublicationError&) {
        return 0;
    }
    return 1;
}
