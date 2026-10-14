#ifndef DASMIG_BIRTHGEN_HPP
#define DASMIG_BIRTHGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef DASMIG_BIRTH_EMBED
#include "birthgen_data.hpp"
#endif

/// @file birthgen.hpp
/// @brief Birth generator library — procedural birthday generation for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 0);

namespace dasmig
{

#ifndef DASMIG_SEX_DEFINED
#define DASMIG_SEX_DEFINED
/// @brief Biological sex.
enum class sex : std::uint8_t
{
    male,  ///< Male.
    female ///< Female.
};
#endif

/// @brief Strong type for a requested birth year.
struct year_t
{
    std::uint16_t value; ///< The year.

    /// @brief Construct from a year.
    /// @param v The year.
    constexpr explicit year_t(std::uint16_t v) noexcept : value{v} {}
};

/// @brief An inclusive range of ages in completed years.
struct age_range
{
    std::uint8_t min; ///< Youngest age.
    std::uint8_t max; ///< Oldest age.
};

/// @brief All per-call birth filters and settings.
///
/// An omitted country selects a random country. Omitted sex, year and ages
/// use the country's demographic distributions. A fixed year and an age
/// range are mutually exclusive. Replay requires the same request and data.
struct birth_request
{
    /// @brief 1 July 2023, the date of the UN population estimates.
    static constexpr std::chrono::year_month_day default_reference_date{
        std::chrono::year{2023}, std::chrono::July, std::chrono::day{1}};

    /// @brief ISO 3166-1 alpha-2 code, or random.
    std::optional<std::string> country_code{}; // NOLINT(readability-redundant-member-init)
    /// @brief Fixed biological sex, or drawn.
    std::optional<dasmig::sex> bio_sex{}; // NOLINT(readability-redundant-member-init)
    /// @brief Fixed birth year, or derived from age.
    std::optional<year_t> year{}; // NOLINT(readability-redundant-member-init)
    /// @brief Inclusive ages; values above 100 clamp to 100.
    std::optional<age_range> ages{}; // NOLINT(readability-redundant-member-init)
    bool weighted{true}; ///< Population-weighted (true) or uniform country.
    std::chrono::year_month_day reference_date{default_reference_date}; ///< Date ages are computed at.
};

/// @brief Return type for birth generation.
///
/// Supports implicit conversion to std::string (ISO 8601 date),
/// streaming via operator<< and JSON output via to_json().
class birth
{
  public:
    std::string country_code; ///< ISO 3166-1 alpha-2 code.
    std::uint16_t year{0};    ///< Birth year.
    std::uint8_t month{0};    ///< Birth month (1–12).
    std::uint8_t day{0};      ///< Birth day (1–31).
    std::uint8_t age{0};      ///< Age in completed years at the reference date.
    sex bio_sex{sex::male};   ///< Biological sex.
    std::uint8_t weekday{0};  ///< Day of week (0=Sun, 1=Mon, …, 6=Sat).
    double le_remaining{0.0}; ///< Estimated years of life remaining.
    std::string cohort;       ///< Generational cohort label.

    /// @brief Retrieve the random seed used to generate this birth.
    /// @return The call seed; pass it to get_birth(request, seed) with the
    ///         original request and data to generate the same birth again.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this birth was generated from.
    /// @return The replay signature; `data` is bthg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief ISO 8601 date string (YYYY-MM-DD).
    /// @return The date.
    [[nodiscard]] std::string date_string() const
    {
        return std::format("{:04d}-{:02d}-{:02d}", static_cast<int>(year),
                           static_cast<int>(month), static_cast<int>(day));
    }

    /// @brief Write this birth as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country_code", country_code)
            .field("date", date_string())
            .field("year", static_cast<int>(year))
            .field("month", static_cast<int>(month))
            .field("day", static_cast<int>(day))
            .field("age", static_cast<int>(age))
            .field("sex", bio_sex == sex::male ? "male" : "female")
            .field("weekday", static_cast<int>(weekday))
            .field("le_remaining", le_remaining)
            .field("cohort", cohort)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This birth as a JSON document.
    /// @return Compact JSON.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return ISO 8601 date string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return date_string();
    }

    /// @brief Stream the ISO date to an output stream.
    /// @param os Output stream.
    /// @param b Birth to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const birth& b)
    {
        os << b.date_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class bthg;
};

/// @brief Birth generator that produces demographically plausible
///        birthdays from country age pyramids.
///
/// The generation pipeline, each step drawing from its own stream split
/// from the call seed (so fixing the sex, year or age range leaves the
/// other steps' draws unchanged):
/// 1. Sex, weighted by the country's male:female population.
/// 2. Age in completed years, from the country's age pyramid for that sex
///    (optionally restricted to an age range), or derived from a fixed year.
/// 3. Month, from the country's seasonal weights.
/// 4. Day, uniform within the month and placed so that the age at the
///    reference date is exact; weekend days are redrawn with a probability
///    that grows with the country's caesarean rate.
/// 5. Weekday, remaining life expectancy and generational cohort.
///
/// Ages are relative to a fixed reference date (default 2023-07-01, the
/// date of the population estimates), not the clock, so a seed gives the
/// same birth every time. Output is identical on every compiler and
/// platform (dasmig-core).
///
/// @par Thread safety
/// Each instance is independent. Concurrent calls to get_birth() on the
/// **same** instance require external synchronisation.
class bthg
{
  public:
    /// @brief The three data tables.
    enum class table : std::uint8_t
    {
        countries,     ///< `countries.tsv`: names, life expectancy, caesarean rate.
        age_pyramid,   ///< `age_pyramid.tsv`: population by single age and sex.
        monthly_births ///< `monthly_births.tsv`: seasonal birth weights.
    };

    /// @brief The default reference date: 1 July 2023, the date the UN
    ///        World Population Prospects estimates in the data refer to.
    static constexpr std::chrono::year_month_day default_reference_date{
        birth_request::default_reference_date};

    /// @brief Default constructor — creates an empty generator with no data.
    bthg() = default;

    bthg(const bthg&) = delete;            ///< Not copyable.
    bthg& operator=(const bthg&) = delete; ///< Not copyable.
    bthg(bthg&&) noexcept = default;       ///< Movable.
    bthg& operator=(bthg&&) noexcept = default; ///< Movable.
    ~bthg() = default;                     ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static bthg& instance()
    {
        static bthg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random birth using per-call filters and settings.
    /// @param request All filters and settings for this call.
    /// @return The birth, including its seed for replay with the same request.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the country is unknown, the reference
    ///         date or age range is invalid, or both year and ages are specified.
    [[nodiscard]] birth get_birth(const birth_request& request)
    {
        return get_birth(request, draw_seed_());
    }

    /// @brief Generate a deterministic birth using per-call filters and settings.
    ///
    /// Does not advance the internal random engine. Replay with the same
    /// request, seed and data.
    /// @param request Filters and settings for this call.
    /// @param call_seed Seed.
    /// @return The birth.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If the country is unknown, the reference
    ///         date or age range is invalid, or both year and ages are specified.
    [[nodiscard]] birth get_birth(const birth_request& request, std::uint64_t call_seed) const
    {
        validate_reference_(request.reference_date);
        if (request.ages && request.ages->min > request.ages->max)
        {
            throw std::invalid_argument("age_range: min must be <= max");
        }
        if (request.year && request.ages)
        {
            throw std::invalid_argument("birth_request: year and ages are mutually exclusive");
        }
        const auto& e = request.country_code ? lookup_entry_(*request.country_code)
                                             : random_entry_(call_seed, request.weighted);
        return generate_(e, call_seed, request);
    }

    // -- Seeding ----------------------------------------------------------

    /// @brief Seed the internal random engine for deterministic sequences.
    /// @param seed_value Seed.
    /// @return `*this`.
    bthg& seed(std::uint64_t seed_value)
    {
        _engine.seed(seed_value);
        return *this;
    }

    /// @brief Reseed the engine with a non-deterministic source.
    /// @return `*this`.
    bthg& unseed()
    {
        _engine.unseed();
        return *this;
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any country can be generated.
    /// @return True when at least one country has an age pyramid and life
    ///         expectancy.
    [[nodiscard]] bool has_data() const { return !_entries.empty(); }

    /// @brief Number of countries that can be generated.
    /// @return The count.
    [[nodiscard]] std::size_t country_count() const { return _entries.size(); }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// Reads `countries.tsv`, `age_pyramid.tsv` and `monthly_births.tsv`;
    /// a missing file leaves its table empty. A path that is not a
    /// directory, or has no `countries.tsv`, is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(table::countries), ec))
        {
            return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_BIRTH_EMBED`, otherwise searches `resources/`,
    /// `../resources`, `birth-generator/resources` and
    /// `../birth-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(table::countries)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)},
                                                     embedded_files_(), root);
        }
        if (!docs.at(index_(table::countries))) return false;
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of `countries.tsv`, `age_pyramid.tsv` and
    /// `monthly_births.tsv`; all are applied together or, if any fails,
    /// none. A file must have one of those names. Rows are keyed by
    /// `cca2`; see Extending Data in doc/usage.md.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected countries.tsv, age_pyramid.tsv or "
                                    "monthly_births.tsv, got " + path.filename().string());
    }

    /// @brief Add a data layer to one table.
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text.
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data (all three tables), recorded in each
    ///        result's signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        return any ? h : 0;
    }

  private:
    static constexpr std::string_view library_name_ = "birth-generator";
    static constexpr std::size_t max_age_ = 100;
    static constexpr std::size_t table_count_ = 3;
    static constexpr std::array<table, table_count_> all_tables_{
        table::countries, table::age_pyramid, table::monthly_births};
    static constexpr std::array<std::string_view, 12> month_columns_{
        "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        switch (t)
        {
        case table::countries: return "countries.tsv";
        case table::age_pyramid: return "age_pyramid.tsv";
        case table::monthly_births: return "monthly_births.tsv";
        }
        return "";
    }

    static const std::vector<std::string>& pyramid_columns_()
    {
        static const std::vector<std::string> columns = [] {
            std::vector<std::string> c;
            for (const char s : {'m', 'f'})
            {
                for (std::size_t a = 0; a <= max_age_; ++a) c.push_back(std::string(1, s) + std::to_string(a));
            }
            return c;
        }();
        return columns;
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::countries:
            return {.key = {"cca2"},
                    .required = {"name", "csection_rate"},
                    .optional = {"cca3", "region", "subregion", "latitude", "independent",
                                 "le_male", "le_female"}};
        case table::age_pyramid: return {.key = {"cca2"}, .required = pyramid_columns_(), .optional = {}};
        case table::monthly_births:
            return {.key = {"cca2"},
                    .required = {month_columns_.begin(), month_columns_.end()},
                    .optional = {}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::countries)},
                core::layered_table{schema_(table::age_pyramid)},
                core::layered_table{schema_(table::monthly_births)}};
    }

    // -- Row parsing (also the validation of layers) ------------------------

    struct country_row
    {
        std::string cca2;
        std::string name;
        std::optional<double> le_male;
        std::optional<double> le_female;
        double csection_rate{0.0};
    };

    static country_row parse_country_(const core::tsv_row& row)
    {
        country_row c;
        c.cca2 = row.get<std::string>("cca2");
        c.name = row.get<std::string>("name");
        c.csection_rate = row.get<double>("csection_rate");
        if (c.csection_rate < 0.0 || c.csection_rate > 1.0)
        {
            throw row.error("csection_rate", "must be between 0 and 1");
        }
        for (const auto& [column, value] : {std::pair{"le_male", &c.le_male}, std::pair{"le_female", &c.le_female}})
        {
            if (!row.has(column)) continue;
            const auto v = row.get<double>(column);
            if (!(v > 0.0)) throw row.error(column, "must be positive");
            *value = v;
        }
        return c;
    }

    // Weights by age, males then females.
    struct pyramid_row
    {
        std::vector<double> male;
        std::vector<double> female;
    };

    static pyramid_row parse_pyramid_(const core::tsv_row& row)
    {
        pyramid_row p;
        const auto& columns = pyramid_columns_();
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            const auto v = row.get<double>(columns[i]);
            if (v < 0.0) throw row.error(columns[i], "must not be negative");
            (i <= max_age_ ? p.male : p.female).push_back(v);
        }
        for (const auto* weights : {&p.male, &p.female})
        {
            double total = 0.0;
            for (const double w : *weights) total += w;
            if (!(total > 0.0))
            {
                throw row.error(weights == &p.male ? "m0" : "f0", "every age of this sex is 0");
            }
        }
        return p;
    }

    static std::vector<double> parse_monthly_(const core::tsv_row& row)
    {
        std::vector<double> w;
        double total = 0.0;
        for (const auto column : month_columns_)
        {
            const auto v = row.get<double>(column);
            if (v < 0.0) throw row.error(column, "must not be negative");
            w.push_back(v);
            total += v;
        }
        if (!(total > 0.0)) throw row.error("jan", "every month is 0");
        return w;
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        switch (t)
        {
        case table::countries: (void)parse_country_(row); break;
        case table::age_pyramid: (void)parse_pyramid_(row); break;
        case table::monthly_births: (void)parse_monthly_(row); break;
        }
    }

    // -- Assembled data ------------------------------------------------------

    struct entry
    {
        std::string cca2;
        std::string name;
        double le_male{0.0};
        double le_female{0.0};
        double csection_rate{0.0};
        double total_male{0.0};
        double total_female{0.0};
        std::vector<double> male_weights;   // by age 0..100
        std::vector<double> female_weights; // by age 0..100
        core::discrete male_age{1.0};
        core::discrete female_age{1.0};
        core::discrete month{1.0};
    };

    tables _tables{make_tables_()};
    std::vector<entry> _entries; // countries-table order
    std::map<std::string, std::size_t, std::less<>> _index;
    core::discrete _country_dist{1.0};
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit bthg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_BIRTH_EMBED
        return embedded::birthgen::files;
#else
        return {};
#endif
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                docs.at(index_(t)) = core::tsv_document::load(file);
            }
        }
        return docs;
    }

    // Apply layers to copies of the tables, then commit: all or nothing.
    // With @p reset (load), tables without a document are emptied.
    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        rebuild_(next);
        _tables = std::move(next);
    }

    // A country is generated when it has a countries row with life
    // expectancy and an age pyramid; monthly weights default to uniform.
    void rebuild_(const tables& t)
    {
        std::map<std::string, const core::tsv_row*, std::less<>> pyramids;
        std::map<std::string, const core::tsv_row*, std::less<>> months;
        for (const auto& row : t.at(index_(table::age_pyramid)).rows()) pyramids.emplace(row["cca2"], &row);
        for (const auto& row : t.at(index_(table::monthly_births)).rows()) months.emplace(row["cca2"], &row);

        std::vector<entry> entries;
        std::map<std::string, std::size_t, std::less<>> index;
        std::vector<double> population;
        for (const auto& row : t.at(index_(table::countries)).rows())
        {
            auto c = parse_country_(row);
            const auto p = pyramids.find(c.cca2);
            if (p == pyramids.end() || !c.le_male || !c.le_female) continue;
            auto pyramid = parse_pyramid_(*p->second);
            entry e;
            e.cca2 = std::move(c.cca2);
            e.name = std::move(c.name);
            e.le_male = *c.le_male;
            e.le_female = *c.le_female;
            e.csection_rate = c.csection_rate;
            for (const double w : pyramid.male) e.total_male += w;
            for (const double w : pyramid.female) e.total_female += w;
            e.male_age = core::discrete{pyramid.male};
            e.female_age = core::discrete{pyramid.female};
            e.male_weights = std::move(pyramid.male);
            e.female_weights = std::move(pyramid.female);
            const auto m = months.find(e.cca2);
            e.month = m == months.end() ? core::discrete{std::vector<double>(12, 1.0)}
                                        : core::discrete{parse_monthly_(*m->second)};
            population.push_back(e.total_male + e.total_female);
            index.emplace(e.cca2, entries.size());
            entries.push_back(std::move(e));
        }
        _country_dist = population.empty() ? core::discrete{1.0} : core::discrete{population};
        _entries = std::move(entries);
        _index = std::move(index);
    }

    // -- Generation ----------------------------------------------------------

    std::uint64_t draw_seed_() { return _engine(); }

    static void validate_reference_(std::chrono::year_month_day date)
    {
        if (!date.ok())
        {
            throw std::invalid_argument("reference_date: not a valid date");
        }
    }

    void require_data_() const
    {
        if (_entries.empty())
        {
            throw std::runtime_error("No birth data loaded. Call load() first.");
        }
    }

    [[nodiscard]] const entry& lookup_entry_(std::string_view cca2) const
    {
        require_data_();
        const auto it = _index.find(cca2);
        if (it == _index.end())
        {
            throw std::invalid_argument(std::string{"Unknown country code: "} += cca2);
        }
        return _entries[it->second];
    }

    [[nodiscard]] const entry& random_entry_(std::uint64_t call_seed, bool weighted) const
    {
        require_data_();
        auto r = core::rng{call_seed}.split("country");
        const auto idx = weighted ? _country_dist(r)
                                   : static_cast<std::size_t>(core::bounded(r, _entries.size()));
        return _entries[idx];
    }

    static unsigned days_in_(int year, unsigned month)
    {
        return static_cast<unsigned>(
            std::chrono::year_month_day_last{std::chrono::year{year} / std::chrono::month{month} /
                                             std::chrono::last}
                .day());
    }

    // Whether month/day falls after the reference date's month/day.
    [[nodiscard]] static bool after_reference_(unsigned month, unsigned day,
                                               std::chrono::year_month_day reference)
    {
        const auto ref_m = static_cast<unsigned>(reference.month());
        const auto ref_d = static_cast<unsigned>(reference.day());
        return month > ref_m || (month == ref_m && day > ref_d);
    }

    // Draw a day in @p month such that the person is @p age at the reference
    // date (or born in @p fixed_year), redrawing weekend days with
    // probability csection_rate / 2, at most three times.
    struct placed
    {
        int year;
        unsigned day;
    };

    [[nodiscard]] static placed place_(core::rng& r, const entry& e, unsigned month, unsigned age,
                                       std::optional<year_t> fixed_year,
                                       std::chrono::year_month_day reference)
    {
        const int ref_year = static_cast<int>(reference.year());
        auto draw = [&]() -> placed {
            if (fixed_year)
            {
                const int y = fixed_year->value;
                return {.year = y, .day = core::uniform_int(r, 1U, days_in_(y, month))};
            }
            for (;;)
            {
                const auto d = core::uniform_int(r, 1U, 31U);
                const int y = ref_year - static_cast<int>(age) -
                              (after_reference_(month, d, reference) ? 1 : 0);
                if (d <= days_in_(y, month)) return {.year = y, .day = d};
            }
        };
        placed p = draw();
        static constexpr int max_weekend_redraws = 3;
        for (int attempt = 0; attempt < max_weekend_redraws; ++attempt)
        {
            const std::chrono::weekday wd{std::chrono::sys_days{
                std::chrono::year{p.year} / std::chrono::month{month} / std::chrono::day{p.day}}};
            const bool weekend = wd.iso_encoding() >= 6;
            if (!weekend || !core::bernoulli(r, core::math::fenced(e.csection_rate * 0.5))) break;
            p = draw();
        }
        return p;
    }

    [[nodiscard]] birth generate_(const entry& e, std::uint64_t call_seed, const birth_request& opts) const
    {
        const core::rng call{call_seed};
        birth b;
        b._signature = {.seed = call_seed, .data = data_hash()};
        b.country_code = e.cca2;

        // 1. Sex, weighted by the male:female population.
        if (opts.bio_sex)
        {
            b.bio_sex = *opts.bio_sex;
        }
        else
        {
            auto r = call.split("sex");
            const double female_share = e.total_female / (e.total_male + e.total_female);
            b.bio_sex = core::bernoulli(r, female_share) ? sex::female : sex::male;
        }
        const bool male = b.bio_sex == sex::male;

        // 2. Age (unless the year is fixed), from the pyramid or a range of it.
        unsigned age = 0;
        if (!opts.year)
        {
            auto r = call.split("age");
            const auto& weights = male ? e.male_weights : e.female_weights;
            const auto top = static_cast<std::uint8_t>(max_age_);
            const auto range = opts.ages.value_or(age_range{.min = 0, .max = top});
            const auto age_min = std::min(range.min, top);
            const auto age_max = std::min(range.max, top);
            if (age_min == 0 && age_max == max_age_)
            {
                age = static_cast<unsigned>((male ? e.male_age : e.female_age)(r));
            }
            else
            {
                const std::vector<double> slice(weights.begin() + age_min,
                                                weights.begin() + age_max + 1);
                double total = 0.0;
                for (const double w : slice) total += w;
                const auto offset = total > 0.0 ? core::discrete{slice}(r)
                                                : static_cast<std::size_t>(core::bounded(r, slice.size()));
                age = age_min + static_cast<unsigned>(offset);
            }
        }

        // 3. Month, from the seasonal weights.
        {
            auto r = call.split("month");
            b.month = static_cast<std::uint8_t>(e.month(r) + 1);
        }

        // 4. Day (and year), with the weekend deficit.
        {
            auto r = call.split("day");
            const auto p = place_(r, e, b.month, age, opts.year, opts.reference_date);
            b.year = static_cast<std::uint16_t>(p.year);
            b.day = static_cast<std::uint8_t>(p.day);
            if (opts.year)
            {
                const int completed = static_cast<int>(opts.reference_date.year()) - p.year -
                                      (after_reference_(b.month, p.day, opts.reference_date) ? 1 : 0);
                age = static_cast<unsigned>(std::clamp(completed, 0, static_cast<int>(max_age_)));
            }
        }
        b.age = static_cast<std::uint8_t>(age);

        // 5. Weekday, remaining life expectancy, cohort.
        const std::chrono::weekday wd{std::chrono::sys_days{
            std::chrono::year{b.year} / std::chrono::month{b.month} / std::chrono::day{b.day}}};
        b.weekday = static_cast<std::uint8_t>(wd.c_encoding());
        const double le = male ? e.le_male : e.le_female;
        b.le_remaining = std::max(0.0, le - static_cast<double>(b.age));
        b.cohort = std::string{cohort_label_(b.year)};
        return b;
    }

    static std::string_view cohort_label_(int year)
    {
        if (year <= 1927) return "Greatest Generation";
        if (year <= 1945) return "Silent Generation";
        if (year <= 1964) return "Baby Boomer";
        if (year <= 1980) return "Generation X";
        if (year <= 1996) return "Millennial";
        if (year <= 2012) return "Generation Z";
        return "Generation Alpha";
    }
};

} // namespace dasmig

#endif // DASMIG_BIRTHGEN_HPP
