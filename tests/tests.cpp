#include "catch_amalgamated.hpp"
#include "../dasmig/birthgen.hpp"
#include <chrono>
#include <concepts>
#include <string_view>
#include <fstream>

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

template <typename Generator, typename... Args>
concept can_get_birth = requires(Generator& gen, Args... args) {
    { gen.get_birth(args...) } -> std::same_as<dasmig::birth>;
};

template <typename Generator>
concept has_weighted_setting = requires(Generator& gen) { gen.weighted(); } ||
                               requires(Generator& gen) { gen.weighted(false); };

template <typename Generator>
concept has_reference_setting = requires(Generator& gen) { gen.reference_date(); } ||
                                requires(Generator& gen) {
                                    gen.reference_date(dasmig::birth_request::default_reference_date);
                                };

template <typename Generator>
concept has_seed_control = requires(Generator& gen) { gen.seed(std::uint64_t{42}); } ||
                           requires(Generator& gen) { gen.unseed(); };

static_assert(std::is_aggregate_v<dasmig::birth_request>);
static_assert(can_get_birth<dasmig::bthg, dasmig::birth_request>);
static_assert(can_get_birth<const dasmig::bthg, dasmig::birth_request, std::uint64_t>);
static_assert(!can_get_birth<const dasmig::bthg, dasmig::birth_request>);
static_assert(!can_get_birth<dasmig::bthg>);
static_assert(!can_get_birth<dasmig::bthg, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, std::uint64_t, dasmig::birth_request>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, dasmig::sex>);
static_assert(!can_get_birth<dasmig::bthg, dasmig::sex, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::sex>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::sex, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::year_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::year_t, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::sex, dasmig::year_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::sex, dasmig::year_t, std::uint64_t>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::age_range>);
static_assert(!can_get_birth<dasmig::bthg, std::string_view, dasmig::age_range, std::uint64_t>);
static_assert(!has_weighted_setting<dasmig::bthg>);
static_assert(!has_reference_setting<dasmig::bthg>);
static_assert(!has_seed_control<dasmig::bthg>);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dasmig::bthg make_loaded_generator()
{
    dasmig::bthg gen;
    if (!gen.load(dasmig::dataset::lite) &&
        !gen.load(dasmig::dataset::full))
    {
        for (const auto& candidate :
             {"resources/lite",
              "../resources/lite",
              "birth-generator/resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(candidate);
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load - populates birth data from resource directory", "[loading]")
{
    auto gen = make_loaded_generator();
    REQUIRE(gen.country_count() > 100);
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    dasmig::bthg gen;
    gen.load("does_not_exist_dir");
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.country_count() == 0);
}

TEST_CASE("load - file path (not directory) is a no-op", "[loading]")
{
    dasmig::bthg gen;
    gen.load("dasmig/birthgen.hpp");
    REQUIRE_FALSE(gen.has_data());
}

// ---------------------------------------------------------------------------
// Dataset tier loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads fewer than full", "[loading][tier]")
{
    dasmig::bthg gen_lite;
    dasmig::bthg gen_full;

    bool lite_ok = gen_lite.load(dasmig::dataset::lite);
    bool full_ok = gen_full.load(dasmig::dataset::full);

    if (lite_ok && full_ok)
    {
        REQUIRE(gen_lite.country_count() > 0);
        REQUIRE(gen_full.country_count() > 0);
        REQUIRE(gen_full.country_count() > gen_lite.country_count());
    }
}

#ifndef DASMIG_BIRTH_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for missing tier", "[loading][tier]")
{
    dasmig::bthg gen;
    auto old_cwd = std::filesystem::current_path();
    auto tmp = std::filesystem::temp_directory_path() / "bthg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);

    REQUIRE_FALSE(gen.load(dasmig::dataset::lite));
    REQUIRE_FALSE(gen.load(dasmig::dataset::full));
    REQUIRE_FALSE(gen.has_data());

    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
}
#endif

// ---------------------------------------------------------------------------
// Generation — basic
// ---------------------------------------------------------------------------

TEST_CASE("country request returns a valid birth", "[generation]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"});

    REQUIRE(b.country_code == "US");
    REQUIRE(b.year > 1900);
    REQUIRE(b.year <= 2030);
    REQUIRE(b.month >= 1);
    REQUIRE(b.month <= 12);
    REQUIRE(b.day >= 1);
    REQUIRE(b.day <= 31);
    REQUIRE(b.age <= 100);
    REQUIRE(b.weekday <= 6);
    REQUIRE(b.le_remaining >= 0.0);
    REQUIRE_FALSE(b.cohort.empty());
    REQUIRE(b.signature().data == gen.data_hash());
    REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "US"}, b.seed()).to_json() == b.to_json());
}

TEST_CASE("default request returns any valid birth", "[generation]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth(dasmig::birth_request{});

    REQUIRE_FALSE(b.country_code.empty());
    REQUIRE(b.month >= 1);
    REQUIRE(b.month <= 12);
    REQUIRE(b.day >= 1);
    REQUIRE(b.day <= 31);
}

TEST_CASE("get_birth with seed is deterministic", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "BR"}, std::uint64_t{42});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "BR"}, std::uint64_t{42});

    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.age == b2.age);
    REQUIRE(b1.bio_sex == b2.bio_sex);
}

TEST_CASE("get_birth seed replay", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "JP"});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "JP"}, std::uint64_t{b1.seed()});

    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

TEST_CASE("get_birth throws when empty", "[generation]")
{
    dasmig::bthg gen;
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{.country_code = "US"}), std::runtime_error);
}

TEST_CASE("seeded request throws when empty", "[generation]")
{
    const dasmig::bthg gen;
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{}, 42), std::runtime_error);
}

TEST_CASE("get_birth throws for unknown country", "[generation]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{.country_code = "ZZ"}), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Date validity
// ---------------------------------------------------------------------------

TEST_CASE("generated dates are always valid", "[generation][date]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
}

TEST_CASE("February dates respect leap years", "[generation][date]")
{
    auto gen = make_loaded_generator();

    // Generate many births and check February dates.
    bool found_feb = false;
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        if (b.month == 2)
        {
            found_feb = true;
            auto yr = std::chrono::year{static_cast<int>(b.year)};
            unsigned max_day = yr.is_leap() ? 29U : 28U;
            REQUIRE(b.day <= max_day);
        }
    }
    REQUIRE(found_feb);
}

// ---------------------------------------------------------------------------
// Sex distribution
// ---------------------------------------------------------------------------

TEST_CASE("both sexes are generated", "[generation][sex]")
{
    auto gen = make_loaded_generator();

    int males = 0;
    int females = 0;

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        if (b.bio_sex == dasmig::sex::male) ++males;
        else ++females;
    }

    REQUIRE(males > 100);
    REQUIRE(females > 100);
}

// ---------------------------------------------------------------------------
// Age distribution
// ---------------------------------------------------------------------------

TEST_CASE("ages span a reasonable range", "[generation][age]")
{
    auto gen = make_loaded_generator();

    std::uint8_t min_age = 255;
    std::uint8_t max_age = 0;

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        if (b.age < min_age) min_age = b.age;
        if (b.age > max_age) max_age = b.age;
    }

    REQUIRE(min_age <= 5);
    REQUIRE(max_age >= 50);
}

// ---------------------------------------------------------------------------
// Cohort labels
// ---------------------------------------------------------------------------

TEST_CASE("cohort labels are correct", "[generation][cohort]")
{
    auto gen = make_loaded_generator();

    // Use deterministic seeds that produce known ages/years.
    // Generate many and collect cohorts.

    std::set<std::string> cohorts;
    for (std::uint64_t s = 0; s < 1000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        cohorts.insert(b.cohort);
    }

    // Should see at least a few different cohorts.
    REQUIRE(cohorts.size() >= 3);
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

TEST_CASE("implicit string conversion returns ISO date", "[conversion]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{42});
    std::string s = b;
    REQUIRE(s == b.date_string());
    REQUIRE(s.size() == 10);
    REQUIRE(s[4] == '-');
    REQUIRE(s[7] == '-');
}

TEST_CASE("ostream operator outputs ISO date", "[conversion]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{42});
    std::ostringstream oss;
    oss << b;
    REQUIRE(oss.str() == b.date_string());
}

// ---------------------------------------------------------------------------
// Seeding
// ---------------------------------------------------------------------------

TEST_CASE("explicit call seeds reproduce full results across instances", "[request][seed]")
{
    const auto gen = make_loaded_generator();
    const auto independent = make_loaded_generator();
    const dasmig::birth_request request{.country_code = "DE"};
    for (const std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{100},
                                    std::uint64_t{101}, UINT64_MAX})
    {
        const auto b = gen.get_birth(request, seed);
        REQUIRE(b.seed() == seed);
        REQUIRE(gen.get_birth(request, seed).to_json() == b.to_json());
        REQUIRE(independent.get_birth(request, seed).to_json() == b.to_json());
    }
}

// ---------------------------------------------------------------------------
// Weighted vs uniform country selection
// ---------------------------------------------------------------------------

TEST_CASE("weighted mode favours populous countries", "[weighted]")
{
    auto gen = make_loaded_generator();

    std::unordered_map<std::string, int> counts;
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{}, s);
        counts[b.country_code]++;
    }

    // India and China combined should appear often.
    int big = counts["IN"] + counts["CN"];
    REQUIRE(big > 300);
}

TEST_CASE("uniform mode spreads across countries", "[weighted]")
{
    auto gen = make_loaded_generator();

    std::set<std::string> seen;
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.weighted = false}, s);
        seen.insert(b.country_code);
    }

    // Should hit a good fraction of countries.
    REQUIRE(seen.size() > gen.country_count() / 2);
}

// ---------------------------------------------------------------------------
// Multiple countries
// ---------------------------------------------------------------------------

TEST_CASE("can generate for many different countries", "[generation]")
{
    auto gen = make_loaded_generator();

    for (const auto* code : {"US", "BR", "JP", "NG", "DE", "IN", "CN",
                              "AU", "ZA", "MX", "FR", "GB"})
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = code});
        REQUIRE(b.country_code == code);
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
    }
}

// ---------------------------------------------------------------------------
// Life expectancy
// ---------------------------------------------------------------------------

TEST_CASE("life expectancy remaining is non-negative", "[generation][le]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        REQUIRE(b.le_remaining >= 0.0);
    }
}

// ---------------------------------------------------------------------------
// Weekday correctness
// ---------------------------------------------------------------------------

TEST_CASE("weekday field matches the actual calendar date", "[generation][weekday]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        auto wd = std::chrono::weekday{std::chrono::sys_days{ymd}};
        REQUIRE(b.weekday == wd.c_encoding());
    }
}

// ---------------------------------------------------------------------------
// Month distribution (seasonality)
// ---------------------------------------------------------------------------

TEST_CASE("month distribution is non-degenerate", "[generation][month]")
{
    auto gen = make_loaded_generator();

    std::array<int, 12> month_counts{};
    for (std::uint64_t s = 0; s < 3000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        ++month_counts[b.month - 1];
    }

    // Every month should appear at least once.
    for (int m = 0; m < 12; ++m)
    {
        REQUIRE(month_counts[m] > 50);
    }
}

// ---------------------------------------------------------------------------
// Random country with seed is deterministic
// ---------------------------------------------------------------------------

TEST_CASE("seeded default request selects same country deterministically", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{}, std::uint64_t{9999});
    auto b2 = gen.get_birth(dasmig::birth_request{}, std::uint64_t{9999});

    REQUIRE(b1.country_code == b2.country_code);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.bio_sex == b2.bio_sex);
}

// ---------------------------------------------------------------------------
// date_string format
// ---------------------------------------------------------------------------

TEST_CASE("date_string produces valid ISO 8601 format", "[conversion]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 100; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        auto ds = b.date_string();

        REQUIRE(ds.size() == 10);
        // Check digit pattern: DDDD-DD-DD
        for (int pos : {0, 1, 2, 3})
            REQUIRE(std::isdigit(static_cast<unsigned char>(ds[pos])));
        REQUIRE(ds[4] == '-');
        for (int pos : {5, 6})
            REQUIRE(std::isdigit(static_cast<unsigned char>(ds[pos])));
        REQUIRE(ds[7] == '-');
        for (int pos : {8, 9})
            REQUIRE(std::isdigit(static_cast<unsigned char>(ds[pos])));
    }
}

// ---------------------------------------------------------------------------
// has_data after load
// ---------------------------------------------------------------------------

TEST_CASE("has_data returns true after successful load", "[loading]")
{
    auto gen = make_loaded_generator();
    REQUIRE(gen.has_data());
    REQUIRE(gen.country_count() > 0);
}

// ---------------------------------------------------------------------------
// Life expectancy differs by sex
// ---------------------------------------------------------------------------

TEST_CASE("life expectancy differs for male vs female", "[generation][le]")
{
    auto gen = make_loaded_generator();

    double sum_male_le = 0;
    double sum_female_le = 0;
    int males = 0;
    int females = 0;

    for (std::uint64_t s = 0; s < 1000; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "JP"}, s);
        if (b.bio_sex == dasmig::sex::male)
        {
            sum_male_le += b.le_remaining;
            ++males;
        }
        else
        {
            sum_female_le += b.le_remaining;
            ++females;
        }
    }

    REQUIRE(males > 100);
    REQUIRE(females > 100);

    // Female life expectancy is typically higher.
    double avg_male = sum_male_le / males;
    double avg_female = sum_female_le / females;
    REQUIRE(avg_female > avg_male);
}

// ===========================================================================
// Sex-specific generation
// ===========================================================================

TEST_CASE("male country request always returns male", "[generation][sex]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 200; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .bio_sex = dasmig::sex::male}, s);
        REQUIRE(b.bio_sex == dasmig::sex::male);
        REQUIRE(b.country_code == "US");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
    }
}

TEST_CASE("female country request always returns female", "[generation][sex]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 200; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "JP", .bio_sex = dasmig::sex::female}, s);
        REQUIRE(b.bio_sex == dasmig::sex::female);
        REQUIRE(b.country_code == "JP");
    }
}

TEST_CASE("seeded sex and country request is deterministic", "[generation][sex][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "BR", .bio_sex = dasmig::sex::female}, std::uint64_t{777});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "BR", .bio_sex = dasmig::sex::female}, std::uint64_t{777});

    REQUIRE(b1.bio_sex == dasmig::sex::female);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.age == b2.age);
}

TEST_CASE("sex request selects random country with fixed sex", "[generation][sex]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 100; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.bio_sex = dasmig::sex::male}, s);
        REQUIRE(b.bio_sex == dasmig::sex::male);
        REQUIRE_FALSE(b.country_code.empty());
    }
}

TEST_CASE("seeded sex request is deterministic", "[generation][sex][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.bio_sex = dasmig::sex::female}, std::uint64_t{555});
    auto b2 = gen.get_birth(dasmig::birth_request{.bio_sex = dasmig::sex::female}, std::uint64_t{555});

    REQUIRE(b1.bio_sex == dasmig::sex::female);
    REQUIRE(b1.country_code == b2.country_code);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

// ===========================================================================
// Year-specific generation
// ===========================================================================

TEST_CASE("year and country request fixes the birth year", "[generation][year]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 200; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .year = dasmig::year_t{1990}}, s);
        REQUIRE(b.year == 1990);
        REQUIRE(b.country_code == "US");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
        REQUIRE(b.day >= 1);
        REQUIRE(b.day <= 31);
    }
}

TEST_CASE("year and country request derives correct age", "[generation][year]")
{
    // Ages are completed years at the reference date (default 2023-07-01),
    // not at today's date.
    auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 50; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "DE", .year = dasmig::year_t{2000}}, s);
        const bool after_july_1 = b.month > 7 || (b.month == 7 && b.day > 1);
        REQUIRE(b.year == 2000);
        REQUIRE(b.age == (after_july_1 ? 22 : 23));
    }
}

TEST_CASE("reference_date moves ages, not the clock", "[generation][year]")
{
    auto gen = make_loaded_generator();
    dasmig::birth_request request{.country_code = "DE"};
    REQUIRE(request.reference_date == dasmig::birth_request::default_reference_date);
    const auto before = gen.get_birth(request, std::uint64_t{42});
    request.reference_date = std::chrono::year{2033} / std::chrono::July / std::chrono::day{1};
    const auto after = gen.get_birth(request, std::uint64_t{42});
    REQUIRE(after.age == before.age);          // same draw
    REQUIRE(after.year == before.year + 10);   // ten years later
    REQUIRE(after.month == before.month);
    request.reference_date = std::chrono::year{2023} / std::chrono::February / std::chrono::day{30};
    REQUIRE_THROWS_AS(gen.get_birth(request, 42), std::invalid_argument);
}

TEST_CASE("seeded year and country request is deterministic", "[generation][year][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "JP", .year = dasmig::year_t{1985}}, std::uint64_t{333});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "JP", .year = dasmig::year_t{1985}}, std::uint64_t{333});

    REQUIRE(b1.year == 1985);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.bio_sex == b2.bio_sex);
}

TEST_CASE("get_birth dates are valid for fixed year", "[generation][year][date]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 300; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "BR", .year = dasmig::year_t{2000}}, s);
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
}

// ===========================================================================
// Sex + year generation
// ===========================================================================

TEST_CASE("sex and year request fixes both", "[generation][sex][year]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 200; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{
            .country_code = "DE", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1985}}, s);
        REQUIRE(b.bio_sex == dasmig::sex::female);
        REQUIRE(b.year == 1985);
        REQUIRE(b.country_code == "DE");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
    }
}

TEST_CASE("seeded sex and year request is deterministic", "[generation][sex][year][seed]")
{
    auto gen = make_loaded_generator();
    const dasmig::birth_request request{
        .country_code = "IN", .bio_sex = dasmig::sex::male, .year = dasmig::year_t{1970}};
    auto b1 = gen.get_birth(request, std::uint64_t{111});
    auto b2 = gen.get_birth(request, std::uint64_t{111});

    REQUIRE(b1.bio_sex == dasmig::sex::male);
    REQUIRE(b1.year == 1970);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

TEST_CASE("sex and year request cohort matches year", "[generation][sex][year][cohort]")
{
    auto gen = make_loaded_generator();

    auto b1 = gen.get_birth(dasmig::birth_request{
        .country_code = "US", .bio_sex = dasmig::sex::male, .year = dasmig::year_t{1950}});
    REQUIRE(b1.cohort == "Baby Boomer");

    auto b2 = gen.get_birth(dasmig::birth_request{
        .country_code = "US", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{2005}});
    REQUIRE(b2.cohort == "Generation Z");

    auto b3 = gen.get_birth(dasmig::birth_request{
        .country_code = "US", .bio_sex = dasmig::sex::male, .year = dasmig::year_t{2020}});
    REQUIRE(b3.cohort == "Generation Alpha");
}

// ===========================================================================
// Age-range generation
// ===========================================================================

TEST_CASE("age range request bounds the age", "[generation][age_range]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 500; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{18, 65}}, s);
        REQUIRE(b.age >= 18);
        REQUIRE(b.age <= 65);
        REQUIRE(b.country_code == "US");
    }
}

TEST_CASE("get_birth age range narrow band", "[generation][age_range]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 100; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "JP", .ages = dasmig::age_range{25, 30}}, s);
        REQUIRE(b.age >= 25);
        REQUIRE(b.age <= 30);
    }
}

TEST_CASE("seeded age range request is deterministic", "[generation][age_range][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "BR", .ages = dasmig::age_range{20, 40}}, std::uint64_t{888});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "BR", .ages = dasmig::age_range{20, 40}}, std::uint64_t{888});

    REQUIRE(b1.age >= 20);
    REQUIRE(b1.age <= 40);
    REQUIRE(b1.age == b2.age);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

TEST_CASE("get_birth age range dates are valid", "[generation][age_range][date]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 300; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{0, 100}}, s);
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
}

TEST_CASE("get_birth age range throws when min > max", "[generation][age_range]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(
        gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{50, 20}}),
        std::invalid_argument);
}

TEST_CASE("get_birth age range single age", "[generation][age_range]")
{
    auto gen = make_loaded_generator();

    for (std::uint64_t s = 0; s < 50; ++s)
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{30, 30}}, s);
        REQUIRE(b.age == 30);
    }
}

// ===========================================================================
// Seed round-trip for a random-country request
// ===========================================================================

TEST_CASE("default request seed round-trip", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::birth_request{});
    auto b2 = gen.get_birth(dasmig::birth_request{}, b1.seed());

    REQUIRE(b1.country_code == b2.country_code);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

// ---------------------------------------------------------------------------
// Per-call requests
// ---------------------------------------------------------------------------

TEST_CASE("birth_request defaults preserve population weighting and reference date", "[request]")
{
    const dasmig::birth_request request;
    REQUIRE_FALSE(request.country_code);
    REQUIRE_FALSE(request.bio_sex);
    REQUIRE_FALSE(request.year);
    REQUIRE_FALSE(request.ages);
    REQUIRE(request.weighted);
    REQUIRE(request.reference_date == std::chrono::year{2023} / std::chrono::July / std::chrono::day{1});
    REQUIRE(request.reference_date == dasmig::bthg::default_reference_date);
}

TEST_CASE("birth_request combines sex and ages for fixed or random countries", "[request]")
{
    const auto gen = make_loaded_generator();
    for (const auto bio_sex : {dasmig::sex::male, dasmig::sex::female})
    {
        for (const auto& country : {std::optional<std::string>{"JP"}, std::optional<std::string>{}})
        {
            const dasmig::birth_request request{
                .country_code = country, .bio_sex = bio_sex, .ages = dasmig::age_range{25, 30}};
            for (std::uint64_t s = 0; s < 100; ++s)
            {
                const auto b = gen.get_birth(request, s);
                REQUIRE(b.bio_sex == bio_sex);
                REQUIRE(b.age >= 25);
                REQUIRE(b.age <= 30);
                if (country) REQUIRE(b.country_code == *country);
                else REQUIRE_FALSE(b.country_code.empty());
                const auto born = std::chrono::year{b.year} / std::chrono::month{b.month} /
                                  std::chrono::day{b.day};
                REQUIRE(born.ok());
            }
        }
    }
}

TEST_CASE("birth_request fixes year with a random country", "[request][year]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        const dasmig::birth_request request{.bio_sex = dasmig::sex::female, .year = dasmig::year_t{2000}};
        const auto b = gen.get_birth(request, s);
        REQUIRE(b.year == 2000);
        REQUIRE(b.bio_sex == dasmig::sex::female);
        REQUIRE(b.country_code == gen.get_birth(dasmig::birth_request{}, s).country_code);
        const bool after_reference = b.month > 7 || (b.month == 7 && b.day > 1);
        REQUIRE(b.age == (after_reference ? 22 : 23));
    }
}

TEST_CASE("birth_request age ranges preserve clamping and single ages", "[request][age_range]")
{
    const auto gen = make_loaded_generator();
    for (const dasmig::age_range range : {dasmig::age_range{0, 0}, dasmig::age_range{30, 30},
                                         dasmig::age_range{0, 255}, dasmig::age_range{101, 255}})
    {
        const dasmig::birth_request request{.country_code = "US", .ages = range};
        for (std::uint64_t s = 0; s < 50; ++s)
        {
            const auto b = gen.get_birth(request, s);
            REQUIRE(b.age >= std::min(range.min, std::uint8_t{100}));
            REQUIRE(b.age <= std::min(range.max, std::uint8_t{100}));
            const auto born = std::chrono::year{b.year} / std::chrono::month{b.month} /
                              std::chrono::day{b.day};
            REQUIRE(born.ok());
        }
    }
}

TEST_CASE("birth_request weighting controls country distributions", "[request][weighted]")
{
    auto gen = make_loaded_generator();
    int weighted_big = 0;
    int uniform_big = 0;
    std::set<std::string> uniform_countries;
    for (const bool weighted : {true, false})
    {
        const dasmig::birth_request request{.weighted = weighted};
        for (std::uint64_t s = 0; s < 2000; ++s)
        {
            const auto b = gen.get_birth(request, s);
            const bool big = b.country_code == "IN" || b.country_code == "CN";
            if (weighted) weighted_big += big;
            else
            {
                uniform_big += big;
                uniform_countries.insert(b.country_code);
            }
        }
    }
    REQUIRE(weighted_big > 300);
    REQUIRE(uniform_big < 100);
    REQUIRE(uniform_countries.size() > gen.country_count() / 2);
    REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "US", .weighted = false}, 42).to_json() ==
            gen.get_birth(dasmig::birth_request{.country_code = "US"}, 42).to_json());
}

TEST_CASE("birth_request reference date controls exact ages and placement", "[request][reference]")
{
    const auto gen = make_loaded_generator();
    for (const auto reference : {std::chrono::year{2040} / std::chrono::January / std::chrono::day{1},
                                 std::chrono::year{2024} / std::chrono::February / std::chrono::day{29},
                                 std::chrono::year{2033} / std::chrono::July / std::chrono::day{1}})
    {
        const dasmig::birth_request request{
            .country_code = "BR", .bio_sex = dasmig::sex::female,
            .ages = dasmig::age_range{18, 65}, .reference_date = reference};
        for (std::uint64_t s = 0; s < 100; ++s)
        {
            const auto b = gen.get_birth(request, s);
            const auto born = std::chrono::year{b.year} / std::chrono::month{b.month} /
                              std::chrono::day{b.day};
            REQUIRE(born.ok());
            int completed = static_cast<int>(reference.year()) - b.year;
            if (std::chrono::month_day{born.month(), born.day()} >
                std::chrono::month_day{reference.month(), reference.day()})
            {
                --completed;
            }
            REQUIRE(b.age == completed);
            const auto fixed = gen.get_birth(dasmig::birth_request{
                .country_code = "BR", .year = dasmig::year_t{2000}, .reference_date = reference}, s);
            REQUIRE(fixed.year == 2000);
            const bool after_reference = std::chrono::month_day{std::chrono::month{fixed.month},
                                                                std::chrono::day{fixed.day}} >
                                         std::chrono::month_day{reference.month(), reference.day()};
            REQUIRE(fixed.age == static_cast<int>(reference.year()) - 2000 - after_reference);
        }
    }
}

TEST_CASE("birth_request preserves fixed year age clamping", "[request][year]")
{
    const auto gen = make_loaded_generator();
    for (const dasmig::year_t year : {dasmig::year_t{1900}, dasmig::year_t{2040}})
    {
        const auto b = gen.get_birth(dasmig::birth_request{.country_code = "US", .year = year}, 42);
        REQUIRE(b.year == year.value);
        REQUIRE(b.age == (year.value == 1900 ? 100 : 0));
        const auto born = std::chrono::year{b.year} / std::chrono::month{b.month} /
                          std::chrono::day{b.day};
        REQUIRE(born.ok());
    }
}

TEST_CASE("birth_request replays full results and signatures", "[request][seed]")
{
    const auto gen = make_loaded_generator();
    const dasmig::birth_request request{
        .bio_sex = dasmig::sex::male, .ages = dasmig::age_range{18, 65}, .weighted = false,
        .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
    for (const std::uint64_t s : {std::uint64_t{0}, std::uint64_t{42}, UINT64_MAX})
    {
        const auto b = gen.get_birth(request, s);
        REQUIRE(b.seed() == s);
        REQUIRE(b.signature().data == gen.data_hash());
        REQUIRE(gen.get_birth(request, b.seed()).to_json() == b.to_json());
    }
}

TEST_CASE("birth_request validates filters with existing error messages", "[request][errors]")
{
    const auto gen = make_loaded_generator();
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{.country_code = "ZZ"}, 42),
                        "Unknown country code: ZZ");
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{.country_code = ""}, 42),
                        "Unknown country code: ");
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{.ages = dasmig::age_range{50, 20}}, 42),
                        "age_range: min must be <= max");
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{.country_code = "ZZ",
                                                          .ages = dasmig::age_range{50, 20}}, 42),
                        "age_range: min must be <= max");
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{
        .reference_date = std::chrono::year{2023} / std::chrono::February / std::chrono::day{30}}, 42),
        "reference_date: not a valid date");
    REQUIRE_THROWS_WITH(gen.get_birth(dasmig::birth_request{
        .year = dasmig::year_t{1990}, .ages = dasmig::age_range{18, 65}}, 42),
        "birth_request: year and ages are mutually exclusive");
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{.ages = dasmig::age_range{50, 20}}, 42),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{
        .year = dasmig::year_t{1990}, .ages = dasmig::age_range{18, 65}}, 42), std::invalid_argument);

    const dasmig::bthg empty;
    REQUIRE_THROWS_WITH(empty.get_birth(dasmig::birth_request{}, 42),
                        "No birth data loaded. Call load() first.");
    REQUIRE_THROWS_AS(empty.get_birth(dasmig::birth_request{.country_code = "US"}, 42), std::runtime_error);
    REQUIRE_THROWS_WITH(empty.get_birth(dasmig::birth_request{.ages = dasmig::age_range{50, 20}}, 42),
                        "age_range: min must be <= max");
}

TEST_CASE("birth_request settings and failures cannot leak into subsequent calls", "[request][isolation]")
{
    auto gen = make_loaded_generator();
    auto baseline = make_loaded_generator();
    const bool weighted = GENERATE(true, false);
    const auto request_before = gen.get_birth(dasmig::birth_request{}, 42).to_json();

    const dasmig::birth_request request{
        .bio_sex = dasmig::sex::female, .ages = dasmig::age_range{30, 30}, .weighted = weighted,
        .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
    const auto result = gen.get_birth(request, 42);
    REQUIRE(result.age == 30);
    REQUIRE(gen.get_birth(dasmig::birth_request{}, 42).to_json() == request_before);
    REQUIRE(gen.data_hash() == baseline.data_hash());
    REQUIRE(gen.country_count() == baseline.country_count());

    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{
        .weighted = true,
        .reference_date = std::chrono::year{2040} / std::chrono::February / std::chrono::day{30}}, 42),
        std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{
        .country_code = "ZZ", .reference_date = dasmig::bthg::default_reference_date}, 42),
        std::invalid_argument);
    for (std::uint64_t s = 0; s < 10; ++s)
    {
        const auto random = gen.get_birth(dasmig::birth_request{});
        REQUIRE(gen.get_birth(dasmig::birth_request{}, random.seed()).to_json() == random.to_json());
        REQUIRE(gen.get_birth(dasmig::birth_request{}, s).to_json() ==
                baseline.get_birth(dasmig::birth_request{}, s).to_json());
    }

    REQUIRE(gen.get_birth(dasmig::birth_request{}, 42).to_json() == request_before);
    REQUIRE(gen.get_birth(request, 42).to_json() == result.to_json());
}

TEST_CASE("random requests validate filters with the same errors as seeded requests", "[request][errors]")
{
    auto gen = make_loaded_generator();
    const std::array<dasmig::birth_request, 5> requests{{
        {.country_code = "ZZ"},
        {.country_code = ""},
        {.ages = dasmig::age_range{50, 20}},
        {.year = dasmig::year_t{1990}, .ages = dasmig::age_range{18, 65}},
        {.reference_date = std::chrono::year{2023} / std::chrono::February / std::chrono::day{30}}}};
    const std::array messages{
        "Unknown country code: ZZ", "Unknown country code: ",
        "age_range: min must be <= max", "birth_request: year and ages are mutually exclusive",
        "reference_date: not a valid date"};
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        REQUIRE_THROWS_AS(gen.get_birth(requests[i]), std::invalid_argument);
        REQUIRE_THROWS_WITH(gen.get_birth(requests[i]), messages[i]);
        REQUIRE_THROWS_WITH(gen.get_birth(requests[i], 42), messages[i]);
    }

    dasmig::bthg empty;
    REQUIRE_THROWS_WITH(empty.get_birth(dasmig::birth_request{}),
                        "No birth data loaded. Call load() first.");
}

TEST_CASE("random requests apply all filters and replay full results", "[request][seed]")
{
    auto gen = make_loaded_generator();
    const auto reference = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1};
    const std::array<dasmig::birth_request, 8> requests{{
        {.weighted = false, .reference_date = reference},
        {.country_code = "US", .weighted = false, .reference_date = reference},
        {.bio_sex = dasmig::sex::female, .weighted = false, .reference_date = reference},
        {.country_code = "JP", .bio_sex = dasmig::sex::male, .weighted = false, .reference_date = reference},
        {.country_code = "DE", .year = dasmig::year_t{1990}, .weighted = false, .reference_date = reference},
        {.country_code = "BR", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1985},
         .weighted = false, .reference_date = reference},
        {.country_code = "US", .ages = dasmig::age_range{18, 65}, .weighted = false, .reference_date = reference},
        {.bio_sex = dasmig::sex::female, .ages = dasmig::age_range{30, 30},
         .weighted = false, .reference_date = reference}}};
    for (const auto& request : requests)
    {
        const auto b = gen.get_birth(request);
        REQUIRE(gen.get_birth(request, b.seed()).to_json() == b.to_json());
        REQUIRE(b.signature().data == gen.data_hash());
        if (request.country_code) REQUIRE(b.country_code == *request.country_code);
        if (request.bio_sex) REQUIRE(b.bio_sex == *request.bio_sex);
        if (request.year) REQUIRE(b.year == request.year->value);
        if (request.ages)
        {
            REQUIRE(b.age >= request.ages->min);
            REQUIRE(b.age <= request.ages->max);
        }
        const bool after_reference = b.month > 1 || (b.month == 1 && b.day > 1);
        REQUIRE(b.age == 2040 - b.year - after_reference);
    }
}

TEST_CASE("random request choices do not affect subsequent requests", "[request][isolation]")
{
    auto gen = make_loaded_generator();
    const auto baseline = make_loaded_generator();
    const dasmig::birth_request custom{
        .country_code = "JP", .bio_sex = dasmig::sex::female, .ages = dasmig::age_range{30, 30},
        .weighted = false,
        .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
    const auto changed = gen.get_birth(custom);
    REQUIRE(gen.get_birth(custom, changed.seed()).to_json() == changed.to_json());
    REQUIRE(changed.country_code == "JP");
    REQUIRE(changed.bio_sex == dasmig::sex::female);
    REQUIRE(changed.age == 30);
    REQUIRE(changed.year == (changed.month == 1 && changed.day == 1 ? 2010 : 2009));
    for (int i = 0; i < 10; ++i)
    {
        const auto b = gen.get_birth(dasmig::birth_request{});
        REQUIRE(b.to_json() == baseline.get_birth(dasmig::birth_request{}, b.seed()).to_json());
    }
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed, data and reference date give these
// exact results on every compiler and platform. A change here is a change of
// output and needs a major version.
// ---------------------------------------------------------------------------

namespace
{

dasmig::bthg load_lite_dir()
{
    dasmig::bthg gen;
    for (const char* path : {"resources/lite", "../resources/lite"})
    {
        if (std::filesystem::is_directory(path))
        {
            gen.load(std::filesystem::path{path});
            break;
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

std::string pyramid_header()
{
    std::string h = "cca2";
    for (const char s : {'m', 'f'})
    {
        for (int a = 0; a <= 100; ++a) h += "\t" + std::string(1, s) + std::to_string(a);
    }
    return h + "\n";
}

// A pyramid where everyone is @p age.
std::string pyramid_row(std::string_view cca2, int age)
{
    std::string r{cca2};
    for (int s = 0; s < 2; ++s)
    {
        for (int a = 0; a <= 100; ++a) r += a == age ? "\t10" : "\t0";
    }
    return r + "\n";
}

const std::string countries_header =
    "cca2\tcca3\tname\tregion\tsubregion\tlatitude\tindependent\tle_male\tle_female\tcsection_rate\n";

} // namespace

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    auto gen = load_lite_dir();
    REQUIRE(gen.data_hash() == 0x99276bbdb2b0e563ULL);

    REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{1}).to_json() ==
            R"({"country_code":"US","date":"1999-05-19","year":1999,"month":5,"day":19,"age":24,)"
            R"("sex":"male","weekday":3,"le_remaining":52.900000000000006,"cohort":"Generation Z",)"
            R"("signature":{"seed":"0000000000000001","data":"99276bbdb2b0e563"}})");
    REQUIRE(gen.get_birth(dasmig::birth_request{
                .country_code = "JP", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1990}},
                std::uint64_t{2}).date_string() ==
            "1990-12-04");
    REQUIRE(gen.get_birth(dasmig::birth_request{}, std::uint64_t{3}).to_json() ==
            R"({"country_code":"IN","date":"2013-08-19","year":2013,"month":8,"day":19,"age":9,)"
            R"("sex":"male","weekday":1,"le_remaining":61.5,"cohort":"Generation Alpha",)"
            R"("signature":{"seed":"0000000000000003","data":"99276bbdb2b0e563"}})");
    REQUIRE(gen.get_birth(dasmig::birth_request{
                .country_code = "BR", .ages = dasmig::age_range{.min = 18, .max = 65}},
                std::uint64_t{4}).date_string() ==
            "1982-04-23");
    REQUIRE(gen.get_birth(dasmig::birth_request{.weighted = false}, std::uint64_t{5}).country_code == "HN");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_birth(dasmig::birth_request{}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0xe717822b5c6d5d49ULL);
}

TEST_CASE("ages are exact at the reference date", "[generation][age]")
{
    auto gen = make_loaded_generator();
    const auto ref = dasmig::birth_request::default_reference_date;
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        const auto b = gen.get_birth(dasmig::birth_request{.country_code = "NG"}, s);
        const auto born = std::chrono::year{b.year} / std::chrono::month{b.month} / std::chrono::day{b.day};
        REQUIRE(born.ok());
        int completed = static_cast<int>(ref.year()) - b.year;
        if (std::chrono::month_day{born.month(), born.day()} >
            std::chrono::month_day{ref.month(), ref.day()})
        {
            --completed;
        }
        REQUIRE(completed == b.age);
    }
}

TEST_CASE("forcing the sex leaves the other draws unchanged", "[generation][sex]")
{
    auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto free = gen.get_birth(dasmig::birth_request{.country_code = "US"}, s);
        REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "US", .bio_sex = free.bio_sex}, s).to_json() == free.to_json());
    }
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

TEST_CASE("extend a single table", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto count = gen.country_count();
    const auto base_hash = gen.data_hash();

    // A new country needs a countries row and an age pyramid.
    gen.extend_from_string(dasmig::bthg::table::countries,
                           countries_header + "QQ\tQQQ\tAtlantis\tOcean\t\t30\t1\t80\t84\t0.1\n", "atl.tsv");
    REQUIRE(gen.country_count() == count); // no pyramid yet
    REQUIRE_THROWS_AS(gen.get_birth(dasmig::birth_request{.country_code = "QQ"}), std::invalid_argument);
    gen.extend_from_string(dasmig::bthg::table::age_pyramid, pyramid_header() + pyramid_row("QQ", 30), "pyr.tsv");
    REQUIRE(gen.country_count() == count + 1);
    REQUIRE(gen.data_hash() != base_hash);

    // Everyone in QQ is 30; without monthly data every month is possible.
    std::set<int> months;
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        const auto b = gen.get_birth(dasmig::birth_request{.country_code = "QQ"}, s);
        REQUIRE(b.age == 30);
        REQUIRE(b.le_remaining == Catch::Approx(b.bio_sex == dasmig::sex::male ? 50.0 : 54.0));
        months.insert(b.month);
    }
    REQUIRE(months.size() == 12);
    REQUIRE(gen.layers(dasmig::bthg::table::countries).size() == 2);
    REQUIRE(gen.layers(dasmig::bthg::table::monthly_births).size() == 1);
}

TEST_CASE("a directory layer is applied all or nothing", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    const auto count = gen.country_count();
    const auto dir = std::filesystem::temp_directory_path() / "bthg_layer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream{dir / "countries.tsv"} << countries_header << "QQ\tQQQ\tAtlantis\tOcean\t\t30\t1\t80\t84\t0.1\n";
        std::ofstream{dir / "age_pyramid.tsv"} << pyramid_header() << pyramid_row("QQ", 30);
        std::ofstream{dir / "monthly_births.tsv"} << "cca2\tjan\tfeb\tmar\tapr\tmay\tjun\tjul\taug\tsep\toct\tnov\tdec\n"
                                                  << "QQ\t1\t1\t1\t1\t1\t1\t1\t1\t1\t1\t1\tmany\n";
    }
    try
    {
        gen.extend(dir);
        FAIL("expected core::tsv_error");
    }
    catch (const dasmig::core::tsv_error& e)
    {
        REQUIRE(e.column() == "dec");
    }
    REQUIRE(gen.data_hash() == hash); // the valid countries and pyramid files were not applied either
    REQUIRE(gen.country_count() == count);

    std::ofstream{dir / "monthly_births.tsv"} << "cca2\tjan\tfeb\tmar\tapr\tmay\tjun\tjul\taug\tsep\toct\tnov\tdec\n"
                                              << "QQ\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t5\t0\n";
    gen.extend(dir);
    REQUIRE(gen.country_count() == count + 1);
    REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "QQ"}).month == 11); // every QQ birth is in November

    REQUIRE_THROWS_AS(gen.extend(dir / "other.tsv"), std::invalid_argument);
    std::filesystem::remove_all(dir);
}

TEST_CASE("bad rows are rejected with their location", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    REQUIRE_THROWS_WITH(gen.extend_from_string(dasmig::bthg::table::countries,
                                               countries_header + "QQ\tQQQ\tAtlantis\tOcean\t\t30\t1\t80\t84\t1.5\n",
                                               "c.tsv"),
                        "c.tsv:2: column 'csection_rate': must be between 0 and 1");
    REQUIRE_THROWS_WITH(gen.extend_from_string(dasmig::bthg::table::age_pyramid,
                                               pyramid_header() + pyramid_row("QQ", 200), "p.tsv"),
                        "p.tsv:2: column 'm0': every age of this sex is 0");
    REQUIRE(gen.data_hash() == hash);
}

#ifdef DASMIG_BIRTH_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "bthg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::bthg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(dasmig::bthg::table::countries).front().source == "embedded:lite/countries.tsv");
    REQUIRE(gen.data_hash() == 0x99276bbdb2b0e563ULL); // same as the files
    REQUIRE(gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{1}).date_string() == "1999-05-19");
}
#endif
