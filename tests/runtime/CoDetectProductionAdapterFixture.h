#pragma once

#include "runtime/procedure/CoDetectSupportBundle.h"

#include <memory>

[[nodiscard]] std::shared_ptr<const baas::runtime::procedure::CoDetectSupportBundle>
make_co_detect_production_test_bundle();
