#include "scenario.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

const OrbitPreset PRESETS[] = {
    { "ISS (LEO)",          408.0f,    0.0007f, 51.6f,  30.0f,  0.0f },
    { "GEO",                35786.0f,  0.0f,    0.0f,   0.0f,   0.0f },
    { "Molniya",            20229.0f,  0.74f,   63.4f,  -70.0f, 270.0f },
    { "Sun-sync (SSO)",     600.0f,    0.001f,  97.8f,  45.0f,  0.0f },
    { "GPS (MEO)",          20200.0f,  0.0f,    55.0f,  0.0f,   0.0f },
    { "Hubble",             547.0f,    0.0003f, 28.5f,  0.0f,   0.0f },
};

const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

static float wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

static float getOrDefaultFloat(const json& obj, const char* key, float fallback) {
    if (!obj.contains(key) || !obj[key].is_number()) return fallback;
    return obj[key].get<float>();
}

static bool getOrDefaultBool(const json& obj, const char* key, bool fallback) {
    if (!obj.contains(key) || !obj[key].is_boolean()) return fallback;
    return obj[key].get<bool>();
}

static std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool trySubstr(const std::string& src, size_t pos, size_t len, std::string& out) {
    if (pos + len > src.size()) return false;
    out = src.substr(pos, len);
    return true;
}

static bool parseTleEpochUnix(const std::string& line1, double& outEpochUnix, std::string& err) {
    std::string yy, day;
    if (!trySubstr(line1, 18, 2, yy) || !trySubstr(line1, 20, 12, day)) {
        err = "TLE line1 missing epoch fields";
        return false;
    }

    int year2 = 0;
    double dayOfYear = 0.0;
    try {
        year2 = std::stoi(trim(yy));
        dayOfYear = std::stod(trim(day));
    } catch (...) {
        err = "Could not parse TLE epoch";
        return false;
    }

    int fullYear = (year2 < 57) ? (2000 + year2) : (1900 + year2);
    std::tm t0{};
    t0.tm_year = fullYear - 1900;
    t0.tm_mon = 0;
    t0.tm_mday = 1;
    t0.tm_hour = 0;
    t0.tm_min = 0;
    t0.tm_sec = 0;
#ifdef _WIN32
    std::time_t jan1 = _mkgmtime(&t0);
#else
    std::time_t jan1 = timegm(&t0);
#endif
    if (jan1 < 0) {
        err = "Could not convert TLE epoch to UTC";
        return false;
    }

    outEpochUnix = (double)jan1 + (dayOfYear - 1.0) * 86400.0;
    return true;
}

static bool parseTleToOrbit(const std::string& line1, const std::string& line2, OrbitalElements& outEl,
                            double& outMeanMotionRadS, double& outEpochUnix, std::string& err) {
    if (line1.size() < 69 || line2.size() < 69) {
        err = "TLE requires lines of at least 69 chars";
        return false;
    }

    std::string incS, raanS, eccS, argpS, maS, mmS;
    if (!trySubstr(line2, 8, 8, incS) ||
        !trySubstr(line2, 17, 8, raanS) ||
        !trySubstr(line2, 26, 7, eccS) ||
        !trySubstr(line2, 34, 8, argpS) ||
        !trySubstr(line2, 43, 8, maS) ||
        !trySubstr(line2, 52, 11, mmS)) {
        err = "TLE line2 missing expected fields";
        return false;
    }

    try {
        const double incDeg = std::stod(trim(incS));
        const double raanDeg = std::stod(trim(raanS));
        const double ecc = std::stod("0." + trim(eccS));
        const double argpDeg = std::stod(trim(argpS));
        const double maDeg = std::stod(trim(maS));
        const double mmRevDay = std::stod(trim(mmS));

        outMeanMotionRadS = mmRevDay * 2.0 * glm::pi<double>() / 86400.0;
        if (outMeanMotionRadS <= 0.0) {
            err = "Invalid TLE mean motion";
            return false;
        }

        const double a = std::cbrt(phys::MU / (outMeanMotionRadS * outMeanMotionRadS));
        outEl.a = a;
        outEl.e = glm::clamp(ecc, 0.0, 0.99);
        outEl.i = glm::radians(incDeg);
        outEl.raan = glm::radians(raanDeg);
        outEl.omega = glm::radians(argpDeg);
        outEl.M0 = glm::radians(maDeg);
    } catch (...) {
        err = "Could not parse TLE line2";
        return false;
    }

    return parseTleEpochUnix(line1, outEpochUnix, err);
}

void pushWarn(ConfigDiagnostics& d, const std::string& msg) { d.warnings.push_back(msg); }
void pushErr(ConfigDiagnostics& d, const std::string& msg) { d.errors.push_back(msg); }

static SatelliteScenario defaultSatellite() {
    SatelliteScenario sat;
    sat.name = "ISS";
    sat.propagator = "kepler";
    sat.elements = {
        phys::R_EARTH + 408000.0,
        0.0007,
        glm::radians(51.6),
        glm::radians(30.0),
        0.0,
        0.0
    };
    sat.tx_power_dbw = 12.0;
    sat.tx_gain_dbi = 10.0;
    sat.downlink_freq_hz = 2.2e9;
    sat.bandwidth_hz = 2.0e6;
    sat.required_ebn0_db = 4.5;
    return sat;
}

static std::vector<AntennaScenario> defaultAntennas() {
    return {
        { "Madrid-GS", 40.4168f, -3.7038f, 7.0f, 32.0f, 220.0f, 1.1f, 1.2f, 2.0f, 2.0f },
        { "BuenosAires-GS", -34.6037f, -58.3816f, 7.0f, 30.0f, 240.0f, 1.4f, 1.4f, 2.0f, 2.0f }
    };
}

static SimScenario defaultSimScenario() {
    SimScenario s;
    s.real_utc_default = true;
    s.initial_time_warp = 1.0f;
    s.rain_rate_mm_h = 5.0f;
    s.use_shannon_model = true;
    s.shannon_efficiency = 0.70f;
    s.elevation_mask_deg = 5.0f;
    return s;
}

static SatelliteScenario parseSatelliteFromJson(const json& j, ConfigDiagnostics& diag, int satIdx) {
    SatelliteScenario sat = defaultSatellite();
    std::string satPrefix = "satellites.json[sat " + std::to_string(satIdx) + "]";
    try {
        if (j.contains("name") && !j["name"].is_string()) {
            pushErr(diag, satPrefix + ":name must be a string");
        } else {
            sat.name = j.value("name", sat.name);
        }

        if (j.contains("propagator") && !j["propagator"].is_string()) {
            pushErr(diag, satPrefix + ":propagator must be a string");
        } else {
            sat.propagator = j.value("propagator", std::string("kepler"));
        }

        if (j.contains("orbit") && j["orbit"].is_object()) {
            const json& o = j["orbit"];
            float altKm = getOrDefaultFloat(o, "altitude_km", 408.0f);
            if (altKm < 120.0f || altKm > 50000.0f) {
                pushWarn(diag, satPrefix + ":orbit.altitude_km is out of range [120,50000], clamped");
                altKm = glm::clamp(altKm, 120.0f, 50000.0f);
            }
            sat.elements.a = phys::R_EARTH + (double)altKm * 1000.0;
            sat.elements.e = getOrDefaultFloat(o, "eccentricity", 0.0007f);
            if (sat.elements.e < 0.0 || sat.elements.e >= 1.0) {
                pushErr(diag, satPrefix + ":orbit.eccentricity must be in [0,1)");
                sat.elements.e = glm::clamp(sat.elements.e, 0.0, 0.98);
            }
            sat.elements.i = glm::radians((double)getOrDefaultFloat(o, "inclination_deg", 51.6f));
            sat.elements.raan = glm::radians((double)getOrDefaultFloat(o, "raan_deg", 30.0f));
            sat.elements.omega = glm::radians((double)getOrDefaultFloat(o, "arg_periapsis_deg", 0.0f));
            sat.elements.M0 = glm::radians((double)getOrDefaultFloat(o, "mean_anomaly_deg", 0.0f));
        }

        if (j.contains("radio") && j["radio"].is_object()) {
            const json& r = j["radio"];
            sat.tx_power_dbw = getOrDefaultFloat(r, "tx_power_dbw", (float)sat.tx_power_dbw);
            sat.tx_gain_dbi = getOrDefaultFloat(r, "tx_gain_dbi", (float)sat.tx_gain_dbi);
            sat.downlink_freq_hz = getOrDefaultFloat(r, "downlink_freq_hz", (float)sat.downlink_freq_hz);
            sat.bandwidth_hz = getOrDefaultFloat(r, "bandwidth_hz", (float)sat.bandwidth_hz);
            sat.required_ebn0_db = getOrDefaultFloat(r, "required_ebn0_db", (float)sat.required_ebn0_db);
            if (sat.downlink_freq_hz < 1e6 || sat.bandwidth_hz <= 0.0) {
                pushErr(diag, satPrefix + ":radio.downlink_freq_hz and bandwidth_hz must be positive");
                sat.downlink_freq_hz = std::max(1e6, sat.downlink_freq_hz);
                sat.bandwidth_hz = std::max(1.0, sat.bandwidth_hz);
            }
        }

        if (sat.propagator == "sgp4_tle") {
            if (!(j.contains("tle") && j["tle"].is_object())) {
                pushErr(diag, satPrefix + ":propagator=sgp4_tle requires tle object");
            } else {
                const json& tle = j["tle"];
                if (!tle.contains("line1") || !tle["line1"].is_string() ||
                    !tle.contains("line2") || !tle["line2"].is_string()) {
                    pushErr(diag, satPrefix + ":tle.line1 and tle.line2 are required strings");
                } else {
                    sat.tle_line1 = tle["line1"].get<std::string>();
                    sat.tle_line2 = tle["line2"].get<std::string>();
                    std::string err;
                    if (parseTleToOrbit(sat.tle_line1, sat.tle_line2, sat.elements, sat.tle_mean_motion_rad_s, sat.tle_epoch_unix, err)) {
                        sat.tle_loaded = true;
                    } else {
                        pushErr(diag, satPrefix + ":invalid TLE: " + err);
                    }
                }
            }
            if (!sat.tle_loaded) {
                pushWarn(diag, satPrefix + ":falling back to kepler due to invalid TLE");
                sat.propagator = "kepler";
            }
        }
    } catch (const std::exception& ex) {
        pushErr(diag, satPrefix + ": error parsing satellite: " + std::string(ex.what()));
    }

    return sat;
}

std::vector<SatelliteScenario> loadSatelliteScenarios(const std::string& path, ConfigDiagnostics& diag) {
    std::vector<SatelliteScenario> sats;
    std::ifstream f(path);
    if (!f.good()) {
        pushWarn(diag, "Missing " + path + ", using default satellite");
        sats.push_back(defaultSatellite());
        return sats;
    }

    try {
        json j;
        f >> j;
        if (j.contains("satellites") && j["satellites"].is_array()) {
            int idx = 0;
            for (const auto& entry : j["satellites"]) {
                if (!entry.is_object()) {
                    pushErr(diag, "satellites.json:satellites[" + std::to_string(idx) + "] must be an object");
                } else {
                    sats.push_back(parseSatelliteFromJson(entry, diag, idx));
                }
                idx++;
            }
        } else if (j.is_object()) {
            sats.push_back(parseSatelliteFromJson(j, diag, 0));
        } else {
            pushErr(diag, "satellites.json must be an object or contain a satellites array");
        }
    } catch (const std::exception& ex) {
        pushErr(diag, "Error reading " + path + ": " + ex.what());
    }

    if (sats.empty()) sats.push_back(defaultSatellite());
    return sats;
}

std::vector<AntennaScenario> loadAntennaScenario(const std::string& path, ConfigDiagnostics& diag) {
    std::vector<AntennaScenario> out;
    std::ifstream f(path);
    if (!f.good()) {
        pushWarn(diag, "Missing " + path + ", using default antennas");
        return defaultAntennas();
    }

    try {
        json j;
        f >> j;
        if (!j.contains("antennas") || !j["antennas"].is_array()) {
            pushErr(diag, "antennas.json must contain an antennas array");
            return defaultAntennas();
        }

        for (const auto& item : j["antennas"]) {
            if (!item.is_object()) continue;
            AntennaScenario a;
            if (!item.contains("name") || !item["name"].is_string()) {
                pushErr(diag, "antennas.json:antenna.name is required (string)");
            }
            a.name = item.value("name", std::string("GS"));
            a.latitude_deg = getOrDefaultFloat(item, "latitude_deg", 0.0f);
            a.longitude_deg = getOrDefaultFloat(item, "longitude_deg", 0.0f);
            if (a.latitude_deg < -90.0f || a.latitude_deg > 90.0f) {
                pushErr(diag, "antennas.json:" + a.name + ".latitude_deg out of range [-90,90]");
                a.latitude_deg = glm::clamp(a.latitude_deg, -90.0f, 90.0f);
            }
            if (a.longitude_deg < -180.0f || a.longitude_deg > 180.0f) {
                pushErr(diag, "antennas.json:" + a.name + ".longitude_deg out of range [-180,180]");
                a.longitude_deg = wrap180(a.longitude_deg);
            }
            a.min_elevation_deg = getOrDefaultFloat(item, "min_elevation_deg", 5.0f);
            a.rx_gain_dbi = getOrDefaultFloat(item, "rx_gain_dbi", 28.0f);
            a.system_temp_k = std::max(30.0f, getOrDefaultFloat(item, "system_temp_k", 220.0f));
            a.noise_figure_db = getOrDefaultFloat(item, "noise_figure_db", 1.2f);
            a.misc_losses_db = getOrDefaultFloat(item, "misc_losses_db", 1.0f);
            if (item.contains("tracking") && item["tracking"].is_object()) {
                const json& tr = item["tracking"];
                a.slew_az_deg_s = std::max(0.1f, getOrDefaultFloat(tr, "slew_az_deg_s", 2.0f));
                a.slew_el_deg_s = std::max(0.1f, getOrDefaultFloat(tr, "slew_el_deg_s", 2.0f));
            }
            out.push_back(a);
        }
    } catch (const std::exception& ex) {
        pushErr(diag, "Error reading " + path + ": " + ex.what());
        return defaultAntennas();
    }

    if (out.empty()) out = defaultAntennas();
    return out;
}

SimScenario loadSimScenario(const std::string& path, ConfigDiagnostics& diag) {
    SimScenario sim = defaultSimScenario();
    std::ifstream f(path);
    if (!f.good()) {
        pushWarn(diag, "Missing " + path + ", using default simulation config");
        return sim;
    }

    try {
        json j;
        f >> j;
        sim.real_utc_default = getOrDefaultBool(j, "real_utc_default", sim.real_utc_default);
        sim.initial_time_warp = std::max(1.0f, getOrDefaultFloat(j, "initial_time_warp", sim.initial_time_warp));
        sim.rain_rate_mm_h = std::max(0.0f, getOrDefaultFloat(j, "rain_rate_mm_h", sim.rain_rate_mm_h));
        sim.use_shannon_model = getOrDefaultBool(j, "use_shannon_model", sim.use_shannon_model);
        sim.shannon_efficiency = glm::clamp(getOrDefaultFloat(j, "shannon_efficiency", sim.shannon_efficiency), 0.1f, 1.0f);
        sim.elevation_mask_deg = glm::clamp(getOrDefaultFloat(j, "elevation_mask_deg", sim.elevation_mask_deg), 0.0f, 45.0f);
    } catch (const std::exception& ex) {
        pushErr(diag, "Error reading " + path + ": " + ex.what());
    }

    return sim;
}
