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

**[API Reference](https://dasmig.github.io/birth-generator/)** · **[Usage Guide](doc/usage.md)** · **[Releases](https://github.com/dasmig/birth-generator/releases)**

## Features

- **Demographically Plausible Birthdays**. Generates random birthdays using a multi-axis pipeline: country-specific age pyramids, monthly birth seasonality, and weekday deficit from C-section rates.

- **Population-Weighted Country Selection**. Countries are selected with probability proportional to their population, mirroring real-world demographics — a birth from India or China is far more likely than one from Iceland.

- **Rich Birth Data**. Every generated birth includes: ISO date (YYYY-MM-DD), year, month, day, age, biological sex, weekday, life expectancy remaining, generational cohort label, and country code.

- **UN WPP 2024 Data**. Age pyramids and life expectancy sourced from the United Nations World Population Prospects 2024 revision via the [PPgp/wpp2024](https://github.com/PPgp/wpp2024) R package.

- **Monthly Seasonality**. Latitude-based sinusoidal model — Northern Hemisphere peaks in September, Southern Hemisphere peaks in March.

- **Weekday Deficit**. Weekend births are less probable in countries with high C-section / scheduled delivery rates, reflecting real-world hospital scheduling patterns.

- **Deterministic Seeding**. Per-call `get_birth(seed)` for reproducible results, generator-level `seed()` / `unseed()` for deterministic sequences, and `birth::seed()` for replaying a previous generation.

- **Uniform Selection**. Switch to equal-probability country selection with `weighted(false)`.

- **Multi-Instance Support**. Construct independent `bthg` instances with their own data and random engine.

## Integration

[`birthgen.hpp`](https://github.com/dasmig/birth-generator/blob/master/dasmig/birthgen.hpp) is the single required file [released here](https://github.com/dasmig/birth-generator/releases). You also need [`random.hpp`](https://github.com/dasmig/birth-generator/blob/master/dasmig/random.hpp) in the same directory. Add

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
auto b = bthg::instance().get_birth();
std::cout << b.date_string() << " — "
          << b.country_code << ", age " << +b.age
          << ", " << b.cohort << '\n';

// Generate a birth from a specific country.
auto us = bthg::instance().get_birth("US");
std::cout << "US: " << us << '\n';           // implicit string conversion

// Access all available fields.
std::cout << "Sex:      " << (b.bio_sex == dasmig::sex::male ? "M" : "F") << '\n';
std::cout << "Weekday:  " << +b.weekday << " (0=Sun..6=Sat)" << '\n';
std::cout << "LE left:  " << b.le_remaining << " years" << '\n';

// Deterministic generation — same seed always produces the same birth.
auto seeded = bthg::instance().get_birth(42);

// Replay a previous birth using its seed.
auto replay = bthg::instance().get_birth(seeded.seed());

// Seed the engine for a deterministic sequence.
bthg::instance().seed(100);
// ... generate births ...
bthg::instance().unseed(); // restore non-deterministic state

// Switch to uniform random selection (equal probability per country).
bthg::instance().weighted(false);
auto uniform = bthg::instance().get_birth();
bthg::instance().weighted(true);

// Independent instance — separate data and random engine.
bthg my_gen;
my_gen.load("path/to/resources/lite");
auto c = my_gen.get_birth();
```

For the complete feature guide — fields, seeding, weighting, and more — see the **[Usage Guide](doc/usage.md)**.

## Generation Pipeline

Each call to `get_birth()` runs this pipeline:

1. **Country** — select from loaded countries (population-weighted or uniform).
2. **Sex** — male or female, weighted by the country's M:F population ratio.
3. **Age** — drawn from the country-specific age pyramid (discrete distribution over 0–100).
4. **Birth year** — `reference_year − age`.
5. **Month** — drawn from latitude-based seasonal weights.
6. **Day** — uniform within the month, then rejection-sampled for weekday deficit (weekend births rejected with probability proportional to C-section rate).
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
