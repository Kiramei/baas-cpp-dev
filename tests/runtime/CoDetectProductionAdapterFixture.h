#pragma once

#include "runtime/procedure/CoDetectSupportBundle.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

[[nodiscard]] std::vector<std::byte>
make_co_detect_production_test_archive(
    std::string_view bundle_id =
        "procedure-support/navigation.to-main-page/v1");

[[nodiscard]] std::shared_ptr<const baas::runtime::procedure::CoDetectSupportBundle>
make_co_detect_production_test_bundle();
