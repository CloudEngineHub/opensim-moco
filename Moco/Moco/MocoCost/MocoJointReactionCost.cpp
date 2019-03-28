/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoJointReactionCost.h                                  *
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

#include "MocoJointReactionCost.h"
#include "../MocoUtilities.h"
#include <OpenSim/Simulation/Model/Model.h>
    
using namespace OpenSim;

MocoJointReactionCost::MocoJointReactionCost() {
    constructProperties();
}

void MocoJointReactionCost::constructProperties() {
    constructProperty_joint_path("");
    constructProperty_reaction_component(-1);
}

void MocoJointReactionCost::initializeOnModelImpl(
        const Model& model) const {

    OPENSIM_THROW_IF_FRMOBJ(get_joint_path().empty(), Exception,
        "Empty model joint path detected. Please provide a valid joint path.");

    OPENSIM_THROW_IF_FRMOBJ(!model.hasComponent<Joint>(get_joint_path()),
        Exception,
        format("Joint at path %s not found in the model. "
               "Please provide a valid joint path.", get_joint_path()));
    
    OPENSIM_THROW_IF_FRMOBJ(get_reaction_component() < -1 ||
                            get_reaction_component() > 5, Exception, 
            "Invalid reaction component value.");

    if (get_reaction_component() < 3) {
        m_vec = 0;
        m_elt = get_reaction_component();
    } else {
        m_vec = 1;
        m_elt = get_reaction_component() - 3;
    }

    m_joint = &getModel().getComponent<Joint>(get_joint_path());
}

void MocoJointReactionCost::calcIntegralCostImpl(const SimTK::State& state,
        double& integrand) const {

    getModel().realizeAcceleration(state);
    double reaction;
    if (get_reaction_component() == -1) {
        reaction = m_joint->calcReactionOnChildExpressedInGround(state).norm();
    } else { 
        reaction = 
            m_joint->calcReactionOnChildExpressedInGround(state)[m_vec][m_elt];
    }
    integrand = reaction * reaction;
}
