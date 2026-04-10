#ifndef DASMIG_BIRTHGEN_HPP
#define DASMIG_BIRTHGEN_HPP

#include "random.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file birthgen.hpp
/// @brief Birth generator library — procedural birthday generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

namespace dasmig
{

/// @brief Dataset size tier for resource loading.
enum class dataset : std::uint8_t
{
    lite, ///< ~195 sovereign states only.
    full  ///< ~237 countries and territories (UN WPP coverage).
};

/// @brief Biological sex for birth generation.
enum class sex : std::uint8_t
{
    male,
    female
};

/// @brief Return type for birth generation, holding all data fields.
///
/// Supports implicit conversion to std::string (returns ISO date)
/// and streaming via operator<<.
class birth
{
  public:
    std::string country_code; ///< ISO 3166-1 alpha-2 code.
    std::uint16_t year{0};    ///< Birth year.
    std::uint8_t month{0};    ///< Birth month (1–12).
    std::uint8_t day{0};      ///< Birth day (1–31).
    std::uint8_t age{0};      ///< Age in completed years.
    sex bio_sex{sex::male};   ///< Biological sex.
    std::uint8_t weekday{0};  ///< Day of week (0=Sun, 1=Mon, …, 6=Sat).
    double le_remaining{0.0}; ///< Estimated years of life remaining.
    std::string cohort;       ///< Generational cohort label.

    /// @brief Retrieve the random seed used to generate this birth.
    [[nodiscard]] std::uint64_t seed() const { return _seed; }

    /// @brief ISO 8601 date string (YYYY-MM-DD).
    [[nodiscard]] std::string date_string() const
    {
        return std::format("{:04d}-{:02d}-{:02d}",
                           static_cast<int>(year),
                           static_cast<int>(month),
                           static_cast<int>(day));
    }

    /// @brief Implicit conversion to std::string.
    /// @return ISO 8601 date string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return date_string();
    }

    /// @brief Stream the ISO date to an output stream.
    friend std::ostream& operator<<(std::ostream& os, const birth& b)
    {
        os << b.date_string();
        return os;
    }

  private:
    std::uint64_t _seed{0};
    friend class bthg;
};

/// @brief Birth generator that produces demographically plausible
///        random birthdays using UN WPP 2024 population data.
///
/// The generation pipeline:
/// 1. Select biological sex (population M:F ratio).
/// 2. Select age from country-specific age pyramid.
/// 3. Compute birth year = current year − age.
/// 4. Select month from latitude-based seasonal weights.
/// 5. Select day within month (weekday-aware deficit).
///
/// Can be used as a singleton via instance() or constructed independently.
///
/// @par Thread safety
/// Each instance is independent.  Concurrent calls to get_birth() on
/// the **same** instance require external synchronisation.
class bthg
{
  public:
    /// @brief Default constructor — creates an empty generator with no data.
    bthg() : _ref_year(current_year_()) {}

    bthg(const bthg&) = delete;
    bthg& operator=(const bthg&) = delete;
    bthg(bthg&&) noexcept = default;
    bthg& operator=(bthg&&) noexcept = default;
    ~bthg() = default;

    /// @brief Access the global singleton instance.
    ///
    /// Auto-probes common resource paths on first access.
    static bthg& instance()
    {
        static bthg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random birth for a specific country.
    /// @param cca2 ISO 3166-1 alpha-2 country code (e.g. "US", "BR").
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the country code is unknown.
    [[nodiscard]] birth get_birth(const std::string& cca2)
    {
        return get_birth(cca2, draw_seed_());
    }

    /// @brief Generate a deterministic birth for a specific country.
    [[nodiscard]] birth get_birth(const std::string& cca2,
                                  std::uint64_t call_seed) const
    {
        if (_entries.empty())
        {
            throw std::runtime_error(
                "No birth data loaded. Call load() first.");
        }
        auto it = _entries.find(cca2);
        if (it == _entries.end())
        {
            throw std::invalid_argument(
                "Unknown country code: " + cca2);
        }
        return generate_(it->second, call_seed);
    }

    /// @brief Generate a random birth from a random country.
    /// @throws std::runtime_error If no data has been loaded.
    [[nodiscard]] birth get_birth()
    {
        return get_birth(draw_seed_());
    }

    /// @brief Generate a deterministic birth from a random country.
    [[nodiscard]] birth get_birth(std::uint64_t call_seed) const
    {
        if (_entries.empty())
        {
            throw std::runtime_error(
                "No birth data loaded. Call load() first.");
        }
        effolkronium::random_local rng;
        rng.seed(static_cast<std::mt19937::result_type>(
            call_seed ^ (call_seed >> seed_shift_)));

        auto idx = _weighted
                       ? _country_dist(rng.engine())
                       : _country_uniform(rng.engine());
        return get_birth(_cca2_order[idx], call_seed); // NOLINT
    }

    // -- Seeding ----------------------------------------------------------

    /// @brief Seed the internal random engine for deterministic sequences.
    bthg& seed(std::uint64_t seed_value)
    {
        _engine.seed(seed_value);
        return *this;
    }

    /// @brief Reseed the engine with a non-deterministic source.
    bthg& unseed()
    {
        _engine.seed(std::random_device{}());
        return *this;
    }

    /// @brief Set whether country selection is population-weighted.
    bthg& weighted(bool enable)
    {
        _weighted = enable;
        return *this;
    }

    /// @brief Query whether country selection is population-weighted.
    [[nodiscard]] bool weighted() const { return _weighted; }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    [[nodiscard]] bool has_data() const { return !_entries.empty(); }

    /// @brief Return the number of loaded countries.
    [[nodiscard]] std::size_t country_count() const
    {
        return _entries.size();
    }

    /// @brief Load birth data from a resource directory.
    ///
    /// Expects the directory to contain: countries.tsv,
    /// age_pyramid.tsv, monthly_births.tsv.
    void load(const std::filesystem::path& dir)
    {
        if (!std::filesystem::is_directory(dir))
        {
            return;
        }

        load_countries_(dir / "countries.tsv");
        load_age_pyramid_(dir / "age_pyramid.tsv");
        load_monthly_(dir / "monthly_births.tsv");
        rebuild_indices_();
    }

    /// @brief Load a specific dataset tier from auto-probed paths.
    [[nodiscard]] bool load(dataset tier)
    {
        std::string_view sub = (tier == dataset::full) ? "full" : "lite";
        auto found = std::ranges::find_if(
            probe_bases_, [&](std::string_view base) {
                auto d = std::filesystem::path{base} / sub;
                return std::filesystem::is_regular_file(
                    d / "countries.tsv");
            });
        if (found != probe_bases_.end())
        {
            load(std::filesystem::path{*found} / sub);
            return true;
        }
        return false;
    }

  private:
    // -- Internal data structures -----------------------------------------

    struct entry
    {
        std::string cca2;
        std::string cca3;
        std::string name;
        double le_male{0};
        double le_female{0};
        double csection_rate{0};
        double total_male{0};
        double total_female{0};

        // Pre-built age distributions (indexed 0..MAX_AGE).
        mutable std::discrete_distribution<unsigned> male_age_dist;
        mutable std::discrete_distribution<unsigned> female_age_dist;

        // Pre-built month distribution (indexed 0..11).
        mutable std::discrete_distribution<unsigned> month_dist;
    };

    std::unordered_map<std::string, entry> _entries;

    // Ordered cca2 list for random-country selection.
    std::vector<std::string> _cca2_order;
    mutable std::discrete_distribution<std::size_t> _country_dist;
    mutable std::uniform_int_distribution<std::size_t> _country_uniform;

    bool _weighted{true};
    int _ref_year{};

    static constexpr unsigned seed_shift_{32U};
    static constexpr std::size_t max_age_{100};

    static constexpr std::array<std::string_view, 3> probe_bases_{
        "resources", "../resources", "birth-generator/resources"};

    std::mt19937_64 _engine{std::random_device{}()};

    struct auto_probe_tag {};

    explicit bthg(auto_probe_tag /*tag*/) : _ref_year(current_year_())
    {
        auto found = std::ranges::find_if(
            probe_bases_, [](std::string_view p) {
                return std::filesystem::exists(p) &&
                       std::filesystem::is_directory(p);
            });
        if (found != probe_bases_.end())
        {
            const std::filesystem::path base{*found};
            auto lite = base / "lite";
            auto full = base / "full";
            if (std::filesystem::is_regular_file(
                    lite / "countries.tsv"))
            {
                load(lite);
            }
            else if (std::filesystem::is_regular_file(
                         full / "countries.tsv"))
            {
                load(full);
            }
        }
    }

    // -- Helpers ----------------------------------------------------------

    static int current_year_()
    {
        auto now = std::chrono::system_clock::now();
        auto dp = std::chrono::floor<std::chrono::days>(now);
        auto ymd = std::chrono::year_month_day{dp};
        return static_cast<int>(ymd.year());
    }

    std::uint64_t draw_seed_()
    {
        return static_cast<std::uint64_t>(_engine());
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    [[nodiscard]] birth generate_(const entry& e,
                                  std::uint64_t call_seed) const
    {
        effolkronium::random_local rng;
        rng.seed(static_cast<std::mt19937::result_type>(
            call_seed ^ (call_seed >> seed_shift_)));

        birth b;
        b._seed = call_seed;
        b.country_code = e.cca2;

        // 1. Sex — weighted by total male:female population.
        const double total = e.total_male + e.total_female;
        const double male_prob = (total > 0) ? (e.total_male / total) : 0.5;
        std::bernoulli_distribution sex_dist(1.0 - male_prob);
        b.bio_sex = sex_dist(rng.engine()) ? sex::female : sex::male;

        // 2. Age — from country-specific age pyramid.
        b.age = (b.bio_sex == sex::male)
                    ? e.male_age_dist(rng.engine())
                    : e.female_age_dist(rng.engine());

        // 3. Birth year.
        b.year = static_cast<std::uint16_t>(_ref_year - b.age);

        // 4. Month — from seasonal weights.
        b.month = static_cast<std::uint8_t>(
            e.month_dist(rng.engine()) + 1);

        // 5. Day within month.
        auto yr = std::chrono::year{static_cast<int>(b.year)};
        auto mo = std::chrono::month{b.month};
        auto last_day = static_cast<unsigned>(
            std::chrono::year_month_day_last{yr / mo / std::chrono::last}
                .day());
        std::uniform_int_distribution<unsigned> day_dist(1, last_day);

        // Apply weekday deficit: weekend births are less likely in
        // countries with high C-section / scheduled delivery rates.
        // Max rejection probability ~30% at csection_rate ≈ 0.6.
        static constexpr unsigned max_weekday_retries_{3};
        static constexpr double weekday_deficit_scale_{0.5};
        b.day = static_cast<std::uint8_t>(day_dist(rng.engine()));

        for (unsigned attempt = 0; attempt < max_weekday_retries_;
             ++attempt)
        {
            auto ymd = yr / mo / std::chrono::day{b.day};
            auto wd = std::chrono::weekday{
                std::chrono::sys_days{ymd}};
            const unsigned iso = wd.iso_encoding(); // 1=Mon..7=Sun
            if (iso >= 6) // Saturday or Sunday
            {
                const double reject_p =
                    e.csection_rate * weekday_deficit_scale_;
                std::bernoulli_distribution reject(reject_p);
                if (reject(rng.engine()))
                {
                    b.day = static_cast<std::uint8_t>(
                        day_dist(rng.engine()));
                    continue;
                }
            }
            break;
        }

        // 6. Weekday of final date.
        {
            auto ymd = yr / mo / std::chrono::day{b.day};
            auto wd = std::chrono::weekday{
                std::chrono::sys_days{ymd}};
            b.weekday = static_cast<std::uint8_t>(
                wd.c_encoding()); // 0=Sun..6=Sat
        }

        // 7. Life expectancy remaining.
        const double le = (b.bio_sex == sex::male) ? e.le_male : e.le_female;
        b.le_remaining = std::max(0.0, le - static_cast<double>(b.age));

        // 8. Generational cohort.
        b.cohort = cohort_label_(b.year);

        return b;
    }

    static std::string cohort_label_(int year)
    {
        if (year <= 1927) return "Greatest Generation";
        if (year <= 1945) return "Silent Generation";
        if (year <= 1964) return "Baby Boomer";
        if (year <= 1980) return "Generation X";
        if (year <= 1996) return "Millennial";
        if (year <= 2012) return "Generation Z";
        return "Generation Alpha";
    }

    // -- Loading ----------------------------------------------------------

    // Locale-independent double parser via std::from_chars.
    static double parse_double_(std::string_view str,
                                double fallback = 0.0)
    {
        if (str.empty()) { return fallback; }
        double val{};
        auto [ptr, ec] =
            std::from_chars(str.data(), str.data() + str.size(), val); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return ec == std::errc{} ? val : fallback;
    }

    static std::vector<std::string> split_tab_(const std::string& line)
    {
        std::vector<std::string> fields;
        for (auto part : line | std::views::split('\t'))
        {
            fields.emplace_back(std::ranges::begin(part),
                                std::ranges::end(part));
        }
        return fields;
    }

    void load_countries_(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path)) return;

        std::ifstream file{path};
        if (!file.is_open()) return;

        std::string line;
        if (!std::getline(file, line)) return; // skip header

        // Header: cca2 cca3 name region subregion latitude
        //         independent le_male le_female csection_rate
        static constexpr std::size_t min_fields{10};

        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            if (line.back() == '\r') line.pop_back();

            auto f = split_tab_(line);
            if (f.size() < min_fields) continue;

            entry e;
            e.cca2 = std::move(f[0]);
            e.cca3 = std::move(f[1]);
            e.name = std::move(f[2]);
            // fields 3..5 are region, subregion, latitude (metadata)
            // field 6: independent
            e.le_male = parse_double_(f[7]);
            e.le_female = parse_double_(f[8]);
            e.csection_rate = parse_double_(f[9]);

            auto key = e.cca2;
            _entries.insert_or_assign(std::move(key), std::move(e));
        }
    }

    void load_age_pyramid_(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path)) return;

        std::ifstream file{path};
        if (!file.is_open()) return;

        std::string line;
        if (!std::getline(file, line)) return; // skip header

        // Header: cca2  m0..m100  f0..f100  (203 columns)
        static constexpr std::size_t expected_cols{1 + 2 * (max_age_ + 1)};

        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            if (line.back() == '\r') line.pop_back();

            auto f = split_tab_(line);
            if (f.size() < expected_cols) continue;

            auto it = _entries.find(f[0]);
            if (it == _entries.end()) continue;

            auto& e = it->second;

            std::vector<double> male_w(max_age_ + 1);
            std::vector<double> female_w(max_age_ + 1);

            e.total_male = 0;
            e.total_female = 0;

            for (std::size_t a = 0; a <= max_age_; ++a)
            {
                const double mv = parse_double_(f[1 + a]);
                const double fv = parse_double_(f[1 + (max_age_ + 1) + a]);
                male_w[a] = std::max(mv, 0.0);
                female_w[a] = std::max(fv, 0.0);
                e.total_male += male_w[a];
                e.total_female += female_w[a];
            }

            // Ensure at least some weight to avoid degenerate distributions.
            if (e.total_male <= 0)
            {
                std::ranges::fill(male_w, 1.0);
                e.total_male = static_cast<double>(max_age_ + 1);
            }
            if (e.total_female <= 0)
            {
                std::ranges::fill(female_w, 1.0);
                e.total_female = static_cast<double>(max_age_ + 1);
            }

            e.male_age_dist = std::discrete_distribution<unsigned>(
                male_w.begin(), male_w.end());
            e.female_age_dist = std::discrete_distribution<unsigned>(
                female_w.begin(), female_w.end());
        }
    }

    void load_monthly_(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path)) return;

        std::ifstream file{path};
        if (!file.is_open()) return;

        std::string line;
        if (!std::getline(file, line)) return; // skip header

        // Header: cca2 jan feb ... dec (13 columns)
        static constexpr std::size_t expected_cols{13};

        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            if (line.back() == '\r') line.pop_back();

            auto f = split_tab_(line);
            if (f.size() < expected_cols) continue;

            auto it = _entries.find(f[0]);
            if (it == _entries.end()) continue;

            std::vector<double> w(12);
            for (std::size_t m = 0; m < 12; ++m)
            {
                w[m] = std::max(parse_double_(f[1 + m], 1.0), 0.0);
            }
            it->second.month_dist =
                std::discrete_distribution<unsigned>(w.begin(), w.end());
        }
    }

    void rebuild_indices_()
    {
        _cca2_order.clear();
        _cca2_order.reserve(_entries.size());

        std::vector<double> weights;
        weights.reserve(_entries.size());

        for (auto& [cca2, e] : _entries)
        {
            // Only include countries that have age pyramid data.
            if (e.total_male + e.total_female <= 0) continue;

            _cca2_order.push_back(cca2);
            weights.push_back(
                std::max(e.total_male + e.total_female, 1.0));
        }

        if (!_cca2_order.empty())
        {
            _country_dist = std::discrete_distribution<std::size_t>(
                weights.begin(), weights.end());
            _country_uniform =
                std::uniform_int_distribution<std::size_t>(
                    0, _cca2_order.size() - 1);
        }
    }
};

} // namespace dasmig

#endif // DASMIG_BIRTHGEN_HPP
