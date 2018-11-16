/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: testMuscolloInterface.cpp                                *
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

#include <Muscollo/osimMuscollo.h>
#include <OpenSim/Common/STOFileAdapter.h>
#include <OpenSim/Simulation/SimbodyEngine/SliderJoint.h>
#include <OpenSim/Simulation/SimbodyEngine/PinJoint.h>
#include <OpenSim/Simulation/Manager/Manager.h>
#include <OpenSim/Actuators/CoordinateActuator.h>

using namespace OpenSim;

// TODO
// - add setGuess
// - add documentation. pre/post conditions.
// - write test cases for exceptions, for calling methods out of order.
// - model_file vs model.
// - test problems without controls (including with setting guesses).
// - test that names for setStateInfo() are actual existing states in the model.

Model createSlidingMassModel() {
    Model model;
    model.setName("sliding_mass");
    model.set_gravity(SimTK::Vec3(0, 0, 0));
    auto* body = new Body("body", 10.0, SimTK::Vec3(0), SimTK::Inertia(0));
    model.addComponent(body);

    // Allows translation along x.
    auto* joint = new SliderJoint("slider", model.getGround(), *body);
    auto& coord = joint->updCoordinate(SliderJoint::Coord::TranslationX);
    coord.setName("position");
    model.addComponent(joint);

    auto* actu = new CoordinateActuator();
    actu->setCoordinate(&coord);
    actu->setName("actuator");
    actu->setOptimalForce(1);
    actu->setMinControl(-10);
    actu->setMaxControl(10);
    model.addComponent(actu);

    model.finalizeConnections();

    return model;
}

MucoTool createSlidingMassMucoTool() {
    MucoTool muco;
    muco.setName("sliding_mass");
    muco.set_write_solution("false");
    MucoProblem& mp = muco.updProblem();
    mp.setModel(createSlidingMassModel());
    mp.setTimeBounds(MucoInitialBounds(0), MucoFinalBounds(0, 10));
    mp.setStateInfo("/slider/position/value", MucoBounds(0, 1),
            MucoInitialBounds(0), MucoFinalBounds(1));
    mp.setStateInfo("/slider/position/speed", {-100, 100}, 0, 0);
    MucoFinalTimeCost ftCost;
    mp.addCost(ftCost);

    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(20);
    return muco;
}

/// This model is torque-actuated.
Model createPendulumModel() {
    Model model;
    model.setName("pendulum");

    using SimTK::Vec3;
    using SimTK::Inertia;

    auto* b0 = new Body("b0", 1, Vec3(0), Inertia(1));
    model.addBody(b0);

    // Default pose: COM of pendulum is 1 meter down from the pin.
    auto* j0 = new PinJoint("j0", model.getGround(), Vec3(0), Vec3(0),
            *b0, Vec3(0, 1.0, 0), Vec3(0));
    auto& q0 = j0->updCoordinate();
    q0.setName("q0");
    model.addJoint(j0);

    auto* tau0 = new CoordinateActuator();
    tau0->setCoordinate(&j0->updCoordinate());
    tau0->setName("tau0");
    tau0->setOptimalForce(1);
    model.addForce(tau0);

    // Add display geometry.
    Ellipsoid bodyGeometry(0.1, 0.5, 0.1);
    SimTK::Transform transform(SimTK::Vec3(0, 0.5, 0));
    auto* b0Center = new PhysicalOffsetFrame("b0_center", *b0, transform);
    b0->addComponent(b0Center);
    b0Center->attachGeometry(bodyGeometry.clone());

    model.finalizeConnections();

    return model;
}

void testSlidingMass() {
    MucoTool muco = createSlidingMassMucoTool();
    MucoSolution solution = muco.solve();
    int numTimes = 20;
    int numStates = 2;
    int numControls = 1;

    // Check dimensions and metadata of the solution.
    SimTK_TEST((solution.getStateNames() == std::vector<std::string>{
            "/slider/position/value",
            "/slider/position/speed"}));
    SimTK_TEST((solution.getControlNames() ==
            std::vector<std::string>{"/actuator"}));
    SimTK_TEST(solution.getTime().size() == numTimes);
    const auto& states = solution.getStatesTrajectory();
    SimTK_TEST(states.nrow() == numTimes);
    SimTK_TEST(states.ncol() == numStates);
    const auto& controls = solution.getControlsTrajectory();
    SimTK_TEST(controls.nrow() == numTimes);
    SimTK_TEST(controls.ncol() == numControls);

    // Check the actual solution.
    const double expectedFinalTime = 2.0;
    SimTK_TEST_EQ_TOL(solution.getTime().get(numTimes-1), expectedFinalTime,
            1e-2);
    const double half = 0.5 * expectedFinalTime;

    for (int itime = 0; itime < numTimes; ++itime) {
        const double& t = solution.getTime().get(itime);
        // Position is a quadratic.
        double expectedPos =
                t < half ? 0.5 * pow(t, 2)
                         : -0.5 * pow(t - half, 2) + 1.0 * (t - half) + 0.5;
        SimTK_TEST_EQ_TOL(states(itime, 0), expectedPos, 1e-2);

        double expectedSpeed = t < half ? t : 2.0 - t;
        SimTK_TEST_EQ_TOL(states(itime, 1), expectedSpeed, 1e-2);

        double expectedForce = t < half ? 10 : -10;
        SimTK_TEST_EQ_TOL(controls(itime, 0), expectedForce, 1e-2);
    }
}

void testSolverOptions() {
    MucoTool muco = createSlidingMassMucoTool();
    MucoTropterSolver& ms = muco.initSolver();
    MucoSolution solDefault = muco.solve();
    ms.set_verbosity(3); // Invalid value.
    SimTK_TEST_MUST_THROW_EXC(muco.solve(), Exception);
    ms.set_verbosity(2);

    ms.set_optim_solver("nonexistent");
    SimTK_TEST_MUST_THROW_EXC(muco.solve(), Exception);
    ms.set_optim_solver("ipopt");

    ms.set_optim_hessian_approximation("nonexistent");
    SimTK_TEST_MUST_THROW(muco.solve());
    ms.set_optim_hessian_approximation("limited-memory");

    {
        ms.set_optim_max_iterations(1);
        MucoSolution solution = muco.solve();
        SimTK_TEST(solution.isSealed());
        solution.unseal();
        SimTK_TEST(solution.getNumIterations() == 1);
        ms.set_optim_max_iterations(-1);
    }

    {
        ms.set_optim_convergence_tolerance(1e-2);
        MucoSolution solLooseConvergence = muco.solve();
        // Ensure that we unset max iterations from being 1.
        SimTK_TEST(solLooseConvergence.getNumIterations() > 1);
        SimTK_TEST(solLooseConvergence.getNumIterations() <
                solDefault.getNumIterations());
        ms.set_optim_convergence_tolerance(-1);
    }
    {
        // Tightening the constraint tolerance means more iterations.
        ms.set_optim_constraint_tolerance(1e-12);
        MucoSolution solution = muco.solve();
        SimTK_TEST(solution.getNumIterations() > solDefault.getNumIterations());
        ms.set_optim_constraint_tolerance(-1);
    }
}

/*

void testEmpty() {
    // It's possible to solve an empty problem.
    MucoTool muco;
    MucoSolution solution = muco.solve();
    // 100 is the default num_mesh_points.
    SimTK_TEST(solution.getTime().size() == 100);
    SimTK_TEST(solution.getStatesTrajectory().ncol() == 0);
    SimTK_TEST(solution.getStatesTrajectory().nrow() == 0);
    SimTK_TEST(solution.getControlsTrajectory().ncol() == 0);
    SimTK_TEST(solution.getControlsTrajectory().nrow() == 0);
}

void testOrderingOfCalls() {

    // Solve a problem, edit the problem, re-solve.
    {
        // It's fine to
        MucoTool muco = createSlidingMassMucoTool();
        auto& solver = muco.initSolver();
        muco.solve();
        // This flips the "m_solverInitialized" flag:
        muco.updProblem();
        // This will call initSolver() internally:
        muco.solve();
    }

    // Solve a problem, edit the problem, ask the solver to do something.
    {
        MucoTool muco = createSlidingMassMucoTool();
        auto& solver = muco.initSolver();
        muco.solve();
        // This resets the problem to null on the solver.
        muco.updProblem();
        // The solver can't do anything if you've edited the model.
        SimTK_TEST_MUST_THROW_EXC(solver.getProblem(), Exception);
        SimTK_TEST_MUST_THROW_EXC(solver.solve(), Exception);
    }

    // Solve a problem, edit the solver, re-solve.
    {
        MucoTool muco = createSlidingMassMucoTool();
        auto& solver = muco.initSolver();
        const int initNumMeshPoints = solver.get_num_mesh_points();
        MucoSolution sol0 = muco.solve();
        solver.set_num_mesh_points(2 * initNumMeshPoints);
        MucoSolution sol1 = muco.solve();
        solver.set_num_mesh_points(initNumMeshPoints);
        MucoSolution sol2 = muco.solve();
        // Ensure that changing the mesh has an effect.
        SimTK_TEST(!sol0.isNumericallyEqual(sol1));
        // Ensure we get repeatable results with the initial settings.
        SimTK_TEST(sol0.isNumericallyEqual(sol2));

    }
}

/// Test that we can read in a Muscollo setup file, solve, edit the setup,
/// re-solve.
void testOMUCOSerialization() {
    std::string fname = "testMuscolloInterface_testOMUCOSerialization.omuco";
    MucoSolution sol0;
    MucoSolution sol1;
    {
        MucoTool muco = createSlidingMassMucoTool();
        sol0 = muco.solve();
        muco.print(fname);
    }
    {
        MucoTool mucoDeserialized(fname);
        MucoSolution sol1 = mucoDeserialized.solve();
    }
    SimTK_TEST(sol0.isNumericallyEqual(sol1));
}

void testCopy() {
    MucoTool muco = createSlidingMassMucoTool();
    MucoSolution solution = muco.solve();
    std::unique_ptr<MucoTool> copy(muco.clone());
    MucoSolution solutionFromCopy = copy->solve();
    SimTK_TEST(solution.isNumericallyEqual(solutionFromCopy));


    // TODO what happens if just the MucoProblem is copied, or if just the
    // MucoSolver is copied?
}
 */

void testBounds() {
    {
        SimTK_TEST(!MucoBounds().isSet());
        SimTK_TEST(MucoBounds(5.3).isSet());
        SimTK_TEST(MucoBounds(5.3).isEquality());
        SimTK_TEST(MucoBounds(5.3, 5.3).isSet());
        SimTK_TEST(MucoBounds(5.3, 5.3).isEquality());
        SimTK_TEST(!MucoBounds(5.3, 5.3 + SimTK::SignificantReal).isEquality());

        SimTK_TEST(MucoBounds(5.3).isWithinBounds(5.3));
        SimTK_TEST(
                !MucoBounds(5.3).isWithinBounds(5.3 + SimTK::SignificantReal));
        SimTK_TEST(MucoBounds(5.2, 5.4).isWithinBounds(5.3));
    }
    // TODO what to do about clamped coordinates? Use the range in the
    // coordinate, or ignore that? I think that if the coordinate is clamped,
    // then

    // By default, the bounds for coordinates, if clamped, are the
    // coordinate's range.

    // TODO create intermediate class, Delegate or Proxy or Decorator
    // TODO or simply have a "finalize()" method on MucoProblem.
    /*
    {
        MucoTool muco;
        MucoProblem& mp = muco.updProblem();
        mp.setModel(createSlidingMassModel());
        mp.setStateInfo("slider/position/value", {}, 0, 0);
        // TODO something...
        SimTK_TEST(state_bounds.lower == coord.get_range(0));
        SimTK_TEST(state_bounds.upper == coord.get_range(0));
    }
     */

    // TODO what to do if the user does not specify info for some variables?

    // Get error if state/control name does not exist.

    {
        auto model = createSlidingMassModel();
        model.initSystem();
        {
            MucoTool muco;
            MucoProblem& mp = muco.updProblem();
            mp.setModel(model);
            mp.setStateInfo("nonexistent", {0, 1});
            SimTK_TEST_MUST_THROW_EXC(mp.initialize(model), Exception);
        }
        {
            MucoTool muco;
            MucoProblem& mp = muco.updProblem();
            mp.setModel(model);
            mp.setControlInfo("nonexistent", {0, 1});
            SimTK_TEST_MUST_THROW_EXC(mp.initialize(model), Exception);
        }
    }
    // TODO what if bounds are missing for some states?
}

void testBuildingProblem() {
    {
        MucoTool muco;
        MucoProblem& mp = muco.updProblem();
        mp.setModel(createSlidingMassModel());

        // Costs have the name "cost" by default.
        {
            MucoFinalTimeCost c0;
            SimTK_TEST(c0.getName() == "cost");
            mp.addCost(c0);
        }
        // Names of costs must be unique.
        {
            MucoFinalTimeCost c1;
            SimTK_TEST_MUST_THROW_EXC(mp.addCost(c1), Exception);
        }
        // Costs must have a name.
        {
            MucoFinalTimeCost cEmptyName;
            cEmptyName.setName("");
            SimTK_TEST_MUST_THROW_EXC(mp.addCost(cEmptyName), Exception);
        }
        // Parameters have the name "parameter" by default.
        {
            MucoParameter p0;
            SimTK_TEST(p0.getName() == "parameter");
            mp.addParameter(p0);
        }
        // Names of parameters must be unique.
        {
            MucoParameter p1;
            SimTK_TEST_MUST_THROW_EXC(mp.addParameter(p1), Exception);
        }
        // Parameters must have a name.
        {
            MucoParameter pEmptyName;
            pEmptyName.setName("");
            SimTK_TEST_MUST_THROW_EXC(mp.addParameter(pEmptyName), Exception);
        }

    }
}

void testStateTracking() {
    // TODO move to another test file?
    auto makeTool = []() {
        MucoTool muco;
        muco.setName("state_tracking");
        muco.set_write_solution("false");
        MucoProblem& mp = muco.updProblem();
        mp.setModel(createSlidingMassModel());
        mp.setTimeBounds(0, 1);
        mp.setStateInfo("/slider/position/value", {-1, 1});
        mp.setStateInfo("/slider/position/speed", {-100, 100});
        mp.setControlInfo("/actuator", {-50, 50});
        return muco;
    };

    // Reference trajectory.
    std::string fname = "testMuscolloInterface_testStateTracking_ref.sto";
    {
        TimeSeriesTable ref;
        ref.setColumnLabels({"/slider/position/value"});
        using SimTK::Pi;
        for (double time = -0.01; time < 1.02; time += 0.01) {
            // Move at constant speed from x=0 to x=1. Really basic stuff.
            ref.appendRow(time, {1.0 * time});
        }
        STOFileAdapter::write(ref, fname);
    }

    // Setting the TimeSeriesTable directly.
    MucoSolution solDirect;
    {
        auto muco = makeTool();
        MucoProblem& mp = muco.updProblem();
        MucoStateTrackingCost tracking;
        tracking.setReference(STOFileAdapter::read(fname));
        mp.addCost(tracking);
        MucoTropterSolver& ms = muco.initSolver();
        ms.set_num_mesh_points(5);
        ms.set_optim_hessian_approximation("exact");
        solDirect = muco.solve();
    }

    // Setting the reference to be a file.
    std::string setup_fname = "testMuscolloInterface_testStateTracking.omuco";
    if (std::ifstream(setup_fname).good()) std::remove(setup_fname.c_str());
    MucoSolution solFile;
    {

        auto muco = makeTool();
        MucoProblem& mp = muco.updProblem();
        MucoStateTrackingCost tracking;
        tracking.setReferenceFile(fname);
        mp.addCost(tracking);
        MucoTropterSolver& ms = muco.initSolver();
        ms.set_num_mesh_points(5);
        ms.set_optim_hessian_approximation("exact");
        solFile = muco.solve();
        muco.print(setup_fname);
    }

    // Run the tool from a setup file.
    MucoSolution solDeserialized;
    {
        MucoTool muco(setup_fname);
        solDeserialized = muco.solve();
    }

    SimTK_TEST(solDirect.isNumericallyEqual(solFile));
    SimTK_TEST(solFile.isNumericallyEqual(solDeserialized));

    // Error if neither file nor table were provided.
    {
        auto muco = makeTool();
        MucoProblem& mp = muco.updProblem();
        MucoStateTrackingCost tracking;
        mp.addCost(tracking);
        SimTK_TEST_MUST_THROW_EXC(muco.solve(), Exception);
    }

    // TODO error if data does not cover time window.

}

void testGuess() {
    MucoTool muco = createSlidingMassMucoTool();
    MucoTropterSolver& ms = muco.initSolver();
    const int N = 6;
    ms.set_num_mesh_points(N);

    std::vector<std::string> expectedStateNames{
            "/slider/position/value", "/slider/position/speed"
    };
    std::vector<std::string> expectedControlNames{"/actuator"};

    SimTK::Matrix expectedStatesTraj(N, 2);
    expectedStatesTraj.col(0) = 0.5; // bounds are [0, 1].
    expectedStatesTraj(0, 0) = 0; // initial value fixed to 0.
    expectedStatesTraj(N-1, 0) = 1; // final value fixed to 1.
    expectedStatesTraj.col(1) = 0.0; // bounds are [-100, 100]
    expectedStatesTraj(0, 1) = 0; // initial speed fixed to 0.
    expectedStatesTraj(N-1, 1) = 0; // final speed fixed to 1.

    SimTK::Matrix expectedControlsTraj(N, 1);
    expectedControlsTraj.col(0) = 0;

    // createGuess().
    // --------------

    // Initial guess based on bounds.
    {
        MucoIterate guess = ms.createGuess("bounds");
        SimTK_TEST(guess.getTime().size() == N);
        SimTK_TEST(guess.getStateNames() == expectedStateNames);
        SimTK_TEST(guess.getControlNames() == expectedControlNames);
        SimTK_TEST(guess.getTime()[0] == 0);
        SimTK_TEST_EQ(guess.getTime()[N-1], 5.0); // midpoint of bounds [0, 10]

        SimTK_TEST_EQ(guess.getStatesTrajectory(), expectedStatesTraj);
        SimTK_TEST_EQ(guess.getControlsTrajectory(), expectedControlsTraj);
    }

    // Random initial guess.
    {
        MucoIterate guess = ms.createGuess("random");
        SimTK_TEST(guess.getTime().size() == N);
        SimTK_TEST(guess.getStateNames() == expectedStateNames);
        SimTK_TEST(guess.getControlNames() == expectedControlNames);

        // The numbers are random, so we don't what they are; only that they
        // are different from the guess from bounds.
        SimTK_TEST_NOTEQ(guess.getStatesTrajectory(), expectedStatesTraj);
        SimTK_TEST_NOTEQ(guess.getControlsTrajectory(), expectedControlsTraj);
    }

    // Setting a guess programmatically.
    // ---------------------------------

    // Don't need a converged solution; so ensure the following tests are fast.
    ms.set_optim_max_iterations(2);

    ms.clearGuess();
    MucoIterate solNoGuess = muco.solve().unseal();
    {
        // Using the guess from bounds is the same as not providing a guess.


        ms.setGuess(ms.createGuess());
        MucoIterate solDefaultGuess = muco.solve().unseal();

        SimTK_TEST(solDefaultGuess.isNumericallyEqual(solNoGuess));

        // Can also use convenience version of setGuess().
        ms.setGuess("bounds");
        SimTK_TEST(muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // Using a random guess should give us a different "solution."
        ms.setGuess(ms.createGuess("random"));
        MucoIterate solRandomGuess = muco.solve().unseal();
        SimTK_TEST(!solRandomGuess.isNumericallyEqual(solNoGuess));

        // Convenience.
        ms.setGuess("random");
        SimTK_TEST(!muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // Clearing the guess works (this check must come after using a
        // random guess).
        ms.clearGuess();
        SimTK_TEST(muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // Can call clearGuess() multiple times with no weird issues.
        ms.clearGuess(); ms.clearGuess();
        SimTK_TEST(muco.solve().unseal().isNumericallyEqual(solNoGuess));
    }

    // Guess is incompatible with problem.
    {
        MucoIterate guess = ms.createGuess();
        // Delete the second state variable name.
        const_cast<std::vector<std::string>&>(guess.getStateNames()).resize(1);
        SimTK_TEST_MUST_THROW_EXC(ms.setGuess(std::move(guess)), Exception);
    }

    // Unrecognized guess type.
    SimTK_TEST_MUST_THROW_EXC(ms.createGuess("unrecognized"), Exception);
    SimTK_TEST_MUST_THROW_EXC(ms.setGuess("unrecognized"), Exception);

    // Setting a guess from a file.
    // ----------------------------
    {
        MucoIterate guess = ms.createGuess("bounds");
        // Use weird number to ensure the solver actually loads the file:
        guess.setControl("/actuator", SimTK::Vector(N, 13.28));
        const std::string fname = "testMuscolloInterface_testGuess_file.sto";
        guess.write(fname);
        ms.setGuessFile(fname);

        SimTK_TEST(ms.getGuess().isNumericallyEqual(guess));
        SimTK_TEST(!muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // Using setGuess(MucoIterate) overrides the file setting.
        ms.setGuess(ms.createGuess("bounds"));
        SimTK_TEST(muco.solve().unseal().isNumericallyEqual(solNoGuess));

        ms.setGuessFile(fname);
        SimTK_TEST(ms.getGuess().isNumericallyEqual(guess));
        SimTK_TEST(!muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // Clearing the file causes the default guess type to be used.
        ms.setGuessFile("");
        SimTK_TEST(muco.solve().unseal().isNumericallyEqual(solNoGuess));

        // TODO mismatched state/control names.

        // Solve from deserialization.
        // TODO
    }

    // Customize a guess.
    // ------------------
    // This is really just a test of the MucoIterate class.
    {
        MucoIterate guess = ms.createGuess();
        guess.setNumTimes(2);
        SimTK_TEST(SimTK::isNaN(guess.getTime()[0]));
        SimTK_TEST(SimTK::isNaN(guess.getStatesTrajectory()(0, 0)));
        SimTK_TEST(SimTK::isNaN(guess.getControlsTrajectory()(0, 0)));

        // TODO look at how TimeSeriesTable handles this.
        // Make sure this uses the initializer list variant.
        guess.setState("/slider/position/value", {2, 0.3});
        SimTK::Vector expectedv(2);
        expectedv[0] = 2;
        expectedv[1] = 0.3;
        SimTK_TEST_EQ(guess.getState("/slider/position/value"), expectedv);

        // Can use SimTK::Vector.
        expectedv[1] = 9.4;
        guess.setState("/slider/position/value", expectedv);
        SimTK_TEST_EQ(guess.getState("/slider/position/value"), expectedv);

        // Controls
        guess.setControl("/actuator", {1, 0.6});
        SimTK::Vector expecteda(2);
        expecteda[0] = 1.0;
        expecteda[1] = 0.6;
        SimTK_TEST_EQ(guess.getControl("/actuator"), expecteda);

        expecteda[0] = 0.7;
        guess.setControl("/actuator", expecteda);
        SimTK_TEST_EQ(guess.getControl("/actuator"), expecteda);


        // Errors.

        // Nonexistent state/control.
        SimTK_TEST_MUST_THROW_EXC(guess.setState("none", SimTK::Vector(2)),
                Exception);
        SimTK_TEST_MUST_THROW_EXC(guess.setControl("none", SimTK::Vector(2)),
                Exception);
        SimTK_TEST_MUST_THROW_EXC(guess.getState("none"), Exception);
        SimTK_TEST_MUST_THROW_EXC(guess.getControl("none"), Exception);

        // Incorrect length.
        SimTK_TEST_MUST_THROW_EXC(
                guess.setState("/slider/position/value", SimTK::Vector(1)),
                Exception);
        SimTK_TEST_MUST_THROW_EXC(
                guess.setControl("/actuator", SimTK::Vector(3)),
                Exception);

    }

    // Resampling.
    {
        ms.set_num_mesh_points(5);
        MucoIterate guess0 = ms.createGuess();
        guess0.setControl("/actuator",
                createVectorLinspace(5, 2.8, 7.3));
        SimTK_TEST(guess0.getTime().size() == 5); // midpoint of [0, 10]
        SimTK_TEST_EQ(guess0.getTime()[4], 5);

        // resampleWithNumTimes
        {
            MucoIterate guess = guess0;
            guess.resampleWithNumTimes(10);
            SimTK_TEST(guess.getTime().size() == 10);
            SimTK_TEST_EQ(guess.getTime()[9], 5);
            SimTK_TEST(guess.getStatesTrajectory().nrow() == 10);
            SimTK_TEST(guess.getControlsTrajectory().nrow() == 10);
            SimTK_TEST_EQ(guess.getControl("/actuator"),
                    createVectorLinspace(10, 2.8, 7.3));
        }

        // resampleWithInterval
        {
            MucoIterate guess = guess0;
            // We can't achieve exactly the interval the user provides.
            // time_interval = duration/(num_times - 1)
            // actual_num_times = ceil(duration/desired_interval) + 1
            // actual_interval = duration/(actual_num_times - 1)
            auto actualInterval = guess.resampleWithInterval(0.9);
            int expectedNumTimes = (int)ceil(5/0.9) + 1;
            SimTK_TEST_EQ(actualInterval, 5 / ((double)expectedNumTimes - 1));
            SimTK_TEST(guess.getTime().size() == expectedNumTimes);
            SimTK_TEST_EQ(guess.getTime()[expectedNumTimes - 1], 5);
            SimTK_TEST(guess.getStatesTrajectory().nrow() == expectedNumTimes);
            SimTK_TEST(
                    guess.getControlsTrajectory().nrow() == expectedNumTimes);
            SimTK_TEST_EQ(guess.getControl("/actuator"),
                    createVectorLinspace(expectedNumTimes, 2.8, 7.3));
        }

        // resampleWithFrequency
        {
            // We can't achieve exactly the interval the user provides.
            // frequency = num_times/duration
            MucoIterate guess = guess0;
            // Here, we also ensure that we can downsample.
            auto actualFrequency = guess.resampleWithFrequency(0.7);
            int expectedNumTimes = (int)ceil(5 * 0.7); // 4
            SimTK_TEST_EQ(actualFrequency, (double)expectedNumTimes / 5);
            SimTK_TEST(guess.getTime().size() == expectedNumTimes);
            SimTK_TEST_EQ(guess.getTime()[expectedNumTimes - 1], 5);
            SimTK_TEST(guess.getStatesTrajectory().nrow() == expectedNumTimes);
            SimTK_TEST(
                    guess.getControlsTrajectory().nrow() == expectedNumTimes);
            SimTK_TEST_EQ(guess.getControl("/actuator"),
                    createVectorLinspace(expectedNumTimes, 2.8, 7.3));
        }

    }

    // Number of points required for splining.
    {
        // 3 and 2 points are okay.
        ms.set_num_mesh_points(3);
        MucoIterate guess3 = ms.createGuess();
        guess3.resampleWithNumTimes(10);

        ms.set_num_mesh_points(2);
        MucoIterate guess2 = ms.createGuess();
        guess2.resampleWithNumTimes(10);

        // 1 point is too few.
        MucoIterate guess1(guess2);
        guess1.setNumTimes(1);
        SimTK_TEST_MUST_THROW_EXC(guess1.resampleWithNumTimes(10), Exception);
    }

    // TODO ordering of states and controls in MucoIterate should not matter!

    // TODO getting a guess, editing the problem, asking for another guess,
    // requires calling initSolver(). TODO can check the Problem's
    // isObjectUptoDateWithProperties(); simply flipping a flag with
    // updProblem() is not sufficient, b/c a user could make changes way
    // after they get the mutable reference.
}

void testGuessTimeStepping() {
    // This problem is just a simulation (there are no costs), and so the
    // forward simulation guess should reduce the number of iterations to
    // converge, and the guess and solution should also match our own forward
    // simulation.
    MucoTool muco;
    muco.setName("pendulum");
    muco.set_write_solution("false");
    auto& problem = muco.updProblem();
    problem.setModel(createPendulumModel());
    const SimTK::Real initialAngle = 0.25 * SimTK::Pi;
    // Make the simulation interesting.
    problem.setTimeBounds(0, 1);
    problem.setStateInfo("/jointset/j0/q0/value", {-10, 10}, initialAngle);
    problem.setStateInfo("/jointset/j0/q0/speed", {-50, 50}, 0);
    problem.setControlInfo("/forceset/tau0", 0);
    MucoTropterSolver& solver = muco.initSolver();
    solver.set_num_mesh_points(20);
    solver.setGuess("random");
    // With MUMPS: 4 iterations.
    MucoSolution solutionRandom = muco.solve();

    solver.setGuess("time-stepping");
    // With MUMPS: 2 iterations.
    MucoSolution solutionSim = muco.solve();

    SimTK_TEST(solutionSim.getNumIterations() <
            solutionRandom.getNumIterations());

    {
        MucoIterate guess = solver.createGuess("time-stepping");
        SimTK_TEST(solutionSim.compareContinuousVariablesRMS(guess) < 1e-2);

        Model modelCopy(muco.updProblem().getPhase().getModel());
        SimTK::State state = modelCopy.initSystem();
        modelCopy.setStateVariableValue(state, "/jointset/j0/q0/value", initialAngle);
        Manager manager(modelCopy, state);
        manager.integrate(1.0);

        auto controlsTable = modelCopy.getControlsTable();
        auto labels = controlsTable.getColumnLabels();
        for (auto& label : labels) { label = "/forceset/" + label; }
        controlsTable.setColumnLabels(labels);
        const auto iterateFromManager =
                MucoIterate::createFromStatesControlsTables(
                muco.updProblem(), manager.getStatesTable(), controlsTable);
        SimTK_TEST(solutionSim.compareContinuousVariablesRMS(iterateFromManager) <
                        1e-2);
    }

    // Ensure the forward simulation guess uses the correct time bounds.
    {
        muco.updProblem().setTimeBounds({-10, -5}, {6, 15});
        MucoTropterSolver& solver = muco.initSolver();
        MucoIterate guess = solver.createGuess("time-stepping");
        SimTK_TEST(guess.getTime()[0] == -5);
        SimTK_TEST(guess.getTime()[guess.getNumTimes()-1] == 6);
    }
}

void testMucoIterate() {
    // Reading and writing.
    {
        const std::string fname = "testMuscolloInterface_testMucoIterate.sto";
        SimTK::Vector time(3); time[0] = 0; time[1] = 0.1; time[2] = 0.25;
        MucoIterate orig(time, {"a", "b"}, {"g", "h", "i", "j"}, {"m"},
                {"n", "o"}, SimTK::Test::randMatrix(3, 2), 
                SimTK::Test::randMatrix(3, 4), SimTK::Test::randMatrix(3, 1),
                SimTK::Test::randVector(2).transpose());
        orig.write(fname);

        MucoIterate deserialized(fname);
        SimTK_TEST(deserialized.isNumericallyEqual(orig));
    }

    // Test sealing/unsealing.
    {
        // Create a class that gives access to the sealed functions, which are
        // otherwise protected.
        class MucoIterateDerived : public MucoIterate {
        public:
            using MucoIterate::MucoIterate;
            MucoIterateDerived* clone() const override
            {   return new MucoIterateDerived(*this); }
            void setSealedD(bool sealed) { MucoIterate::setSealed(sealed); }
            bool isSealedD() const { return MucoIterate::isSealed(); }
        };
        MucoIterateDerived iterate;
        SimTK_TEST(!iterate.isSealedD());
        iterate.setSealedD(true);
        SimTK_TEST(iterate.isSealedD());
        SimTK_TEST_MUST_THROW_EXC(iterate.getNumTimes(), MucoIterateIsSealed);
        SimTK_TEST_MUST_THROW_EXC(iterate.getTime(), MucoIterateIsSealed);
        SimTK_TEST_MUST_THROW_EXC(iterate.getStateNames(), MucoIterateIsSealed);
        SimTK_TEST_MUST_THROW_EXC(iterate.getControlNames(),
                MucoIterateIsSealed);
        SimTK_TEST_MUST_THROW_EXC(iterate.getControlNames(),
                MucoIterateIsSealed);

        // The clone() function doesn't call ensureSealed(), but the clone should
        // preserve the value of m_sealed.
        std::unique_ptr<MucoIterateDerived> ptr(iterate.clone());
        SimTK_TEST(ptr->isSealedD());
        SimTK_TEST_MUST_THROW_EXC(iterate.getNumTimes(), MucoIterateIsSealed);
    }


    // compareContinuousVariablesRMS
    auto testCompareContinuousVariablesRMS = [](int NT, int NS, int NC, int NM,
            double duration, double error,
            std::vector<std::string> statesToCompare = {},
            std::vector<std::string> controlsToCompare = {},
            std::vector<std::string> multipliersToCompare = {}) {
        const double t0 = 0.2;
        std::vector<std::string> snames;
        for (int i = 0; i < NS; ++i) snames.push_back("s" + std::to_string(i));
        std::vector<std::string> cnames;
        for (int i = 0; i < NC; ++i) cnames.push_back("c" + std::to_string(i));
        std::vector<std::string> mnames;
        for (int i = 0; i < NM; ++i) mnames.push_back("m" + std::to_string(i));
        SimTK::Matrix states(NT, NS);
        for (int i = 0; i < NS; ++i) {
            states.updCol(i) = createVectorLinspace(NT,
                    SimTK::Test::randDouble(), SimTK::Test::randDouble());
        }
        SimTK::Matrix controls(NT, NC);
        for (int i = 0; i < NC; ++i) {
            controls.updCol(i) = createVectorLinspace(NT,
                    SimTK::Test::randDouble(), SimTK::Test::randDouble());
        }
        SimTK::Matrix multipliers(NT, NM);
        for (int i = 0; i < NM; ++i) {
            multipliers.updCol(i) = createVectorLinspace(NT,
                SimTK::Test::randDouble(), SimTK::Test::randDouble());
        }
        SimTK::Vector time = createVectorLinspace(NT, t0, t0 + duration);
        MucoIterate a(time, snames, cnames, mnames, {}, states, controls, 
                multipliers, SimTK::RowVector());
        MucoIterate b(time, snames, cnames, mnames, {},
                states.elementwiseAddScalar(error),
                controls.elementwiseAddScalar(error),
                multipliers.elementwiseAddScalar(error),
                SimTK::RowVector());
        // If error is constant:
        // sqrt(1/T * integral_t (sum_i^N (err_{i,t}^2))) = sqrt(N)*err
        auto rmsBA = b.compareContinuousVariablesRMS(a, statesToCompare, 
            controlsToCompare, multipliersToCompare);
        int N = 0;
        if (statesToCompare.empty()) N += NS;
        else if (statesToCompare[0] == "none") N += 0;
        else N += (int)statesToCompare.size();
        if (controlsToCompare.empty()) N += NC;
        else if (controlsToCompare[0] == "none") N += 0;
        else N += (int)controlsToCompare.size();
        if (multipliersToCompare.empty()) N += NM;
        else if (multipliersToCompare[0] == "none") N += 0;
        else N += (int)multipliersToCompare.size();
        auto rmsExpected = sqrt(N) * error;
        SimTK_TEST_EQ(rmsBA, rmsExpected);
        auto rmsAB = a.compareContinuousVariablesRMS(b, statesToCompare, 
            controlsToCompare, multipliersToCompare);
        SimTK_TEST_EQ(rmsAB, rmsExpected);
    };

    testCompareContinuousVariablesRMS(10, 2, 1, 1, 0.6, 0.05);
    testCompareContinuousVariablesRMS(21, 2, 0, 2, 15.0, 0.01);
    // 6 is the minimum required number of times; ensure that it works.
    testCompareContinuousVariablesRMS(6, 0, 3, 0, 0.1, 0.9);

    // Providing a subset of states/columns to compare.
    testCompareContinuousVariablesRMS(10, 2, 3, 1, 0.6, 0.05, {"s1"});
    testCompareContinuousVariablesRMS(10, 2, 3, 1, 0.6, 0.05, {}, {"c1"});
    testCompareContinuousVariablesRMS(10, 2, 3, 1, 0.6, 0.05, {"none"}, 
        {"none"}, {"none"});
    // Can't provide "none" along with other state names.
    SimTK_TEST_MUST_THROW_EXC(
        testCompareContinuousVariablesRMS(10, 2, 3, 1, 0.6, 0.05, 
            {"none", "s1"}), Exception);
    SimTK_TEST_MUST_THROW_EXC(
        testCompareContinuousVariablesRMS(10, 2, 3, 1, 0.6, 0.05, {}, 
            {"none, c0"}), Exception);

    // compareParametersRMS
    auto testCompareParametersRMS = [](int NP, double error, 
        std::vector<std::string> parametersToCompare = {}) {
        std::vector<std::string> pnames;
        for (int i = 0; i < NP; ++i) pnames.push_back("p" + std::to_string(i));
        SimTK::RowVector parameters = SimTK::Test::randVector(NP).transpose();
        MucoIterate a(SimTK::Vector(), {}, {}, {}, pnames, SimTK::Matrix(),
            SimTK::Matrix(), SimTK::Matrix(), parameters);
        MucoIterate b(SimTK::Vector(), {}, {}, {}, pnames, SimTK::Matrix(),
            SimTK::Matrix(), SimTK::Matrix(),
            parameters.elementwiseAddScalar(error).getAsRowVector());
        // If error is constant:
        // sqrt(sum_i^N (err_{i}^2) / N) = err
        auto rmsBA = b.compareParametersRMS(a, parametersToCompare);
        auto rmsExpected = error;
        SimTK_TEST_EQ(rmsBA, rmsExpected);
        auto rmsAB = a.compareParametersRMS(b, parametersToCompare);
        SimTK_TEST_EQ(rmsAB, rmsExpected);
    };
    // Compare one parameter.
    testCompareParametersRMS(1, 0.01);
    // Compare subsets of available parameters.
    testCompareParametersRMS(5, 0.5);
    testCompareParametersRMS(5, 0.5, {"p0"});
    testCompareParametersRMS(5, 0.5, {"p1", "p2"});
    // Compare a lot of parameters.
    testCompareParametersRMS(100, 0.5);
}

void testInterpolate() {
    SimTK::Vector x(2);
    x[0] = 0;
    x[1] = 1;

    SimTK::Vector y(2);
    y[0] = 1;
    y[1] = 0;

    SimTK::Vector newX(4);
    newX[0] = -1;
    newX[1] = 0.25;
    newX[2] = 0.75;
    newX[3] = 1.5;

    SimTK::Vector newY = interpolate(x, y, newX);

    SimTK_TEST(SimTK::isNaN(newY[0]));
    SimTK_TEST_EQ(newY[1], 0.75);
    SimTK_TEST_EQ(newY[2], 0.25);
    SimTK_TEST(SimTK::isNaN(newY[3]));
}

int main() {
    SimTK_START_TEST("testMuscolloInterface");
        SimTK_SUBTEST(testSlidingMass);
        SimTK_SUBTEST(testSolverOptions);
        SimTK_SUBTEST(testStateTracking);
        SimTK_SUBTEST(testGuess);
        SimTK_SUBTEST(testGuessTimeStepping);
        SimTK_SUBTEST(testMucoIterate);

        //SimTK_SUBTEST(testEmpty);
        //SimTK_SUBTEST(testCopy);
        //SimTK_SUBTEST(testSolveRepeatedly);
        //SimTK_SUBTEST(testOMUCOSerialization);
        SimTK_SUBTEST(testBounds);
        SimTK_SUBTEST(testBuildingProblem);

        SimTK_SUBTEST(testInterpolate);
    SimTK_END_TEST();
}
