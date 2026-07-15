#include "service/trigger/TriggerDispatch.h"

#include <iostream>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace trigger = baas::service::trigger;

namespace {

int failures = 0;

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (!static_cast<bool>(condition)) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

trigger::TriggerHandler harmless_handler()
{
    return [](const trigger::AdmittedTriggerRequest&,
              trigger::TriggerResponseSink& sink, std::stop_token) {
        static_cast<void>(sink.success());
    };
}

void test_registry_is_immutable_and_exact()
{
    auto invalid_limits = trigger::TriggerDispatchLimits{};
    invalid_limits.max_exception_error_bytes = 1;
    check(trigger::TriggerDispatcher::create({}, invalid_limits).error
              == trigger::TriggerRegistryError::invalid_limits,
          "invalid limits must be rejected");
    check(trigger::TriggerDispatcher::create({
              {"not_a_command", harmless_handler()},
          }).error == trigger::TriggerRegistryError::unknown_descriptor,
          "unknown descriptor must be rejected");
    check(trigger::TriggerDispatcher::create({{"status", {}}}).error
              == trigger::TriggerRegistryError::empty_handler,
          "empty handler must be rejected");
    check(trigger::TriggerDispatcher::create({
              {"status", harmless_handler()},
              {"status", harmless_handler()},
          }).error == trigger::TriggerRegistryError::duplicate_registration,
          "duplicate descriptor must be rejected");

    auto built = trigger::TriggerDispatcher::create({
        {"status", harmless_handler()},
        {"start_*", harmless_handler()},
    });
    check(built, "exact and prefix descriptors must build an immutable registry");
}

void test_public_surface_requires_execution_owner()
{
    check(trigger::trigger_registry_error_name(
              trigger::TriggerRegistryError::duplicate_registration)
              == "duplicate_registration",
          "registry error names must remain stable");
    check(trigger::trigger_dispatch_disposition_name(
              trigger::TriggerDispatchDisposition::retry_response)
              == "retry_response",
          "execution result names must remain stable for the owner bridge");
}

}  // namespace

int main()
{
    test_registry_is_immutable_and_exact();
    test_public_surface_requires_execution_owner();
    if (failures != 0) {
        std::cerr << failures << " trigger dispatch test(s) failed\n";
        return 1;
    }
    std::cout << "trigger dispatch tests passed\n";
    return 0;
}
