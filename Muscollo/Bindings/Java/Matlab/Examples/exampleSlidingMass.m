% -------------------------------------------------------------------------- %
% OpenSim Muscollo: exampleSlidingMass.m                                     %
% -------------------------------------------------------------------------- %
% Copyright (c) 2017 Stanford University and the Authors                     %
%                                                                            %
% Author(s): Christopher Dembia                                              %
%                                                                            %
% Licensed under the Apache License, Version 2.0 (the "License"); you may    %
% not use this file except in compliance with the License. You may obtain a  %
% copy of the License at http://www.apache.org/licenses/LICENSE-2.0          %
%                                                                            %
% Unless required by applicable law or agreed to in writing, software        %
% distributed under the License is distributed on an "AS IS" BASIS,          %
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
% See the License for the specific language governing permissions and        %
% limitations under the License.                                             %
% -------------------------------------------------------------------------- %

import org.opensim.modeling.*;

model = Model();
model.setName('sliding_mass');
model.set_gravity(Vec3(0, 0, 0));
body = Body('body', 2.0, Vec3(0), Inertia(0));
model.addComponent(body);

% Allows translation along x.
joint = SliderJoint('slider', model.getGround(), body);
coord = joint.updCoordinate();
coord.setName('position');
model.addComponent(joint);

actu = CoordinateActuator();
actu.setCoordinate(coord);
actu.setName('actuator');
actu.setOptimalForce(1);
model.addComponent(actu);

body.attachGeometry(Sphere(0.05));

model.finalizeConnections();

% Create MucoTool.
% ================
muco = MucoTool();
muco.setName('sliding_mass');

% Define the optimal control problem.
% ===================================
problem = muco.updProblem();

% Model (dynamics).
% -----------------
problem.setModel(model);

% Bounds.
% -------
% Initial time must be 0, final time can be within [0, 5].
problem.setTimeBounds(MucoInitialBounds(0.), MucoFinalBounds(0., 5.));

% Initial position must be 0, final position must be 1.
problem.setStateInfo('/slider/position/value', MucoBounds(-5, 5), ...
    MucoInitialBounds(0), MucoFinalBounds(1));
% Initial and final speed must be 0. Use compact syntax.
problem.setStateInfo('/slider/position/speed', [-50, 50], [0], [0]);

% Applied force must be between -50 and 50.
problem.setControlInfo('/actuator', MucoBounds(-50, 50));

% Cost.
% -----
ftCost = MucoFinalTimeCost();
problem.addCost(ftCost);

% Configure the solver.
% =====================
solver = muco.initSolver();
solver.set_num_mesh_points(50);

% Now that we've finished setting up the tool, print it to a file.
muco.print('sliding_mass.omuco');

% Solve the problem.
% ==================
solution = muco.solve();

solution.write('sliding_mass_solution.sto');

if ~strcmp(getenv('OPENSIM_USE_VISUALIZER'), '0')
    muco.visualize(solution);
    plot(solution.getTimeMat(), solution.getStatesTrajectoryMat());
    xlabel('time (s)');
    ylabel('states');
    legend('/slider/position/value', '/slider/position/speed');
end
