/* -------------------------------------------------------------------------- *
 * OpenSim Moco: CasOCHermiteSimpson.cpp                                      *
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
#include "CasOCHermiteSimpson.h"

using casadi::DM;
using casadi::MX;
using casadi::Slice;

namespace CasOC {

DM HermiteSimpson::createQuadratureCoefficientsImpl() const {

    // The duration of each mesh interval.
    const DM mesh = DM::linspace(0, 1, m_numMeshPoints);
    const DM meshIntervals = mesh(Slice(1, m_numMeshPoints)) -
        mesh(Slice(0, m_numMeshPoints - 1));
    // Simpson quadrature includes integrand evaluations at the midpoint.
    DM quadCoeffs(m_numGridPoints, 1);
    // Loop through each mesh interval and update the corresponding components
    // in the total coefficients vector.
    for (int i = 0; i < m_numMeshIntervals; ++i) {
        // The mesh interval quadrature coefficients overlap at the mesh grid 
        // points in the total coefficients vector, so we slice at every other 
        // index to update the coefficients vector.
        quadCoeffs(2*i) += (1.0/6.0) * meshIntervals(i);
        quadCoeffs(2*i + 1) += (2.0/3.0) * meshIntervals(i);
        quadCoeffs(2*i + 2) += (1.0/6.0) * meshIntervals(i);
    }

    return quadCoeffs;
}

DM HermiteSimpson::createKinematicConstraintIndicesImpl() const {
    DM indices = DM::zeros(1, m_numGridPoints);
    for (int i = 0; i < m_numGridPoints; i += 2) {
        indices(i) = 1;
    }
    return indices;
}

DM HermiteSimpson::createResidualConstraintIndicesImpl() const {
    DM indices = DM::zeros(1, m_numGridPoints);
    for (int i = 0; i < m_numGridPoints; ++i) {
        indices(i) = 1;
    }
    return indices;
}

void HermiteSimpson::applyConstraintsImpl() {

    // Breakdown of constraints for Hermite-Simpson collocation.

    // Defect constraints.
    // -------------------
    // For each state variable, there is one pair of defect constraints 
    // (Hermite interpolant defect + Simpson integration defect) per mesh
    // interval. Each mesh interval includes two mesh points (at the interval's
    // endpoints) and an additional collocation point at the mesh interval
    // midpoint. All three mesh interval points (2 mesh points + 1 collocation
    // point) are used to construct the defects (see below).
    
    // Kinematic constraints + path constraints.
    // -----------------------------------------
    // Kinematic constraint and path constraint errors are enforced only at the 
    // mesh points. Errors at collocation points at the mesh interval midpoint 
    // are ignored.

    // We have arranged the code this way so that all constraints at a given
    // mesh point are grouped together (organizing the sparsity of the Jacobian
    // this way might have benefits for sparse linear algebra).
    DM zeroS;
    MX states;
    if (m_solver.isDynamicsModeImplicit()) {
        const auto& derivatives = m_vars[Var::derivatives];
        states = casadi::MX::vertcat({m_vars[Var::states],
            derivatives(Slice(0, m_problem.getNumSpeeds()), Slice())});
        zeroS = casadi::DM::zeros(
            m_problem.getNumStates() + m_problem.getNumSpeeds(), 1);
    } else {
        states = m_vars[Var::states];
        zeroS = casadi::DM::zeros(m_problem.getNumStates(), 1);
    }

    const DM zeroU = casadi::DM::zeros(m_problem.getNumSpeeds(), 1);

    int time_i, time_mid, time_ip1;
    for (int imesh = 0; imesh < m_numMeshPoints; ++imesh) {
        time_i = 2*imesh; // Needed for defects and path constraints.

        // We enforce defect constraints on a mesh interval basis, so add
        // constraints until the number of mesh intervals is reached.
        if (imesh < m_numMeshIntervals) {
            time_mid = 2*imesh + 1;
            time_ip1 = 2*imesh + 2;

            const auto h = m_times(time_ip1) - m_times(time_i);
            const auto x_i = states(Slice(), time_i);
            const auto x_mid = states(Slice(), time_mid);
            const auto x_ip1 = states(Slice(), time_ip1);
            const auto xdot_i = m_xdot(Slice(), time_i);
            const auto xdot_mid = m_xdot(Slice(), time_mid);
            const auto xdot_ip1 = m_xdot(Slice(), time_ip1);

            // Hermite interpolant defects.
            addConstraints(zeroS, zeroS,
                x_mid - 0.5*(x_ip1 + x_i) - (h / 8.0) * (xdot_i - xdot_ip1));

            // Simpson integration defects.
            addConstraints(zeroS, zeroS,
                x_ip1 - x_i - (h / 6.0) * (xdot_ip1 + 4.0*xdot_mid + xdot_i));

            // The residuals are enforced at the mesh interval midpoints.
            if (m_solver.isDynamicsModeImplicit()) {
                addConstraints(zeroU, zeroU, m_residual(Slice(), time_i));
                addConstraints(zeroU, zeroU, m_residual(Slice(), time_mid));
                // We only need to add a constraint on this time point for the 
                // last mesh interval since, for all other mesh intervals, the
                // time_ip1 point for a given mesh interval is covered by the 
                // next mesh interval's time_i point.
                if (imesh == m_numMeshIntervals-1) {
                    addConstraints(zeroU, zeroU, m_residual(Slice(), time_ip1));
                }
            }
        } 
        
        // Kinematic constraint errors.
        if (m_problem.getNumKinematicConstraintEquations()) {
            DM kinConLowerBounds(
                m_problem.getNumKinematicConstraintEquations(), 1);
            DM kinConUpperBounds(
                m_problem.getNumKinematicConstraintEquations(), 1);

            const auto& bounds = m_problem.getKinematicConstraintBounds();
            kinConLowerBounds(Slice()) = bounds.lower;
            kinConUpperBounds(Slice()) = bounds.upper;

            addConstraints(kinConLowerBounds, kinConUpperBounds, 
                m_kcerr(Slice(), imesh));
        }

        // The individual path constraint functions are passed to CasADi to
        // maximize CasADi's ability to take derivatives efficiently.
        for (const auto& pathInfo : m_problem.getPathConstraintInfos()) {
            const auto output = pathInfo.function->operator()(
            {m_times(time_i), m_vars[Var::states](Slice(), time_i),
                m_vars[Var::controls](Slice(), time_i),
                m_vars[Var::parameters]});
            const auto& errors = output.at(0);
            addConstraints(
                pathInfo.lowerBounds, pathInfo.upperBounds, errors);
        }
    }
}

} // namespace CasOC
