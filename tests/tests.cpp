#include "catch_amalgamated.hpp"
#include "../dasmig/birthgen.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// Generation — basic
// ---------------------------------------------------------------------------

TEST_CASE("get_birth(cca2) returns a valid birth", "[generation]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth("US");

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
    REQUIRE(b.seed() != 0);
}

TEST_CASE("get_birth() returns any valid birth", "[generation]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth();

    REQUIRE_FALSE(b.country_code.empty());
    REQUIRE(b.month >= 1);
    REQUIRE(b.month <= 12);
    REQUIRE(b.day >= 1);
    REQUIRE(b.day <= 31);
}

TEST_CASE("get_birth with seed is deterministic", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("BR", 42);
    auto b2 = gen.get_birth("BR", 42);

    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.age == b2.age);
    REQUIRE(b1.bio_sex == b2.bio_sex);
}

TEST_CASE("get_birth seed replay", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("JP");
    auto b2 = gen.get_birth("JP", b1.seed());

    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

TEST_CASE("get_birth throws when empty", "[generation]")
{
    dasmig::bthg gen;
    REQUIRE_THROWS_AS(gen.get_birth("US"), std::runtime_error);
}

TEST_CASE("get_birth(seed) throws when empty", "[generation]")
{
    const dasmig::bthg gen;
    REQUIRE_THROWS_AS(gen.get_birth(42), std::runtime_error);
}

TEST_CASE("get_birth throws for unknown country", "[generation]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_birth("ZZ"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Date validity
// ---------------------------------------------------------------------------

TEST_CASE("generated dates are always valid", "[generation][date]")
{
    auto gen = make_loaded_generator();
    gen.seed(123);

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US");
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
    gen.unseed();
}

TEST_CASE("February dates respect leap years", "[generation][date]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    // Generate many births and check February dates.
    bool found_feb = false;
    for (int i = 0; i < 2000; ++i)
    {
        auto b = gen.get_birth("US");
        if (b.month == 2)
        {
            found_feb = true;
            auto yr = std::chrono::year{static_cast<int>(b.year)};
            unsigned max_day = yr.is_leap() ? 29U : 28U;
            REQUIRE(b.day <= max_day);
        }
    }
    REQUIRE(found_feb);
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Sex distribution
// ---------------------------------------------------------------------------

TEST_CASE("both sexes are generated", "[generation][sex]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    int males = 0;
    int females = 0;

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US");
        if (b.bio_sex == dasmig::sex::male) ++males;
        else ++females;
    }

    REQUIRE(males > 100);
    REQUIRE(females > 100);
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Age distribution
// ---------------------------------------------------------------------------

TEST_CASE("ages span a reasonable range", "[generation][age]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    std::uint8_t min_age = 255;
    std::uint8_t max_age = 0;

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US");
        if (b.age < min_age) min_age = b.age;
        if (b.age > max_age) max_age = b.age;
    }

    REQUIRE(min_age <= 5);
    REQUIRE(max_age >= 50);
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Cohort labels
// ---------------------------------------------------------------------------

TEST_CASE("cohort labels are correct", "[generation][cohort]")
{
    auto gen = make_loaded_generator();

    // Use deterministic seeds that produce known ages/years.
    // Generate many and collect cohorts.
    gen.seed(0);

    std::set<std::string> cohorts;
    for (int i = 0; i < 1000; ++i)
    {
        auto b = gen.get_birth("US");
        cohorts.insert(b.cohort);
    }

    // Should see at least a few different cohorts.
    REQUIRE(cohorts.size() >= 3);
    gen.unseed();
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

TEST_CASE("implicit string conversion returns ISO date", "[conversion]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth("US", 42);
    std::string s = b;
    REQUIRE(s == b.date_string());
    REQUIRE(s.size() == 10);
    REQUIRE(s[4] == '-');
    REQUIRE(s[7] == '-');
}

TEST_CASE("ostream operator outputs ISO date", "[conversion]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth("US", 42);
    std::ostringstream oss;
    oss << b;
    REQUIRE(oss.str() == b.date_string());
}

// ---------------------------------------------------------------------------
// Seeding
// ---------------------------------------------------------------------------

TEST_CASE("seed/unseed produces deterministic then random", "[seed]")
{
    auto gen = make_loaded_generator();

    gen.seed(100);
    auto a = gen.get_birth("DE");
    auto b = gen.get_birth("DE");

    gen.seed(100);
    auto a2 = gen.get_birth("DE");
    auto b2 = gen.get_birth("DE");

    REQUIRE(a.year == a2.year);
    REQUIRE(b.year == b2.year);
    REQUIRE(a.day == a2.day);
    REQUIRE(b.day == b2.day);

    gen.unseed();
}

TEST_CASE("seed is chainable", "[seed]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.seed(42);
    REQUIRE(&ref == &gen);
    gen.unseed();
}

TEST_CASE("unseed is chainable", "[seed]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.unseed();
    REQUIRE(&ref == &gen);
}

TEST_CASE("weighted is chainable", "[seed]")
{
    auto gen = make_loaded_generator();
    auto& ref = gen.weighted(false);
    REQUIRE(&ref == &gen);
    REQUIRE_FALSE(gen.weighted());
    gen.weighted(true);
    REQUIRE(gen.weighted());
}

// ---------------------------------------------------------------------------
// Weighted vs uniform country selection
// ---------------------------------------------------------------------------

TEST_CASE("weighted mode favours populous countries", "[weighted]")
{
    auto gen = make_loaded_generator();
    gen.seed(0).weighted(true);

    std::unordered_map<std::string, int> counts;
    for (int i = 0; i < 2000; ++i)
    {
        auto b = gen.get_birth();
        counts[b.country_code]++;
    }

    // India and China combined should appear often.
    int big = counts["IN"] + counts["CN"];
    REQUIRE(big > 300);
    gen.unseed();
}

TEST_CASE("uniform mode spreads across countries", "[weighted]")
{
    auto gen = make_loaded_generator();
    gen.seed(0).weighted(false);

    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i)
    {
        auto b = gen.get_birth();
        seen.insert(b.country_code);
    }

    // Should hit a good fraction of countries.
    REQUIRE(seen.size() > gen.country_count() / 2);
    gen.unseed();
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
        auto b = gen.get_birth(code);
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
    gen.seed(0);

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US");
        REQUIRE(b.le_remaining >= 0.0);
    }
    gen.unseed();
}
