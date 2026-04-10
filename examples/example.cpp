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
    auto b1 = gen.get_birth("US", 42);
    auto b2 = gen.get_birth("US", 42);
    std::cout << "  seed 42 → " << b1.date_string() << "\n";
    std::cout << "  seed 42 → " << b2.date_string()
              << (b1.date_string() == b2.date_string() ? "  ✓ match" : "  ✗ mismatch")
              << "\n";

    return 0;
}
