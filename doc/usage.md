# Usage Guide

This guide covers every feature of the birth-generator library in detail. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-births">Generating Births</a>
  <ul>
  <li><a href="#per-call-requests">Per-Call Requests</a></li>
  <li><a href="#sex-specific-generation">Sex-Specific Generation</a></li>
  <li><a href="#year-specific-generation">Year-Specific Generation</a></li>
  <li><a href="#sex--year-generation">Sex + Year Generation</a></li>
  <li><a href="#age-range-generation">Age-Range Generation</a></li>
  </ul>
</li>
<li><a href="#birth-fields">Birth Fields</a></li>
<li><a href="#country-specific-generation">Country-Specific Generation</a></li>
<li><a href="#population-weighting">Population Weighting</a></li>
<li><a href="#uniform-selection">Uniform Selection</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#multi-instance-support">Multi-Instance Support</a></li>
<li><a href="#generation-pipeline">Generation Pipeline</a></li>
<li><a href="#data-pipeline">Data Pipeline</a></li>
<li><a href="#reference-date">Reference Date</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#migrating-to-request-first-generation">Migrating to Request-First Generation</a></li>
<li><a href="#migrating-from-v1">Migrating from v1</a></li>
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
    auto b = gen.get_birth(dasmig::birth_request{});
    std::cout << b.date_string() << " — "
              << b.country_code << ", age " << +b.age
              << ", " << b.cohort << "\n";

    // Country-specific birth.
    auto jp = gen.get_birth(dasmig::birth_request{.country_code = "JP"});
    std::cout << "Japan: " << jp << " (age " << +jp.age << ")\n";

    // Sex-specific birth.
    auto m = gen.get_birth(dasmig::birth_request{.country_code = "BR", .bio_sex = dasmig::sex::male});
    std::cout << "Male from Brazil: " << m << "\n";

    // Year-specific birth.
    auto y = gen.get_birth(dasmig::birth_request{.country_code = "DE", .year = dasmig::year_t{1990}});
    std::cout << "Born in 1990: " << y << "\n";

    // Age-range birth.
    auto adult = gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{18, 65}});
    std::cout << "Adult: " << adult << " (age " << +adult.age << ")\n";
}
```

## Installation

1. Copy `dasmig/birthgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them side by side.
2. Copy the `resources/` folder (containing `full/` and/or `lite/` subdirectories with their three TSV files) so it is accessible at runtime.
3. Compile with C++23 enabled: `-std=c++23`.

## Loading Resources

The library ships two dataset tiers:

| Tier | Enum | Countries | Description |
|------|------|-----------|-------------|
| lite | `dasmig::dataset::lite` | 193 | Sovereign states with UN age data |
| full | `dasmig::dataset::full` | 235 | All countries and territories with UN age data |

Each tier stores three TSV files:

| File | Content |
|------|---------|
| `countries.tsv` | ISO codes, name, region, latitude, life expectancy, C-section rate |
| `age_pyramid.tsv` | Population by single year of age (0–100) for male and female |
| `monthly_births.tsv` | Seasonal birth weights by month (latitude-derived) |

### Automatic loading (singleton)

On first access the singleton loads the `lite` tier (or `full` if `lite` is missing). It uses the compiled-in copy when built with `DASMIG_BIRTH_EMBED` (see Compiled-in Data), and otherwise looks for `lite/countries.tsv` in these directories, relative to the working directory:

| Priority | Base path |
|----------|-----------|
| 1 | `resources/` |
| 2 | `../resources/` |
| 3 | `birth-generator/resources/` |
| 4 | `../birth-generator/resources/` |

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
auto b = gen.get_birth(dasmig::birth_request{});

// Country-specific birth.
auto us = gen.get_birth(dasmig::birth_request{.country_code = "US"});

// Seeded (deterministic) birth.
auto det = gen.get_birth(dasmig::birth_request{}, std::uint64_t{42});
```

If the country code is not found (e.g., `"XX"`), `get_birth()` throws `std::invalid_argument`.

### Per-call requests

Every generation call takes a `birth_request`: use `get_birth(request)` for a random birth or `get_birth(request, seed)` for a deterministic birth. The request owns its country code and can be saved and reused:

```cpp
dasmig::birth_request request{
    .country_code = "US",
    .bio_sex = dasmig::sex::female,
    .ages = dasmig::age_range{18, 65},
    .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
auto adult = gen.get_birth(request, std::uint64_t{42});
auto replay = gen.get_birth(request, adult.seed()); // identical full result
auto random_adult = gen.get_birth(request);        // same filters, a fresh seed

// Random country, fixed sex and year.
dasmig::birth_request fixed_year{
    .bio_sex = dasmig::sex::male, .year = dasmig::year_t{1990}};
auto born_in_1990 = gen.get_birth(fixed_year, std::uint64_t{7});

// Uniform country selection for this call only.
auto uniform = gen.get_birth(dasmig::birth_request{.weighted = false}, std::uint64_t{42});
```

| Field | Type | Default / meaning |
|-------|------|-------------------|
| `country_code` | `std::optional<std::string>` | Omitted: random country; otherwise a loaded ISO alpha-2 code |
| `bio_sex` | `std::optional<dasmig::sex>` | Omitted: draw from the country's M:F ratio |
| `year` | `std::optional<dasmig::year_t>` | Omitted: derive birth year from the drawn age |
| `ages` | `std::optional<dasmig::age_range>` | Omitted: full age pyramid; otherwise inclusive range, clamped to 100 |
| `weighted` | `bool` | `true`: population-weighted; `false`: uniform (ignored when country is fixed) |
| `reference_date` | `std::chrono::year_month_day` | `birth_request::default_reference_date`, 2023-07-01 |

`year` and `ages` are mutually exclusive; specifying both throws `std::invalid_argument`. The existing range validation (`min > max`), age clamping, unknown-country errors, and invalid-reference-date errors also apply to requests. A fixed year's derived age is still clamped to [0, 100].

All generation choices live on the request; there are no generator-wide weighting or reference-date settings. `birth_request{}` always uses population weighting and the default reference date. The random overload draws a seed from the internal engine; the explicit-seed overload does not advance it.

The result shape, seeded output, seed signature and stream splits are unchanged. Replay needs the same request and loaded data as well as the seed: the signature records only seed and data hash, not request fields.

### Sex-specific generation

Fix the biological sex instead of drawing it from the M:F ratio:

```cpp
auto m = gen.get_birth(dasmig::birth_request{
    .country_code = "BR", .bio_sex = dasmig::sex::male});    // always male
auto f = gen.get_birth(dasmig::birth_request{
    .country_code = "US", .bio_sex = dasmig::sex::female}); // always female

// Random country, fixed sex.
auto r = gen.get_birth(dasmig::birth_request{.bio_sex = dasmig::sex::male});

// Deterministic variant.
auto d = gen.get_birth(dasmig::birth_request{
    .country_code = "JP", .bio_sex = dasmig::sex::female}, std::uint64_t{42});
```

### Year-specific generation

Fix the birth year instead of drawing from the age pyramid. The age is
the completed years at the reference date (see Reference Date), clamped to
[0, 100]:

```cpp
auto b = gen.get_birth(dasmig::birth_request{.country_code = "DE", .year = dasmig::year_t{1990}});

// Deterministic variant.
auto d = gen.get_birth(dasmig::birth_request{.country_code = "JP", .year = dasmig::year_t{1985}}, std::uint64_t{42});
```

### Sex + year generation

Fix both sex and year — only month, day, weekday, LE, and cohort are
randomised:

```cpp
auto b = gen.get_birth(dasmig::birth_request{
    .country_code = "US", .bio_sex = dasmig::sex::female, .year = dasmig::year_t{1985}});

// Deterministic variant.
auto d = gen.get_birth(dasmig::birth_request{
    .country_code = "IN", .bio_sex = dasmig::sex::male, .year = dasmig::year_t{1970}},
    std::uint64_t{42});
```

### Age-range generation

Constrain the age to an inclusive range. The age is drawn from the
country's age pyramid restricted to the range, so ages inside it keep
their relative frequencies:

```cpp
// Adults only.
auto adult = gen.get_birth(dasmig::birth_request{.country_code = "US", .ages = dasmig::age_range{18, 65}});

// Narrow band.
auto young = gen.get_birth(dasmig::birth_request{.country_code = "JP", .ages = dasmig::age_range{25, 30}});

// Deterministic variant.
auto d = gen.get_birth(dasmig::birth_request{.country_code = "BR", .ages = dasmig::age_range{20, 40}}, std::uint64_t{42});
```

Throws `std::invalid_argument` if `min > max`.

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

Set `birth_request::country_code` to an ISO 3166-1 alpha-2 code to generate a birth from a specific country:

```cpp
auto brazil = gen.get_birth(dasmig::birth_request{.country_code = "BR"});
auto japan  = gen.get_birth(dasmig::birth_request{.country_code = "JP"});
auto nigeria = gen.get_birth(dasmig::birth_request{.country_code = "NG"});
```

The age pyramid, life expectancy, seasonal model, and C-section rate are all country-specific.

## Population Weighting

By default, countries are selected proportional to their total population. This means births from China, India, and the United States are far more common than births from small nations.

```cpp
auto b = gen.get_birth(dasmig::birth_request{}); // weighted is true by default
```

## Uniform Selection

Switch to equal-probability country selection:

```cpp
auto b = gen.get_birth(dasmig::birth_request{.weighted = false}); // any country equally likely
auto weighted = gen.get_birth(dasmig::birth_request{});          // population-weighted again
```

The choice affects only that request; it is ignored when `country_code` is fixed. Add a seed as the second argument for deterministic selection.

## Seeding and Deterministic Generation

### Per-call seeding

```cpp
auto b = gen.get_birth(dasmig::birth_request{}, std::uint64_t{42});   // deterministic
auto b2 = gen.get_birth(dasmig::birth_request{}, std::uint64_t{42});  // identical to b
```

### Replay via birth seed

Every birth stores the random seed used to generate it:

```cpp
auto b = gen.get_birth(dasmig::birth_request{});            // random
auto replay = gen.get_birth(dasmig::birth_request{}, b.seed()); // exact same birth
```

### Deterministic sequences

```cpp
const dasmig::birth_request request{.country_code = "DE"};
auto a = gen.get_birth(request, std::uint64_t{100});
auto b = gen.get_birth(request, std::uint64_t{101});

auto a2 = gen.get_birth(request, std::uint64_t{100}); // same as a
auto b2 = gen.get_birth(request, std::uint64_t{101}); // same as b
```

Deterministic generation takes a seed explicitly on every call. There are no generator-level `seed()` / `unseed()` controls. `get_birth(request)` draws from an entropy-initialized internal random engine; its result still records the call seed for replay. `get_birth(request, seed)` neither uses nor advances that engine.

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
2. **Biological sex** — Bernoulli trial weighted by the country's male:female population ratio (or fixed by `birth_request::bio_sex`).
3. **Age** — drawn from the country's age pyramid over 0–100 (or the part of it inside an `age_range`), or derived from a fixed `year_t`.
4. **Birth year** — the year that makes the person exactly that age on the reference date (default 2023-07-01): a birthday after the reference date's month and day is one year earlier.
5. **Birth month** — drawn from 12 seasonal weights derived from the country's latitude (sinusoidal model: NH peaks in September, SH peaks in March).
6. **Birth day** — uniform within the month's valid range, then rejection-sampled for weekday deficit: if the candidate day falls on a weekend, it is rejected with probability `csection_rate × 0.5` (up to 3 retries).
7. **Weekday** — computed via `std::chrono::weekday` from the final date.
8. **Life expectancy remaining** — `max(0, LE_at_birth − age)` using sex-specific period life expectancy from UN WPP 2024.
9. **Cohort label** — mapped from birth year to generational label.

## Data Pipeline

The three files are built by `scripts/prepare_births.py` from UN World Population Prospects 2024, REST Countries and WHO caesarean rates. [Data and Method](data.md) describes the sources, every column, the generation model and its limitations.

## Reference Date

Ages are completed years at a fixed **reference date**, not at today's date, so the same request, seed and data give the same birth whenever it runs. The default is `birth_request::default_reference_date`, 1 July 2023, the date of the UN population estimates in the data.

```cpp
dasmig::birth_request request{
    .country_code = "BR",
    .reference_date = std::chrono::year{2040} / std::chrono::January / std::chrono::day{1}};
auto b = gen.get_birth(request); // ages as of 2040-01-01; the 2023 age structure is kept
```

Changing the request's reference date shifts age-drawn birth years (a fixed `year_t` stays fixed and its age changes). Other requests are unaffected. Replaying a seed requires the same request, including its reference date and weighting.

## Extending Data

The shipped data is a base layer in three tables (see Loading Resources). Files with the same columns can be loaded on top to add countries, change them or remove them, without editing the shipped files. Rows are keyed by `cca2`.

```cpp
auto& gen = dasmig::bthg::instance();

// A directory with any of countries.tsv, age_pyramid.tsv and
// monthly_births.tsv: all are applied together, or none if any is invalid.
gen.extend("mods/new_country");

// One table, from a file named after it or from text.
gen.extend("mods/age_pyramid.tsv");
gen.extend_from_string(dasmig::bthg::table::monthly_births, text, "scenario months");

// Remove a country: a layer needs only the key and a disabled column.
gen.extend_from_string(dasmig::bthg::table::countries, "cca2\tdisabled\nVA\t1\n", "cleanup");
```

- A country is generated when it has a `countries.tsv` row with `le_male` and `le_female`, and an `age_pyramid.tsv` row. Without a `monthly_births.tsv` row its months are uniform.
- Every row is checked before anything changes: numbers, a caesarean rate between 0 and 1, positive life expectancy, non-negative weights, and at least one non-zero age per sex and one non-zero month. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- `layers(table)` lists what is loaded in each table; `data_hash()` covers all three.
- `load(...)` replaces the data and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
const dasmig::birth_request request{.country_code = "JP"};
auto b = gen.get_birth(request);
b.seed();                  // the call seed
b.signature().data;        // gen.data_hash() when it was generated
auto again = gen.get_birth(request, b.seed()); // same birth, on every platform
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume:

```json
{"country_code":"US","date":"1999-05-19","year":1999,"month":5,"day":19,"age":24,"sex":"male",
 "weekday":3,"le_remaining":52.900000000000006,"cohort":"Generation Z",
 "signature":{"seed":"0000000000000001","data":"99276bbdb2b0e563"}}
```

Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`.

## Compiled-in Data

Define `DASMIG_BIRTH_EMBED` before including the header to compile the `lite` tier into the program (about 350 KB of source). `load(dataset::lite)` and `instance()` then use it and need no files, which is what a WebAssembly build wants. The compiled-in data has the same `data_hash()` as the files, so seeds replay the same either way.

```cpp
#define DASMIG_BIRTH_EMBED
#include <dasmig/birthgen.hpp>
```

`dasmig/birthgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Migrating to Request-First Generation

The public generation API now consists only of `get_birth(request)` and `get_birth(request, seed)`. The no-argument, seed-only and positional country/sex/year/age overloads have been removed, as have the generator-wide `weighted()` and `reference_date()` getter/setters and `seed()` / `unseed()` controls.

| Previous call or setting | Request-first replacement |
|--------------------------|---------------------------|
| `get_birth()` | `get_birth(dasmig::birth_request{})` |
| `get_birth(seed)` | `get_birth(dasmig::birth_request{}, seed)` |
| `get_birth("US", seed)` | `get_birth(dasmig::birth_request{.country_code = "US"}, seed)` |
| Country, sex, year or age arguments | Set `country_code`, `bio_sex`, `year` or `ages` on the request |
| `weighted(false)` | Set `request.weighted = false` |
| `reference_date(date)` | Set `request.reference_date = date` |
| Generator `seed(seed)` / `unseed()` | Pass a seed to each `get_birth(request, seed)`, or omit it for ordinary random generation |

For random generation, omit only the second (seed) argument, never the request. The request defaults, demographic distributions, result fields and seeded output are unchanged. Retain and reuse the full request when replaying a saved seed. Data loading, extension and inspection APIs are unchanged.

## Migrating from v1

Version 2 moves the library onto [dasmig-core](https://github.com/dasmig/dasmig-core).

- **Every seeded result changes.** The engine and the distributions are the core's own, so a seed gives the same birth on every compiler, standard library and CPU. Seeds saved with v1 do not reproduce their v1 results. The age, sex, month, weekday and country distributions are unchanged (checked against v1 over 20,000 draws per scenario).
- **No more clock.** v1 computed ages from the current year, so the same seed gave different results as years passed. v2 uses a fixed reference date (see Reference Date).
- **Ages are exact.** v1 set the birth year to the reference year minus the age even when the birthday fell after the reference date, so some ages were one year too high. v2 places the date so the age is exact; birth years average about half a year earlier.
- **Age ranges are exact.** v1 redrew up to 200 times and then clamped; v2 draws from the age pyramid restricted to the range.
- **Fixes:** a random country is chosen in data-file order (v1 used hash-map order, which differs between standard libraries); a country without monthly data gets uniform months (v1 gave every such birth January); a country without an age pyramid is not generated (v1 gave it age 0).
- **Sex, age, month and day draw from their own streams**, so fixing the sex, year or age range no longer shifts the other draws.
- **`load(path)`** still takes a directory and ignores a path that is not one; malformed files now throw `dasmig::core::tsv_error` with the file, line and column instead of skipping rows.
- **Files:** replace `random.hpp` with `core.hpp` and the `core/` directory (see Installation).
- **New:** the request's `reference_date`, `extend()`, `extend_from_string()`, `layers()`, `data_hash()`, `birth::signature()`, `birth::to_json()`, `DASMIG_BIRTH_EMBED`.

## Thread Safety

The random engine inside each `bthg` instance is **not** thread-safe. If you call `get_birth()` from multiple threads, either:

- Use a separate `bthg` instance per thread, or
- Protect calls with a mutex.

The singleton `bthg::instance()` returns a single shared instance, so concurrent access must be synchronized.

## Error Reference

| Error | Cause |
|-------|-------|
| `std::runtime_error("No birth data loaded. Call load() first.")` | Called `get_birth()` before any `load()` and auto-probe found nothing. |
| `std::invalid_argument("Unknown country code: XX")` | `birth_request::country_code` is not in the loaded dataset. |
| `std::invalid_argument("age_range: min must be <= max")` | Passed an `age_range` where `min > max`. |
| `std::invalid_argument("reference_date: not a valid date")` | Invalid date in `birth_request::reference_date`. |
| `std::invalid_argument("birth_request: year and ages are mutually exclusive")` | Specified both a fixed year and an age range in one request. |
| `std::invalid_argument` | `extend(file)` with a file not named after a table. |
| `dasmig::core::tsv_error` | A malformed data file or layer (message gives file, line and column); the data is unchanged. |
