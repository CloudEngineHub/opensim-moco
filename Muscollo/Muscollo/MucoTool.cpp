/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: MucoTool.cpp                                             *
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
#include "MucoTool.h"
#include "MucoProblem.h"
#include "MucoTropterSolver.h"
#include "MuscolloUtilities.h"

#include <OpenSim/Simulation/StatesTrajectory.h>
#include <OpenSim/Common/IO.h>

using namespace OpenSim;

MucoTool::MucoTool() {
    constructProperties();
}

MucoTool::MucoTool(const std::string& omucoFile) : Object(omucoFile) {
    constructProperties();
    updateFromXMLDocument();
}

void MucoTool::constructProperties() {
    constructProperty_write_solution("./");
    constructProperty_problem(MucoProblem());
    constructProperty_solver(MucoTropterSolver());
}

const MucoProblem& MucoTool::getProblem() const {
    return get_problem();
}

MucoProblem& MucoTool::updProblem() {
    return upd_problem();
}

MucoSolver& MucoTool::initSolverInternal() {
    // TODO what to do if we already have a solver (from cloning?)
    upd_solver().resetProblem(get_problem());
    return upd_solver();
}

MucoTropterSolver& MucoTool::initSolver() {
    return initCustomSolver<MucoTropterSolver>();
}

MucoTropterSolver& MucoTool::updSolver() {
    return updCustomSolver<MucoTropterSolver>();
}

template <typename SolverType>
void MucoTool::setCustomSolver() {
    set_solver(SolverType());
}

MucoSolution MucoTool::solve() const {
    // TODO avoid const_cast.
    const_cast<Self*>(this)->initSolverInternal();
    MucoSolution solution = get_solver().solve();
    bool originallySealed = solution.isSealed();
    if (get_write_solution() != "false") {
        OpenSim::IO::makeDir(get_write_solution());
        std::string prefix = getName().empty() ? "MucoTool" : getName();
        solution.unseal();
        solution.write(get_write_solution() +
                SimTK::Pathname::getPathSeparator() + prefix + "_solution.sto");
        if (originallySealed) solution.seal();
    }
    return solution;
}

void MucoTool::visualize(const MucoIterate& it) const {
    // TODO this does not need the Solver at all, so this could be moved to
    // MucoProblem.
    const auto& model = get_problem().getPhase(0).getModel();
    OpenSim::visualize(model, it.exportToStatesStorage());
}
