/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    testImuFactor.cpp
 * @brief   Unit test for ImuFactor
 * @author  Krunal Chande, Luca Carlone
 */

#include <gtsam/navigation/AHRSFactor.h>
#include <gtsam/nonlinear/Values.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/navigation/ImuBias.h>
#include <gtsam/base/LieVector.h>
#include <gtsam/base/TestableAssertions.h>
#include <gtsam/base/numericalDerivative.h>
#include <CppUnitLite/TestHarness.h>

#include <boost/bind.hpp>
#include <list>

using namespace std;
using namespace gtsam;

// Convenience for named keys
using symbol_shorthand::X;
using symbol_shorthand::V;
using symbol_shorthand::B;

/* ************************************************************************* */
namespace {
Vector callEvaluateError(const AHRSFactor& factor, const Rot3 rot_i,
    const Rot3 rot_j, const imuBias::ConstantBias& bias) {
  return factor.evaluateError(rot_i, rot_j, bias);
}

Rot3 evaluateRotationError(const AHRSFactor& factor, const Rot3 rot_i,
    const Rot3 rot_j, const imuBias::ConstantBias& bias) {
  return Rot3::Expmap(factor.evaluateError(rot_i, rot_j, bias).tail(3));
}

AHRSFactor::PreintegratedMeasurements evaluatePreintegratedMeasurements(
    const imuBias::ConstantBias& bias,
    const list<Vector3>& measuredOmegas,
    const list<double>& deltaTs,
    const Vector3& initialRotationRate = Vector3(0.0, 0.0, 0.0)) {
  AHRSFactor::PreintegratedMeasurements result(bias, Matrix3::Identity());

  list<Vector3>::const_iterator itOmega = measuredOmegas.begin();
  list<double>::const_iterator itDeltaT = deltaTs.begin();
  for (; itOmega != measuredOmegas.end(); ++itOmega, ++itDeltaT) {
    result.integrateMeasurement(*itOmega, *itDeltaT);
  }

  return result;
}

Rot3 evaluatePreintegratedMeasurementsRotation(
    const imuBias::ConstantBias& bias,
    const list<Vector3>& measuredOmegas,
    const list<double>& deltaTs,
    const Vector3& initialRotationRate = Vector3(0.0, 0.0, 0.0)) {
  return evaluatePreintegratedMeasurements(bias, measuredOmegas,
      deltaTs, initialRotationRate).deltaRij_;
}

Rot3 evaluateRotation(const Vector3 measuredOmega, const Vector3 biasOmega,
    const double deltaT) {
  return Rot3::Expmap((measuredOmega - biasOmega) * deltaT);
}

Vector3 evaluateLogRotation(const Vector3 thetahat, const Vector3 deltatheta) {
  return Rot3::Logmap(Rot3::Expmap(thetahat).compose(Rot3::Expmap(deltatheta)));
}

}
/* ************************************************************************* */
TEST( AHRSFactor, PreintegratedMeasurements ) {
  // Linearization point
  imuBias::ConstantBias bias(Vector3(0, 0, 0), Vector3(0, 0, 0)); ///< Current estimate of acceleration and angular rate biases

  // Measurements
  Vector3 measuredOmega(M_PI / 100.0, 0.0, 0.0);
  double deltaT = 0.5;

  // Expected preintegrated values
  Rot3 expectedDeltaR1 = Rot3::RzRyRx(0.5 * M_PI / 100.0, 0.0, 0.0);
  double expectedDeltaT1(0.5);

  // Actual preintegrated values
  AHRSFactor::PreintegratedMeasurements actual1(bias, Matrix3::Zero());
  actual1.integrateMeasurement(measuredOmega, deltaT);

  EXPECT(assert_equal(expectedDeltaR1, actual1.deltaRij_, 1e-6));
  DOUBLES_EQUAL(expectedDeltaT1, actual1.deltaTij_, 1e-6);

  // Integrate again
  Rot3 expectedDeltaR2 = Rot3::RzRyRx(2.0 * 0.5 * M_PI / 100.0, 0.0, 0.0);
  double expectedDeltaT2(1);

  // Actual preintegrated values
  AHRSFactor::PreintegratedMeasurements actual2 = actual1;
  actual2.integrateMeasurement(measuredOmega, deltaT);

  EXPECT(assert_equal(expectedDeltaR2, actual2.deltaRij_, 1e-6));
  DOUBLES_EQUAL(expectedDeltaT2, actual2.deltaTij_, 1e-6);
}

/* ************************************************************************* */
TEST( ImuFactor, Error ) {
  // Linearization point
  imuBias::ConstantBias bias; // Bias
  Rot3 x1(Rot3::RzRyRx(M_PI / 12.0, M_PI / 6.0, M_PI / 4.0));
  Rot3 x2(Rot3::RzRyRx(M_PI / 12.0 + M_PI / 100.0, M_PI / 6.0, M_PI / 4.0));

  // Measurements
  Vector3 gravity;
  gravity << 0, 0, 9.81;
  Vector3 omegaCoriolis;
  omegaCoriolis << 0, 0, 0;
  Vector3 measuredOmega;
  measuredOmega << M_PI / 100, 0, 0;
  double deltaT = 1.0;
  AHRSFactor::PreintegratedMeasurements pre_int_data(bias, Matrix3::Zero());
  pre_int_data.integrateMeasurement(measuredOmega, deltaT);

  // Create factor
  AHRSFactor factor(X(1), X(2), B(1), pre_int_data, omegaCoriolis,
      false);

  Vector errorActual = factor.evaluateError(x1, x2, bias);

  // Expected error
  Vector errorExpected(3);
  errorExpected << 0, 0, 0;
  EXPECT(assert_equal(errorExpected, errorActual, 1e-6));

  // Expected Jacobians
  Matrix H1e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, _1, x2, bias), x1);
  Matrix H2e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, x1, _1, bias), x2);
  Matrix H3e = numericalDerivative11<Vector, imuBias::ConstantBias>(
      boost::bind(&callEvaluateError, factor, x1, x2, _1), bias);

  // Check rotation Jacobians
  Matrix RH1e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, _1, x2, bias), x1);
  Matrix RH2e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, x1, _1, bias), x2);

  // Actual Jacobians
  Matrix H1a, H2a, H3a;
  (void) factor.evaluateError(x1, x2, bias, H1a, H2a, H3a);

  // rotations
  EXPECT(assert_equal(RH1e, H1a, 1e-5)); // 1e-5 needs to be added only when using quaternions for rotations

  EXPECT(assert_equal(H2e, H2a, 1e-5));

  // rotations
  EXPECT(assert_equal(RH2e, H2a, 1e-5)); // 1e-5 needs to be added only when using quaternions for rotations

  EXPECT(assert_equal(H3e, H3a, 1e-5)); // FIXME!! DOes not work. Different matrix dimensions.
}

/* ************************************************************************* */
TEST( ImuFactor, ErrorWithBiases ) {
  // Linearization point
//  Vector bias(6); bias << 0.2, 0, 0, 0.1, 0, 0; // Biases (acc, rot)
//  Pose3 x1(Rot3::RzRyRx(M_PI/12.0, M_PI/6.0, M_PI/4.0), Point3(5.0, 1.0, -50.0));
//  LieVector v1((Vector(3) << 0.5, 0.0, 0.0));
//  Pose3 x2(Rot3::RzRyRx(M_PI/12.0 + M_PI/10.0, M_PI/6.0, M_PI/4.0), Point3(5.5, 1.0, -50.0));
//  LieVector v2((Vector(3) << 0.5, 0.0, 0.0));

  imuBias::ConstantBias bias(Vector3(0.2, 0, 0), Vector3(0, 0, 0.3)); // Biases (acc, rot)
  Rot3 x1(Rot3::Expmap(Vector3(0, 0, M_PI / 4.0)));
  Rot3 x2(Rot3::Expmap(Vector3(0, 0, M_PI / 4.0 + M_PI / 10.0)));

  // Measurements
  Vector3 omegaCoriolis;
  omegaCoriolis << 0, 0.1, 0.1;
  Vector3 measuredOmega;
  measuredOmega << 0, 0, M_PI / 10.0 + 0.3;
  double deltaT = 1.0;

  AHRSFactor::PreintegratedMeasurements pre_int_data(
      imuBias::ConstantBias(Vector3(0.2, 0.0, 0.0), Vector3(0.0, 0.0, 0.0)), Matrix3::Zero());
  pre_int_data.integrateMeasurement(measuredOmega, deltaT);

//  ImuFactor::PreintegratedMeasurements pre_int_data(bias.head(3), bias.tail(3));
//    pre_int_data.integrateMeasurement(measuredAcc, measuredOmega, deltaT);

  // Create factor
  AHRSFactor factor(X(1), X(2), B(1), pre_int_data, omegaCoriolis);

  SETDEBUG("ImuFactor evaluateError", false);
  Vector errorActual = factor.evaluateError(x1, x2, bias);
  SETDEBUG("ImuFactor evaluateError", false);

  // Expected error
  Vector errorExpected(3);
  errorExpected << 0, 0, 0;
//    EXPECT(assert_equal(errorExpected, errorActual, 1e-6));

  // Expected Jacobians
  Matrix H1e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, _1, x2, bias), x1);
  Matrix H2e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, x1, _1, bias), x2);
  Matrix H3e = numericalDerivative11<Vector, imuBias::ConstantBias>(
      boost::bind(&callEvaluateError, factor, x1, x2, _1), bias);

  // Check rotation Jacobians
  Matrix RH1e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, _1, x2, bias), x1);
  Matrix RH2e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, x1, _1, bias), x2);
  Matrix RH3e = numericalDerivative11<Rot3, imuBias::ConstantBias>(
      boost::bind(&evaluateRotationError, factor, x1, x2, _1), bias);

  // Actual Jacobians
  Matrix H1a, H2a, H3a;
  (void) factor.evaluateError(x1, x2, bias, H1a, H2a, H3a);

  EXPECT(assert_equal(H1e, H1a));
  EXPECT(assert_equal(H2e, H2a));
    EXPECT(assert_equal(H3e, H3a));
}

/* ************************************************************************* */
TEST( AHRSFactor, PartialDerivativeExpmap ) {
  // Linearization point
  Vector3 biasOmega;
  biasOmega << 0, 0, 0; ///< Current estimate of rotation rate bias

  // Measurements
  Vector3 measuredOmega;
  measuredOmega << 0.1, 0, 0;
  double deltaT = 0.5;

  // Compute numerical derivatives
  Matrix expectedDelRdelBiasOmega = numericalDerivative11<Rot3, Vector3>(
      boost::bind(&evaluateRotation, measuredOmega, _1, deltaT),
      biasOmega);

  const Matrix3 Jr = Rot3::rightJacobianExpMapSO3(
      (measuredOmega - biasOmega) * deltaT);

  Matrix3 actualdelRdelBiasOmega = -Jr * deltaT; // the delta bias appears with the minus sign

  // Compare Jacobians
  EXPECT(assert_equal(expectedDelRdelBiasOmega, actualdelRdelBiasOmega, 1e-3)); // 1e-3 needs to be added only when using quaternions for rotations

}

/* ************************************************************************* */
TEST( AHRSFactor, PartialDerivativeLogmap ) {
  // Linearization point
  Vector3 thetahat;
  thetahat << 0.1, 0.1, 0; ///< Current estimate of rotation rate bias

  // Measurements
  Vector3 deltatheta;
  deltatheta << 0, 0, 0;

  // Compute numerical derivatives
  Matrix expectedDelFdeltheta = numericalDerivative11<Vector3, Vector3>(
      boost::bind(&evaluateLogRotation, thetahat, _1), deltatheta);

  const Vector3 x = thetahat; // parametrization of so(3)
  const Matrix3 X = skewSymmetric(x); // element of Lie algebra so(3): X = x^
  double normx = norm_2(x);
  const Matrix3 actualDelFdeltheta = Matrix3::Identity() + 0.5 * X
      + (1 / (normx * normx) - (1 + cos(normx)) / (2 * normx * sin(normx))) * X
          * X;

//  std::cout << "actualDelFdeltheta" << actualDelFdeltheta << std::endl;
//  std::cout << "expectedDelFdeltheta" << expectedDelFdeltheta << std::endl;

  // Compare Jacobians
  EXPECT(assert_equal(expectedDelFdeltheta, actualDelFdeltheta));

}

/* ************************************************************************* */
TEST( AHRSFactor, fistOrderExponential ) {
  // Linearization point
  Vector3 biasOmega;
  biasOmega << 0, 0, 0; ///< Current estimate of rotation rate bias

  // Measurements
  Vector3 measuredOmega;
  measuredOmega << 0.1, 0, 0;
  double deltaT = 1.0;

  // change w.r.t. linearization point
  double alpha = 0.0;
  Vector3 deltabiasOmega;
  deltabiasOmega << alpha, alpha, alpha;

  const Matrix3 Jr = Rot3::rightJacobianExpMapSO3(
      (measuredOmega - biasOmega) * deltaT);

  Matrix3 delRdelBiasOmega = -Jr * deltaT; // the delta bias appears with the minus sign

  const Matrix expectedRot = Rot3::Expmap(
      (measuredOmega - biasOmega - deltabiasOmega) * deltaT).matrix();

  const Matrix3 hatRot =
      Rot3::Expmap((measuredOmega - biasOmega) * deltaT).matrix();
  const Matrix3 actualRot = hatRot
      * Rot3::Expmap(delRdelBiasOmega * deltabiasOmega).matrix();
  //hatRot * (Matrix3::Identity() + skewSymmetric(delRdelBiasOmega * deltabiasOmega));

  // Compare Jacobians
  EXPECT(assert_equal(expectedRot, actualRot));
}

/* ************************************************************************* */
TEST( AHRSFactor, FirstOrderPreIntegratedMeasurements ) {
  // Linearization point
  imuBias::ConstantBias bias; ///< Current estimate of acceleration and rotation rate biases

  Pose3 body_P_sensor(Rot3::Expmap(Vector3(0, 0.1, 0.1)), Point3(1, 0, 1));

  // Measurements
  list<Vector3> measuredOmegas;
  list<double> deltaTs;
  measuredOmegas.push_back(Vector3(M_PI / 100.0, 0.0, 0.0));
  deltaTs.push_back(0.01);
  measuredOmegas.push_back(Vector3(M_PI / 100.0, 0.0, 0.0));
  deltaTs.push_back(0.01);
  for (int i = 1; i < 100; i++) {
    measuredOmegas.push_back(
        Vector3(M_PI / 100.0, M_PI / 300.0, 2 * M_PI / 100.0));
    deltaTs.push_back(0.01);
  }

  // Actual preintegrated values
  AHRSFactor::PreintegratedMeasurements preintegrated =
      evaluatePreintegratedMeasurements(bias, measuredOmegas,
          deltaTs, Vector3(M_PI / 100.0, 0.0, 0.0));

  // Compute numerical derivatives
  Matrix expectedDelRdelBias =
      numericalDerivative11<Rot3, imuBias::ConstantBias>(
          boost::bind(&evaluatePreintegratedMeasurementsRotation, _1,
              measuredOmegas, deltaTs,
              Vector3(M_PI / 100.0, 0.0, 0.0)), bias);
  Matrix expectedDelRdelBiasAcc = expectedDelRdelBias.leftCols(3);
  Matrix expectedDelRdelBiasOmega = expectedDelRdelBias.rightCols(3);

  // Compare Jacobians
  EXPECT(assert_equal(expectedDelRdelBiasAcc, Matrix::Zero(3, 3)));
  EXPECT(
      assert_equal(expectedDelRdelBiasOmega, preintegrated.delRdelBiasOmega,
          1e-3)); // 1e-3 needs to be added only when using quaternions for rotations
}

#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>

/* ************************************************************************* */
TEST( AHRSFactor, ErrorWithBiasesAndSensorBodyDisplacement ) {

  imuBias::ConstantBias bias(Vector3(0.2, 0, 0), Vector3(0, 0, 0.3)); // Biases (acc, rot)
  Rot3 x1(Rot3::Expmap(Vector3(0, 0, M_PI / 4.0)));
  Rot3 x2(Rot3::Expmap(Vector3(0, 0, M_PI / 4.0 + M_PI / 10.0)));

  // Measurements
  Vector3 gravity;
  gravity << 0, 0, 9.81;
  Vector3 omegaCoriolis;
  omegaCoriolis << 0, 0.1, 0.1;
  Vector3 measuredOmega;
  measuredOmega << 0, 0, M_PI / 10.0 + 0.3;
  double deltaT = 1.0;

  const Pose3 body_P_sensor(Rot3::Expmap(Vector3(0, 0.10, 0.10)),
      Point3(1, 0, 0));

//  ImuFactor::PreintegratedMeasurements pre_int_data(imuBias::ConstantBias(Vector3(0.2, 0.0, 0.0),
//        Vector3(0.0, 0.0, 0.0)), Matrix3::Zero(), Matrix3::Zero(), Matrix3::Zero(), measuredOmega);

  AHRSFactor::PreintegratedMeasurements pre_int_data(
      imuBias::ConstantBias(Vector3(0.2, 0.0, 0.0), Vector3(0.0, 0.0, 0.0)),
      Matrix3::Zero());

  pre_int_data.integrateMeasurement(measuredOmega, deltaT);

  // Create factor
  AHRSFactor factor(X(1), X(2), B(1), pre_int_data, omegaCoriolis);

  // Expected Jacobians
  Matrix H1e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, _1, x2, bias), x1);
  Matrix H2e = numericalDerivative11<Vector, Rot3>(
      boost::bind(&callEvaluateError, factor, x1, _1, bias), x2);
  Matrix H3e = numericalDerivative11<Vector, imuBias::ConstantBias>(
      boost::bind(&callEvaluateError, factor, x1, x2, _1), bias);

  // Check rotation Jacobians
  Matrix RH1e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, _1, x2, bias), x1);
  Matrix RH2e = numericalDerivative11<Rot3, Rot3>(
      boost::bind(&evaluateRotationError, factor, x1, _1, bias), x2);
  Matrix RH3e = numericalDerivative11<Rot3, imuBias::ConstantBias>(
      boost::bind(&evaluateRotationError, factor, x1, x2, _1), bias);

  // Actual Jacobians
  Matrix H1a, H2a, H3a;
  (void) factor.evaluateError(x1, x2, bias, H1a, H2a, H3a);

  EXPECT(assert_equal(H1e, H1a));
  EXPECT(assert_equal(H2e, H2a));
    EXPECT(assert_equal(H3e, H3a));
}

#include <gtsam/linear/GaussianFactorGraph.h>
#include <gtsam/nonlinear/Marginals.h>

using namespace std;
TEST (AHRSFactor, graphTest) {
  // linearization point
  Rot3 x1(Rot3::RzRyRx(0, 0, 0));
  Rot3 x2(Rot3::RzRyRx(0, M_PI/4, 0));
  imuBias::ConstantBias bias(Vector3(0, 0, 0), Vector3(0, 0, 0));

  // PreIntegrator
  imuBias::ConstantBias biasHat(Vector3(0, 0, 0), Vector3(0, 0, 0));
  Vector3 gravity;
  gravity << 0, 0, 9.81;
  Vector3 omegaCoriolis;
  omegaCoriolis << 0, 0, 0;
  AHRSFactor::PreintegratedMeasurements pre_int_data(biasHat,
      Matrix3::Identity());

  // Pre-integrate measurements
  Vector3 measuredAcc(0.0, 0.0, 0.0);
  Vector3 measuredOmega(0, M_PI/20, 0);
  double deltaT = 1;
//  pre_int_data.integrateMeasurement(measuredAcc, measuredOmega, deltaT);

  // Create Factor
  noiseModel::Base::shared_ptr model = noiseModel::Gaussian::Covariance(pre_int_data.PreintMeasCov);
//  cout<<"model: \n"<<noiseModel::Gaussian::Covariance(pre_int_data.PreintMeasCov)<<endl;
//  cout<<"pre int measurement cov: \n "<<pre_int_data.PreintMeasCov<<endl;
  NonlinearFactorGraph graph;
  Values values;
  for(size_t i = 0; i < 5; ++i) {
    pre_int_data.integrateMeasurement(measuredOmega, deltaT);
  }
//  pre_int_data.print("Pre integrated measurementes");
  AHRSFactor factor(X(1), X(2), B(1), pre_int_data, omegaCoriolis);
  values.insert(X(1), x1);
  values.insert(X(2), x2);
  values.insert(B(1), bias);
  graph.push_back(factor);
//  values.print();
  LevenbergMarquardtOptimizer optimizer(graph, values);
  Values result = optimizer.optimize();
//  result.print("Final Result:\n");
//  cout<<"******************************\n";
//  cout<<"result.at(X(2)): \n"<<result.at<Rot3>(X(2)).ypr()*(180/M_PI);
  Rot3 expectedRot(Rot3::RzRyRx(0, M_PI/4, 0));
  EXPECT(assert_equal(expectedRot, result.at<Rot3>(X(2))));
//  Marginals marginals(graph, result);
//  cout << "x1 covariance:\n" << marginals.marginalCovariance(X(1)) << endl;
//  cout << "x2 covariance:\n" << marginals.marginalCovariance(X(2)) << endl;

}

/* ************************************************************************* */
int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}
/* ************************************************************************* */
