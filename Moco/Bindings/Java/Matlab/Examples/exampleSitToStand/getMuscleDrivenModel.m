function [model] = getMuscleDrivenModel(ignoreActDyn, subjectInfo)

import org.opensim.modeling.*;

% Load the base model.
model = Model('sitToStand_3dof9musc.osim');
model.finalizeConnections();

% Replace the muscles in the model with muscles from DeGroote, Fregly,
% et al. 2016, "Evaluation of Direct Collocation Optimal Control Problem
% Formulations for Solving the Muscle Redundancy Problem". These muscles
% have the same properties as the original muscles but their characteristic
% curves are optimized for direct collocation (i.e. no discontinuities,
% twice differentiable, etc).
DeGrooteFregly2016Muscle().replaceMuscles(model);

% TODO move this function to a separate file.
if nargin >= 1
    ignore_act_dyn = ignoreActDyn;
else
    ignore_act_dyn = false;
end

if nargin == 2
    fields = fieldnames(subjectInfo);
    for ifield = 1:numel(fields)
        if ~strcmp(fields{ifield}, 'name')
            body = model.updBodySet().get(fields{ifield});
            origMass = body.getMass();
            factor = subjectInfo.(fields{ifield});
            body.setMass(factor * origMass);
        end
    end
end

% Turn off activation dynamics and muscle-tendon dynamics to keep the
% problem simple.
for m = 0:model.getMuscles().getSize()-1
    musc = model.updMuscles().get(m);
    musc.setMinControl(0);
    musc.set_ignore_activation_dynamics(ignore_act_dyn);
    musc.set_ignore_tendon_compliance(true);
    musc.set_max_isometric_force(2 * musc.get_max_isometric_force());
    dgf = DeGrooteFregly2016Muscle.safeDownCast(musc);
    dgf.set_active_force_width_scale(1.5);
    if strcmp(char(musc.getName()), 'soleus_r')
        % Soleus has a very long tendon, so modeling its tendon as rigid
        % causes the fiber to be unrealistically long and generate
        % excessive passive fiber force.
        dgf.set_ignore_passive_fiber_force(true);
    end
end

end

