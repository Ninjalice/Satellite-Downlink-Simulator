#include "orbit.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

float wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

float wrap360(float deg) {
    while (deg >= 360.0f) deg -= 360.0f;
    while (deg < 0.0f) deg += 360.0f;
    return deg;
}

std::string utcStringFromUnix(double unixSeconds) {
    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tmUtc{};
#ifdef _WIN32
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << tmUtc.tm_hour << ":"
        << std::setw(2) << tmUtc.tm_min << ":"
        << std::setw(2) << tmUtc.tm_sec << " UTC";
    return oss.str();
}

glm::vec3 rotateY(const glm::vec3& p, float angRad) {
    glm::mat4 r = glm::rotate(glm::mat4(1.0f), angRad, glm::vec3(0, 1, 0));
    return glm::vec3(r * glm::vec4(p, 1.0f));
}

glm::vec3 localFromLatLon(float latDeg, float lonDeg, float radius) {
    float lat = glm::radians(latDeg);
    float lon = glm::radians(lonDeg);
    // For this sphere UVs, lon=0 (Greenwich) is at texture center (u=0.5).
    float x = -radius * std::cos(lat) * std::cos(lon);
    float y = radius * std::sin(lat);
    float z = radius * std::cos(lat) * std::sin(lon);
    return glm::vec3(x, y, z);
}

float rainFadeDb(float rainRateMmH, float elevationDeg) {
    float s = std::max(0.2f, std::sin(glm::radians(std::max(1.0f, elevationDeg))));
    return 0.02f * rainRateMmH / s;
}

float throughputTableMbps(float ebn0Db) {
    if (ebn0Db < 2.0f) return 0.10f;
    if (ebn0Db < 5.0f) return 0.50f;
    if (ebn0Db < 8.0f) return 1.00f;
    if (ebn0Db < 11.0f) return 2.00f;
    if (ebn0Db < 14.0f) return 5.00f;
    return 10.0f;
}

Orbit::Orbit(const OrbitalElements& e) : el(e) {}

double Orbit::meanMotion() const {
    if (hasCustomMeanMotion && customMeanMotionRadS > 0.0) return customMeanMotionRadS;
    return std::sqrt(phys::MU / (el.a * el.a * el.a));
}

double Orbit::period() const {
    return 2.0 * glm::pi<double>() / meanMotion();
}

double Orbit::solveKepler(double M) const {
    double E = M;
    for (int it = 0; it < 50; ++it) {
        double dE = (E - el.e * std::sin(E) - M) / (1.0 - el.e * std::cos(E));
        E -= dE;
        if (std::abs(dE) < 1e-12) break;
    }
    return E;
}

double Orbit::trueAnomaly(double E) const {
    return 2.0 * std::atan2(std::sqrt(1.0 + el.e) * std::sin(E / 2.0),
                            std::sqrt(1.0 - el.e) * std::cos(E / 2.0));
}

glm::dvec3 Orbit::positionAt(double t) const {
    double n = meanMotion();
    double M = el.M0 - n * t;
    M = std::fmod(M, 2.0 * glm::pi<double>());
    double E = solveKepler(M);
    double nu = trueAnomaly(E);
    double r = el.a * (1.0 - el.e * std::cos(E));

    double xo = r * std::cos(nu), yo = r * std::sin(nu);
    double ci = std::cos(el.i), si = std::sin(el.i);
    double co = std::cos(el.raan), so = std::sin(el.raan);
    double cw = std::cos(el.omega), sw = std::sin(el.omega);

    double x = (co * cw - so * sw * ci) * xo + (-co * sw - so * cw * ci) * yo;
    double y = (so * cw + co * sw * ci) * xo + (-so * sw + co * cw * ci) * yo;
    double z = (sw * si) * xo + (cw * si) * yo;
    return glm::dvec3(x, z, y);
}

glm::vec3 Orbit::posScaled(double t, float er) const {
    return glm::vec3(positionAt(t) * ((double)er / phys::R_EARTH));
}

void Orbit::setMeanMotionOverride(double nRadS) {
    if (nRadS > 0.0) {
        hasCustomMeanMotion = true;
        customMeanMotionRadS = nRadS;
    } else {
        hasCustomMeanMotion = false;
        customMeanMotionRadS = 0.0;
    }
}

void Orbit::update(double dt) {
    time += dt;
}
