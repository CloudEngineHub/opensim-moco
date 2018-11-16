/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: sandboxMarkerTrackingWholeBody.cpp                       *
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

 /// Solves two tracking problems using a 10 DOF OpenSim model.

#include <OpenSim/Common/osimCommon.h>
#include <OpenSim/Simulation/osimSimulation.h>
#include <OpenSim/Actuators/osimActuators.h>
#include <OpenSim/Tools/InverseKinematicsTool.h>
#include <OpenSim/Common/TimeSeriesTable.h>
#include <Muscollo/osimMuscollo.h>

#include "MuscleLikeCoordinateActuator.h"

using namespace OpenSim;

class /*OSIMMUSCOLLO_API*/
    ActivationMuscleLikeCoordinateActuator : 
            public MuscleLikeCoordinateActuator {
    OpenSim_DECLARE_CONCRETE_OBJECT(ActivationMuscleLikeCoordinateActuator,
        MuscleLikeCoordinateActuator);
public:
    OpenSim_DECLARE_PROPERTY(activation_time_constant, double,
        "Larger value means activation can change more rapidly "
        "(units: seconds).");

    OpenSim_DECLARE_PROPERTY(default_activation, double,
        "Value of activation in the default state returned by initSystem().");

    ActivationMuscleLikeCoordinateActuator() {
        constructProperties();
    }

    void extendAddToSystem(SimTK::MultibodySystem& system) const override {
        Super::extendAddToSystem(system);
        addStateVariable("activation", SimTK::Stage::Dynamics);
    }

    void extendInitStateFromProperties(SimTK::State& s) const override {
        Super::extendInitStateFromProperties(s);
        setStateVariableValue(s, "activation", get_default_activation());
    }

    void extendSetPropertiesFromState(const SimTK::State& s) override {
        Super::extendSetPropertiesFromState(s);
        set_default_activation(getStateVariableValue(s, "activation"));
    }

    // TODO no need to do clamping, etc; CoordinateActuator is bidirectional.
    void computeStateVariableDerivatives(const SimTK::State& s) const override {
        const auto& tau = get_activation_time_constant();
        const auto& u = getControl(s);
        const auto& a = getStateVariableValue(s, "activation");
        const SimTK::Real adot = (u - a) / tau;
        setStateVariableDerivativeValue(s, "activation", adot);
    }

    double computeActuation(const SimTK::State& s) const override {
        return getStateVariableValue(s, "activation") * getOptimalForce();
    }
private:
    void constructProperties() {
        constructProperty_activation_time_constant(0.010);
        constructProperty_default_activation(0.5);
    }
};

/// Convenience function to apply an ActivationCoordinateActuator to the model.
void addActivationCoordinateActuator(Model& model, std::string coordName,
        double optimalForce) {

    auto& coordSet = model.updCoordinateSet();

    auto* actu = new ActivationCoordinateActuator();
    actu->set_default_activation(0.1);
    actu->setName("tau_" + coordName);
    actu->setCoordinate(&coordSet.get(coordName));
    actu->setOptimalForce(optimalForce);
    actu->setMinControl(-1);
    actu->setMaxControl(1);
    model.addComponent(actu);
}

/// Convenience function to apply an MuscleLikeCoordinateActuator to the model.
void addMuscleLikeCoordinateActuator(Model& model, std::string coordName, 
        double optimalForce) {

    auto& coordSet = model.updCoordinateSet();

    auto* actu = new ActivationMuscleLikeCoordinateActuator();
    actu->setName("tau_" + coordName);
    actu->setCoordinate(&coordSet.get(coordName));
    actu->setOptimalForce(optimalForce);
    actu->setMinControl(-1);
    actu->setMaxControl(1);

    auto* posFunc = new PolynomialFunction();
    posFunc->setName("pos_force_vs_coordinate_function");
    auto* negFunc = new PolynomialFunction();
    negFunc->setName("neg_force_vs_coordinate_function");

    // Polynomial coefficients from Carmichael Ong's SimTK project
    // "Predictive Simulation of Standing Long Jumps". Borrowed from the
    // "AshbyModel_twoConstraints.osim" model.
    if (coordName.find("hip") != std::string::npos) {
        posFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(27.175, -163.26, 146.58, 203.88)));
        negFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(-15.492, 0.99992, 188.07, 326.63)));
        actu->set_qdot_max(20.0);
    } else if (coordName.find("knee") != std::string::npos) {
        posFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(11.285, -135.23, 282.53, 238.77)));
        negFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(69.248, -454.99, 712.19, 203.07)));
        actu->set_qdot_max(18.0);
    } else if (coordName.find("ankle") != std::string::npos) {
        posFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(-80.378, -173.56, -102.12, 91.211)));
        negFunc->setCoefficients(
            SimTK::Vector(SimTK::Vec4(-748.14, -1054.1, 38.366, 407.2)));
        actu->set_qdot_max(16.0);
    }
    
    actu->setPosForceVsCoordinateFunction(posFunc);
    actu->setNegForceVsCoordinateFunction(negFunc);
    model.addComponent(actu);
}

/// Load the base OpenSim model (gait10dof18musc) and apply actuators based on
/// specified actuator type.
Model setupModel(bool usingMuscleLikeActuators) {

    Model model("subject01.osim");  

    addActivationCoordinateActuator(model, "lumbar_extension", 500);
    addActivationCoordinateActuator(model, "pelvis_tilt", 500);
    addActivationCoordinateActuator(model, "pelvis_tx", 1000);
    addActivationCoordinateActuator(model, "pelvis_ty", 2500);

    if (usingMuscleLikeActuators) {
        addMuscleLikeCoordinateActuator(model, "hip_flexion_r", 100);
        addMuscleLikeCoordinateActuator(model, "knee_angle_r", 100);
        addMuscleLikeCoordinateActuator(model, "ankle_angle_r", 100);
        addMuscleLikeCoordinateActuator(model, "hip_flexion_l", 100);
        addMuscleLikeCoordinateActuator(model, "knee_angle_l", 100);
        addMuscleLikeCoordinateActuator(model, "ankle_angle_l", 100);
    } else {
        addActivationCoordinateActuator(model, "hip_flexion_r", 100);
        addActivationCoordinateActuator(model, "knee_angle_r", 100);
        addActivationCoordinateActuator(model, "ankle_angle_r", 100);
        addActivationCoordinateActuator(model, "hip_flexion_l", 100);
        addActivationCoordinateActuator(model, "knee_angle_l", 100);
        addActivationCoordinateActuator(model, "ankle_angle_l", 100);
    }

    return model;
}

/// Set the bounds for the specified MucoProblem.
void setBounds(MucoProblem& mp) {

    double finalTime = 1.25;
    mp.setTimeBounds(0, finalTime);
    mp.setStateInfo("/tau_lumbar_extension/activation", { -1, 1 });
    mp.setStateInfo("/tau_pelvis_tilt/activation", {-1, 1});
    mp.setStateInfo("/tau_pelvis_tx/activation", { -1, 1 });
    mp.setStateInfo("/tau_pelvis_ty/activation", { -1, 1 });
    mp.setStateInfo("/tau_hip_flexion_r/activation", { -1, 1 });
    mp.setStateInfo("/tau_knee_angle_r/activation", { -1, 1 });
    mp.setStateInfo("/tau_ankle_angle_r/activation", { -1, 1 });
    mp.setStateInfo("/tau_hip_flexion_l/activation", { -1, 1 });
    mp.setStateInfo("/tau_knee_angle_l/activation", { -1, 1 });
    mp.setStateInfo("/tau_ankle_angle_l/activation", { -1, 1 });
}

/// Solve a full-body (10 DOF) tracking problem by having the model markers
/// track the marker trajectories directly.
///
/// Estimated time to solve: 45-95 minutes.
MucoSolution solveMarkerTrackingProblem(bool usingMuscleLikeActuators, 
        bool prevSolutionInitialization) {

    MucoTool muco;
    muco.setName("whole_body_marker_tracking");

    // Define the optimal control problem.
    // ===================================
    MucoProblem& mp = muco.updProblem();

    // Model(dynamics).
    // -----------------
    mp.setModel(setupModel(usingMuscleLikeActuators));

    // Bounds.
    // -------
    setBounds(mp);
        
    // Cost.
    // -----
    MucoMarkerTrackingCost tracking;
    tracking.setName("tracking");
    auto ref = TRCFileAdapter::read("marker_trajectories.trc");

    // Set marker weights to match IK task weights.
    Set<MarkerWeight> markerWeights;
    markerWeights.cloneAndAppend({ "Top.Head", 3 });
    markerWeights.cloneAndAppend({ "R.ASIS", 3 });
    markerWeights.cloneAndAppend({ "L.ASIS", 3 });
    markerWeights.cloneAndAppend({ "V.Sacral", 3 });
    markerWeights.cloneAndAppend({ "R.Heel", 2 });
    markerWeights.cloneAndAppend({ "R.Toe.Tip", 2 });
    markerWeights.cloneAndAppend({ "L.Heel", 2 });
    markerWeights.cloneAndAppend({ "L.Toe.Tip", 2 });
    MarkersReference markersRef(ref, &markerWeights);
    
    tracking.setMarkersReference(markersRef);
    tracking.setAllowUnusedReferences(true);
    mp.addCost(tracking);

    // Configure the solver.
    // =====================
    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(10);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_hessian_approximation("exact");

    // Create guess.
    // =============
    if (prevSolutionInitialization) {
        MucoIterate prevSolution(
            "sandboxMarkerTrackingWholeBody_marker_solution.sto");
        ms.setGuess(prevSolution);
    }

    // Solve the problem.
    // ==================
    MucoSolution solution = muco.solve();
    std::string output;
    if (usingMuscleLikeActuators) {
        output = "sandboxMarkerTrackingWholeBody_marker_solution_AMLCAs.sto";
    } else {
        output = "sandboxMarkerTrackingWholeBody_marker_solution_ACAs.sto";
    }
    solution.write(output);

    muco.visualize(solution);

    return solution;
}

int main() {


    MucoSolution markerTrackingSolution = solveMarkerTrackingProblem(true, 
        false);

    return EXIT_SUCCESS;
}
