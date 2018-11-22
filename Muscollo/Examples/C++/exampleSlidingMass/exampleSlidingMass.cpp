/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: exampleSlidingMass.cpp                                   *
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

/// Translate a point mass in one dimension in minimum time. This is a very
/// simple example that shows only the basics of Muscollo.
///
/// @verbatim
/// minimize   t_f
/// subject to xdot = v
///            vdot = F/m
///            x(0)   = 0
///            x(t_f) = 1
///            v(0)   = 0
///            v(t_f) = 0
/// w.r.t.     x   in [-5, 5]    position of mass
///            v   in [-50, 50]  speed of mass
///            F   in [-50, 50]  force applied to the mass
///            t_f in [0, 5]     final time
/// constants  m       mass
/// @endverbatim

#include <OpenSim/Simulation/SimbodyEngine/SliderJoint.h>
#include <OpenSim/Actuators/CoordinateActuator.h>
#include <Muscollo/osimMuscollo.h>

using namespace OpenSim;

std::unique_ptr<Model> createSlidingMassModel() {
    auto model = make_unique<Model>();
    model->setName("sliding_mass");
    model->set_gravity(SimTK::Vec3(0, 0, 0));
    auto* body = new Body("body", 2.0, SimTK::Vec3(0), SimTK::Inertia(0));
    model->addComponent(body);

    // Allows translation along x.
    auto* joint = new SliderJoint("slider", model->getGround(), *body);
    auto& coord = joint->updCoordinate(SliderJoint::Coord::TranslationX);
    coord.setName("position");
    model->addComponent(joint);

    auto* actu = new CoordinateActuator();
    actu->setCoordinate(&coord);
    actu->setName("actuator");
    actu->setOptimalForce(1);
    model->addComponent(actu);

    body->attachGeometry(new Sphere(0.05));

    model->finalizeConnections();

    return model;
}

int main() {

    MucoTool muco;
    muco.setName("sliding_mass");

    // Define the optimal control problem.
    // ===================================
    MucoProblem& problem = muco.updProblem();

    // Model (dynamics).
    // -----------------
    problem.setModel(createSlidingMassModel());

    // Bounds.
    // -------
    // Initial time must be 0, final time can be within [0, 5].
    problem.setTimeBounds(MucoInitialBounds(0), MucoFinalBounds(0, 5));

    // Initial position must be 0, final position must be 1.
    problem.setStateInfo("/slider/position/value", MucoBounds(-5, 5),
            MucoInitialBounds(0), MucoFinalBounds(1));
    // Initial and final speed must be 0. Use compact syntax.
    problem.setStateInfo("/slider/position/speed", {-50, 50}, 0, 0);

    // Applied force must be between -50 and 50.
    problem.setControlInfo("/actuator", MucoBounds(-50, 50));

    // Cost.
    // -----
    problem.addCost<MucoFinalTimeCost>();

    // Configure the solver.
    // =====================
    MucoTropterSolver& solver = muco.initSolver();
    solver.set_num_mesh_points(50);

    // TODO interface for setting these options:
    // TODO solver.setOption("optim.hessian-approximation", "limited-memory");
    // TODO solver.set_optimizer_algorithm("ipopt");


    // Now that we've finished setting up the tool, print it to a file.
    muco.print("sliding_mass.omuco");

    // Solve the problem.
    // ==================
    MucoSolution solution = muco.solve();

    //solution.write("sliding_mass_solution.sto");

    // Visualize.
    // ==========
    muco.visualize(solution);

    return EXIT_SUCCESS;
}
