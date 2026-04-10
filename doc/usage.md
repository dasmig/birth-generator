# Usage Guide

This guide covers every feature of the birth-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-births">Generating Births</a></li>
<li><a href="#birth-fields">Birth Fields</a></li>
<li><a href="#country-specific-generation">Country-Specific Generation</a></li>
<li><a href="#population-weighting">Population Weighting</a></li>
<li><a href="#uniform-selection">Uniform Selection</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#multi-instance-support">Multi-Instance Support</a></li>
<li><a href="#generation-pipeline">Generation Pipeline</a></li>
<li><a href="#data-pipeline">Data Pipeline</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/birthgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::bthg::instance();

    // Random birth (population-weighted country selection).
    auto b = gen.get_birth();
    std::cout << b.date_string() << " — "
              << b.country_code << ", age " << +b.age
              << ", " << b.cohort << "\n";

    // Country-specific birth.
    auto jp = gen.get_birth("JP");
    std::cout << "Japan: " << jp << " (age " << +jp.age << ")\n";
}
```

## Installation

1. Copy `dasmig/birthgen.hpp` and `dasmig/random.hpp` into your include path.
2. Copy the `resources/` folder (containing `full/` and/or `lite/` subdirectories with their three TSV files) so it is accessible at runtime.
3. Compile with C++23 enabled: `-std=c++23`.

## Loading Resources

The library ships two dataset tiers:

| Tier | Enum | Countries | Description |
|------|------|-----------|-------------|
| lite | `dasmig::dataset::lite` | ~195 | Sovereign states only |
| full | `dasmig::dataset::full` | ~235 | All countries and territories (UN WPP coverage) |

Each tier stores three TSV files:

| File | Content |
|------|---------|
| `countries.tsv` | ISO codes, name, region, latitude, life expectancy, C-section rate |
| `age_pyramid.tsv` | Population by single year of age (0–100) for male and female |
| `monthly_births.tsv` | Seasonal birth weights by month (latitude-derived) |

### Automatic loading (singleton)

On first access the singleton constructor probes these base paths:

| Priority | Base path |
|----------|-----------|
| 1 | `resources/` |
| 2 | `../resources/` |
| 3 | `birth-generator/resources/` |

It loads `lite/` if found, otherwise falls back to `full/`.

### Explicit tier loading

```cpp
dasmig::bthg gen;
gen.load(dasmig::dataset::lite);  // ~195 sovereign states
gen.load(dasmig::dataset::full);  // ~235 countries (can combine)
```

### Direct path loading

```cpp
dasmig::bthg gen;
gen.load("/data/births/lite");  // directory containing the 3 TSVs
```

## Generating Births

```cpp
auto& gen = dasmig::bthg::instance();

// Random birth (population-weighted country).
auto b = gen.get_birth();

// Country-specific birth.
auto us = gen.get_birth("US");

// Seeded (deterministic) birth.
auto det = gen.get_birth(42);
```

If the country code is not found (e.g., `"XX"`), `get_birth()` throws `std::runtime_error`.

## Birth Fields

Every `dasmig::birth` object exposes these fields:

| Field | Type | Description |
|-------|------|-------------|
| `country_code` | `std::string` | ISO 3166-1 alpha-2 code |
| `year` | `std::uint16_t` | Birth year |
| `month` | `std::uint8_t` | Birth month (1–12) |
| `day` | `std::uint8_t` | Birth day (1–31) |
| `age` | `std::uint8_t` | Age in completed years |
| `bio_sex` | `dasmig::sex` | `sex::male` or `sex::female` |
| `weekday` | `std::uint8_t` | Day of week (0=Sun, 1=Mon, …, 6=Sat) |
| `le_remaining` | `double` | Estimated years of life remaining |
| `cohort` | `std::string` | Generational cohort label |

### String conversion

```cpp
// Implicit conversion to std::string (ISO 8601 date).
std::string iso = b;

// Explicit method.
std::string date = b.date_string();  // "1987-09-15"

// Stream output.
std::cout << b;  // prints "1987-09-15"
```

### Generational cohorts

| Cohort | Birth years |
|--------|-------------|
| Greatest Generation | ≤ 1927 |
| Silent Generation | 1928–1945 |
| Baby Boomer | 1946–1964 |
| Generation X | 1965–1980 |
| Millennial | 1981–1996 |
| Generation Z | 1997–2012 |
| Generation Alpha | ≥ 2013 |

## Country-Specific Generation

Pass an ISO 3166-1 alpha-2 code to generate a birth from a specific country:

```cpp
auto brazil = gen.get_birth("BR");
auto japan  = gen.get_birth("JP");
auto nigeria = gen.get_birth("NG");
```

The age pyramid, life expectancy, seasonal model, and C-section rate are all country-specific.

## Population Weighting

By default, countries are selected proportional to their total population. This means births from China, India, and the United States are far more common than births from small nations.

```cpp
// Check current mode.
bool w = gen.weighted();  // true by default
```

## Uniform Selection

Switch to equal-probability country selection:

```cpp
gen.weighted(false);
auto b = gen.get_birth();  // any country equally likely
gen.weighted(true);         // restore population-weighted
```

## Seeding and Deterministic Generation

### Per-call seeding

```cpp
auto b = gen.get_birth(42);       // deterministic
auto b2 = gen.get_birth(42);      // identical to b
```

### Replay via birth seed

Every birth stores the random seed used to generate it:

```cpp
auto b = gen.get_birth();          // random
auto replay = gen.get_birth(b.seed());  // exact same birth
```

### Generator-level seeding

```cpp
gen.seed(100);
auto a = gen.get_birth();
auto b = gen.get_birth();

gen.seed(100);
auto a2 = gen.get_birth();  // same as a
auto b2 = gen.get_birth();  // same as b

gen.unseed();  // restore non-deterministic state
```

## Multi-Instance Support

Construct independent instances for isolation:

```cpp
dasmig::bthg gen1;
gen1.load(dasmig::dataset::lite);

dasmig::bthg gen2;
gen2.load("path/to/custom/data");

// gen1 and gen2 have separate data and random engines.
```

Useful when embedding inside other generators or when different threads need independent generators.

## Generation Pipeline

Each `get_birth()` call runs the following pipeline:

1. **Country selection** — population-weighted or uniform from loaded entries.
2. **Biological sex** — Bernoulli trial weighted by the country's male:female population ratio.
3. **Age** — drawn from a discrete distribution over 0–100, shaped by the country's age pyramid.
4. **Birth year** — `reference_year − age` (reference year = 2024).
5. **Birth month** — drawn from 12 seasonal weights derived from the country's latitude (sinusoidal model: NH peaks in September, SH peaks in March).
6. **Birth day** — uniform within the month's valid range, then rejection-sampled for weekday deficit: if the candidate day falls on a weekend, it is rejected with probability `csection_rate × 0.5` (up to 3 retries).
7. **Weekday** — computed via `std::chrono::weekday` from the final date.
8. **Life expectancy remaining** — `max(0, LE_at_birth − age)` using sex-specific period life expectancy from UN WPP 2024.
9. **Cohort label** — mapped from birth year to generational label.

## Data Pipeline

The `scripts/prepare_births.py` script downloads and processes data from three sources:

1. **UN WPP 2024** (via [PPgp/wpp2024](https://github.com/PPgp/wpp2024) GitHub): population by age + sex, life expectancy by sex.
2. **REST Countries v3.1**: ISO codes, latitude, independence status, regions.
3. **WHO C-section rates**: hardcoded from WHO Global Health Observatory data.

It produces three TSV files per tier:

```
resources/
├── full/
│   ├── countries.tsv        # 250 rows
│   ├── age_pyramid.tsv      # 235 rows × 203 columns
│   └── monthly_births.tsv   # 250 rows × 13 columns
└── lite/
    ├── countries.tsv        # 195 rows
    ├── age_pyramid.tsv      # 193 rows × 203 columns
    └── monthly_births.tsv   # 195 rows × 13 columns
```

Regenerate with:

```bash
python scripts/prepare_births.py
```

Downloaded files are cached in `scripts/.cache/` to avoid repeated downloads.

## Thread Safety

The random engine inside each `bthg` instance is **not** thread-safe. If you call `get_birth()` from multiple threads, either:

- Use a separate `bthg` instance per thread, or
- Protect calls with a mutex.

The singleton `bthg::instance()` returns a single shared instance, so concurrent access must be synchronized.

## Error Reference

| Error | Cause |
|-------|-------|
| `std::runtime_error("bthg: no data loaded")` | Called `get_birth()` before any `load()` and auto-probe found nothing. |
| `std::runtime_error("bthg: country code not found: XX")` | The country code passed to `get_birth("XX")` is not in the loaded dataset. |
