# Birth Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Birth Generator for C++](https://raw.githubusercontent.com/dasmig/birth-generator/master/doc/birth-generator.png)](https://github.com/dasmig/birth-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/birth-generator/master/LICENSE.MIT)
[![CI](https://github.com/dasmig/birth-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/birth-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/birth-generator.svg)](https://github.com/dasmig/birth-generator/releases)
[![GitHub Issues](https://img.shields.io/github/issues/dasmig/birth-generator.svg)](https://github.com/dasmig/birth-generator/issues)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/birth-generator#integration)
[![Platform](https://img.shields.io/badge/platform-linux%20|%20windows%20|%20macos-lightgrey.svg)](https://github.com/dasmig/birth-generator)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/birth-generator/)

**[API Reference](https://dasmig.github.io/birth-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/birth-generator/releases)**

## Features

- **Demographically Plausible Birthdays**. Generates random birthdays using a multi-axis pipeline: country-specific age pyramids, monthly birth seasonality, and weekday deficit from C-section rates.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Population-Weighted Country Selection**. Countries are selected with probability proportional to their population, mirroring real-world demographics — a birth from India or China is far more likely than one from Iceland.

- **Rich Birth Data**. Every generated birth includes: ISO date (YYYY-MM-DD), year, month, day, age, biological sex, weekday, life expectancy remaining, generational cohort label, and country code.

- **UN WPP 2024 Data**. Age pyramids and life expectancy sourced from the United Nations World Population Prospects 2024 revision via the [PPgp/wpp2024](https://github.com/PPgp/wpp2024) R package.

- **Monthly Seasonality**. Latitude-based sinusoidal model — Northern Hemisphere peaks in September, Southern Hemisphere peaks in March.

- **Weekday Deficit**. Weekend births are less probable in countries with high C-section / scheduled delivery rates, reflecting real-world hospital scheduling patterns.

- **Deterministic Seeding**. Per-call `get_birth(request, seed)` for reproducible results, and `birth::seed()` for replaying a previous generation with the same request. Unseeded requests use ordinary random generation.

- **Uniform Selection**. Use `birth_request{.weighted = false}` for equal-probability country selection.

- **Per-Call Requests**. Use `birth_request` with `get_birth(request)` or `get_birth(request, seed)` to combine country, sex, year or age filters with per-call weighting and reference date. All generation choices live on the request.

- **Identical Everywhere**. A seed gives the same birth on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests. Ages are relative to a fixed reference date, not the clock.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove countries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_BIRTH_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `bthg` instances with their own data and random engine.

## Integration

[`birthgen.hpp`](https://github.com/dasmig/birth-generator/blob/master/dasmig/birthgen.hpp) is the library [released here](https://github.com/dasmig/birth-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/birth-generator/blob/master/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/birth-generator/tree/master/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/birthgen.hpp>

// For convenience.
using bthg = dasmig::bthg;
```

to the files you want to generate births and set the necessary switches to enable C++23 (e.g., `-std=c++23` for GCC and Clang).

Additionally you must supply the birth generator with the [`resources`](https://github.com/dasmig/birth-generator/tree/master/resources) folder containing `full/` and/or `lite/` subdirectories with the three TSV data files, also available in the [release](https://github.com/dasmig/birth-generator/releases).

## Usage

```cpp
#include <dasmig/birthgen.hpp>
#include <iostream>

// For convenience.
using bthg = dasmig::bthg;

// Manually load a specific dataset tier if necessary.
bthg::instance().load(dasmig::dataset::lite);  // ~195 sovereign states
// OR
bthg::instance().load(dasmig::dataset::full);  // ~235 countries & territories

// Generate a random birth (population-weighted country selection).
auto b = bthg::instance().get_birth(dasmig::birth_request{});
std::cout << b.date_string() << " — "
          << b.country_code << ", age " << +b.age
          << ", " << b.cohort << '\n';

// Generate a birth from a specific country.
auto us = bthg::instance().get_birth(dasmig::birth_request{.country_code = "US"});
std::cout << "US: " << us << '\n';           // implicit string conversion

// Request a specific sex.
auto m = bthg::instance().get_birth(dasmig::birth_request{
    .country_code = "BR", .bio_sex = dasmig::sex::male});

// Request a specific birth year.
auto y = bthg::instance().get_birth(dasmig::birth_request{
    .country_code = "JP", .year = dasmig::year_t{1990}});

// Request both sex and year.
auto sy = bthg::instance().get_birth(dasmig::birth_request{
    .country_code = "DE", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1985}});

// Request an age range (e.g. adults 18–65).
auto ar = bthg::instance().get_birth(dasmig::birth_request{
    .country_code = "US", .ages = dasmig::age_range{18, 65}});

// Access all available fields.
std::cout << "Sex:      " << (b.bio_sex == dasmig::sex::male ? "M" : "F") << '\n';
std::cout << "Weekday:  " << +b.weekday << " (0=Sun..6=Sat)" << '\n';
std::cout << "LE left:  " << b.le_remaining << " years" << '\n';

// Deterministic generation — same request, seed and data produce the same birth.
auto seeded = bthg::instance().get_birth(dasmig::birth_request{}, 42);

// Per-call filters and settings.
dasmig::birth_request request{
    .country_code = "US",
    .bio_sex = dasmig::sex::female,
    .ages = dasmig::age_range{18, 65},
    .weighted = false,
    .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
auto requested = bthg::instance().get_birth(request, 42);
auto requested_replay = bthg::instance().get_birth(request, requested.seed());

// Replay a previous birth using its seed.
auto replay = bthg::instance().get_birth(dasmig::birth_request{}, seeded.seed());

// Deterministic sequences use an explicit seed for every call.
for (std::uint64_t seed = 100; seed < 103; ++seed)
    std::cout << bthg::instance().get_birth(dasmig::birth_request{}, seed) << '\n';

// Switch to uniform random selection (equal probability per country).
auto uniform = bthg::instance().get_birth(dasmig::birth_request{.weighted = false});

// Independent instance — separate data and random engine.
bthg my_gen;
my_gen.load("path/to/resources/lite");
auto c = my_gen.get_birth(dasmig::birth_request{});
```

`birth_request{}` uses population-weighted selection and `birth_request::default_reference_date` (2023-07-01). Omit `country_code` for a random country; set `year = dasmig::year_t{1990}` for a fixed year instead of `ages`. Specifying both `year` and `ages` throws `std::invalid_argument`. `get_birth(request)` draws a seed from the generator's random engine; `get_birth(request, seed)` does not advance it. Replay requires the same request, seed and loaded data.

The positional/no-argument `get_birth` overloads and generator-wide `weighted()` / `reference_date()` getter/setters and `seed()` / `unseed()` controls have been removed. Pass an explicit request on every call, including `birth_request{}` for defaults; move weighting and reference-date choices onto that request, and pass deterministic seeds as the second argument. Result fields, seeded output and data APIs are unchanged. See [Migrating to request-first generation](doc/usage.md#migrating-to-request-first-generation).

For the complete feature guide — fields, seeding, weighting, and more — see the **[Usage Guide](doc/usage.md)**.

## Build and Test

The library is header-only; the Makefile builds the example and test executables. Use GNU Make, a POSIX-compatible shell (on Windows, for example, WSL), and a C++23 compiler and standard library. The default compiler is `clang++`; CI uses Clang 18 with libstdc++ 14.

Run these commands from the repository root, with the shipped `resources/` folder in place:

```bash
make all        # build build/example
./build/example # run the example
make test       # build and run the unit tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # build and run the tests with the lite tier compiled in
./build/test_runner "[request],[kat]" # targeted API/replay checks after make test
```

The test framework is included in `tests/`; no separate installation is needed. `make test-embed` defines `DASMIG_BIRTH_EMBED` and uses the checked-in `dasmig/birthgen_data.hpp`.

To select another compiler, override `CXX`, for example:

```bash
make all test test-embed CXX=clang++-18
```

CI also runs `make coverage` (requires `llvm-profdata` and `llvm-cov`), `make lint` (requires `clang-tidy`), and `make docs` (requires Doxygen).

## Generation Pipeline

Each call to `get_birth()` runs this pipeline:

1. **Country** — select from loaded countries (population-weighted or uniform).
2. **Sex** — male or female, weighted by the country's M:F population ratio (or fixed if specified).
3. **Age** — drawn from the country-specific age pyramid, optionally restricted to a range (or derived from a fixed year).
4. **Birth year** — the year that makes the age exact on the reference date (default 2023-07-01, the date of the UN estimates).
5. **Month** — drawn from latitude-based seasonal weights.
6. **Day** — uniform within the month, with a weekend deficit (weekend days redrawn with probability proportional to the C-section rate).
7. **Weekday** — computed from the final date using `std::chrono`.
8. **Life expectancy remaining** — `max(0, LE_at_birth − age)`.
9. **Cohort label** — Greatest Generation through Generation Alpha.

## Data

The birth data is sourced from:

| Source | License | Contribution |
|--------|---------|--------------|
| [PPgp/wpp2024](https://github.com/PPgp/wpp2024) (UN WPP 2024) | CC BY 3.0 IGO | Age pyramids, life expectancy at birth |
| [REST Countries v3.1](https://gitlab.com/restcountries/restcountries) | Open Source | ISO codes, latitude, independence status |
| WHO Global Health Observatory | Reference | C-section rates by country |

See `LICENSE_DATA.txt` for details.

To regenerate datasets:

```bash
python scripts/prepare_births.py             # generate both tiers
python scripts/prepare_births.py --tier lite # lite only
python scripts/prepare_births.py --tier full # full only
```

## Related Libraries

| Library | Description |
|---------|-------------|
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2020-2026 Diego Dasso Migotto
```
