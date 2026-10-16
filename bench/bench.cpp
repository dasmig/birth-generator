// Benchmarks for birth-generator; see bench.hpp for the options.

#include "../dasmig/birthgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"birth-generator", argc, argv};

    run.load("load", [&] {
        dasmig::bthg gen;
        return gen.load(run.tier());
    });

    dasmig::bthg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_birth", [&](std::uint64_t seed) { return gen.get_birth({}, seed); });
    run.calls("get_birth BR female 20-40", [&](std::uint64_t seed) {
        return gen.get_birth({.country_code = "BR",
                              .bio_sex = dasmig::sex::female,
                              .ages = dasmig::age_range{20, 40}},
                             seed);
    });
    run.calls("get_birth to_json", [&](std::uint64_t seed) {
        return gen.get_birth({}, seed).to_json();
    });

    return run.finish();
}
