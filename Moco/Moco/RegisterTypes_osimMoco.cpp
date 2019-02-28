/* -------------------------------------------------------------------------- *
 * OpenSim Moco: RegisterTypes_osimMoco.cpp                                   *
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
#include "RegisterTypes_osimMoco.h"
#include <OpenSim/Common/Object.h>

#include "MocoCost/MocoCost.h"
#include "MocoBounds.h"
#include "MocoProblem.h"
#include "MocoSolver.h"
#include "MocoTool.h"
#include "MocoTropterSolver.h"
#include "InverseMuscleSolver/GlobalStaticOptimization.h"
#include "InverseMuscleSolver/INDYGO.h"
#include "MocoWeightSet.h"
#include "MocoCost/MocoStateTrackingCost.h"
#include "MocoCost/MocoMarkerTrackingCost.h"
#include "MocoCost/MocoMarkerEndpointCost.h"
#include "MocoCost/MocoControlCost.h"
#include "MocoCost/MocoJointReactionNormCost.h"
#include "MocoParameter.h"
#include "ActivationCoordinateActuator.h"

#include "MocoCasADiSolver/MocoCasADiSolver.h"

#include "Components/StationPlaneContactForce.h"
#include "Components/PositionMotion.h"

// TODO: Move to osimSimulation.
#include <OpenSim/Simulation/MarkersReference.h>

#include <exception>
#include <iostream>

using namespace OpenSim;

static osimMocoInstantiator instantiator;

OSIMMOCO_API void RegisterTypes_osimMoco() {
    try {
        Object::registerType(MocoFinalTimeCost());
        Object::registerType(MocoWeight());
        Object::registerType(MocoWeightSet());
        Object::registerType(MocoStateTrackingCost());
        Object::registerType(MocoMarkerTrackingCost());
        Object::registerType(MocoMarkerEndpointCost());
        Object::registerType(MocoControlCost());
        Object::registerType(MocoJointReactionNormCost());
        Object::registerType(MocoBounds());
        Object::registerType(MocoInitialBounds());
        Object::registerType(MocoFinalBounds());
        Object::registerType(MocoPhase());
        Object::registerType(MocoVariableInfo());
        Object::registerType(MocoProblem());
        Object::registerType(MocoTool());
        Object::registerType(MocoTropterSolver());
        Object::registerType(MocoParameter());

        Object::registerType(MocoCasADiSolver());

        Object::registerType(ActivationCoordinateActuator());
        Object::registerType(GlobalStaticOptimization());
        Object::registerType(INDYGO());

        Object::registerType(AckermannVanDenBogert2010Force());
        Object::registerType(MeyerFregly2016Force());
        Object::registerType(EspositoMiller2018Force());
        Object::registerType(PositionMotion());

        // TODO: Move to osimSimulation.
        Object::registerType(MarkersReference());
        Object::registerType(MarkerWeight());
        Object::registerType(Set<MarkerWeight>());
    } catch (const std::exception& e) {
        std::cerr << "ERROR during osimMoco Object registration:\n"
                << e.what() << std::endl;
    }
}

osimMocoInstantiator::osimMocoInstantiator() {
    registerDllClasses();
}

void osimMocoInstantiator::registerDllClasses() {
    RegisterTypes_osimMoco();
}
