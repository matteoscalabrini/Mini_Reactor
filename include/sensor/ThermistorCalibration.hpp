/*
 * ThermistorCalibration.hpp — pure NTC calibration model + fitting.
 *
 * Header-only, dependency-free (host-testable). Converts a measured NTC
 * resistance to °C via one of three models, fitted from captured points:
 *   1 point  → constant offset on the factory Beta curve
 *   2 points → Beta refit (recompute R0, Beta)
 *   >=3      → Steinhart-Hart  1/T = A + B*ln(R) + C*ln(R)^3   (uses first/mid/last)
 * Resistances in ohms, temperatures in °C (converted to Kelvin internally).
 */
#pragma once

#include <cmath>

class ThermistorCalibration {
 public:
  enum class Method { Beta, Offset, Steinhart };
  struct BetaParams { float r0Ohms; float beta; float t0C; };
  static constexpr int kMaxPoints = 6;

  void begin(const BetaParams& beta,
             float minPlausibleC = -50.0f, float maxPlausibleC = 200.0f) {
    factory_ = beta;
    r0_ = beta.r0Ohms; beta_ = beta.beta; t0C_ = beta.t0C;
    method_ = Method::Beta; offsetC_ = 0.0f;
    a_ = b_ = c_ = 0.0f; nPoints_ = 0; calibrated_ = false;
    // Plausible-resistance band, calibration-INDEPENDENT (always factory Beta).
    // Cold -> high R, hot -> low R: the min-temp edge gives the max resistance.
    minPlausibleC_ = minPlausibleC; maxPlausibleC_ = maxPlausibleC;
    rMaxPlausible_ = factoryResistanceAt(minPlausibleC_);
    rMinPlausible_ = factoryResistanceAt(maxPlausibleC_);
  }

  bool addPoint(float referenceC, float resistanceOhms) {
    if (nPoints_ >= kMaxPoints) return false;
    points_[nPoints_].refC = referenceC;
    points_[nPoints_].rOhms = resistanceOhms;
    ++nPoints_;
    return true;
  }
  int pointCount() const { return nPoints_; }
  // Point accessors — caller must ensure 0 <= i < pointCount().
  float pointRefC(int i) const { return points_[i].refC; }
  float pointR(int i) const { return points_[i].rOhms; }

  bool compute() {
    if (nPoints_ == 1) {
      offsetC_ = points_[0].refC - betaCelsius(points_[0].rOhms);
      method_ = Method::Offset; calibrated_ = true; return true;
    }
    if (nPoints_ == 2) {
      const float t1 = points_[0].refC + 273.15f, t2 = points_[1].refC + 273.15f;
      const float denom = (1.0f / t1 - 1.0f / t2);
      if (std::fabs(denom) < 1e-9f) return false;
      beta_ = std::log(points_[0].rOhms / points_[1].rOhms) / denom;
      const float t0K = t0C_ + 273.15f;
      r0_ = points_[0].rOhms / std::exp(beta_ * (1.0f / t1 - 1.0f / t0K));
      method_ = Method::Beta; calibrated_ = true; return true;
    }
    if (nPoints_ >= 3) {
      const Point& p0 = points_[0];
      const Point& p1 = points_[nPoints_ / 2];
      const Point& p2 = points_[nPoints_ - 1];
      if (!solveSteinhart(p0, p1, p2)) return false;
      method_ = Method::Steinhart; calibrated_ = true; return true;
    }
    return false;  // 0 points
  }

  void reset() { begin(factory_, minPlausibleC_, maxPlausibleC_); }

  // True when R is within the factory-Beta plausible band; false => probe fault.
  bool plausibleResistance(float r) const {
    return r >= rMinPlausible_ && r <= rMaxPlausible_;
  }

  float resistanceToCelsius(float R) const {
    if (R <= 0.0f) return NAN;  // log of non-positive R is undefined
    switch (method_) {
      case Method::Offset: return betaCelsius(R) + offsetC_;
      case Method::Steinhart: {
        const float L = std::log(R);
        const float invT = a_ + b_ * L + c_ * L * L * L;
        return 1.0f / invT - 273.15f;
      }
      default: return betaCelsius(R);
    }
  }

  Method method() const { return method_; }
  bool calibrated() const { return calibrated_; }
  const char* methodStr() const {
    switch (method_) {
      case Method::Offset: return "offset";
      case Method::Steinhart: return "steinhart";
      default: return "beta";
    }
  }

 private:
  struct Point { float refC; float rOhms; };

  float betaCelsius(float R) const {
    const float t0K = t0C_ + 273.15f;
    const float invT = 1.0f / t0K + (1.0f / beta_) * std::log(R / r0_);
    return 1.0f / invT - 273.15f;
  }

  // Resistance at temperature tC on the FACTORY Beta curve (calibration-independent).
  float factoryResistanceAt(float tC) const {
    const float t0K = factory_.t0C + 273.15f;
    const float tK = tC + 273.15f;
    return factory_.r0Ohms * std::exp(factory_.beta * (1.0f / tK - 1.0f / t0K));
  }

  // Solve [[1,L1,L1^3],[1,L2,L2^3],[1,L3,L3^3]] [a,b,c]^T = [1/T1,1/T2,1/T3]^T (Cramer).
  bool solveSteinhart(const Point& p0, const Point& p1, const Point& p2) {
    const float L1 = std::log(p0.rOhms), L2 = std::log(p1.rOhms), L3 = std::log(p2.rOhms);
    const float Y1 = 1.0f / (p0.refC + 273.15f);
    const float Y2 = 1.0f / (p1.refC + 273.15f);
    const float Y3 = 1.0f / (p2.refC + 273.15f);
    const float c1 = L1 * L1 * L1, c2 = L2 * L2 * L2, c3 = L3 * L3 * L3;
    const float det = det3(1, L1, c1, 1, L2, c2, 1, L3, c3);
    if (std::fabs(det) < 1e-30f) return false;
    a_ = det3(Y1, L1, c1, Y2, L2, c2, Y3, L3, c3) / det;
    b_ = det3(1, Y1, c1, 1, Y2, c2, 1, Y3, c3) / det;
    c_ = det3(1, L1, Y1, 1, L2, Y2, 1, L3, Y3) / det;
    return true;
  }
  static float det3(float a, float b, float c, float d, float e, float f,
                    float g, float h, float i) {
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  }

  BetaParams factory_{10000.0f, 3950.0f, 25.0f};
  Method method_ = Method::Beta;
  float r0_ = 10000.0f, beta_ = 3950.0f, t0C_ = 25.0f;
  float offsetC_ = 0.0f, a_ = 0.0f, b_ = 0.0f, c_ = 0.0f;
  bool calibrated_ = false;
  Point points_[kMaxPoints];
  int nPoints_ = 0;
  // Plausible-resistance band (permissive until begin() sets it).
  float minPlausibleC_ = -50.0f, maxPlausibleC_ = 200.0f;
  float rMinPlausible_ = 0.0f, rMaxPlausible_ = 1e12f;
};
