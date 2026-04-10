#!/usr/bin/env python3
"""Download demographic data and produce TSV files for birth-generator.

Sources:
  1. REST Countries v3.1 (GitLab) — country metadata, ISO codes, latitude
  2. UN WPP 2024 (PPgp/wpp2024 GitHub repo, CC BY 3.0 IGO)
     — Population by single year of age and sex (popM.txt, popF.txt)
     — Life expectancy at birth by sex (e0M.txt, e0F.txt)
     — UN locations for country M49 code mapping (UNlocations.txt)
  3. WHO Global Health Observatory (hardcoded latest values)
     — Caesarean section rates by country
  4. Mathematical latitude model
     — Monthly birth seasonality weights

Produces per tier (full ~237, lite ~195 sovereign):
  resources/{tier}/countries.tsv       — metadata per country
  resources/{tier}/age_pyramid.tsv     — population by single age and sex
  resources/{tier}/monthly_births.tsv  — 12 monthly birth weights

Usage:
    python prepare_births.py              # generate both tiers
    python prepare_births.py --tier full  # full only
    python prepare_births.py --tier lite  # lite only
"""

import argparse
import json
import math
import os
import sys
import urllib.request

# ---------------------------------------------------------------------------
# URLs & Config
# ---------------------------------------------------------------------------

_RESTCOUNTRIES_URL = (
    "https://gitlab.com/restcountries/restcountries"
    "/-/raw/master/src/main/resources/countriesV3.1.json"
)

_WPP_RAW = "https://raw.githubusercontent.com/PPgp/wpp2024/main/data-raw"
_WPP_DATA = "https://raw.githubusercontent.com/PPgp/wpp2024/main/data"

_CACHE_DIR = os.path.join(os.path.dirname(__file__), ".cache")

_REFERENCE_YEAR = "2023"  # Latest historical estimate year in the WPP R package
_MAX_AGE = 100            # Ages 0..100 (101 buckets)

# ---------------------------------------------------------------------------
# Caesarean section rates (WHO GHO, latest available year)
# Proportion (0.0 – 1.0). Used for weekend birth deficit weighting.
# ---------------------------------------------------------------------------

_CSECTION_RATES: dict[str, float] = {
    "AD": 0.30, "AE": 0.31, "AL": 0.30, "AR": 0.35, "AT": 0.30,
    "AU": 0.34, "AZ": 0.25, "BA": 0.25, "BD": 0.33, "BE": 0.21,
    "BG": 0.38, "BH": 0.25, "BR": 0.56, "BY": 0.24, "CA": 0.29,
    "CH": 0.33, "CL": 0.47, "CN": 0.37, "CO": 0.44, "CR": 0.27,
    "CU": 0.37, "CY": 0.56, "CZ": 0.27, "DE": 0.31, "DK": 0.21,
    "DO": 0.58, "DZ": 0.16, "EC": 0.35, "EE": 0.22, "EG": 0.52,
    "ES": 0.25, "FI": 0.17, "FR": 0.21, "GB": 0.28, "GE": 0.47,
    "GH": 0.13, "GR": 0.50, "GT": 0.25, "HK": 0.28, "HR": 0.24,
    "HU": 0.37, "ID": 0.18, "IE": 0.32, "IL": 0.21, "IN": 0.22,
    "IQ": 0.30, "IR": 0.47, "IS": 0.17, "IT": 0.35, "JM": 0.25,
    "JO": 0.30, "JP": 0.27, "KE": 0.06, "KR": 0.45, "KW": 0.24,
    "KZ": 0.20, "LB": 0.42, "LK": 0.32, "LT": 0.27, "LU": 0.30,
    "LV": 0.27, "MA": 0.22, "MK": 0.28, "MM": 0.16, "MN": 0.22,
    "MT": 0.33, "MX": 0.46, "MY": 0.25, "MZ": 0.04, "NG": 0.02,
    "NL": 0.17, "NO": 0.17, "NP": 0.13, "NZ": 0.27, "OM": 0.20,
    "PA": 0.30, "PE": 0.35, "PH": 0.13, "PK": 0.20, "PL": 0.37,
    "PR": 0.48, "PT": 0.36, "PY": 0.45, "QA": 0.30, "RO": 0.45,
    "RS": 0.33, "RU": 0.23, "SA": 0.25, "SE": 0.18, "SG": 0.36,
    "SI": 0.21, "SK": 0.30, "TH": 0.33, "TN": 0.25, "TR": 0.53,
    "TT": 0.26, "TW": 0.36, "TZ": 0.06, "UA": 0.23, "UG": 0.06,
    "US": 0.32, "UY": 0.44, "UZ": 0.12, "VE": 0.30, "VN": 0.28,
    "ZA": 0.27, "ZW": 0.06,
}

_REGIONAL_CSECTION: dict[str, float] = {
    "Northern America": 0.32,
    "South America": 0.43,
    "Central America": 0.35,
    "Caribbean": 0.35,
    "Northern Europe": 0.20,
    "Western Europe": 0.25,
    "Southern Europe": 0.30,
    "Eastern Europe": 0.27,
    "Central Asia": 0.20,
    "Southern Asia": 0.20,
    "South-Eastern Asia": 0.20,
    "Eastern Asia": 0.35,
    "Western Asia": 0.30,
    "Northern Africa": 0.25,
    "Sub-Saharan Africa": 0.05,
    "Eastern Africa": 0.05,
    "Middle Africa": 0.05,
    "Southern Africa": 0.20,
    "Western Africa": 0.03,
    "Australia and New Zealand": 0.33,
    "Melanesia": 0.10,
    "Micronesia": 0.15,
    "Polynesia": 0.15,
}

# ---------------------------------------------------------------------------
# countries.tsv columns
# ---------------------------------------------------------------------------

_COUNTRY_COLUMNS = [
    "cca2", "cca3", "name", "region", "subregion",
    "latitude", "independent", "le_male", "le_female", "csection_rate",
]

_COUNTRY_HEADER = "\t".join(_COUNTRY_COLUMNS)

# ---------------------------------------------------------------------------
# Download / cache helpers
# ---------------------------------------------------------------------------


def _fetch_cached(url: str, cache_key: str) -> bytes:
    """Download a URL and cache the raw bytes locally."""
    path = os.path.join(_CACHE_DIR, cache_key)
    if os.path.exists(path):
        with open(path, "rb") as fh:
            return fh.read()

    print(f"    GET {url[:120]}{'...' if len(url) > 120 else ''}")
    req = urllib.request.Request(url, headers={"User-Agent": "birth-generator/1.0"})
    resp = urllib.request.urlopen(req, timeout=120)  # noqa: S310
    raw = resp.read()

    os.makedirs(os.path.dirname(path) or _CACHE_DIR, exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(raw)
    return raw


def _fetch_json(url: str, cache_key: str) -> list | dict:
    """Fetch JSON from a URL with filesystem cache."""
    raw = _fetch_cached(url, cache_key + ".json")
    return json.loads(raw)


def _fetch_tsv(url: str, cache_key: str) -> list[list[str]]:
    """Fetch a tab-separated text file, return rows as list of string lists."""
    raw = _fetch_cached(url, cache_key + ".txt")
    text = raw.decode("utf-8")
    rows: list[list[str]] = []
    for line in text.split("\n"):
        line = line.strip()
        if line:
            rows.append(line.split("\t"))
    return rows


# ---------------------------------------------------------------------------
# 1. Country metadata (REST Countries)
# ---------------------------------------------------------------------------


def _fetch_rest_countries() -> dict[str, dict]:
    """Return REST Countries data keyed by alpha-2 code.

    Also builds ccn3→cca2 mapping for M49 code matching.
    """
    data = _fetch_json(_RESTCOUNTRIES_URL, "restcountries_v3.1")
    result: dict[str, dict] = {}
    for c in data:
        cca2 = c.get("cca2", "")
        if cca2:
            latlng = c.get("latlng") or [0, 0]
            result[cca2] = {
                "cca2": cca2,
                "cca3": c.get("cca3", ""),
                "ccn3": c.get("ccn3", ""),
                "name": c.get("name", {}).get("common", ""),
                "region": c.get("region", ""),
                "subregion": c.get("subregion", ""),
                "latitude": latlng[0] if len(latlng) > 0 else 0,
                "independent": c.get("independent", False),
            }
    return result


# ---------------------------------------------------------------------------
# 2. WPP population by single age and sex (GitHub PPgp/wpp2024)
# ---------------------------------------------------------------------------


def _parse_wpp_wide(
    rows: list[list[str]], year_col: str
) -> dict[int, dict[int, float]]:
    """Parse a WPP wide-format file (country_code, country, age, y1, y2, ...).

    Returns: {m49_code: {age: value_in_thousands}}
    """
    header = rows[0]
    try:
        year_idx = header.index(year_col)
    except ValueError:
        sys.exit(f"  ERROR: year column '{year_col}' not found in WPP file. "
                 f"Available: {header[3:8]}...{header[-3:]}")

    result: dict[int, dict[int, float]] = {}
    for row in rows[1:]:
        if len(row) <= year_idx:
            continue
        try:
            m49 = int(row[0])
            age = int(row[2])
            val = float(row[year_idx]) if row[year_idx] else 0.0
        except (ValueError, IndexError):
            continue
        if m49 not in result:
            result[m49] = {}
        result[m49][age] = val
    return result


def _fetch_un_country_m49() -> set[int]:
    """Return the set of M49 codes that are countries (location_type=4)."""
    rows = _fetch_tsv(f"{_WPP_DATA}/UNlocations.txt", "UNlocations")
    header = rows[0]
    type_idx = header.index("location_type")
    code_idx = header.index("country_code")
    country_codes: set[int] = set()
    for row in rows[1:]:
        if len(row) > type_idx and row[type_idx].strip() == "4":
            try:
                country_codes.add(int(row[code_idx]))
            except ValueError:
                pass
    return country_codes


def _fetch_age_pyramids(
    m49_to_cca2: dict[int, str],
) -> dict[str, dict[str, list[float]]]:
    """Download WPP pop files and build age pyramids per country.

    Returns: {cca2: {"male": [pop_0..pop_100], "female": [pop_0..pop_100]}}
    Values are population in thousands (as published).
    """
    print("  Downloading popM.txt ...")
    rows_m = _fetch_tsv(f"{_WPP_RAW}/popM.txt", "wpp_popM")
    print("  Downloading popF.txt ...")
    rows_f = _fetch_tsv(f"{_WPP_RAW}/popF.txt", "wpp_popF")

    pop_m = _parse_wpp_wide(rows_m, _REFERENCE_YEAR)
    pop_f = _parse_wpp_wide(rows_f, _REFERENCE_YEAR)

    result: dict[str, dict[str, list[float]]] = {}
    for m49, cca2 in m49_to_cca2.items():
        male_ages = pop_m.get(m49, {})
        female_ages = pop_f.get(m49, {})
        if not male_ages and not female_ages:
            continue
        male_list = [0.0] * (_MAX_AGE + 1)
        female_list = [0.0] * (_MAX_AGE + 1)
        for age in range(_MAX_AGE + 1):
            male_list[age] = male_ages.get(age, 0.0)
            female_list[age] = female_ages.get(age, 0.0)
        result[cca2] = {"male": male_list, "female": female_list}
    return result


# ---------------------------------------------------------------------------
# 3. Life expectancy from WPP (GitHub PPgp/wpp2024)
# ---------------------------------------------------------------------------


def _parse_wpp_e0(rows: list[list[str]], year_col: str) -> dict[int, float]:
    """Parse a WPP wide-format e0 file (country_code, country, year_cols...).

    Returns: {m49_code: life_expectancy}
    """
    header = rows[0]
    try:
        year_idx = header.index(year_col)
    except ValueError:
        return {}
    result: dict[int, float] = {}
    for row in rows[1:]:
        if len(row) <= year_idx:
            continue
        try:
            m49 = int(row[0])
            val = float(row[year_idx]) if row[year_idx] else 0.0
        except (ValueError, IndexError):
            continue
        result[m49] = val
    return result


def _fetch_life_expectancy(
    m49_to_cca2: dict[int, str],
) -> tuple[dict[str, float], dict[str, float]]:
    """Download WPP e0 files and return LE by cca2.

    Returns: (le_male, le_female) — each {cca2: years}
    """
    print("  Downloading e0M.txt ...")
    rows_m = _fetch_tsv(f"{_WPP_RAW}/e0M.txt", "wpp_e0M")
    print("  Downloading e0F.txt ...")
    rows_f = _fetch_tsv(f"{_WPP_RAW}/e0F.txt", "wpp_e0F")

    e0m = _parse_wpp_e0(rows_m, _REFERENCE_YEAR)
    e0f = _parse_wpp_e0(rows_f, _REFERENCE_YEAR)

    le_male: dict[str, float] = {}
    le_female: dict[str, float] = {}
    for m49, cca2 in m49_to_cca2.items():
        if m49 in e0m:
            le_male[cca2] = e0m[m49]
        if m49 in e0f:
            le_female[cca2] = e0f[m49]
    return le_male, le_female


# ---------------------------------------------------------------------------
# 4. C-section rate lookup
# ---------------------------------------------------------------------------


def _csection_rate(cca2: str, subregion: str, region: str) -> float:
    """Return C-section rate for a country, with regional fallback."""
    if cca2 in _CSECTION_RATES:
        return _CSECTION_RATES[cca2]
    # Try subregion, then region
    for key in (subregion, region):
        if key in _REGIONAL_CSECTION:
            return _REGIONAL_CSECTION[key]
    return 0.15  # Global fallback


# ---------------------------------------------------------------------------
# 5. Monthly birth seasonality (latitude model)
# ---------------------------------------------------------------------------


def _monthly_weights(latitude: float) -> list[float]:
    """Compute 12 monthly birth weights based on latitude.

    Northern hemisphere peaks in September, southern in March.
    Amplitude scales with absolute latitude (stronger near poles).
    """
    abs_lat = abs(latitude)
    # Max amplitude ~8% at poles, ~0% at equator (research: ~5.8% average)
    amplitude = 0.08 * (abs_lat / 90.0)

    # Peak month: September (9) for NH, March (3) for SH
    peak_month = 9.0 if latitude >= 0 else 3.0

    weights: list[float] = []
    for m in range(1, 13):
        w = 1.0 + amplitude * math.cos(2 * math.pi * (m - peak_month) / 12.0)
        weights.append(round(w, 6))
    return weights


# ---------------------------------------------------------------------------
# TSV output
# ---------------------------------------------------------------------------


def _write_countries_tsv(
    rows: list[dict[str, str]], output: str
) -> None:
    """Write countries.tsv."""
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(_COUNTRY_HEADER + "\n")
        for row in rows:
            line = "\t".join(row.get(col, "") for col in _COUNTRY_COLUMNS)
            fh.write(line + "\n")
    print(f"  Wrote {len(rows)} countries to {output}")


def _write_age_pyramid_tsv(
    pyramids: dict[str, dict[str, list[float]]],
    country_order: list[str],
    output: str,
) -> None:
    """Write age_pyramid.tsv (cca2 + m0..m100 + f0..f100)."""
    cols = ["cca2"]
    for prefix in ("m", "f"):
        for age in range(_MAX_AGE + 1):
            cols.append(f"{prefix}{age}")
    header = "\t".join(cols)

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    count = 0
    with open(output, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(header + "\n")
        for cca2 in country_order:
            pyr = pyramids.get(cca2)
            if not pyr:
                continue
            parts = [cca2]
            for prefix, key in [("m", "male"), ("f", "female")]:
                for age in range(_MAX_AGE + 1):
                    parts.append(f"{pyr[key][age]:.3f}")
            fh.write("\t".join(parts) + "\n")
            count += 1
    print(f"  Wrote {count} age pyramids to {output}")


def _write_monthly_tsv(
    monthly: dict[str, list[float]],
    country_order: list[str],
    output: str,
) -> None:
    """Write monthly_births.tsv (cca2 + jan..dec)."""
    month_names = [
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec",
    ]
    header = "\t".join(["cca2"] + month_names)

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    count = 0
    with open(output, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(header + "\n")
        for cca2 in country_order:
            weights = monthly.get(cca2)
            if not weights:
                continue
            parts = [cca2] + [f"{w:.6f}" for w in weights]
            fh.write("\t".join(parts) + "\n")
            count += 1
    print(f"  Wrote {count} monthly profiles to {output}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare demographic TSV files for birth-generator."
    )
    parser.add_argument(
        "--tier",
        default="both",
        choices=["full", "lite", "both"],
        help="Which dataset tier to generate (default: both).",
    )
    args = parser.parse_args()
    os.makedirs(_CACHE_DIR, exist_ok=True)

    # ── Step 1: Country metadata ──────────────────────────────────────────
    print("\n=== 1. Country metadata (REST Countries) ===")
    rest = _fetch_rest_countries()
    print(f"  {len(rest)} countries loaded.")

    # ── Step 2: Age pyramid from UN WPP ──────────────────────────────────
    # ── Step 2: M49 ↔ ISO mapping ──────────────────────────────────────
    print("\n=== 2. Building M49 to ISO code mapping ===")
    # Build M49 → cca2 from REST Countries (ccn3 is the M49 numeric code)
    m49_to_cca2: dict[int, str] = {}
    for cca2, rc in rest.items():
        ccn3 = rc.get("ccn3", "")
        if ccn3:
            try:
                m49_to_cca2[int(ccn3)] = cca2
            except ValueError:
                pass

    # Filter to country-type locations in WPP (location_type=4)
    un_countries = _fetch_un_country_m49()
    m49_countries = {m: c for m, c in m49_to_cca2.items() if m in un_countries}
    print(f"  {len(m49_countries)} WPP countries matched to ISO codes "
          f"(of {len(un_countries)} WPP total)")

    # ── Step 3: Age pyramids (WPP GitHub) ────────────────────────────────
    print("\n=== 3. Age pyramids (WPP 2024, year {}) ===".format(_REFERENCE_YEAR))
    pyramids = _fetch_age_pyramids(m49_countries)
    print(f"  Age pyramids built for {len(pyramids)} countries.")

    # ── Step 4: Life expectancy (WPP GitHub) ─────────────────────────────
    print("\n=== 4. Life expectancy (WPP 2024) ===")
    le_male, le_female = _fetch_life_expectancy(m49_countries)
    print(f"  Male LE: {len(le_male)} countries")
    print(f"  Female LE: {len(le_female)} countries")

    # ── Step 5: Assemble & write TSVs ────────────────────────────────────
    print("\n=== 5. Assembling TSV data ===")

    # Build rows ordered by common name
    all_cca2 = sorted(rest.keys(), key=lambda k: rest[k]["name"])

    rows_full: list[dict[str, str]] = []
    rows_lite: list[dict[str, str]] = []
    monthly_full: dict[str, list[float]] = {}
    monthly_lite: dict[str, list[float]] = {}
    order_full: list[str] = []
    order_lite: list[str] = []

    for cca2 in all_cca2:
        rc = rest[cca2]
        lat = rc["latitude"]

        row: dict[str, str] = {
            "cca2": cca2,
            "cca3": rc["cca3"],
            "name": rc["name"],
            "region": rc["region"],
            "subregion": rc["subregion"],
            "latitude": str(lat),
            "independent": "1" if rc["independent"] else "0",
            "le_male": f"{le_male[cca2]:.1f}" if cca2 in le_male else "",
            "le_female": f"{le_female[cca2]:.1f}" if cca2 in le_female else "",
            "csection_rate": f"{_csection_rate(cca2, rc['subregion'], rc['region']):.3f}",
        }

        rows_full.append(row)
        order_full.append(cca2)
        monthly_full[cca2] = _monthly_weights(lat)

        if rc["independent"]:
            rows_lite.append(row)
            order_lite.append(cca2)
            monthly_lite[cca2] = monthly_full[cca2]

    base_dir = os.path.join(os.path.dirname(__file__), "..", "resources")
    tiers = ["full", "lite"] if args.tier == "both" else [args.tier]

    for tier in tiers:
        print(f"\n=== Writing {tier} tier ===")
        rows = rows_full if tier == "full" else rows_lite
        order = order_full if tier == "full" else order_lite
        mon = monthly_full if tier == "full" else monthly_lite
        tier_dir = os.path.join(base_dir, tier)

        _write_countries_tsv(rows, os.path.join(tier_dir, "countries.tsv"))
        _write_age_pyramid_tsv(pyramids, order, os.path.join(tier_dir, "age_pyramid.tsv"))
        _write_monthly_tsv(mon, order, os.path.join(tier_dir, "monthly_births.tsv"))

    print("\nDone.")


if __name__ == "__main__":
    main()
