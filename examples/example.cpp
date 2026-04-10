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
        auto b = gen.get_birth(code);
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
        auto b = gen.get_birth();
        std::cout << "  " << b.country_code << " " << b.date_string()
                  << "  age=" << static_cast<int>(b.age)
                  << "  sex=" << (b.bio_sex == dasmig::sex::male ? "M" : "F")
                  << "\n";
    }

    // Deterministic replay.
    std::cout << "\nDeterministic replay:\n";
    auto b1 = gen.get_birth("US", std::uint64_t{42});
    auto b2 = gen.get_birth("US", std::uint64_t{42});
    std::cout << "  seed 42 → " << b1.date_string() << "\n";
    std::cout << "  seed 42 → " << b2.date_string()
              << (b1.date_string() == b2.date_string() ? "  ✓ match" : "  ✗ mismatch")
              << "\n";

    // Sex-specific generation.
    std::cout << "\nSex-specific:\n";
    for (int i = 0; i < 3; ++i)
    {
        auto m = gen.get_birth("US", dasmig::sex::male);
        std::cout << "  Male:   " << m.date_string()
                  << "  age=" << static_cast<int>(m.age) << "\n";
    }
    for (int i = 0; i < 3; ++i)
    {
        auto f = gen.get_birth("US", dasmig::sex::female);
        std::cout << "  Female: " << f.date_string()
                  << "  age=" << static_cast<int>(f.age) << "\n";
    }

    // Year-specific generation.
    std::cout << "\nYear-specific (1990):\n";
    for (int i = 0; i < 3; ++i)
    {
        auto y = gen.get_birth("JP", dasmig::year_t{1990});
        std::cout << "  " << y.date_string()
                  << "  sex=" << (y.bio_sex == dasmig::sex::male ? "M" : "F")
                  << "  cohort=" << y.cohort << "\n";
    }

    // Sex + year generation.
    std::cout << "\nSex + year (female, 1985):\n";
    auto sy = gen.get_birth("DE", dasmig::sex::female, dasmig::year_t{1985});
    std::cout << "  " << sy.date_string()
              << "  sex=" << (sy.bio_sex == dasmig::sex::male ? "M" : "F")
              << "  age=" << static_cast<int>(sy.age) << "\n";

    // Age-range generation.
    std::cout << "\nAge-range (adults 18-65):\n";
    for (int i = 0; i < 5; ++i)
    {
        auto ar = gen.get_birth("US", dasmig::age_range{18, 65});
        std::cout << "  " << ar.date_string()
                  << "  age=" << static_cast<int>(ar.age) << "\n";
    }

    return 0;
}
