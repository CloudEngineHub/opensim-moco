/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoControlCost.cpp                                          *
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

#include "MocoControlCost.h"
#include <OpenSim/Simulation/Model/Model.h>
#include "../MocoUtilities.h"

using namespace OpenSim;

MocoControlCost::MocoControlCost() {
    constructProperties();
}

void MocoControlCost::constructProperties() {
    constructProperty_control_weights(MocoWeightSet());
    constructProperty_exponent(2);
}

void MocoControlCost::setWeight(
        const std::string& controlName, const double& weight) {
    if (get_control_weights().contains(controlName)) {
        upd_control_weights().get(controlName).setWeight(weight);
    } else {
        upd_control_weights().cloneAndAppend({controlName, weight});
    }
}

void MocoControlCost::initializeOnModelImpl(const Model& model) const {

    std::vector<std::string> actuPaths;
    const auto modelPath = model.getAbsolutePath();
    for (const auto& actu : model.getComponentList<Actuator>()) {
        OPENSIM_THROW_IF_FRMOBJ(actu.numControls() != 1, Exception,
                "Currently, only ScalarActuators are supported.");
        actuPaths.push_back(
                actu.getAbsolutePath().formRelativePath(modelPath).toString());
    }

    // TODO this assumes controls are in the same order as actuators.
    // The loop that processes weights (two down) assumes that controls are in
    // the same order as actuators. However, the control indices are allocated
    // in the order in which addToSystem() is invoked (not necessarily the order
    // used by getComponentList()). So until we can be absolutely sure that the
    // controls are in the asme order as actuators, we run the following check:
    // in order, set an actuator's control signal to NaN and ensure the i-th
    // control is NaN.
    {
        SimTK::Vector nan(1, SimTK::NaN);
        const SimTK::State state = model.getWorkingState();
        int i = 0;
        auto modelControls = model.updControls(state);
        for (const auto& actu : model.getComponentList<Actuator>()) {
            SimTK::Vector origControls(1);
            actu.getControls(modelControls, origControls);
            actu.setControls(nan, modelControls);
            OPENSIM_THROW_IF_FRMOBJ(!SimTK::isNaN(modelControls[i]), Exception,
                    "Internal error: actuators are not in the expected order. "
                    "Submit a bug report.");
            actu.setControls(origControls, modelControls);
            ++i;
        }
    }
    
    // Make sure there are no weights for nonexistent controls.
    for (int i = 0; i < get_control_weights().getSize(); ++i) {
        const auto& thisName = get_control_weights()[i].getName();
        if (std::find(actuPaths.begin(), actuPaths.end(), thisName) ==
                actuPaths.end()) {
            OPENSIM_THROW_FRMOBJ(Exception,
                    "Unrecognized control '" + thisName + "'.");
        }
    }

    m_weights.resize(model.getNumControls());
    int i = 0;
    for (const auto& actuPath : actuPaths) {
        double weight = 1.0;
        if (get_control_weights().contains(actuPath)) {
            weight = get_control_weights().get(actuPath).getWeight();
        }
        m_weights[i] = weight;
        ++i;
    }

    OPENSIM_THROW_IF_FRMOBJ(get_exponent() < 1, Exception,
            format("Exponent must be >= 1, but got %i.", get_exponent()));
    m_exponent = get_exponent();
}

void MocoControlCost::calcIntegralCostImpl(const SimTK::State& state,
        double& integrand) const {
    getModel().realizeVelocity(state); // TODO would avoid this, ideally.
    const auto& controls = getModel().getControls(state);
    integrand = 0;
    assert((int)m_weights.size() == controls.size());
    for (int i = 0; i < controls.size(); ++i) {
        integrand += m_weights[i] * pow(controls[i], m_exponent);
    }
}
