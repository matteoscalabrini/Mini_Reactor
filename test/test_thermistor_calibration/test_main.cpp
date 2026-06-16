#include <unity.h>
#include <cmath>
#include "sensor/ThermistorCalibration.hpp"

// Factory 10k NTC, Beta 3950, T0 25C.
static ThermistorCalibration::BetaParams kBeta{10000.0f, 3950.0f, 25.0f};

void setUp() {}
void tearDown() {}

// Beta model: R == R0 at T0 → 25 C.
void test_beta_default() {
  ThermistorCalibration c; c.begin(kBeta);
  TEST_ASSERT_FALSE(c.calibrated());
  TEST_ASSERT_EQUAL_FLOAT(25.0f, c.resistanceToCelsius(10000.0f));
}

// 1 point → constant offset on the Beta curve.
void test_one_point_offset() {
  ThermistorCalibration c; c.begin(kBeta);
  const float R = 12000.0f;
  const float betaC = c.resistanceToCelsius(R);   // uncalibrated reading at R
  c.addPoint(betaC + 2.0f, R);                     // operator says it's really +2C
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_TRUE(c.calibrated());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Offset, (int)c.method());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, betaC + 2.0f, c.resistanceToCelsius(R));
}

// 2 points → Beta refit that reproduces both points.
void test_two_point_beta_refit() {
  ThermistorCalibration c; c.begin(kBeta);
  // Generate two synthetic points from a KNOWN beta=4000,R0=10000,T0=25.
  auto rAt = [](float tC){ float t0=298.15f,T=tC+273.15f; return 10000.0f*expf(4000.0f*(1.0f/T-1.0f/t0)); };
  c.addPoint(10.0f, rAt(10.0f));
  c.addPoint(60.0f, rAt(60.0f));
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Beta, (int)c.method());
  TEST_ASSERT_TRUE(c.calibrated());
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, c.resistanceToCelsius(rAt(10.0f)));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 60.0f, c.resistanceToCelsius(rAt(60.0f)));
}

// 3 points → Steinhart-Hart that reproduces all three.
void test_three_point_steinhart() {
  ThermistorCalibration c; c.begin(kBeta);
  // Known coefficients (typical 10k NTC).
  const float A=1.129241e-3f, B=2.341077e-4f, Cc=8.775468e-8f;
  auto tAt = [&](float R){ float L=logf(R); float invT=A+B*L+Cc*L*L*L; return 1.0f/invT-273.15f; };
  float Rs[3] = {20000.0f, 10000.0f, 4000.0f};
  for (int i=0;i<3;i++) c.addPoint(tAt(Rs[i]), Rs[i]);
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Steinhart, (int)c.method());
  for (int i=0;i<3;i++) TEST_ASSERT_FLOAT_WITHIN(0.1f, tAt(Rs[i]), c.resistanceToCelsius(Rs[i]));
}

void test_compute_zero_points_fails() {
  ThermistorCalibration c; c.begin(kBeta);
  TEST_ASSERT_FALSE(c.compute());
}

void test_reset_back_to_beta() {
  ThermistorCalibration c; c.begin(kBeta);
  c.addPoint(c.resistanceToCelsius(12000.0f) + 5.0f, 12000.0f);
  c.compute();
  c.reset();
  TEST_ASSERT_FALSE(c.calibrated());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Beta, (int)c.method());
  TEST_ASSERT_EQUAL_INT(0, c.pointCount());
}

// Plausible-resistance band over the real 0..110C window (factory 10k/3950/25):
// R(0C) ~= 33618 ohm, R(110C) ~= 529 ohm. Out-of-band -> implausible (probe fault).
void test_plausible_band_window() {
  ThermistorCalibration c; c.begin(kBeta, 0.0f, 110.0f);
  TEST_ASSERT_TRUE(c.plausibleResistance(10000.0f));   // 25C nominal, in band
  TEST_ASSERT_TRUE(c.plausibleResistance(33000.0f));   // just inside cold edge
  TEST_ASSERT_TRUE(c.plausibleResistance(600.0f));     // just inside hot edge
  TEST_ASSERT_FALSE(c.plausibleResistance(240.0f));    // ~140C disconnect: too low
  TEST_ASSERT_FALSE(c.plausibleResistance(50000.0f));  // ~-7C: too high
  TEST_ASSERT_FALSE(c.plausibleResistance(500.0f));    // below R(110C): too low
  TEST_ASSERT_FALSE(c.plausibleResistance(34000.0f));  // above R(0C): too high
}

// The band survives a reset (it is a fixed probe property, not a user calibration).
void test_plausible_band_survives_reset() {
  ThermistorCalibration c; c.begin(kBeta, 0.0f, 110.0f);
  c.addPoint(36.0f, 9000.0f); c.compute();  // a user calibration on top
  c.reset();
  TEST_ASSERT_FALSE(c.plausibleResistance(240.0f));    // still rejects out-of-band
  TEST_ASSERT_TRUE(c.plausibleResistance(10000.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_beta_default);
  RUN_TEST(test_one_point_offset);
  RUN_TEST(test_two_point_beta_refit);
  RUN_TEST(test_three_point_steinhart);
  RUN_TEST(test_compute_zero_points_fails);
  RUN_TEST(test_reset_back_to_beta);
  RUN_TEST(test_plausible_band_window);
  RUN_TEST(test_plausible_band_survives_reset);
  return UNITY_END();
}
