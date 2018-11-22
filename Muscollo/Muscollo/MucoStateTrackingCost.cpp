/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: MucoStateTrackingCost.cpp                                *
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

#include "MucoStateTrackingCost.h"
#include "MuscolloUtilities.h"
#include <OpenSim/Simulation/Model/Model.h>

using namespace OpenSim;

void MucoStateTrackingCost::initializeOnModelImpl(const Model& model) const {

    TimeSeriesTable tableToUse;
    
    if (get_reference_file() != "") {
        // Should not be able to supply both.
        assert(m_table.getNumColumns() == 0);

        auto tablesFromFile = FileAdapter::readFile(get_reference_file());
        // There should only be one table.
        OPENSIM_THROW_IF_FRMOBJ(tablesFromFile.size() != 1, Exception,
                "Expected reference file '" + get_reference_file() +
                "' to contain 1 table, but it contains " +
                std::to_string(tablesFromFile.size()) + " tables.");
        // Get the first table.
        auto* firstTable =
                dynamic_cast<TimeSeriesTable*>(tablesFromFile.begin()->second.get());
        OPENSIM_THROW_IF_FRMOBJ(!firstTable, Exception,
                "Expected reference file to contain a (scalar) "
                "TimeSeriesTable, but it contains a different type of table.");
        tableToUse = *firstTable;
    } else if (m_table.getNumColumns() != 0) {
        tableToUse = m_table;
    } else {
        OPENSIM_THROW_FRMOBJ(Exception,
                "Expected user to either provide a reference"
                " file or to programmatically provide a reference table, but "
                " the user supplied neither.");
    }

    // Convert to degrees if needed and create spline set.
    if (tableToUse.hasTableMetaDataKey("inDegrees") &&
        tableToUse.getTableMetaDataAsString("inDegrees") == "yes") {
        model.getSimbodyEngine().convertDegreesToRadians(tableToUse);
    }
    auto allSplines = GCVSplineSet(tableToUse);

    // Throw exception if a weight is specified for a nonexistent state.
    auto allSysYIndices = createSystemYIndexMap(model);
    for (int i = 0; i < get_state_weights().getSize(); ++i) {
        const auto& weightName = get_state_weights().get(i).getName();
        if (allSysYIndices.count(weightName) == 0) {
            OPENSIM_THROW_FRMOBJ(Exception,
                "Weight provided with name '" + weightName + "' but this is "
                "not a recognized state.");
        }
    }

    // Populate member variables need to compute cost. Unless the property
    // allow_unused_references is set to true, an exception is thrown for
    // names in the references that don't correspond to a state variable. 
    for (int iref = 0; iref < allSplines.getSize(); ++iref) {
        const auto& refName = allSplines[iref].getName();
        if (allSysYIndices.count(refName) == 0) {
            if (get_allow_unused_references()) {
                continue;
            } else {
                OPENSIM_THROW_FRMOBJ(Exception,
                    "State '" + refName + "' unrecognized.");
            }
        }

        m_sysYIndices.push_back(allSysYIndices[refName]);
        double refWeight = 1.0;
        if (get_state_weights().contains(refName)) {
            refWeight = get_state_weights().get(refName).getWeight();
        }
        m_state_weights.push_back(refWeight);
        m_refsplines.cloneAndAppend(allSplines[iref]);
    }
}

void MucoStateTrackingCost::calcIntegralCostImpl(/*int meshIndex,*/
        const SimTK::State& state, double& integrand) const {
    const auto& time = state.getTime();

    SimTK::Vector timeVec(1, time);

    // TODO cache the reference coordinate values at the mesh points, rather
    // than evaluating the spline.
    for (int iref = 0; iref < m_refsplines.getSize(); ++iref) {
        const auto& modelValue = state.getY()[m_sysYIndices[iref]];
        const auto& refValue = m_refsplines[iref].calcValue(timeVec);
        integrand += m_state_weights[iref] * pow(modelValue - refValue, 2);
    }
}
