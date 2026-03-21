#pragma once

#include <string>

#include <glm/glm.hpp>

#include "scenario.h"

float wrap180(float deg);
float wrap360(float deg);
std::string utcStringFromUnix(double unixSeconds);

glm::vec3 rotateY(const glm::vec3& p, float angRad);
glm::vec3 localFromLatLon(float latDeg, float lonDeg, float radius);
float rainFadeDb(float rainRateMmH, float elevationDeg);
float throughputTableMbps(float ebn0Db);

class Orbit {
public:
    OrbitalElements el;
    double time = 0.0;
    bool hasCustomMeanMotion = false;
    double customMeanMotionRadS = 0.0;

    explicit Orbit(const OrbitalElements& e);

    double meanMotion() const;
    double period() const;
    double solveKepler(double M) const;
    double trueAnomaly(double E) const;
    glm::dvec3 positionAt(double t) const;
    glm::vec3 posScaled(double t, float er) const;
    void setMeanMotionOverride(double nRadS);
    void update(double dt);
};
