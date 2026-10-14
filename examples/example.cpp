#include "../dasmig/birthgen.hpp"
#include <iostream>
#include <string>

int main()
{
    dasmig::bthg gen;

    // Try auto-probe, then explicit tiers, then raw paths.
    if (!gen.load(dasmig::dataset::lite) &&
        !gen.load(dasmig::dataset::full))
    {
        gen.load(std::filesystem::path{"resources/lite"});
    }

    if (!gen.has_data())
    {
        std::cerr << "Could not load birth data.\n";
        return 1;
    }

    std::cout << "Loaded " << gen.country_count() << " countries.\n\n";

    // Generate a few random births for specific countries.
    for (const auto* code : {"US", "BR", "JP", "NG", "DE"})
    {
        auto b = gen.get_birth(dasmig::birth_request{.country_code = code});
        std::cout << code << ": " << b.date_string()
                  << "  age=" << static_cast<int>(b.age)
                  << "  sex=" << (b.bio_sex == dasmig::sex::male ? "M" : "F")
                  << "  weekday=" << static_cast<int>(b.weekday)
                  << "  LE_rem=" << b.le_remaining
                  << "  cohort=" << b.cohort << "\n";
    }

    // Generate from a random country.
    std::cout << "\nRandom:\n";
    for (int i = 0; i < 5; ++i)
    {
        auto b = gen.get_birth(dasmig::birth_request{});
        std::cout << "  " << b.country_code << " " << b.date_string()
                  << "  age=" << static_cast<int>(b.age)
                  << "  sex=" << (b.bio_sex == dasmig::sex::male ? "M" : "F")
                  << "\n";
    }

    // Deterministic replay.
    std::cout << "\nDeterministic replay:\n";
    auto b1 = gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{42});
    auto b2 = gen.get_birth(dasmig::birth_request{.country_code = "US"}, std::uint64_t{42});
    std::cout << "  seed 42 → " << b1.date_string() << "\n";
    std::cout << "  seed 42 → " << b2.date_string()
              << (b1.date_string() == b2.date_string() ? "  ✓ match" : "  ✗ mismatch")
              << "\n";

    // All generation choices are local to the request.
    const dasmig::birth_request request{
        .country_code = "US", .bio_sex = dasmig::sex::female,
        .ages = dasmig::age_range{.min = 18, .max = 65}, .weighted = false,
        .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1},
    };
    const auto requested = gen.get_birth(request, std::uint64_t{42});
    const auto replay = gen.get_birth(request, requested.seed());
    std::cout << "\nPer-call request (female, adult, ages as of 2040-01-01):\n  "
              << requested.to_json() << "\n  Replay match: "
              << (requested.to_json() == replay.to_json() ? "yes" : "no") << "\n";

    // Sex-specific generation.
    std::cout << "\nSex-specific:\n";
    for (int i = 0; i < 3; ++i)
    {
        auto m = gen.get_birth(dasmig::birth_request{
            .country_code = "US", .bio_sex = dasmig::sex::male});
        std::cout << "  Male:   " << m.date_string()
                  << "  age=" << static_cast<int>(m.age) << "\n";
    }
    for (int i = 0; i < 3; ++i)
    {
        auto f = gen.get_birth(dasmig::birth_request{
            .country_code = "US", .bio_sex = dasmig::sex::female});
        std::cout << "  Female: " << f.date_string()
                  << "  age=" << static_cast<int>(f.age) << "\n";
    }

    // Year-specific generation.
    std::cout << "\nYear-specific (1990):\n";
    for (int i = 0; i < 3; ++i)
    {
        auto y = gen.get_birth(dasmig::birth_request{
            .country_code = "JP", .year = dasmig::year_t{1990}});
        std::cout << "  " << y.date_string()
                  << "  sex=" << (y.bio_sex == dasmig::sex::male ? "M" : "F")
                  << "  cohort=" << y.cohort << "\n";
    }

    // Sex + year generation.
    std::cout << "\nSex + year (female, 1985):\n";
    auto sy = gen.get_birth(dasmig::birth_request{
        .country_code = "DE", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1985}});
    std::cout << "  " << sy.date_string()
              << "  sex=" << (sy.bio_sex == dasmig::sex::male ? "M" : "F")
              << "  age=" << static_cast<int>(sy.age) << "\n";

    // Age-range generation.
    std::cout << "\nAge-range (adults 18-65):\n";
    for (int i = 0; i < 5; ++i)
    {
        auto ar = gen.get_birth(dasmig::birth_request{
            .country_code = "US", .ages = dasmig::age_range{.min = 18, .max = 65}});
        std::cout << "  " << ar.date_string()
                  << "  age=" << static_cast<int>(ar.age) << "\n";
    }

    // Ages are relative to a fixed reference date, not the clock.
    std::cout << "\nReference date: "
              << static_cast<int>(dasmig::birth_request::default_reference_date.year()) << '-'
              << static_cast<unsigned>(dasmig::birth_request::default_reference_date.month()) << '-'
              << static_cast<unsigned>(dasmig::birth_request::default_reference_date.day()) << "\n";

    std::cout << "\nUniform country selection:\n  "
              << gen.get_birth(dasmig::birth_request{.weighted = false}).to_json() << "\n";

    // JSON with the replay signature (seed and data hash).
    std::cout << "\nJSON:\n  "
              << gen.get_birth(dasmig::birth_request{.country_code = "BR"}, std::uint64_t{7}).to_json() << "\n";

    // Extend the data: a country whose people are all 30, born in May.
    std::string pyramid = "cca2";
    std::string pyramid_row = "QQ";
    for (const char s : {'m', 'f'})
    {
        for (int a = 0; a <= 100; ++a)
        {
            pyramid += "\t" + std::string(1, s) + std::to_string(a);
            pyramid_row += a == 30 ? "\t100" : "\t0";
        }
    }
    gen.extend_from_string(dasmig::bthg::table::countries,
                           "cca2\tname\tle_male\tle_female\tcsection_rate\n"
                           "QQ\tAtlantis\t81\t85\t0.2\n",
                           "atlantis countries");
    gen.extend_from_string(dasmig::bthg::table::age_pyramid, pyramid + "\n" + pyramid_row + "\n",
                           "atlantis pyramid");
    gen.extend_from_string(dasmig::bthg::table::monthly_births,
                           "cca2\tjan\tfeb\tmar\tapr\tmay\tjun\tjul\taug\tsep\toct\tnov\tdec\n"
                           "QQ\t0\t0\t0\t0\t1\t0\t0\t0\t0\t0\t0\t0\n",
                           "atlantis months");
    std::cout << "\nAtlantis: " << gen.get_birth(dasmig::birth_request{.country_code = "QQ"}).to_json() << "\n";

    return 0;
}
