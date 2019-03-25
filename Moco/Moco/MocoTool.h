#ifndef MOCO_MOCOTOOL_H
#define MOCO_MOCOTOOL_H
/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoTool.h                                                   *
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

#include <OpenSim/Common/Object.h>
#include <OpenSim/Simulation/Model/Model.h>

#include "MocoSolver.h"

namespace OpenSim {

class MocoProblem;
class MocoTropterSolver;
class MocoCasADiSolver;

/// The top-level class for solving a custom optimal control problem.
///
/// This class consists of a MocoProblem, which describes the optimal control
/// problem, and a MocoSolver, which describes the numerical method for
/// solving the problem.
///
/// Workflow
/// --------
/// When building a MocoTool programmatically (e.g., in C++), the workflow is as
/// follows:
///
/// 1. Build the MocoProblem (set the model, constraints, etc.).
/// 2. Call MocoTool::initSolver(), which returns a reference to the MocoSolver.
///    After this, you cannot edit the MocoProblem.
/// 3. Edit the settings of the MocoSolver (returned by initSolver()).
/// 4. Call MocoTool::solve(). This returns the MocoSolution.
/// 5. (Optional) Postprocess the solution, perhaps using MocoTool::visualize().
///
/// After calling solve(), you can edit the MocoProblem and/or the MocoSolver.
/// You can then call solve() again, if you wish.
///
/// Saving the tool setup to a file
/// -------------------------------
/// You can save the MocoTool to a file by calling MocoTool::print(), and you
/// can load the setup using MocoTool(const std::string& omocoFile).
/// MocoTool setup files have a `.omoco` extension.
///
/// Solver
/// ------
/// The default solver uses the **tropter** direct
/// collocation library. We also provide the **CasADi** solver, which
/// depends on the **CasADi** automatic differentiation and optimization library.
/// If you want to use CasADi programmatically, call initCasADiSolver() before
/// solve().
/// We would like to support users plugging in their own
/// solvers, but there is no timeline for this. If you require additional
/// features or enhancements to the solver, please consider contributing to
/// **tropter**.

// TODO rename to MocoFramework.

class OSIMMOCO_API MocoTool : public Object {
    OpenSim_DECLARE_CONCRETE_OBJECT(MocoTool, Object);
public:
    OpenSim_DECLARE_PROPERTY(write_solution, std::string,
    "Provide the folder path (relative to working directory) to which the "
    "solution files should be written. Set to 'false' to not write the "
    "solution to disk.");

    MocoTool();

    /// Load a MocoTool setup file.
    MocoTool(const std::string& omocoFile);

    const MocoProblem& getProblem() const;

    /// If using this method in C++, make sure to include the "&" in the
    /// return type; otherwise, you'll make a copy of the problem, and the copy
    /// will have no effect on this MocoTool.
    MocoProblem& updProblem();

    /// Call this method once you have finished setting up your MocoProblem.
    /// This returns a reference to the MocoSolver, which you can then edit.
    /// If using this method in C++, make sure to include the "&" in the
    /// return type; otherwise, you'll make a copy of the solver, and the copy
    /// will have no effect on this MocoTool.
    MocoTropterSolver& initTropterSolver();

    // TODO document
    /// This returns a fresh MucoCasADiSolver and deletes the previous solver.
    MocoCasADiSolver& initCasADiSolver();

    /// Access the solver. Make sure to call `initSolver()` beforehand.
    /// If using this method in C++, make sure to include the "&" in the
    /// return type; otherwise, you'll make a copy of the solver, and the copy
    /// will have no effect on this MocoTool.
    MocoSolver& updSolver();

    /// Solve the provided MocoProblem using the provided MocoSolver, and
    /// obtain the solution to the problem. If the write_solution property
    /// contains a file path (that is, it's not "false"), then the solution is
    /// also written to disk.
    /// @precondition
    ///     You must have finished setting up both the problem and solver.
    /// This reinitializes the solver so that any changes you have made will
    /// hold.
    MocoSolution solve() const;

    /// Interactively visualize an iterate using the simbody-visualizer. The
    /// iterate could be an initial guess, a solution, etc.
    /// @precondition
    ///     The MocoProblem must contain the model corresponding to
    ///     the provided iterate.
    void visualize(const MocoIterate& it) const;

    /// Calculate the requested outputs using the model in the problem and the
    /// states and controls in the MocoIterate.
    /// The output paths can be regular expressions. For example,
    /// ".*activation" gives the activation of all muscles.
    /// Constraints are not enforced but prescribed motion (e.g.,
    /// PositionMotion) is.
    /// @note Parameters in the MocoIterate are **not** applied to the model.
    TimeSeriesTable analyze(const MocoIterate& it,
            std::vector<std::string> outputPaths) const;

    /// @name Using other solvers
    /// @{
    template <typename SolverType>
    void setCustomSolver() {
        set_solver(SolverType());
    }

    /// @precondition If not using MucoTropterSolver or MucoCasADiSolver, you
    /// must invoke setCustomSolver() first.
    template <typename SolverType>
    SolverType& initSolver() {
        return dynamic_cast<SolverType&>(initSolverInternal());
    }

    template <typename SolverType>
    SolverType& updSolver() {
        return dynamic_cast<SolverType&>(upd_solver());
    }
    /// @}

protected:
    OpenSim_DECLARE_PROPERTY(problem, MocoProblem,
    "The optimal control problem to solve.");
    OpenSim_DECLARE_PROPERTY(solver, MocoSolver,
    "The optimal control algorithm for solving the problem.");

private:

    MocoSolver& initSolverInternal();
    void constructProperties();
};

template <>
OSIMMOCO_API
MocoTropterSolver& MocoTool::initSolver<MocoTropterSolver>();

template <>
OSIMMOCO_API
MocoCasADiSolver& MocoTool::initSolver<MocoCasADiSolver>();

} // namespace OpenSim

#endif // MOCO_MOCOTOOL_H
