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
    auto b1 = gen.get_birth("BR", std::uint64_t{42});
    auto b2 = gen.get_birth("BR", std::uint64_t{42});

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
    auto b2 = gen.get_birth("JP", std::uint64_t{b1.seed()});

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
    auto b = gen.get_birth("US", std::uint64_t{42});
    std::string s = b;
    REQUIRE(s == b.date_string());
    REQUIRE(s.size() == 10);
    REQUIRE(s[4] == '-');
    REQUIRE(s[7] == '-');
}

TEST_CASE("ostream operator outputs ISO date", "[conversion]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth("US", std::uint64_t{42});
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

// ---------------------------------------------------------------------------
// Weekday correctness
// ---------------------------------------------------------------------------

TEST_CASE("weekday field matches the actual calendar date", "[generation][weekday]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US");
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        auto wd = std::chrono::weekday{std::chrono::sys_days{ymd}};
        REQUIRE(b.weekday == wd.c_encoding());
    }
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Month distribution (seasonality)
// ---------------------------------------------------------------------------

TEST_CASE("month distribution is non-degenerate", "[generation][month]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    std::array<int, 12> month_counts{};
    for (int i = 0; i < 3000; ++i)
    {
        auto b = gen.get_birth("US");
        ++month_counts[b.month - 1];
    }

    // Every month should appear at least once.
    for (int m = 0; m < 12; ++m)
    {
        REQUIRE(month_counts[m] > 50);
    }
    gen.unseed();
}

// ---------------------------------------------------------------------------
// Random country with seed is deterministic
// ---------------------------------------------------------------------------

TEST_CASE("get_birth(seed) selects same country deterministically", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(std::uint64_t{9999});
    auto b2 = gen.get_birth(std::uint64_t{9999});

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
    gen.seed(0);

    for (int i = 0; i < 100; ++i)
    {
        auto b = gen.get_birth("US");
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
    gen.unseed();
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
    gen.seed(0);

    double sum_male_le = 0;
    double sum_female_le = 0;
    int males = 0;
    int females = 0;

    for (int i = 0; i < 1000; ++i)
    {
        auto b = gen.get_birth("JP");
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
    gen.unseed();
}

// ===========================================================================
// Sex-specific generation
// ===========================================================================

TEST_CASE("get_birth(cca2, sex::male) always returns male", "[generation][sex]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 200; ++i)
    {
        auto b = gen.get_birth("US", dasmig::sex::male);
        REQUIRE(b.bio_sex == dasmig::sex::male);
        REQUIRE(b.country_code == "US");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
    }
    gen.unseed();
}

TEST_CASE("get_birth(cca2, sex::female) always returns female", "[generation][sex]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 200; ++i)
    {
        auto b = gen.get_birth("JP", dasmig::sex::female);
        REQUIRE(b.bio_sex == dasmig::sex::female);
        REQUIRE(b.country_code == "JP");
    }
    gen.unseed();
}

TEST_CASE("get_birth(cca2, sex, seed) is deterministic", "[generation][sex][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("BR", dasmig::sex::female, std::uint64_t{777});
    auto b2 = gen.get_birth("BR", dasmig::sex::female, std::uint64_t{777});

    REQUIRE(b1.bio_sex == dasmig::sex::female);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.age == b2.age);
}

TEST_CASE("get_birth(sex) random country with fixed sex", "[generation][sex]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 100; ++i)
    {
        auto b = gen.get_birth(dasmig::sex::male);
        REQUIRE(b.bio_sex == dasmig::sex::male);
        REQUIRE_FALSE(b.country_code.empty());
    }
    gen.unseed();
}

TEST_CASE("get_birth(sex, seed) is deterministic", "[generation][sex][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth(dasmig::sex::female, std::uint64_t{555});
    auto b2 = gen.get_birth(dasmig::sex::female, std::uint64_t{555});

    REQUIRE(b1.bio_sex == dasmig::sex::female);
    REQUIRE(b1.country_code == b2.country_code);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

// ===========================================================================
// Year-specific generation
// ===========================================================================

TEST_CASE("get_birth(cca2, year) fixes the birth year", "[generation][year]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 200; ++i)
    {
        auto b = gen.get_birth("US", dasmig::year_t{1990});
        REQUIRE(b.year == 1990);
        REQUIRE(b.country_code == "US");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
        REQUIRE(b.day >= 1);
        REQUIRE(b.day <= 31);
    }
    gen.unseed();
}

TEST_CASE("get_birth(cca2, year) derives correct age", "[generation][year]")
{
    auto gen = make_loaded_generator();
    auto b = gen.get_birth("DE", dasmig::year_t{2000});

    auto now_ymd = std::chrono::year_month_day{
        std::chrono::floor<std::chrono::days>(
            std::chrono::system_clock::now())};
    int expected_age = static_cast<int>(now_ymd.year()) - 2000;
    // Age should be ref_year - year, capped at 100.
    REQUIRE(b.age == static_cast<std::uint8_t>(expected_age));
    REQUIRE(b.year == 2000);
}

TEST_CASE("get_birth(cca2, year, seed) is deterministic", "[generation][year][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("JP", dasmig::year_t{1985}, std::uint64_t{333});
    auto b2 = gen.get_birth("JP", dasmig::year_t{1985}, std::uint64_t{333});

    REQUIRE(b1.year == 1985);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
    REQUIRE(b1.bio_sex == b2.bio_sex);
}

TEST_CASE("get_birth dates are valid for fixed year", "[generation][year][date]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 300; ++i)
    {
        auto b = gen.get_birth("BR", dasmig::year_t{2000});
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
    gen.unseed();
}

// ===========================================================================
// Sex + year generation
// ===========================================================================

TEST_CASE("get_birth(cca2, sex, year) fixes both", "[generation][sex][year]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 200; ++i)
    {
        auto b = gen.get_birth("DE", dasmig::sex::female,
                               dasmig::year_t{1985});
        REQUIRE(b.bio_sex == dasmig::sex::female);
        REQUIRE(b.year == 1985);
        REQUIRE(b.country_code == "DE");
        REQUIRE(b.month >= 1);
        REQUIRE(b.month <= 12);
    }
    gen.unseed();
}

TEST_CASE("get_birth(cca2, sex, year, seed) is deterministic", "[generation][sex][year][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("IN", dasmig::sex::male,
                            dasmig::year_t{1970}, std::uint64_t{111});
    auto b2 = gen.get_birth("IN", dasmig::sex::male,
                            dasmig::year_t{1970}, std::uint64_t{111});

    REQUIRE(b1.bio_sex == dasmig::sex::male);
    REQUIRE(b1.year == 1970);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}

TEST_CASE("get_birth(cca2, sex, year) cohort matches year", "[generation][sex][year][cohort]")
{
    auto gen = make_loaded_generator();

    auto b1 = gen.get_birth("US", dasmig::sex::male, dasmig::year_t{1950});
    REQUIRE(b1.cohort == "Baby Boomer");

    auto b2 = gen.get_birth("US", dasmig::sex::female, dasmig::year_t{2005});
    REQUIRE(b2.cohort == "Generation Z");

    auto b3 = gen.get_birth("US", dasmig::sex::male, dasmig::year_t{2020});
    REQUIRE(b3.cohort == "Generation Alpha");
}

// ===========================================================================
// Age-range generation
// ===========================================================================

TEST_CASE("get_birth(cca2, age_range) bounds the age", "[generation][age_range]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 500; ++i)
    {
        auto b = gen.get_birth("US", dasmig::age_range{18, 65});
        REQUIRE(b.age >= 18);
        REQUIRE(b.age <= 65);
        REQUIRE(b.country_code == "US");
    }
    gen.unseed();
}

TEST_CASE("get_birth age range narrow band", "[generation][age_range]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 100; ++i)
    {
        auto b = gen.get_birth("JP", dasmig::age_range{25, 30});
        REQUIRE(b.age >= 25);
        REQUIRE(b.age <= 30);
    }
    gen.unseed();
}

TEST_CASE("get_birth(cca2, age_range, seed) is deterministic", "[generation][age_range][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth("BR", dasmig::age_range{20, 40},
                            std::uint64_t{888});
    auto b2 = gen.get_birth("BR", dasmig::age_range{20, 40},
                            std::uint64_t{888});

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
    gen.seed(0);

    for (int i = 0; i < 300; ++i)
    {
        auto b = gen.get_birth("US", dasmig::age_range{0, 100});
        auto ymd = std::chrono::year{static_cast<int>(b.year)} /
                   std::chrono::month{b.month} /
                   std::chrono::day{b.day};
        REQUIRE(ymd.ok());
    }
    gen.unseed();
}

TEST_CASE("get_birth age range throws when min > max", "[generation][age_range]")
{
    auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(
        gen.get_birth("US", dasmig::age_range{50, 20}),
        std::invalid_argument);
}

TEST_CASE("get_birth age range single age", "[generation][age_range]")
{
    auto gen = make_loaded_generator();
    gen.seed(0);

    for (int i = 0; i < 50; ++i)
    {
        auto b = gen.get_birth("US", dasmig::age_range{30, 30});
        REQUIRE(b.age == 30);
    }
    gen.unseed();
}

// ===========================================================================
// Seed round-trip for random-country overload
// ===========================================================================

TEST_CASE("get_birth() seed round-trip", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    auto b1 = gen.get_birth();
    auto b2 = gen.get_birth(b1.seed());

    REQUIRE(b1.country_code == b2.country_code);
    REQUIRE(b1.year == b2.year);
    REQUIRE(b1.month == b2.month);
    REQUIRE(b1.day == b2.day);
}
