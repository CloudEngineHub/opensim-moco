/* -------------------------------------------------------------------------- *
 * OpenSim Moco: testImplicit.cpp                                             *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */
#define CATCH_CONFIG_MAIN
#include "Testing.h"
#include <Moco/Components/AccelerationMotion.h>
#include <Moco/osimMoco.h>

#include <OpenSim/Actuators/CoordinateActuator.h>
#include <OpenSim/Common/LogManager.h>
#include <OpenSim/Simulation/Model/PhysicalOffsetFrame.h>
#include <OpenSim/Simulation/SimbodyEngine/PinJoint.h>

using namespace OpenSim;
using namespace Catch;

template <typename SolverType>
MocoSolution solveDoublePendulumSwingup(const std::string& dynamics_mode) {

    using SimTK::Vec3;

    MocoStudy moco;
    moco.setName("double_pendulum_swingup_" + dynamics_mode);

    // Define the optimal control problem.
    // ===================================
    MocoProblem& mp = moco.updProblem();

    // Model (dynamics).
    // -----------------
    auto model = ModelFactory::createDoublePendulum();
    Sphere target(0.1);
    target.setColor(SimTK::Red);
    PhysicalOffsetFrame* targetframe = new PhysicalOffsetFrame(
            "targetframe", model.getGround(), SimTK::Transform(Vec3(0, 2, 0)));
    model.updGround().addComponent(targetframe);
    targetframe->attachGeometry(target.clone());

    Sphere start(target);
    PhysicalOffsetFrame* startframe = new PhysicalOffsetFrame(
            "startframe", model.getGround(), SimTK::Transform(Vec3(2, 0, 0)));
    model.updGround().addComponent(startframe);
    start.setColor(SimTK::Green);
    startframe->attachGeometry(start.clone());
    model.finalizeConnections();
    mp.setModelCopy(model);

    // Bounds.
    // -------
    mp.setTimeBounds(0, {0, 5});
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50}, 0, 0);
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10}, 0);
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50}, 0, 0);
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    // Cost.
    // -----
    auto* ftCost = mp.addCost<MocoFinalTimeCost>();
    ftCost->set_weight(0.001);

    auto* endpointCost = mp.addCost<MocoMarkerEndpointCost>("endpoint");
    endpointCost->set_weight(1000.0);
    endpointCost->setPointName("/markerset/marker1");
    endpointCost->setReferenceLocation(SimTK::Vec3(0, 2, 0));

    // Configure the solver.
    // =====================
    int N = 30;
    auto& solver = moco.initSolver<SolverType>();
    solver.set_dynamics_mode(dynamics_mode);
    solver.set_num_mesh_points(N);
    solver.set_transcription_scheme("trapezoidal");
    // solver.set_verbosity(2);

    MocoTrajectory guess = solver.createGuess();
    guess.resampleWithNumTimes(2);
    guess.setTime({0, 1});
    guess.setState("/jointset/j0/q0/value", {0, -SimTK::Pi});
    guess.setState("/jointset/j1/q1/value", {0, 2 * SimTK::Pi});
    guess.setState("/jointset/j0/q0/speed", {0, 0});
    guess.setState("/jointset/j1/q1/speed", {0, 0});
    guess.setControl("/tau0", {0, 0});
    guess.setControl("/tau1", {0, 0});
    guess.resampleWithNumTimes(10);
    solver.setGuess(guess);

    // moco.visualize(guess);

    moco.print("double_pendulum_swingup_" + dynamics_mode + ".omoco");

    // Solve the problem.
    // ==================
    MocoSolution solution = moco.solve();
    // moco.visualize(solution);

    return solution;
}

TEMPLATE_TEST_CASE("Similar solutions between implicit and explicit dynamics",
        "[implicit]", MocoTropterSolver, MocoCasADiSolver) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    GIVEN("solutions to implicit and explicit problems") {

        auto solutionImplicit =
                solveDoublePendulumSwingup<TestType>("implicit");
        // TODO: Solving the implicit problem multiple times gives different
        // results; https://github.com/stanfordnmbl/moco/issues/172.
        // auto solutionImplicit2 = solveDoublePendulumSwingup("implicit");
        // TODO: The solution to this explicit problem changes every time.
        auto solution = solveDoublePendulumSwingup<TestType>("explicit");

        CAPTURE(solutionImplicit.getFinalTime(), solution.getFinalTime());

        const double stateError =
                solutionImplicit.compareContinuousVariablesRMS(
                        solution, {{"states", {}}});

        // There is more deviation in the controls.
        const double controlError =
                solutionImplicit.compareContinuousVariablesRMS(
                        solution, {{"controls", {}}});

        CAPTURE(stateError, controlError);

        // Solutions are approximately equal.
        CHECK(solutionImplicit.getFinalTime() ==
                Approx(solution.getFinalTime()).margin(1e-2));
        CHECK(stateError < 2.0);
        CHECK(controlError < 30.0);

        // Accelerations are correct.
        auto table = solution.exportToStatesTable();
        GCVSplineSet splines(
                table, {"/jointset/j0/q0/speed", "/jointset/j1/q1/speed"});
        OpenSim::Array<double> explicitAccel;
        SimTK::Matrix derivTraj((int)table.getIndependentColumn().size(), 2);
        int i = 0;
        for (const auto& explicitTime : table.getIndependentColumn()) {
            splines.evaluate(explicitAccel, 1, explicitTime);
            derivTraj(i, 0) = explicitAccel[0];
            derivTraj(i, 1) = explicitAccel[1];
            ++i;
        }
        MocoTrajectory explicitWithDeriv(solution.getTime(),
                {{"derivatives",
                        {solutionImplicit.getDerivativeNames(), derivTraj}}});
        const double RMS = solutionImplicit.compareContinuousVariablesRMS(
                explicitWithDeriv, {{"derivatives", {}}});
        CAPTURE(RMS);
        CHECK(RMS < 35.0);
    }
}

TEMPLATE_TEST_CASE("Combining implicit dynamics mode with path constraints",
        "[implicit]", MocoTropterSolver, MocoCasADiSolver) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    class MyPathConstraint : public MocoPathConstraint {
        OpenSim_DECLARE_CONCRETE_OBJECT(MyPathConstraint, MocoPathConstraint);
        void initializeOnModelImpl(
                const Model& model, const MocoProblemInfo&) const override {
            setNumEquations(model.getNumControls());
        }
        void calcPathConstraintErrorsImpl(const SimTK::State& state,
                SimTK::Vector& errors) const override {
            errors = getModel().getControls(state);
        }
    };
    GIVEN("MocoProblem with path constraints") {
        MocoStudy moco;
        auto& prob = moco.updProblem();
        auto model = ModelFactory::createPendulum();
        prob.setTimeBounds(0, 1);
        prob.setModelCopy(model);
        prob.addCost<MocoControlCost>();
        auto* pc = prob.addPathConstraint<MyPathConstraint>();
        MocoConstraintInfo info;
        info.setBounds(std::vector<MocoBounds>(1, {10, 10000}));
        pc->setConstraintInfo(info);
        auto& solver = moco.initSolver<TestType>();
        solver.set_dynamics_mode("implicit");
        const int N = 5;          // mesh points
        const int Nc = 2 * N - 1; // collocation points (Hermite-Simpson)
        solver.set_num_mesh_points(N);
        MocoSolution solution = moco.solve();

        THEN("path constraints are still obeyed") {
            SimTK_TEST_EQ_TOL(solution.getControlsTrajectory(),
                    SimTK::Matrix(Nc, 1, 10.0), 1e-5);
        }
    }
}

TEMPLATE_TEST_CASE("Combining implicit dynamics with kinematic constraints",
        "[implicit]", /*MocoTropterSolver,*/ MocoCasADiSolver) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    GIVEN("MocoProblem with a kinematic constraint") {
        MocoStudy moco;
        auto& prob = moco.updProblem();
        auto model = ModelFactory::createDoublePendulum();
        prob.setTimeBounds(0, 1);
        auto* constraint = new CoordinateCouplerConstraint();
        Array<std::string> names;
        names.append("q0");
        constraint->setIndependentCoordinateNames(names);
        constraint->setDependentCoordinateName("q1");
        LinearFunction func(1.0, 0.0);
        constraint->setFunction(func);
        model.addConstraint(constraint);
        prob.setModelCopy(model);
        auto& solver = moco.initSolver<TestType>();
        solver.set_dynamics_mode("implicit");
        solver.set_num_mesh_points(5);
        solver.set_transcription_scheme("hermite-simpson");
        solver.set_enforce_constraint_derivatives(true);
        MocoSolution solution = moco.solve();

        THEN("kinematic constraint is still obeyed") {
            const auto q0value = solution.getStatesTrajectory().col(0);
            const auto q1value = solution.getStatesTrajectory().col(1);
            SimTK_TEST_EQ_TOL(q0value, q1value, 1e-6);
        }
    }
}

SCENARIO("Using MocoTrajectory with the implicit dynamics mode",
        "[implicit][iterate]") {
    GIVEN("MocoTrajectory with only derivatives") {
        MocoTrajectory iterate;
        const_cast<SimTK::Matrix*>(&iterate.getDerivativesTrajectory())
                ->resize(3, 2);
        THEN("it is not empty") { REQUIRE(!iterate.empty()); }
    }
    GIVEN("MocoTrajectory with only derivative names") {
        MocoTrajectory iterate;
        const_cast<std::vector<std::string>*>(&iterate.getDerivativeNames())
                ->resize(3);
        THEN("it is not empty") { REQUIRE(!iterate.empty()); }
    }
    GIVEN("MocoTrajectory with derivative data") {
        MocoTrajectory iter(createVectorLinspace(6, 0, 1), {}, {}, {},
                {"a", "b"}, {}, {}, {}, {}, {6, 2, 0.5}, {});
        WHEN("calling setNumTimes()") {
            REQUIRE(iter.getDerivativesTrajectory().nrow() != 4);
            iter.setNumTimes(4);
            THEN("setNumTimes() changes number of rows of derivatives") {
                REQUIRE(iter.getDerivativesTrajectory().nrow() == 4);
            }
        }
        WHEN("deserializing") {
            const std::string filename = "testImplicit_MocoTrajectory.sto";
            iter.write(filename);
            THEN("derivatives trajectory is preserved") {
                MocoTrajectory deserialized(filename);
                REQUIRE(iter.getDerivativesTrajectory().nrow() == 6);
                REQUIRE(iter.isNumericallyEqual(deserialized));
            }
        }
    }
    GIVEN("two MocoTrajectorys with different derivative data") {
        const double valueA = 0.5;
        const double valueB = 0.499999;
        MocoTrajectory iterA(createVectorLinspace(6, 0, 1), {}, {}, {},
                {"a", "b"}, {}, {}, {}, {}, {6, 2, valueA}, {});
        MocoTrajectory iterB(createVectorLinspace(6, 0, 1), {}, {}, {},
                {"a", "b"}, {}, {}, {}, {}, {6, 2, valueB}, {});
        THEN("not numerically equal") {
            REQUIRE(!iterA.isNumericallyEqual(iterB));
        }
        THEN("RMS error is computed correctly") {
            REQUIRE(iterA.compareContinuousVariablesRMS(iterB) ==
                    Approx(valueA - valueB));
        }
    }
}

TEST_CASE("AccelerationMotion") {
    Model model = OpenSim::ModelFactory::createNLinkPendulum(1);
    AccelerationMotion* accel = new AccelerationMotion("motion");
    model.addModelComponent(accel);
    auto state = model.initSystem();
    state.updQ()[0] = -SimTK::Pi / 2;
    model.realizeAcceleration(state);
    // Default.
    CHECK(state.getUDot()[0] == Approx(0).margin(1e-10));

    // Enable.
    accel->setEnabled(state, true);
    SimTK::Vector udot(1);
    udot[0] = SimTK::Random::Uniform(-1, 1).getValue();
    accel->setUDot(state, udot);
    model.realizeAcceleration(state);
    CHECK(state.getUDot()[0] == Approx(udot[0]).margin(1e-10));

    // Disable.
    accel->setEnabled(state, false);
    model.realizeAcceleration(state);
    CHECK(state.getUDot()[0] == Approx(0).margin(1e-10));
}

class MyAuxiliaryImplicitDynamics : public Component {
    OpenSim_DECLARE_CONCRETE_OBJECT(MyAuxiliaryImplicitDynamics, Component);

public:
    OpenSim_DECLARE_PROPERTY(default_fiber_length, double, "");
    OpenSim_DECLARE_OUTPUT(implicitresidual_fiber_length, double,
            getImplicitResidualFiberLength, SimTK::Stage::Dynamics);
    MyAuxiliaryImplicitDynamics() {
        constructProperty_default_fiber_length(1.0);
    }
    double getImplicitResidualFiberLength(const SimTK::State& s) const {
        // TODO: Use index instead.
        if (!isCacheVariableValid(s, "implicitresidual_fiber_equilibrium")) {
            const double fiberVel =
                    getDiscreteVariableValue(s, "implicitderiv_fiber_length");
            const double fiberLength = getStateVariableValue(s, "fiber_length");
            // y y' = 1
            double residual = fiberVel * fiberLength - 1;
            setCacheVariableValue(
                    s, "implicitresidual_fiber_equilibrium", residual);
            markCacheVariableValid(s, "implicitresidual_fiber_equilibrium");
        }
        return getCacheVariableValue<double>(
                s, "implicitresidual_fiber_equilibrium");
    }

private:
    void extendInitStateFromProperties(SimTK::State& s) const override {
        Super::extendInitStateFromProperties(s);
        setStateVariableValue(s, "fiber_length", get_default_fiber_length());
    }
    void extendSetPropertiesFromState(const SimTK::State& s) override {
        Super::extendSetPropertiesFromState(s);
        set_default_fiber_length(getStateVariableValue(s, "fiber_length"));
    }
    void computeStateVariableDerivatives(const SimTK::State& s) const override {
        const double fiberLength = getStateVariableValue(s, "fiber_length");
        setStateVariableDerivativeValue(s, "fiber_length", 1.0 / fiberLength);
    }
    void extendAddToSystem(SimTK::MultibodySystem& system) const override {
        Super::extendAddToSystem(system);
        addStateVariable("fiber_length");
        addDiscreteVariable(
                "implicitderiv_fiber_length", SimTK::Stage::Velocity);
        addCacheVariable("implicitresidual_fiber_length", double(0),
                SimTK::Stage::Dynamics);
    }
};

TEST_CASE("Auxiliary implicit dynamics") {
    SECTION("Time stepping") {
        Model model;
        model.addComponent(new MyAuxiliaryImplicitDynamics());
        auto initState = model.initSystem();
        Manager manager(model, initState);
        auto finalState = manager.integrate(1.0);
        std::cout << "DEBUG " << finalState.getY() << std::endl;
    }
    SECTION("Direct collocation implicit") {
        MocoStudy study;
        auto& problem = study.updProblem();
        auto model = OpenSim::make_unique<Model>();
        model->addComponent(new MyAuxiliaryImplicitDynamics());
        problem.setModel(std::move(model));
        problem.setTimeBounds(0, 1);
        problem.setStateInfo("fiber_length", {}, 1.0);
        auto solution = study.solve();
    }
}

// class MyAuxiliaryPathConstraint : public ScalarActuator {
//     OpenSim_DECLARE_CONCRETE_OBJECT(MyAuxiliaryPathConstraint,
//     ScalarActuator);
//
// public:
//     MyAuxiliaryPathConstraint() {
//         constructProperty_explicit_activation_dynamics(true);
//         constructProperty_default_activation(0.5);
//         constructProperty_activation_time_constant(0.015);
//         constructProperty_deactivation_time_constant(0.060);
//     }
//     OpenSim_DECLARE_PROPERTY(explicit_activation_dynamics, bool, "");
//     OpenSim_DECLARE_PROPERTY(default_activation, double, "");
//     OpenSim_DECLARE_PROPERTY(activation_time_constant, double,
//             "Smaller value means activation can change more rapidly (units: "
//             "seconds).");
//     OpenSim_DECLARE_PROPERTY(deactivation_time_constant, double,
//             "Smaller value means activation can decrease more rapidly "
//             "(units: seconds).");
//
//     OpenSim_DECLARE_OUTPUT(pathconstraint_value_activationratemin, double,
//             getActivationRateMinValue, SimTK::Stage::Velocity);
//     OpenSim_DECLARE_OUTPUT(pathconstraint_lower_activationratemin, double,
//             getActivationRateMinLowerBound, SimTK::Stage::Model);
//     OpenSim_DECLARE_OUTPUT(pathconstraint_upper_activationratemin, double,
//             getActivationRateMinUpperBound, SimTK::Stage::Model);
//
//     // TODO create a Vec3 output instead.
//     OpenSim_DECLARE_OUTPUT(pathconstraint_value_activationratemax, double,
//             getActivationRateMaxValue, SimTK::Stage::Velocity);
//     OpenSim_DECLARE_OUTPUT(pathconstraint_lower_activationratemax, double,
//             getActivationRateMaxLowerBound, SimTK::Stage::Model);
//     OpenSim_DECLARE_OUTPUT(pathconstraint_upper_activationratemax, double,
//             getActivationRateMaxUpperBound, SimTK::Stage::Model);
//
//     OpenSim_DECLARE_OUTPUT(
//             excitation, double, getExcitation, SimTK::Stage::Dynamics);
//
//     // TODO: Meaning of the control variable changes if using implicit
//     // activation dynamics.
//     double getActivationRateMinValue(const SimTK::State& s) const {
//         const auto& adot = getControl(s);
//         const auto& a = getStateVariableValue(s, "activation");
//         const auto& taud = get_deactivation_time_constant();
//         return adot + a / taud;
//     }
//     // TODO assume these are constant.
//     double getActivationRateMinLowerBound(const SimTK::State&) const {
//         return 0;
//     }
//     double getActivationRateMinUpperBound(const SimTK::State&) const {
//         return SimTK::Infinity;
//     }
//
//     double getActivationRateMaxValue(const SimTK::State& s) const {
//         const auto& adot = getControl(s);
//         const auto& a = getStateVariableValue(s, "activation");
//         const auto& taua = get_activation_time_constant();
//         return adot + a / taua;
//     }
//     double getActivationRateMaxLowerBound(const SimTK::State&) const {
//         return -SimTK::Infinity;
//     }
//     double getActivationRateMaxUpperBound(const SimTK::State&) const {
//         const auto& taua = get_activation_time_constant();
//         return 1.0 / taua;
//     }
//
//     double getExcitation(const SimTK::State& s) const {
//         if (get_explicit_activation_dynamics()) return getControl(s);
//
//         const auto& adot = getControl(s);
//         const auto& a = getStateVariableValue(s, "activation");
//         const auto& taud = get_deactivation_time_constant();
//         if (adot <= 0) {
//             return taud * adot + a;
//         } else {
//             const auto& taua = get_activation_time_constant();
//             const double c1 = 1.0 / taua - 1 / taud;
//             const double c2 = 1.0 / taud;
//             const double D = SimTK::square(c2 + a * c1) + 4 * c1 * adot;
//             return (a * c1 - c2 + sqrt(D)) / (2 * c1);
//         }
//     }
//
// private:
//     void extendInitStateFromProperties(SimTK::State& s) const override {
//         Super::extendInitStateFromProperties(s);
//         setStateVariableValue(s, "activation", get_default_activation());
//     }
//     void extendSetPropertiesFromState(const SimTK::State& s) override {
//         Super::extendSetPropertiesFromState(s);
//         set_default_activation(getStateVariableValue(s, "activation"));
//     }
//     void computeStateVariableDerivatives(const SimTK::State& s) const
//     override {
//         // TODO: avoid calculating this if not explicit.
//         // TODO: how to handle the Z slot for states we are handling
//         implicitly?
//         // TODO: handle all auxiliary states implicitly!
//         if (get_explicit_activation_dynamics()) {
//             const auto& u = getControl(s);
//             const auto& a = getStateVariableValue(s, "activation");
//             const auto& taud = get_deactivation_time_constant();
//             const auto& taua = get_activation_time_constant();
//             SimTK::Real adot = u - a;
//             // De Groote 2009 (Raasch 1997)
//             if (u >= a) {
//                 adot *= u / taua + (1 - u) / taud;
//             } else {
//                 adot *= 1.0 / taud;
//             }
//             setStateVariableDerivativeValue(s, "activation", adot);
//         }
//     }
//     void extendAddToSystem(SimTK::MultibodySystem& system) const override {
//         Super::extendAddToSystem(system);
//         addStateVariable("activation");
//     }
// };
