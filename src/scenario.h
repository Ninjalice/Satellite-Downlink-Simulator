#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace phys {
constexpr double G = 6.67430e-11;
constexpr double M_EARTH = 5.972e24;
constexpr double R_EARTH = 6.371e6;
constexpr double MU = G * M_EARTH;
constexpr double C_LIGHT = 299792458.0;
}

struct AntennaScenario {
    std::string name;
    float latitude_deg = 0.0f;
    float longitude_deg = 0.0f;
    float min_elevation_deg = 5.0f;
    float rx_gain_dbi = 28.0f;
    float system_temp_k = 220.0f;
    float noise_figure_db = 1.2f;
    float misc_losses_db = 1.0f;
    float slew_az_deg_s = 2.0f;
    float slew_el_deg_s = 2.0f;
    float current_az_deg = 0.0f;
    float current_el_deg = 0.0f;
    bool was_visible = false;
    bool was_locked = false;
};

struct SimScenario {
    bool real_utc_default = true;
    float initial_time_warp = 1.0f;
    float rain_rate_mm_h = 5.0f;
    bool use_shannon_model = true;
    float shannon_efficiency = 0.70f;
    float elevation_mask_deg = 5.0f;
};

struct ConfigDiagnostics {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

struct OrbitalElements {
    double a, e, i, raan, omega, M0;
};

struct SatelliteScenario {
    std::string name = "ISS";
    std::string propagator = "kepler";
    OrbitalElements elements{};
    std::string tle_line1;
    std::string tle_line2;
    bool tle_loaded = false;
    double tle_mean_motion_rad_s = -1.0;
    double tle_epoch_unix = 0.0;
    double tx_power_dbw = 12.0;
    double tx_gain_dbi = 10.0;
    double downlink_freq_hz = 2.2e9;
    double bandwidth_hz = 2.0e6;
    double required_ebn0_db = 4.5;
};

struct LinkTelemetry {
    bool visible = false;
    bool locked = false;
    float range_km = 0.0f;
    float elevation_deg = -90.0f;
    float azimuth_deg = 0.0f;
    float rain_loss_db = 0.0f;
    float fspl_db = 0.0f;
    float cn0_dbhz = -999.0f;
    float ebn0_db = -999.0f;
    float margin_db = -999.0f;
    float ber = 1.0f;
    float throughput_mbps = 0.0f;
};

struct ContactSummary {
    bool in_contact = false;
    double contact_start_sim = 0.0;
    float last_contact_s = 0.0f;
    float total_contact_s = 0.0f;
    int pass_count = 0;
};

struct LinkPick {
    bool has = false;
    int satIndex = -1;
    LinkTelemetry telemetry{};
    glm::vec3 satPos = glm::vec3(0.0f);
};

struct OrbitPreset {
    const char* name;
    float alt_km;
    float ecc;
    float inc_deg;
    float raan_deg;
    float omega_deg;
};

extern const OrbitPreset PRESETS[];
extern const int NUM_PRESETS;

void pushWarn(ConfigDiagnostics& d, const std::string& msg);
void pushErr(ConfigDiagnostics& d, const std::string& msg);

std::vector<SatelliteScenario> loadSatelliteScenarios(const std::string& path, ConfigDiagnostics& diag);
std::vector<AntennaScenario> loadAntennaScenario(const std::string& path, ConfigDiagnostics& diag);
SimScenario loadSimScenario(const std::string& path, ConfigDiagnostics& diag);
