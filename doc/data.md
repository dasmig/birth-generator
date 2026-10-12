# Data and Method

Where the data in `resources/` comes from, how it is built, and what the model leaves out. For using the library, see the [usage guide](usage.md).

[TOC]

## Tiers

| Tier | Countries in the files | Generated |
| --- | --- | --- |
| `lite` | 195 independent states | 193: Vatican City and Kosovo have no UN age data |
| `full` | 250 countries and territories | 235: also not the uninhabited or very small territories without UN data (Antarctica, Bouvet Island, Pitcairn, ...) |

A country is generated only when it has an age pyramid and life expectancy. The countries without them are still in `countries.tsv`, for completeness.

## Sources

| File | Columns | Source |
| --- | --- | --- |
| `countries.tsv` | `cca2`, `cca3`, `name`, `region`, `subregion`, `latitude`, `independent` | [REST Countries](https://restcountries.com/) v3.1 |
| | `le_male`, `le_female` | Life expectancy at birth, 2023: [UN World Population Prospects 2024](https://population.un.org/wpp/) (via the `PPgp/wpp2024` R package data) |
| | `csection_rate` | WHO Global Health Observatory, latest year, curated in the script for 107 countries; the others use a subregion or region average, or 0.15 |
| `age_pyramid.tsv` | `m0` … `m100`, `f0` … `f100` | Population by single year of age and sex, 1 July 2023 (thousands): UN WPP 2024. `100` is 100 and over |
| `monthly_births.tsv` | `jan` … `dec` | A model, not data: `1 + a × cos(2π (m − peak) / 12)`, peaking in September in the northern hemisphere and March in the southern, with an amplitude `a` of up to 8% growing with latitude |

`scripts/prepare_births.py` downloads the sources and writes both tiers:

```bash
python scripts/prepare_births.py              # both tiers
python scripts/prepare_births.py --tier lite  # one tier
```

After changing the data, run `make embed` to regenerate the compiled-in copy, and update the known-answer tests (the data hash and results change).

## Generation Model

1. **Sex**: female with probability equal to the female share of the country's population.
2. **Age**: drawn from the age pyramid for that sex (or from the part of it inside an age range).
3. **Month**: from the monthly weights.
4. **Day and year**: a day of the month, placed so that the person is exactly the drawn age on the reference date, 1 July 2023 by default, the date of the UN estimates. A birthday later in the year than the reference date puts the birth one year earlier.
5. **Weekend deficit**: a weekend birthday is redrawn with probability `csection_rate / 2`, at most three times, because scheduled caesareans and inductions fall on weekdays.
6. **Remaining life expectancy**: life expectancy at birth minus age, at least 0.
7. **Cohort**: from the birth year (Greatest Generation to 1927, Silent to 1945, Baby Boomer to 1964, Generation X to 1980, Millennial to 1996, Generation Z to 2012, Generation Alpha after).

Each step draws from its own stream split from the call seed, so fixing the sex, the year or an age range does not change the other steps' draws.

## Known Limitations

- Remaining life expectancy is life expectancy **at birth** minus age, which understates it for older people (who have already survived early-life risks). Life tables by age would fix this: UN WPP publishes them, and the Human Mortality Database has detailed ones for industrialised countries.
- Monthly seasonality is modelled from latitude, not measured; real patterns differ (for example the strong summer peak in the United States and the winter dip in Japan). Measured live births by month are in the UN Demographic Yearbook (UN Statistics Division's Demographic Statistics Database) and the Human Fertility Database.
- The weekend deficit uses the caesarean rate as a stand-in for all scheduled births, with a fixed factor of one half. National daily birth counts (for example the US CDC/NCHS natality files) could calibrate it.
- Ages above 100 are all 100.
- Every age in the pyramid is a 2023 snapshot: changing the reference date shifts birth years but keeps the 2023 age structure.
