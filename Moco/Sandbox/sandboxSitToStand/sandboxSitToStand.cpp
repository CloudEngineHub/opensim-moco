/* -------------------------------------------------------------------------- *
 * OpenSim Moco: sandboxSitToStand.cpp                                        *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2019 Stanford University and the Authors                     *
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

#include <Moco/osimMoco.h>
#include <OpenSim/Common/osimCommon.h>
#include <OpenSim/Simulation/osimSimulation.h>
#include <OpenSim/Actuators/osimActuators.h>

using namespace OpenSim;
using SimTK::Vec3;
using SimTK::Inertia;
using SimTK::Transform;

 /// Convenience function to apply an CoordinateActuator to the model.
void addCoordinateActuator(Model& model, std::string coordName,
    double optimalForce) {

    auto& coordSet = model.updCoordinateSet();

    auto* actu = new CoordinateActuator();
    actu->setName("tau_" + coordName);
    actu->setCoordinate(&coordSet.get(coordName));
    actu->setOptimalForce(1);
    actu->setMinControl(-optimalForce);
    actu->setMaxControl(optimalForce);
    model.addComponent(actu);
}

/// This essentially removes the effect of passive muscle fiber forces from the 
/// model.
void minimizePassiveFiberForces(Model& model) {
    const auto& muscleSet = model.getMuscles();
    Array<std::string> muscNames;
    muscleSet.getNames(muscNames);
    for (int i = 0; i < muscNames.size(); ++i) {
        const auto& name = muscNames.get(i);
        FiberForceLengthCurve fflc(
            model.getComponent<Millard2012EquilibriumMuscle>(
                "/forceset/" + name).getFiberForceLengthCurve());
        fflc.set_strain_at_one_norm_force(100000);
        fflc.set_stiffness_at_low_force(0.00000001);
        fflc.set_stiffness_at_one_norm_force(0.0001);
        fflc.set_curviness(0);
        model.updComponent<Millard2012EquilibriumMuscle>(
            "/forceset/" + name).setFiberForceLengthCurve(fflc);
    }
}

Model createModel(const std::string& actuatorType) {

    Model model("Rajagopal2015_bottom_up.osim");

    addCoordinateActuator(model, "knee_adduction_r", 50);
    addCoordinateActuator(model, "knee_adduction_l", 50);
    addCoordinateActuator(model, "hip_adduction_r", 50);
    addCoordinateActuator(model, "hip_adduction_l", 50);
    addCoordinateActuator(model, "hip_rotation_r", 50);
    addCoordinateActuator(model, "hip_rotation_l", 50);
    //replaceJointWithWeldJoint(model, "back");

    //replaceJointWithWeldJoint(model, "subtalar_r");
    //replaceJointWithWeldJoint(model, "mtp_r");
    //replaceJointWithWeldJoint(model, "subtalar_l");
    //replaceJointWithWeldJoint(model, "mtp_l");

    //// Replace hip_r ball joint and replace with pin joint.
    //auto& hip_r = model.updJointSet().get("hip_r");
    //PhysicalOffsetFrame* pelvis_offset_hip_r
    //    = PhysicalOffsetFrame().safeDownCast(hip_r.getParentFrame().clone());
    //PhysicalOffsetFrame* femur_r_offset_hip_r
    //    = PhysicalOffsetFrame().safeDownCast(hip_r.getChildFrame().clone());
    //model.updJointSet().remove(&hip_r);
    //auto* hip_r_pin = new PinJoint("hip_r",
    //    model.getBodySet().get("pelvis"),
    //    pelvis_offset_hip_r->get_translation(),
    //    pelvis_offset_hip_r->get_orientation(),
    //    model.getBodySet().get("femur_r"),
    //    femur_r_offset_hip_r->get_translation(),
    //    femur_r_offset_hip_r->get_orientation());
    //hip_r_pin->updCoordinate().setName("hip_flexion_r");
    //model.addJoint(hip_r_pin);

    //// Replace hip_l ball joint and replace with pin joint.
    //auto& hip_l = model.updJointSet().get("hip_l");
    //PhysicalOffsetFrame* pelvis_offset_hip_l
    //    = PhysicalOffsetFrame().safeDownCast(hip_l.getParentFrame().clone());
    //PhysicalOffsetFrame* femur_l_offset_hip_l
    //    = PhysicalOffsetFrame().safeDownCast(hip_l.getChildFrame().clone());
    //model.updJointSet().remove(&hip_l);
    //auto* hip_l_pin = new PinJoint("hip_l",
    //    model.getBodySet().get("pelvis"),
    //    pelvis_offset_hip_l->get_translation(),
    //    pelvis_offset_hip_l->get_orientation(),
    //    model.getBodySet().get("femur_l"),
    //    femur_l_offset_hip_l->get_translation(),
    //    femur_l_offset_hip_l->get_orientation());
    //hip_l_pin->updCoordinate().setName("hip_flexion_l");
    //model.addJoint(hip_l_pin);

    //if (actuatorType == "torques") {
    //    // Remove muscles and add coordinate actuators.

    //    //removeMuscles(model);
    //}
    //else if (actuatorType == "muscles") {

    //    // Remove effect of passive fiber forces.
    //    minimizePassiveFiberForces(model);
    //}

    // Finalize model and print.
    //model.finalizeFromProperties();
    //model.finalizeConnections();
    //model.print("Rajagopal2015_armless_weldedFeet_" + actuatorType + ".osim");

    return model;

}

struct Options {
    std::string actuatorType = "torques";
    int num_mesh_points = 10;
    double convergence_tol = 1e-2;
    double constraint_tol = 1e-2;
    int max_iterations = 100000;
    std::string hessian_approximation = "limited-memory";
    std::string solver = "ipopt";
    std::string dynamics_mode = "explicit";
    TimeSeriesTable controlsGuess = {};
    MocoIterate previousSolution = {};
};

MocoSolution minimizeControlEffort(const Options& opt) {
    MocoTool moco;
    MocoProblem& mp = moco.updProblem();
    Model model = createModel(opt.actuatorType);
    mp.setModelCopy(model);

    // Set bounds.
    mp.setTimeBounds(0, 1);
    mp.setStateInfo("/jointset/hip_r/hip_flexion_r/value", {-1, 1}, -0.2, 0);
    mp.setStateInfo("/jointset/hip_r/hip_adduction_r/value", {-1, 1}, {-1, 1}, 0);
    mp.setStateInfo("/jointset/hip_r/hip_rotation_r/value", {-1, 1}, {-1, 1}, 0);
    mp.setStateInfo("/jointset/walker_knee_r/knee_angle_r/value", {-3, 0}, -0.2, 0);
    mp.setStateInfo("/jointset/walker_knee_r/knee_adduction_r/value", {-0.1, 0.1}, {-0.1, 0.1}, 0);
    mp.setStateInfo("/jointset/ankle_r/ankle_angle_r/value", {-0.55, 0.7}, -0.2, 0);

    mp.setStateInfo("/jointset/hip_r/hip_flexion_r/speed", {-50, 50});
    mp.setStateInfo("/jointset/walker_knee_r/knee_angle_r/speed", {-50, 50});
    mp.setStateInfo("/jointset/ankle_r/ankle_angle_r/speed", {-50, 50});

    mp.setStateInfo("/jointset/hip_l/hip_flexion_l/value", {-1, 1}, -0.2, 0);
    mp.setStateInfo("/jointset/hip_l/hip_adduction_l/value", {-1, 1}, {-1, 1}, 0);
    mp.setStateInfo("/jointset/hip_l/hip_rotation_l/value", {-1, 1}, {-1, 1}, 0);
    mp.setStateInfo("/jointset/walker_knee_l/knee_angle_l/value", {-3, 0}, -0.2, 0);
    mp.setStateInfo("/jointset/walker_knee_l/knee_adduction_l/value", {-0.1, 0.1}, {-0.1, 0.1}, 0);
    mp.setStateInfo("/jointset/ankle_l/ankle_angle_l/value", {-0.55, 0.7}, -0.2, 0);

    mp.setStateInfo("/jointset/hip_l/hip_flexion_l/speed", {-50, 50});
    mp.setStateInfo("/jointset/walker_knee_l/knee_angle_l/speed", {-50, 50});
    mp.setStateInfo("/jointset/ankle_l/ankle_angle_l/speed", {-50, 50});

    auto* effort = mp.addCost<MocoControlCost>();
    effort->setName("control_effort");

    // Set solver options.
    // -------------------
    auto& ms = moco.initCasADiSolver();
    ms.set_num_mesh_points(opt.num_mesh_points);
    ms.set_verbosity(2);
    ms.set_dynamics_mode(opt.dynamics_mode);
    ms.set_optim_convergence_tolerance(opt.convergence_tol);
    ms.set_optim_constraint_tolerance(opt.constraint_tol);
    ms.set_optim_solver(opt.solver);
    ms.set_transcription_scheme("hermite-simpson");
    ms.set_optim_max_iterations(opt.max_iterations);
    ms.set_enforce_constraint_derivatives(true);
    //ms.set_velocity_correction_bounds({-0.0001, 0.0001});
    //ms.set_minimize_lagrange_multipliers(true);
    //ms.set_lagrange_multiplier_weight(10);
    ms.set_optim_hessian_approximation(opt.hessian_approximation);
    ms.set_optim_finite_difference_scheme("forward");

    // Create guess.
    // -------------
    auto guess = ms.createGuess("bounds");
    ms.setGuess(guess);

    MocoSolution solution = moco.solve().unseal();
    moco.visualize(solution);

    return solution;
}

int main() {

    // Set options.
    Options opt;
    opt.num_mesh_points = 10;
    opt.solver = "ipopt";
    opt.constraint_tol = 1e-2;
    opt.convergence_tol = 1e-2;
    //opt.max_iterations = 50;

    // Predictive problem.
    MocoSolution torqueSolEffortCasADi = minimizeControlEffort(opt);


    return EXIT_SUCCESS;
}