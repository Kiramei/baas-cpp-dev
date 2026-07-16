#include "resources/ResourceSnapshot.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace resources = baas::resources;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Function>
void expect_error(
    const resources::ResourceErrorCode code,
    Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const resources::ResourceError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

std::shared_ptr<const std::vector<std::byte>> bytes(const std::string_view text)
{
    auto result = std::make_shared<std::vector<std::byte>>();
    result->reserve(text.size());
    for (const auto value : text)
        result->push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return result;
}

resources::ResourcePayload payload(
    std::string id,
    std::string content,
    std::optional<std::string> locale = std::nullopt,
    std::optional<std::string> activity = std::nullopt,
    std::string media_type = "application/octet-stream")
{
    auto data = bytes(content);
    return {
        std::move(id), std::move(locale), std::move(activity), std::move(media_type),
        data->size(), resources::sha256_hex(*data), std::move(data)};
}

void test_sha256_vectors()
{
    check(resources::sha256_hex({}) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty SHA-256 vector must match FIPS 180-4");
    const auto abc = bytes("abc");
    check(resources::sha256_hex(*abc) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc SHA-256 vector must match FIPS 180-4");
    const auto long_value = bytes(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    check(resources::sha256_hex(*long_value) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "multi-block SHA-256 vector must match FIPS 180-4");
}

void test_validation_is_atomic_and_bounded()
{
    check(resources::valid_resource_id("image/arena/menu") &&
              !resources::valid_resource_id("../menu") &&
              !resources::valid_resource_id("image//menu") &&
              !resources::valid_resource_id("Image/menu") &&
              !resources::valid_resource_id("image\\menu") &&
              !resources::valid_resource_id("c:/menu"),
          "resource IDs must be canonical lowercase logical paths");
    check(resources::valid_resource_locale("Global_en-us") &&
              resources::valid_resource_activity("AbydosResortRestorationCommittee") &&
              !resources::valid_resource_locale("en/us"),
          "selectors must be bounded opaque ASCII tokens");

    auto good = payload("json/rgb-feature", "{}", "CN", std::nullopt, "application/json");
    auto bad_hash = good;
    bad_hash.resource_id = "json/rgb-feature-corrupt";
    bad_hash.sha256.assign(64, '0');
    expect_error(resources::ResourceErrorCode::DigestMismatch, [&] {
        (void)resources::ResourceSnapshot::build({"CN", std::nullopt}, {good, bad_hash});
    }, "a bad digest must fail the whole snapshot before publication");

    expect_error(resources::ResourceErrorCode::DuplicateVariant, [&] {
        (void)resources::ResourceSnapshot::build({"CN", std::nullopt}, {good, good});
    }, "duplicate logical variants must be rejected deterministically");

    auto wrong_size = good;
    ++wrong_size.declared_size;
    expect_error(resources::ResourceErrorCode::SizeMismatch, [&] {
        (void)resources::ResourceSnapshot::build({"CN", std::nullopt}, {wrong_size});
    }, "declared sizes must match immutable payload bytes");

    resources::ResourceSnapshotLimits limits;
    limits.max_entry_bytes = 1;
    limits.max_total_bytes = 1;
    expect_error(resources::ResourceErrorCode::ByteLimitExceeded, [&] {
        (void)resources::ResourceSnapshot::build({"CN", std::nullopt}, {good}, limits);
    }, "entry and aggregate bytes must be bounded before publication");
}

void test_selector_precedence_and_stable_identity()
{
    std::vector<resources::ResourcePayload> entries{
        payload("image/arena/menu", "generic"),
        payload("image/arena/menu", "cn", "CN"),
        payload("image/arena/menu", "jp", "JP"),
        payload("image/activity/current/enter1", "generic-activity", std::nullopt,
                "AbydosResortRestorationCommittee", "image/png"),
        payload("image/activity/current/enter1", "cn-activity", "CN",
                "AbydosResortRestorationCommittee", "image/png"),
        payload("image/activity/current/enter1", "other", "CN", "OtherActivity",
                "image/png"),
    };
    const auto first = resources::ResourceSnapshot::build(
        {"CN", "AbydosResortRestorationCommittee"}, entries);
    std::reverse(entries.begin(), entries.end());
    const auto reordered = resources::ResourceSnapshot::build(
        {"CN", "AbydosResortRestorationCommittee"}, entries);
    check(first->snapshot_id() == reordered->snapshot_id() &&
              first->numeric_snapshot_id() == reordered->numeric_snapshot_id(),
          "snapshot identity must be independent of manifest entry order");
    const auto menu = first->resolve("image/arena/menu");
    const auto jp = first->resolve("image/arena/menu", "JP");
    const auto activity = first->resolve("image/activity/current/enter1");
    check(menu && menu->bytes().size() == 2 && jp && jp->bytes().size() == 2 &&
              activity && activity->bytes().size() == 11,
          "resolution must select locale and then frozen activity specificity");
    check(!first->resolve("image/missing") && !first->resolve("../ambient"),
          "resolution must never probe outside the manifest snapshot");

    const auto other = resources::ResourceSnapshot::build(
        {"CN", "OtherActivity"}, entries);
    check(other->snapshot_id() != first->snapshot_id() &&
              other->resolve("image/activity/current/enter1")->bytes().size() == 5 &&
              activity->bytes().size() == 11,
          "publishing another selector must not mutate entries pinned from the old snapshot");
}

}  // namespace

int main()
{
    test_sha256_vectors();
    test_validation_is_atomic_and_bounded();
    test_selector_precedence_and_stable_identity();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "resource snapshot tests passed\n";
    return EXIT_SUCCESS;
}
