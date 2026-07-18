#pragma once

#include "runtime/procedure/CoDetectSupportBundle.h"

#include <cstddef>
#include <memory>
#include <vector>

[[nodiscard]] std::vector<std::byte>
make_co_detect_production_test_archive();

[[nodiscard]] std::shared_ptr<const baas::runtime::procedure::CoDetectSupportBundle>
make_co_detect_production_test_bundle();
