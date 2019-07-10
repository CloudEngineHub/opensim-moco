/* -------------------------------------------------------------------------- *
 * OpenSim Moco: testConstraints.cpp                                          *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Nicholas Bianco                                                 *
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
using Catch::Contains;
#include <Moco/osimMoco.h>

#include <simbody/internal/Constraint.h>
#include <simbody/internal/Constraint_Ball.h>

#include <OpenSim/Actuators/CoordinateActuator.h>
#include <OpenSim/Actuators/PointActuator.h>
#include <OpenSim/Common/Constant.h>
#include <OpenSim/Common/GCVSpline.h>
#include <OpenSim/Common/LinearFunction.h>
#include <OpenSim/Common/LogManager.h>
#include <OpenSim/Common/Sine.h>
#include <OpenSim/Common/TimeSeriesTable.h>
#include <OpenSim/Simulation/osimSimulation.h>

using namespace OpenSim;
using SimTK::State;
using SimTK::UnitVec3;
using SimTK::Vec3;
using SimTK::Vector;

const int NUM_BODIES = 10;
const double BOND_LENGTH = 0.5;

/// Keep constraints satisfied to this tolerance during testing.
static const double ConstraintTol = 1e-10;

/// Compare two quantities that should have been calculated to machine tolerance
/// given the problem size, which we'll characterize by the number of mobilities
/// (borrowed from Simbody's 'testConstraints.cpp').
#define MACHINE_TEST(a, b) SimTK_TEST_EQ_SIZE(a, b, 10 * state.getNU())

TEST_CASE("(Dummy test to support discovery in Resharper)") { REQUIRE(true); }

/// Create a model consisting of a chain of bodies. This model is nearly
/// identical to the model implemented in Simbody's 'testConstraints.cpp'.
Model createModel() {
    Model model;
    const SimTK::Real mass = 1.23;
    Body* body = new Body("body0", mass, SimTK::Vec3(0.1, 0.2, -0.03),
            mass * SimTK::UnitInertia(1.1, 1.2, 1.3, 0.01, -0.02, 0.07));
    model.addBody(body);

    GimbalJoint* joint = new GimbalJoint("joint0", model.getGround(),
            Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1), *body,
            Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
    model.addJoint(joint);

    for (int i = 1; i < NUM_BODIES; ++i) {
        Body& parent = model.getBodySet().get(model.getNumBodies() - 1);

        std::string bodyName = "body" + std::to_string(i + 1);
        Body* body = new Body(bodyName, mass, SimTK::Vec3(0.1, 0.2, -0.03),
                mass * SimTK::UnitInertia(1.1, 1.2, 1.3, 0.01, -0.02, 0.07));
        model.addBody(body);

        std::string jointName = "joint" + std::to_string(i + 1);
        if (i == NUM_BODIES - 5) {
            UniversalJoint* joint = new UniversalJoint(jointName, parent,
                    Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1), *body,
                    Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        } else if (i == NUM_BODIES - 3) {
            BallJoint* joint = new BallJoint(jointName, parent,
                    Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1), *body,
                    Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        } else {
            GimbalJoint* joint = new GimbalJoint(jointName, parent,
                    Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1), *body,
                    Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        }
    }

    model.finalizeConnections();

    return model;
}

/// Create a random state for the model. This implementation mimics the random
/// state creation in Simbody's 'testConstraints.cpp'.
void createState(
        Model& model, State& state, const Vector& qOverride = Vector()) {
    state = model.initSystem();
    SimTK::Random::Uniform random;
    for (int i = 0; i < state.getNY(); ++i) state.updY()[i] = random.getValue();
    if (qOverride.size()) state.updQ() = qOverride;
    model.realizeVelocity(state);

    model.updMultibodySystem().project(state, ConstraintTol);
    model.realizeAcceleration(state);
}

/// Get model accelerations given the constraint multipliers. This calculation
/// is necessary for computing constraint defects associated with the system
/// dynamics, represented by the equations
///
///     M udot + G^T lambda + f_inertial(q,u) = f_applied
///
/// If using an explicit representation of the system dynamics, the derivatives
/// of the generalized speeds for the system need to be computed in order to
/// construct the defects. Rearranging the equations above (and noting that
/// Simbody does not actually invert the mass matrix, but rather uses an order-N
/// approach), we obtain
///
///     udot = M_inv (f_applied - f_inertial(q,u) - G^T lambda)
///          = f(q, u, lambda)
///
/// where,
///              q | generalized coordinates
///              u | generalized speeds
///         lambda | Lagrange multipliers
///
/// Since the three quantities required to compute the system accelerations
/// will eventually become NLP variables in a direct collocation problem, it is
/// not sufficient to use the internally calculated Lagrange multipliers in
/// Simbody. An intermediate calculation must be made:
///
///     f_constraint(lambda) = G^T lambda
///
/// Therefore, this method computes the generalized speed derivatives via the
/// equation
///
///     udot = M_inv (f_applied - f_inertial(q,u) - f_constraint(lambda))
///
/// Finally, note that in order for f_constraint to be used like an applied
/// force (i.e. appear on the RHS), the multipliers are negated in the call to
/// obtain Simbody constraint forces.
void calcAccelerationsFromMultipliers(const Model& model, const State& state,
        const Vector& multipliers, Vector& udot) {

    SimTK::Vector_<SimTK::SpatialVec> constraintBodyForces;
    Vector constraintMobilityForces;
    // We first need to compute the body and mobility forces associated with the
    // Lagrange multipliers provided by a solver.
    {
        const auto& matter = model.getMatterSubsystem();
        // Multipliers are negated so the constraint forces can be used like
        // applied forces.
        matter.calcConstraintForcesFromMultipliers(state, -multipliers,
                constraintBodyForces, constraintMobilityForces);
    }

    // We would like to eventually compute the model accelerations through
    // realizing to Stage::Acceleration. However, if the model has constraints,
    // realizing to Stage::Acceleration will cause Simbody to compute its own
    // Lagrange multipliers which will not necessarily be consistent with the
    // multipliers provided by a solver. Therefore, we'll first create a copy
    // of the original model, disable the its constraints, and apply the
    // constraint forces we just calculated before computing the accelerations.
    {
        // Create a copy of the original model, whose constraints we'll disable.
        Model modelDisabledConstraints = Model(model);

        // Add an OpenSim::DiscreteForces component to the new model, which
        // we'll use to the apply the constraint forces.
        DiscreteForces* constraintForces = new DiscreteForces();
        modelDisabledConstraints.addComponent(constraintForces);

        // Initialize the new model's underlying system and get a non-const
        // state, which contains slots for the original model's continuous
        // variables and new slots for the discrete variables representing the
        // constraint forces.
        SimTK::State& stateDisabledConstraints =
                modelDisabledConstraints.initSystem();
        // Update the new model's continuous variables from the passed in state.
        stateDisabledConstraints.updY() = state.getY();
        // Update the discrete forces in the new state with the constraint
        // forces we just calculated.
        constraintForces->setAllForces(stateDisabledConstraints,
                constraintMobilityForces, constraintBodyForces);

        // Disable the constraints in the new model.
        auto& matterIgnoringConstraints =
                modelDisabledConstraints.updMatterSubsystem();
        const auto NC = matterIgnoringConstraints.getNumConstraints();
        for (SimTK::ConstraintIndex cid(0); cid < NC; ++cid) {
            SimTK::Constraint& constraint =
                    matterIgnoringConstraints.updConstraint(cid);
            if (!constraint.isDisabled(stateDisabledConstraints)) {
                constraint.disable(stateDisabledConstraints);
            }
        }

        // Now we can simply realize to Stage::Acceleration on the new model to
        // get correct accelerations.
        modelDisabledConstraints.realizeAcceleration(stateDisabledConstraints);
        udot = stateDisabledConstraints.getUDot();
    }
}

/// DAE calculation subtests.
/// -------------------------
/// The following tests add a constraint to a model and check that the method
/// calcAccelerationsFromMultipliers() is implemented correctly. Each test
/// follows a similar structure:
///     1) Create a model and add a constraint between two bodies
///     2) Create a random state and realize the model to Stage::Acceleration
///     3) Check that state contains at least one Lagrange multiplier
///     4) Compute the model accelerations from Simbody
///     5) Retrieve the Lagrange multiplier values for the current state
///     6) Compute the accelerations from calcAccelerationsFromMultipliers()
///     7) Ensure that the accelerations from step 4 and 6 match
TEST_CASE("WeldConstraint", "") {
    State state;
    Model model = createModel();
    const std::string& firstBodyName =
            model.getBodySet().get(0).getAbsolutePathString();
    const std::string& lastBodyName =
            model.getBodySet().get(NUM_BODIES - 1).getAbsolutePathString();
    WeldConstraint* constraint =
            new WeldConstraint("weld", firstBodyName, lastBodyName);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("PointConstraint", "") {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody = model.getBodySet().get(NUM_BODIES - 1);
    PointConstraint* constraint =
            new PointConstraint(firstBody, Vec3(0), lastBody, Vec3(0));
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("PointOnLineConstraint", "") {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody = model.getBodySet().get(NUM_BODIES - 1);
    PointOnLineConstraint* constraint = new PointOnLineConstraint(
            firstBody, Vec3(1, 0, 0), Vec3(0), lastBody, Vec3(0));
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("ConstantDistanceConstraint", "") {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody = model.getBodySet().get(NUM_BODIES - 1);
    ConstantDistanceConstraint* constraint = new ConstantDistanceConstraint(
            firstBody, Vec3(0), lastBody, Vec3(0), 4.56);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("LockedCoordinate", "") {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    coordSet.getLast()->set_locked(true);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("CoordinateCouplerConstraint", "") {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    CoordinateCouplerConstraint* constraint = new CoordinateCouplerConstraint();
    Array<std::string> names;
    coordSet.getNames(names);
    constraint->setIndependentCoordinateNames(
            Array<std::string>(names.get(0), 1));
    constraint->setDependentCoordinateName(names.getLast());
    LinearFunction func(1.0, 0.0);
    constraint->setFunction(func);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

TEST_CASE("PrescribedMotion", "") {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    LinearFunction func(1.0, 0.0);
    coordSet.getLast()->setPrescribedFunction(func);
    coordSet.getLast()->setDefaultIsPrescribed(true);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
            model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(
            model, state, multipliers, udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

/// Create a torque-actuated double pendulum model. Each subtest will add to the
/// model the relevant constraint(s).
std::unique_ptr<Model> createDoublePendulumModel() {
    auto model = make_unique<Model>();
    model->setName("double_pendulum");

    using SimTK::Inertia;
    using SimTK::Vec3;

    // Create two links, each with a mass of 1 kg, center of mass at the body's
    // origin, and moments and products of inertia of zero.
    auto* b0 = new OpenSim::Body("b0", 1, Vec3(0), Inertia(1));
    model->addBody(b0);
    auto* b1 = new OpenSim::Body("b1", 1, Vec3(0), Inertia(1));
    model->addBody(b1);

    // Add station representing the model end-effector.
    auto* endeff = new Station(*b1, Vec3(0));
    endeff->setName("endeff");
    model->addComponent(endeff);

    // Connect the bodies with pin joints. Assume each body is 1 m long.
    auto* j0 = new PinJoint("j0", model->getGround(), Vec3(0), Vec3(0), *b0,
            Vec3(-1, 0, 0), Vec3(0));
    auto& q0 = j0->updCoordinate();
    q0.setName("q0");
    q0.setDefaultValue(0);
    auto* j1 = new PinJoint(
            "j1", *b0, Vec3(0), Vec3(0), *b1, Vec3(-1, 0, 0), Vec3(0));
    auto& q1 = j1->updCoordinate();
    q1.setName("q1");
    q1.setDefaultValue(SimTK::Pi);
    model->addJoint(j0);
    model->addJoint(j1);

    // Add coordinate actuators.
    auto* tau0 = new CoordinateActuator();
    tau0->setCoordinate(&j0->updCoordinate());
    tau0->setName("tau0");
    tau0->setOptimalForce(1);
    model->addComponent(tau0);
    auto* tau1 = new CoordinateActuator();
    tau1->setCoordinate(&j1->updCoordinate());
    tau1->setName("tau1");
    tau1->setOptimalForce(1);
    model->addComponent(tau1);

    // Add display geometry.
    Ellipsoid bodyGeometry(0.5, 0.1, 0.1);
    SimTK::Transform transform(SimTK::Vec3(-0.5, 0, 0));
    auto* b0Center = new PhysicalOffsetFrame("b0_center", *b0, transform);
    b0->addComponent(b0Center);
    b0Center->attachGeometry(bodyGeometry.clone());
    auto* b1Center = new PhysicalOffsetFrame("b1_center", *b1, transform);
    b1->addComponent(b1Center);
    b1Center->attachGeometry(bodyGeometry.clone());

    return model;
}

/// Run a forward simulation using controls from an OCP solution and compare the
/// state trajectories.
MocoTrajectory runForwardSimulation(
        Model model, const MocoSolution& solution, const double& tol) {

    // Get actuator names.
    model.initSystem();
    OpenSim::Array<std::string> actuNames;
    const auto modelPath = model.getAbsolutePath();
    for (const auto& actu : model.getComponentList<Actuator>()) {
        actuNames.append(actu.getAbsolutePathString());
    }

    // Add prescribed controllers to actuators in the model, where the control
    // functions are splined versions of the actuator controls from the OCP
    // solution.
    const SimTK::Vector& time = solution.getTime();
    auto* controller = new PrescribedController();
    controller->setName("prescribed_controller");
    for (int i = 0; i < actuNames.size(); ++i) {
        const auto control = solution.getControl(actuNames[i]);
        auto* controlFunction =
                new GCVSpline(5, time.nrow(), &time[0], &control[0]);
        const auto& actu = model.getComponent<Actuator>(actuNames[i]);
        controller->addActuator(actu);
        controller->prescribeControlForActuator(
                actu.getName(), controlFunction);
    }
    model.addController(controller);

    // Add states reporter to the model.
    auto* statesRep = new StatesTrajectoryReporter();
    statesRep->setName("states_reporter");
    statesRep->set_report_time_interval(0.001);
    model.addComponent(statesRep);

    // Add a TableReporter to collect the controls.
    auto* controlsRep = new TableReporter();
    for (int i = 0; i < actuNames.size(); ++i) {
        controlsRep->addToReport(
                model.getComponent(actuNames[i]).getOutput("actuation"),
                actuNames[i]);
    }
    model.addComponent(controlsRep);

    // Simulate!
    SimTK::State state = model.initSystem();
    state.setTime(time[0]);
    Manager manager(model);
    manager.getIntegrator().setAccuracy(1e-9);
    manager.initialize(state);
    state = manager.integrate(time[time.size() - 1]);

    // Export results from states reporter to a TimeSeries Table
    TimeSeriesTable states;
    states = statesRep->getStates().exportToTable(model);

    TimeSeriesTable controls;
    controls = controlsRep->getTable();

    // Create a MocoTrajectory to facilitate states trajectory comparison (with
    // dummy data for the multipliers, which we'll ignore).
    const auto& statesTimes = states.getIndependentColumn();
    SimTK::Vector timeVec((int)statesTimes.size(), statesTimes.data(), true);
    auto forwardSolution = MocoTrajectory(timeVec, states.getColumnLabels(),
            controls.getColumnLabels(), states.getColumnLabels(), {},
            states.getMatrix(), controls.getMatrix(), states.getMatrix(),
            SimTK::RowVector(0));

    // Compare controls between foward simulation and OCP solution. These
    // should match very closely, since the foward simulation controls are
    // created from splines of the OCP solution controls
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(
                              forwardSolution, {{"controls", {}}}),
            0, 1e-9);

    // Compare states trajectory between forward simulation and OCP solution.
    // The states trajectory may not match as well as the controls.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(
                              forwardSolution, {{"states", {}}}),
            0, tol);

    return forwardSolution;
}

/// Direct collocation subtests.
/// ----------------------------

/// Solve an optimal control problem where a double pendulum must reach a
/// specified final configuration while subject to a constraint that its
/// end-effector must lie on a vertical line through the origin and minimize
/// control effort.
template <typename TestType>
void testDoublePendulumPointOnLine(
        bool enforce_constraint_derivatives, std::string dynamics_mode) {
    MocoStudy moco;
    moco.setName("double_pendulum_point_on_line");
    MocoProblem& mp = moco.updProblem();
    // Create double pendulum model and add the point-on-line constraint. The
    // constraint consists of a vertical line in the y-direction (defined in
    // ground) and the model end-effector point (the origin of body "b1").
    auto model = createDoublePendulumModel();
    const Body& b1 = model->getBodySet().get("b1");
    const Station& endeff = model->getComponent<Station>("endeff");

    PointOnLineConstraint* constraint =
            new PointOnLineConstraint(model->getGround(), Vec3(0, 1, 0),
                    Vec3(0), b1, endeff.get_location());
    model->addConstraint(constraint);
    model->finalizeConnections();
    mp.setModelCopy(*model);

    mp.setTimeBounds(0, 1);
    // Coordinate value state boundary conditions are consistent with the
    // point-on-line constraint.
    const double theta_i = 0.5;
    const double theta_f = SimTK::Pi / 2;
    mp.setStateInfo(
            "/jointset/j0/q0/value", {theta_i, theta_f}, theta_i, theta_f);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50});
    {
        double initial = SimTK::Pi - 2 * theta_i;
        double final = SimTK::Pi - 2 * theta_f;
        mp.setStateInfo(
                "/jointset/j1/q1/value", {final, initial}, initial, final);
    }
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50});
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    mp.addCost<MocoControlCost>();

    auto& ms = moco.initSolver<TestType>();
    ms.set_num_mesh_points(20);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    ms.set_transcription_scheme("hermite-simpson");
    ms.set_enforce_constraint_derivatives(enforce_constraint_derivatives);
    ms.set_minimize_lagrange_multipliers(true);
    ms.set_lagrange_multiplier_weight(10);
    ms.set_dynamics_mode(dynamics_mode);
    ms.setGuess("bounds");

    MocoSolution solution = moco.solve();
    solution.write("testConstraints_testDoublePendulumPointOnLine.sto");
    // moco.visualize(solution);

    model->initSystem();
    StatesTrajectory states = solution.exportToStatesTrajectory(mp);
    for (int i = 0; i < (int)states.getSize(); ++i) {
        const auto& s = states.get(i);
        model->realizePosition(s);
        const SimTK::Vec3& loc = endeff.getLocationInGround(s);

        // The end-effector should not have moved in the x- or z-directions.
        SimTK_TEST_EQ_TOL(loc[0], 0, 1e-2);
        SimTK_TEST_EQ_TOL(loc[2], 0, 1e-2);
    }

    // Run a forward simulation using the solution controls in prescribed
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(*model, solution, 2);
}

/// Solve an optimal control problem where a double pendulum must reach a
/// specified final configuration while subject to a constraint that couples
/// its two coordinates together via a linear relationship and minimizing
/// control effort.
template <typename SolverType>
void testDoublePendulumCoordinateCoupler(MocoSolution& solution,
        bool enforce_constraint_derivatives, std::string dynamics_mode) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    MocoStudy moco;
    moco.setName("double_pendulum_coordinate_coupler");

    // Create double pendulum model and add the coordinate coupler constraint.
    auto model = createDoublePendulumModel();
    const Coordinate& q0 = model->getCoordinateSet().get("q0");
    const Coordinate& q1 = model->getCoordinateSet().get("q1");
    CoordinateCouplerConstraint* constraint = new CoordinateCouplerConstraint();
    Array<std::string> indepCoordNames;
    indepCoordNames.append("q0");
    constraint->setIndependentCoordinateNames(indepCoordNames);
    constraint->setDependentCoordinateName("q1");
    // Represented by the following equation,
    //      q1 = m*q0 + b
    // this linear function couples the two model coordinates such that given
    // the boundary conditions for q0 from testDoublePendulumPointOnLine, the
    // same boundary conditions for q1 should be achieved without imposing
    // bounds for this coordinate.
    const SimTK::Real m = -2;
    const SimTK::Real b = SimTK::Pi;
    LinearFunction linFunc(m, b);
    // Avoid CoordinateCoupler::setFunction(const Function&); it has a leak.
    constraint->setFunction(&linFunc);
    model->addConstraint(constraint);
    model->finalizeConnections();

    MocoProblem& mp = moco.updProblem();
    mp.setModelCopy(*model);
    mp.setTimeBounds(0, 1);
    // Boundary conditions are only enforced for the first coordinate, so we can
    // test that the second coordinate is properly coupled.
    mp.setStateInfo("/jointset/j0/q0/value", {-5, 5}, 0, SimTK::Pi / 2);
    mp.setStateInfo("/jointset/j0/q0/speed", {-10, 10}, 0, 0);
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/speed", {-5, 5}, 0, 0);
    mp.setControlInfo("/tau0", {-50, 50});
    mp.setControlInfo("/tau1", {-50, 50});
    mp.addCost<MocoControlCost>();

    auto& ms = moco.initSolver<SolverType>();
    ms.set_num_mesh_points(20);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    ms.set_transcription_scheme("hermite-simpson");
    ms.set_enforce_constraint_derivatives(enforce_constraint_derivatives);
    ms.set_minimize_lagrange_multipliers(true);
    ms.set_lagrange_multiplier_weight(10);
    ms.set_dynamics_mode(dynamics_mode);
    ms.setGuess("bounds");

    solution = moco.solve();
    solution.write("testConstraints_testDoublePendulumCoordinateCoupler.sto");
    // moco.visualize(solution);

    model->initSystem();
    StatesTrajectory states = solution.exportToStatesTrajectory(mp);
    for (int i = 0; i < (int)states.getSize(); ++i) {
        const auto& s = states.get(i);
        model->realizePosition(s);

        // The coordinates should be coupled according to the linear function
        // described above.
        SimTK_TEST_EQ_TOL(q1.getValue(s), m * q0.getValue(s) + b, 1e-2);
    }

    // Run a forward simulation using the solution controls in prescribed
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(*model, solution, 1e-1);
}

/// Solve an optimal control problem where a double pendulum must follow a
/// prescribed motion based on the previous test case (see
/// testDoublePendulumCoordinateCoupler).
template <typename SolverType>
void testDoublePendulumPrescribedMotion(MocoSolution& couplerSolution,
        bool enforce_constraint_derivatives, std::string dynamics_mode) {
    MocoStudy moco;
    moco.setName("double_pendulum_prescribed_motion");
    MocoProblem& mp = moco.updProblem();

    // Create double pendulum model.
    auto model = createDoublePendulumModel();
    // Create a spline set for the model states from the previous solution. We
    // need to call initSystem() and set the model here in order to convert the
    // solution from the previous problem to a StatesTrajectory.
    model->initSystem();
    mp.setModelCopy(*model);

    TimeSeriesTable statesTrajCoupler =
            couplerSolution.exportToStatesTrajectory(mp).exportToTable(*model);
    GCVSplineSet statesSpline(statesTrajCoupler);

    // Apply the prescribed motion constraints.
    Coordinate& q0 = model->updJointSet().get("j0").updCoordinate();
    q0.setPrescribedFunction(statesSpline.get("/jointset/j0/q0/value"));
    q0.setDefaultIsPrescribed(true);
    Coordinate& q1 = model->updJointSet().get("j1").updCoordinate();
    q1.setPrescribedFunction(statesSpline.get("/jointset/j1/q1/value"));
    q1.setDefaultIsPrescribed(true);
    // Set the model again after implementing the constraints.
    mp.setModelCopy(*model);

    mp.setTimeBounds(0, 1);
    // No bounds here, since the problem is already highly constrained by the
    // prescribed motion constraints on the coordinates.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10});
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50});
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50});
    mp.setControlInfo("/tau0", {-25, 25});
    mp.setControlInfo("/tau1", {-25, 25});

    mp.addCost<MocoControlCost>();

    auto& ms = moco.initSolver<SolverType>();
    ms.set_num_mesh_points(20);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    ms.set_transcription_scheme("hermite-simpson");
    ms.set_enforce_constraint_derivatives(enforce_constraint_derivatives);
    ms.set_minimize_lagrange_multipliers(true);
    ms.set_lagrange_multiplier_weight(10);
    ms.set_dynamics_mode(dynamics_mode);

    // Set guess based on coupler solution trajectory.
    MocoTrajectory guess(ms.createGuess("bounds"));
    guess.setStatesTrajectory(statesTrajCoupler);
    ms.setGuess(guess);

    MocoSolution solution = moco.solve();
    solution.write("testConstraints_testDoublePendulumPrescribedMotion.sto");
    // moco.visualize(solution);

    // Create a TimeSeriesTable containing the splined state data from
    // testDoublePendulumCoordinateCoupler. Since this splined data could be
    // somewhat different from the coordinate coupler OCP solution, we use this
    // to create a direct comparison between the prescribed motion OCP solution
    // states and exactly what the PrescribedMotion constraints should be
    // enforcing.
    auto statesTraj = solution.exportToStatesTrajectory(mp);
    // Initialize data structures to use in the TimeSeriesTable
    // convenience constructor.
    std::vector<double> indVec((int)statesTraj.getSize());
    SimTK::Matrix depData(
            (int)statesTraj.getSize(), (int)solution.getStateNames().size());
    Vector timeVec(1);
    for (int i = 0; i < (int)statesTraj.getSize(); ++i) {
        const auto& s = statesTraj.get(i);
        const SimTK::Real& time = s.getTime();
        indVec[i] = time;
        timeVec.updElt(0, 0) = time;
        depData.set(i, 0,
                statesSpline.get("/jointset/j0/q0/value").calcValue(timeVec));
        depData.set(i, 1,
                statesSpline.get("/jointset/j1/q1/value").calcValue(timeVec));
        // The values for the speed states are created from the spline
        // derivative values.
        depData.set(i, 2,
                statesSpline.get("/jointset/j0/q0/value")
                        .calcDerivative({0}, timeVec));
        depData.set(i, 3,
                statesSpline.get("/jointset/j1/q1/value")
                        .calcDerivative({0}, timeVec));
    }
    TimeSeriesTable splineStateValues(
            indVec, depData, solution.getStateNames());

    // Create a MocoTrajectory containing the splined state values. The splined
    // state values are also set for the controls and adjuncts as dummy data.
    const auto& statesTimes = splineStateValues.getIndependentColumn();
    SimTK::Vector time((int)statesTimes.size(), statesTimes.data(), true);
    auto mocoIterSpline = MocoTrajectory(time, splineStateValues.getColumnLabels(),
            splineStateValues.getColumnLabels(),
            splineStateValues.getColumnLabels(), {},
            splineStateValues.getMatrix(), splineStateValues.getMatrix(),
            splineStateValues.getMatrix(), SimTK::RowVector(0));

    // Only compare the position-level values between the current solution
    // states and the states from the previous test (original and splined).
    // These should match well, since position-level values are enforced
    // directly via a path constraint in the current problem formulation (see
    // MocoTropterSolver for details).

    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(mocoIterSpline,
                              {{"states", {"/jointset/j0/q0/value",
                                                  "/jointset/j1/q1/value"}}}),
            0, 1e-3);
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution,
                              {{"states", {"/jointset/j0/q0/value",
                                                  "/jointset/j1/q1/value"}}}),
            0, 1e-3);
    // Only compare the velocity-level values between the current solution
    // states and the states from the previous test (original and splined).
    // These won't match as well as the position-level values, since velocity-
    // level errors are not enforced in the current problem formulation.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(mocoIterSpline,
                              {{"states", {"/jointset/j0/q0/speed",
                                                  "/jointset/j1/q1/speed"}}}),
            0, 1e-1);
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution,
                              {{"states", {"/jointset/j0/q0/speed",
                                                  "/jointset/j1/q1/speed"}}}),
            0, 1e-1);
    // Compare only the actuator controls. These match worse compared to the
    // velocity-level states. It is currently unclear to what extent this is
    // related to velocity-level states not matching well or the how the model
    // constraints are enforced in the current formulation.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution,
                              {{"controls", {"/tau0", "/tau1"}}}),
            0, 5);

    // Run a forward simulation using the solution controls in prescribed
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(*model, solution, 1e-1);
}

TEMPLATE_TEST_CASE("DoublePendulum with and without constraint derivatives",
        "[explicit]", MocoTropterSolver, MocoCasADiSolver) {
    SECTION("DoublePendulum without constraint derivatives") {
        MocoSolution couplerSol;
        testDoublePendulumCoordinateCoupler<TestType>(
                couplerSol, false, "explicit");
        testDoublePendulumPrescribedMotion<TestType>(
                couplerSol, false, "explicit");
    }

    SECTION("DoublePendulum with constraint derivatives") {
        MocoSolution couplerSol;
        testDoublePendulumCoordinateCoupler<TestType>(
                couplerSol, true, "explicit");
        testDoublePendulumPrescribedMotion<TestType>(
                couplerSol, true, "explicit");
    }
}

TEST_CASE("DoublePendulum with and without constraint derivatives",
        "[implicit]") {
    SECTION("DoublePendulum without constraint derivatives") {
        MocoSolution couplerSol;
        testDoublePendulumCoordinateCoupler<MocoCasADiSolver>(
                couplerSol, false, "implicit");
        testDoublePendulumPrescribedMotion<MocoCasADiSolver>(
                couplerSol, false, "implicit");
    }

    SECTION("DoublePendulum with constraint derivatives") {
        MocoSolution couplerSol;
        testDoublePendulumCoordinateCoupler<MocoCasADiSolver>(
                couplerSol, true, "implicit");
        testDoublePendulumPrescribedMotion<MocoCasADiSolver>(
                couplerSol, true, "implicit");
    }
}

TEMPLATE_TEST_CASE("DoublePendulumPointOnLine without constraint derivatives",
        "[explicit]", MocoTropterSolver, MocoCasADiSolver) {
    testDoublePendulumPointOnLine<TestType>(false, "explicit");
}

TEMPLATE_TEST_CASE("DoublePendulumPointOnLine with constraint derivatives",
        "[explicit]", MocoTropterSolver, MocoCasADiSolver) {
    testDoublePendulumPointOnLine<TestType>(true, "explicit");
}

TEST_CASE("DoublePendulumPointOnLine without constraint derivatives",
        "[implicit]") {
    testDoublePendulumPointOnLine<MocoCasADiSolver>(false, "implicit");
}

TEST_CASE(
        "DoublePendulumPointOnLine with constraint derivatives", "[implicit]") {
    testDoublePendulumPointOnLine<MocoCasADiSolver>(true, "implicit");
}

class EqualControlConstraint : public MocoPathConstraint {
    OpenSim_DECLARE_CONCRETE_OBJECT(EqualControlConstraint, MocoPathConstraint);

protected:
    void initializeOnModelImpl(
            const Model& model, const MocoProblemInfo&) const override {
        // Make sure the model generates a state object with the two controls we
        // expect, no more and no less.
        const auto state = model.getWorkingState();
        model.realizeVelocity(state);
        OPENSIM_THROW_IF(model.getControls(state).size() != 2, Exception,
                "State has incorrect number of controls (two expected).");

        // There is only constraint equation: match the two model controls.
        setNumEquations(1);
    }
    void calcPathConstraintErrorsImpl(
            const SimTK::State& state, SimTK::Vector& errors) const override {
        getModel().realizeVelocity(state);

        const auto& controls = getModel().getControls(state);
        // In the problem below, the actuators are bilateral and act in
        // opposite directions, so we use addition to create the residual here.
        errors[0] = controls[1] + controls[0];
    }
};

/// Solve an optimal control problem where a double pendulum must reach a
/// specified final configuration while subject to a constraint that its
/// actuators must produce an equal control trajectory.
TEMPLATE_TEST_CASE(
        "DoublePendulumEqualControl", "", MocoTropterSolver, MocoCasADiSolver) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    OpenSim::Object::registerType(EqualControlConstraint());
    MocoStudy moco;
    moco.setName("double_pendulum_equal_control");
    MocoProblem& mp = moco.updProblem();
    auto model = createDoublePendulumModel();
    model->finalizeConnections();
    mp.setModelCopy(*model);

    auto* equalControlConstraint =
            mp.addPathConstraint<EqualControlConstraint>();
    MocoConstraintInfo cInfo;
    cInfo.setBounds(std::vector<MocoBounds>(1, {0, 0}));
    equalControlConstraint->setConstraintInfo(cInfo);

    mp.setTimeBounds(0, 1);
    // Coordinate value state boundary conditions are consistent with the
    // point-on-line constraint and should require the model to "unfold" itself.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0, SimTK::Pi / 2);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50});
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10}, SimTK::Pi, 0);
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50});
    mp.setControlInfo("/tau0", {-50, 50});
    mp.setControlInfo("/tau1", {-50, 50});

    mp.addCost<MocoControlCost>();

    auto& ms = moco.initSolver<TestType>();
    ms.set_num_mesh_points(25);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    ms.setGuess("bounds");

    MocoSolution solution = moco.solve();
    solution.write("testConstraints_testDoublePendulumEqualControl.sto");
    // moco.visualize(solution);

    const auto& control_tau0 = solution.getControl("/tau0");
    const auto& control_tau1 = solution.getControl("/tau1");
    const auto& control_res = control_tau1.abs() - control_tau0.abs();
    SimTK_TEST_EQ_TOL(control_res.normRMS(), 0, 1e-6);

    // Run a forward simulation using the solution controls in prescribed
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    // TODO why does the forward solution match so poorly here?
    MocoTrajectory forwardSolution = runForwardSimulation(*model, solution, 2);
    // moco.visualize(forwardSolution);

    // Test de/serialization.
    // ======================
    std::string setup_fname =
            "testConstraints_testDoublePendulumEqualControl.omoco";
    moco.print(setup_fname);
    MocoSolution solutionDeserialized;
    MocoStudy mocoDeserialize(setup_fname);
    solutionDeserialized = mocoDeserialize.solve();
    SimTK_TEST(solution.isNumericallyEqual(solutionDeserialized));
}

// This problem is a point mass welded to ground, with gravity. We are
// solving for the mass that allows the point mass to obey the constraint
// of staying in place. This checks that the parameters are applied to both
// ModelBase and ModelDisabledConstraints.
TEMPLATE_TEST_CASE(
        "Parameters are set properly for Base and DisabledConstraints", "",
        MocoTropterSolver /*, too damn slow: MocoCasADiSolver*/) {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    Model model;
    auto* body = new Body("b", 0.7, SimTK::Vec3(0), SimTK::Inertia(1));
    model.addBody(body);

    auto* joint = new FreeJoint("j", model.getGround(), *body);
    model.addJoint(joint);

    auto* constraint = new WeldConstraint("weld", model.getGround(),
            SimTK::Transform(), *body, SimTK::Transform());
    model.addConstraint(constraint);
    model.finalizeConnections();

    MocoStudy moco;
    auto& problem = moco.updProblem();
    problem.setModelCopy(model);
    problem.setTimeBounds(0, 1);
    problem.addParameter("mass", "/bodyset/b", "mass", MocoBounds(0.5, 1.5));
    auto& solver = moco.initSolver<TestType>();
    solver.set_num_mesh_points(10);
    MocoSolution solution = moco.solve();
    CHECK(solution.getParameter("mass") == Approx(1.0).epsilon(1e-3));
}

class MocoJointReactionComponentCost : public MocoCost {
    OpenSim_DECLARE_CONCRETE_OBJECT(MocoJointReactionComponentCost, MocoCost);

public:
    void calcIntegrandImpl(
            const SimTK::State& state, double& integrand) const override {
        getModel().realizeAcceleration(state);
        const auto& joint = getModel().getComponent<Joint>("jointset/j1");
        // Minus sign since we are maximizng.
        integrand = -joint.calcReactionOnChildExpressedInGround(state)[0][0];
    }
    void calcCostImpl(
            const CostInput& input, SimTK::Real& cost) const override {
        cost = input.integral;
    }
};

template <typename TestType>
void testDoublePendulumPointOnLineJointReaction(
        bool enforce_constraint_derivatives, std::string dynamics_mode) {
    MocoStudy moco;
    moco.setName("double_pendulum_point_on_line");
    MocoProblem& mp = moco.updProblem();
    // Create double pendulum model and add the point-on-line constraint. The
    // constraint consists of a vertical line in the y-direction (defined in
    // ground) and the model end-effector point (the origin of body "b1").
    // TODO: Choose a function with acceleration to ensure that accelerations
    // are propagated successfully.
    const auto pi = SimTK::Pi;
    auto model = createDoublePendulumModel();
    model->updCoordinateSet().get("q0").setPrescribedFunction(
            Constant(0.25 * pi));
    model->updCoordinateSet().get("q0").setDefaultIsPrescribed(true);
    model->updCoordinateSet().get("q1").setPrescribedFunction(
            Constant(0.5 * pi));
    model->updCoordinateSet().get("q1").setDefaultIsPrescribed(true);

    const Station& endeff = model->getComponent<Station>("endeff");
    auto* actuator = new PointActuator("b1");
    actuator->setName("push");
    actuator->set_point(endeff.get_location());
    actuator->set_point_is_global(false);
    actuator->set_direction(Vec3(0, 0, -1));
    actuator->set_force_is_global(true);
    model->addComponent(actuator);

    model->finalizeConnections();
    mp.setModelCopy(*model);

    mp.setTimeBounds(0, 1);
    mp.setStateInfo("/jointset/j0/q0/value", {-0.6 * pi, 0.6 * pi});
    mp.setStateInfo("/jointset/j0/q0/speed", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/value", {0, pi});
    mp.setStateInfo("/jointset/j1/q1/speed", {-10, 10});
    mp.setControlInfo("/tau0", {-20, 20});
    mp.setControlInfo("/tau1", {-20, 20});
    mp.setControlInfo("/push", {-20, 20});

    // This cost tries to *maximize* joint j1's reaction torque in the
    // x-direction, which should cause the actuator "push" to hit its upper
    // bound.
    mp.addCost<MocoJointReactionComponentCost>();

    auto& ms = moco.initSolver<TestType>();
    int N = 5;
    ms.set_num_mesh_points(N);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-6);
    ms.set_transcription_scheme("hermite-simpson");
    ms.set_enforce_constraint_derivatives(enforce_constraint_derivatives);
    ms.set_minimize_lagrange_multipliers(true);
    ms.set_lagrange_multiplier_weight(10);
    ms.set_dynamics_mode(dynamics_mode);
    ms.setGuess("bounds");

    MocoSolution solution = moco.solve().unseal();
    solution.write(
            "testConstraints_testDoublePendulumPointOnLineJointReaction.sto");

    // Check that the actuator "push" is hitting its upper bound.
    CHECK(solution.getControl("/push")[0] == Approx(20).epsilon(1e-4));
    // Check that j1's x-direction reaction torque (the only objective term)
    // is the proper value.
    CHECK(solution.getObjective() == Approx(-1. / sqrt(2) * 20).epsilon(1e-2));
}

TEMPLATE_TEST_CASE(
        "DoublePendulumPointOnLineJointReaction with constraint derivatives",
        "[explicit]", MocoTropterSolver, MocoCasADiSolver) {
    testDoublePendulumPointOnLineJointReaction<TestType>(true, "explicit");
}

TEMPLATE_TEST_CASE("DoublePendulumPointOnLineJointReaction implicit with "
                   "constraint derivatives",
        "[implicit]", MocoCasADiSolver) {
    testDoublePendulumPointOnLineJointReaction<TestType>(true, "implicit");
}

TEST_CASE("Multipliers are correct", "") {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    SECTION("Body welded to ground") {
        auto dynamics_mode =
                GENERATE(as<std::string>{}, "implicit", "explicit");

        Model model;
        const double mass = 1.3169;
        auto* body = new Body("body", mass, SimTK::Vec3(0), SimTK::Inertia(1));
        model.addBody(body);

        auto* joint = new FreeJoint("joint", model.getGround(), *body);
        model.addJoint(joint);

        auto* constr = new WeldConstraint("constraint", model.getGround(),
                SimTK::Transform(), *body, SimTK::Transform());
        model.addConstraint(constr);
        model.finalizeConnections();

        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(model);

        problem.setTimeBounds(0, 0.5);

        auto& solver = moco.initCasADiSolver();
        solver.set_num_mesh_points(5);
        solver.set_dynamics_mode(dynamics_mode);
        solver.set_transcription_scheme("hermite-simpson");
        solver.set_enforce_constraint_derivatives(true);

        MocoSolution solution = moco.solve();

        // Constraints 0 through 5 are the locks for the 6 DOFs.
        const auto MX = solution.getMultiplier("lambda_cid6_p0");
        SimTK::Vector zero(MX);
        zero.setToZero();
        OpenSim_CHECK_MATRIX_TOL(MX, zero, 1e-5);
        const auto MY = solution.getMultiplier("lambda_cid6_p1");
        OpenSim_CHECK_MATRIX_TOL(MY, zero, 1e-5);
        const auto MZ = solution.getMultiplier("lambda_cid6_p2");
        OpenSim_CHECK_MATRIX_TOL(MZ, zero, 1e-5);
        const auto FX = solution.getMultiplier("lambda_cid6_p3");
        OpenSim_CHECK_MATRIX_TOL(FX, zero, 1e-5);
        const auto FY = solution.getMultiplier("lambda_cid6_p4");
        SimTK::Vector g(zero.size(), model.get_gravity()[1]);
        OpenSim_CHECK_MATRIX_TOL(FY, mass * g, 1e-5);
        const auto FZ = solution.getMultiplier("lambda_cid6_p5");
        OpenSim_CHECK_MATRIX_TOL(FZ, zero, 1e-5);
    }

    // This problem is a point mass constrained to the line 0 = x - y.
    // constraint Jacobian G is [1, -1].
    //      m xdd + G(0) * lambda = Fx  -> m xdd + lambda = Fx
    //      m ydd + G(1) * lambda = Fy  -> m ydd - lambda = Fy
    // Since xdd = ydd, we have:
    //      lambda = 0.5 * (Fx - Fy).
    // This test ensures that the multiplier has the correct value.
    SECTION("Planar point mass with CoordinateCouplerConstraint") {

        auto dynamics_mode =
                GENERATE(as<std::string>{}, "implicit", "explicit");

        Model model = ModelFactory::createPlanarPointMass();
        model.set_gravity(Vec3(0));
        CoordinateCouplerConstraint* constraint =
                new CoordinateCouplerConstraint();
        Array<std::string> names;
        names.append("tx");
        constraint->setIndependentCoordinateNames(names);
        constraint->setDependentCoordinateName("ty");
        LinearFunction func(1.0, 0.0);
        constraint->setFunction(func);
        model.addConstraint(constraint);

        model.finalizeConnections();

        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(model);

        problem.setTimeBounds(0, 1);
        problem.setStateInfo("/jointset/tx/tx/value", {-5, 5}, 0, 3);
        problem.setStateInfo("/jointset/tx/tx/speed", {-5, 5}, 0, 0);
        problem.setControlInfo("/forceset/force_x", 0.5);

        problem.addCost<MocoControlCost>();

        auto& solver = moco.initCasADiSolver();
        solver.set_num_mesh_points(10);
        solver.set_dynamics_mode(dynamics_mode);
        solver.set_transcription_scheme("hermite-simpson");
        solver.set_enforce_constraint_derivatives(true);
        MocoSolution solution = moco.solve();
        const auto Fx = solution.getControl("/forceset/force_x");
        const auto Fy = solution.getControl("/forceset/force_y");
        const auto lambda = solution.getMultiplier("lambda_cid2_p0");

        OpenSim_CHECK_MATRIX_TOL(lambda, 0.5 * (Fx - Fy), 1e-5);
    }
}

// Ensure that we correctly handle the combination of prescribed kinematics
// (PositionMotion) and kinematic constraints. This test is similar to the one
// above except that we prescribe motions for tx and ty.
TEST_CASE("Prescribed kinematics with kinematic constraints", "") {
    std::cout.rdbuf(LogManager::cout.rdbuf());
    std::cerr.rdbuf(LogManager::cerr.rdbuf());
    Model model = ModelFactory::createPlanarPointMass();
    model.set_gravity(Vec3(0));
    CoordinateCouplerConstraint* constraint = new CoordinateCouplerConstraint();
    Array<std::string> names;
    names.append("tx");
    constraint->setIndependentCoordinateNames(names);
    constraint->setDependentCoordinateName("ty");
    LinearFunction func(1.0, 0.0);
    constraint->setFunction(func);
    model.addConstraint(constraint);

    auto* posmot = new PositionMotion();
    Sine function = Sine(1.0, 1.0, 0, 1.0);
    posmot->setPositionForCoordinate(model.getCoordinateSet().get(0), function);
    posmot->setPositionForCoordinate(model.getCoordinateSet().get(1), function);
    model.addComponent(posmot);

    model.finalizeConnections();

    MocoStudy moco;
    auto& problem = moco.updProblem();
    problem.setModelCopy(model);

    problem.setTimeBounds(0, 3);
    problem.setControlInfo("/forceset/force_x", 0.5);

    problem.addCost<MocoControlCost>();

    auto& solver = moco.initCasADiSolver();
    solver.set_num_mesh_points(10);
    solver.set_dynamics_mode("implicit");
    solver.set_interpolate_control_midpoints(false);
    MocoSolution solution = moco.solve();
    const auto Fx = solution.getControl("/forceset/force_x");
    const auto Fy = solution.getControl("/forceset/force_y");
    const auto lambda = solution.getMultiplier("lambda_cid2_p0");

    OpenSim_CHECK_MATRIX_TOL(lambda, 0.5 * (Fx - Fy), 1e-5);
}

TEMPLATE_TEST_CASE(
        "MocoControlBoundConstraint", "", MocoTropterSolver, MocoCasADiSolver) {
    SECTION("Lower bound only") {
        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(ModelFactory::createPendulum());
        problem.setTimeBounds(0, 1);
        problem.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0);
        problem.setStateInfo("/jointset/j0/q0/speed", {-10, 10}, 0);
        problem.setControlInfo("/tau0", {-5, 5});
        problem.addCost<MocoControlCost>();
        auto* constr = problem.addPathConstraint<MocoControlBoundConstraint>();
        const double lowerBound = 0.1318;
        constr->addControlPath("/tau0");
        constr->setLowerBound(Constant(lowerBound));

        auto& solver = moco.initSolver<TestType>();
        MocoSolution solution = moco.solve();
        SimTK::Vector expected(solution.getNumTimes());
        expected = lowerBound;
        OpenSim_CHECK_MATRIX_ABSTOL(
                solution.getControlsTrajectory(), expected, 1e-6);
    }

    SECTION("Upper bound only") {
        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(ModelFactory::createPendulum());
        problem.setTimeBounds(0, {0.1, 10});
        problem.setStateInfo("/jointset/j0/q0/value", {0, 1}, 0, 0.53);
        problem.setStateInfo("/jointset/j0/q0/speed", {-10, 10}, 0, 0);
        problem.setControlInfo("/tau0", {-20, 20});
        problem.addCost<MocoFinalTimeCost>();
        auto* constr = problem.addPathConstraint<MocoControlBoundConstraint>();
        constr->addControlPath("/tau0");
        const double upperBound = 11.236;
        constr->setUpperBound(Constant(upperBound));

        auto& solver = moco.initSolver<TestType>();
        MocoSolution solution = moco.solve();
        SimTK::Vector expected(solution.getNumTimes());
        expected = upperBound;
        CHECK(SimTK::max(solution.getControlsTrajectory())[0] ==
                Approx(upperBound).margin(1e-6));
        CHECK(SimTK::min(solution.getControlsTrajectory())[0] ==
                Approx(-20).margin(1e-6));
    }

    SECTION("Upper and lower bounds are the same") {
        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(ModelFactory::createPendulum());
        problem.setTimeBounds(0, 1);
        problem.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0);
        problem.setStateInfo("/jointset/j0/q0/speed", {-10, 10}, 0);
        problem.setControlInfo("/tau0", {-5, 5});
        problem.addCost<MocoControlCost>();
        PiecewiseLinearFunction violateLower;
        violateLower.addPoint(0, 0);
        violateLower.addPoint(0.2, 0.5316);
        violateLower.addPoint(0.7, -0.3137);
        violateLower.addPoint(1, .0319);
        auto* constr = problem.addPathConstraint<MocoControlBoundConstraint>();
        constr->addControlPath("/tau0");
        constr->setLowerBound(violateLower);
        constr->setEqualityWithLower(true);
        auto& solver = moco.initSolver<TestType>();
        MocoSolution solution = moco.solve();
        SimTK::Vector expectedV(solution.getNumTimes());
        for (int itime = 0; itime < expectedV.size(); ++itime) {
            SimTK::Vector arg(1);
            arg[0] = solution.getTime()[itime];
            expectedV[itime] = violateLower.calcValue(arg);
        }
        MocoTrajectory expected = solution;
        expected.setControl("/tau0", expectedV);

        CHECK(solution.compareContinuousVariablesRMS(
                      expected, {{"controls", {}}}) < 1e-3);
    }

    SECTION("Time range of bounds function is too small.") {
        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(ModelFactory::createPendulum());
        problem.setTimeBounds({-31, 0}, {1, 50});
        problem.addCost<MocoControlCost>();
        GCVSpline violateLower;
        violateLower.setDegree(5);
        violateLower.addPoint(-30.9999, 0);
        violateLower.addPoint(0, 0);
        violateLower.addPoint(0.5, 0);
        violateLower.addPoint(0.7, 0);
        violateLower.addPoint(0.8, 0);
        violateLower.addPoint(0.9, 0);
        violateLower.addPoint(50, 0.319);
        auto* constr = problem.addPathConstraint<MocoControlBoundConstraint>();
        constr->addControlPath("/tau0");
        constr->setLowerBound(violateLower);
        CHECK_THROWS_WITH(moco.solve(),
                Contains("must be less than or equal to the minimum"));
        constr->clearLowerBound();
        GCVSpline violateUpper;
        violateUpper.setDegree(5);
        violateUpper.addPoint(-31, 0);
        violateUpper.addPoint(0, 0);
        violateUpper.addPoint(0.5, 0);
        violateUpper.addPoint(0.7, 0);
        violateUpper.addPoint(0.8, 0);
        violateUpper.addPoint(0.9, 0);
        violateUpper.addPoint(49.99999, .0319);
        constr->setUpperBound(violateUpper);
        CHECK_THROWS_WITH(moco.solve(),
                Contains("must be greater than or equal to the maximum"));
    }

    SECTION("Can omit both bounds.") {
        MocoStudy moco;
        auto& problem = moco.updProblem();
        problem.setModelCopy(ModelFactory::createPendulum());
        problem.setTimeBounds(0, 1);
        problem.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0);
        problem.setStateInfo("/jointset/j0/q0/speed", {-10, 10}, 0);
        problem.setControlInfo("/tau0", {-5, 5});
        problem.addCost<MocoControlCost>();
        auto* constr = problem.addPathConstraint<MocoControlBoundConstraint>();
        moco.solve();
        constr->addControlPath("/tau0");
        moco.solve();
    }
}
