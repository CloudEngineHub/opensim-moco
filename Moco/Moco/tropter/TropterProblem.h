#ifndef MOCO_TROPTERPROBLEM_H
#define MOCO_TROPTERPROBLEM_H
/* -------------------------------------------------------------------------- *
 * OpenSim Moco: TropterProblem.h                                             *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2018 Stanford University and the Authors                     *
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

#include "../MocoBounds.h"
#include "../MocoTropterSolver.h"
#include "../MocoUtilities.h"

#include <simbody/internal/Constraint.h>
#include <OpenSim/Simulation/InverseDynamicsSolver.h>

#include <tropter/tropter.h>

namespace OpenSim {

inline tropter::Bounds convertBounds(const MocoBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}
inline tropter::InitialBounds convertBounds(const MocoInitialBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}
inline tropter::FinalBounds convertBounds(const MocoFinalBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}

template <typename T>
class MocoTropterSolver::TropterProblemBase
        : public tropter::Problem<T> {
protected:
    TropterProblemBase(const MocoTropterSolver& solver)
            : tropter::Problem<T>(solver.getProblemRep().getName()),
            m_mocoTropterSolver(solver),
            m_mocoProbRep(solver.getProblemRep()),
            m_model(m_mocoProbRep.getModel()) {
        // TODO set name properly.
        // Disable all controllers.
        // TODO temporary; don't want to actually do this.
        auto controllers = m_model.getComponentList<Controller>();
        for (auto& controller : controllers) {
            OPENSIM_THROW_IF(controller.get_enabled(), Exception,
                    "MocoTropterSolver does not support OpenSim Controllers. "
                    "Disable all controllers in the model.");
        }
        m_state = m_model.getWorkingState();
        m_svNamesInSysOrder = createStateVariableNamesInSystemOrder(m_model);

        addStateVariables();
        addControlVariables();
        addKinematicConstraints();
        addGenericPathConstraints();
        addParameters();
    }

    void addStateVariables() {
        this->set_time(convertBounds(m_mocoProbRep.getTimeInitialBounds()),
                convertBounds(m_mocoProbRep.getTimeFinalBounds()));
        for (const auto& svName : m_svNamesInSysOrder) {
            const auto& info = m_mocoProbRep.getStateInfo(svName);
            this->add_state(svName, convertBounds(info.getBounds()),
                    convertBounds(info.getInitialBounds()),
                    convertBounds(info.getFinalBounds()));
        }
    }

    void addControlVariables() {
        for (const auto& actu : m_model.getComponentList<Actuator>()) {
            // TODO handle a variable number of control signals.
            const auto& actuName = actu.getAbsolutePathString();
            const auto& info = m_mocoProbRep.getControlInfo(actuName);
            this->add_control(actuName, convertBounds(info.getBounds()),
                    convertBounds(info.getInitialBounds()),
                    convertBounds(info.getFinalBounds()));
        }
    }

    void addKinematicConstraints() {
        // Add any scalar constraints associated with kinematic constraints in
        // the model as path constraints in the problem.
        // Whether or not enabled kinematic constraints exist in the model, 
        // check that optional solver properties related to constraints are
        // set properly.
        std::vector<std::string> kcNames =
            m_mocoProbRep.createKinematicConstraintNames();
        if (kcNames.empty()) {
            OPENSIM_THROW_IF(
                !m_mocoTropterSolver
                .getProperty_enforce_constraint_derivatives().empty(),
                Exception, "Solver property 'enforce_constraint_derivatives' "
                "was set but no enabled kinematic constraints exist in the "
                "model.");
            OPENSIM_THROW_IF(
                m_mocoTropterSolver
                .get_minimize_lagrange_multipliers(),
                Exception, "Solver property 'minimize_lagrange_multipliers' "
                "was enabled but no enabled kinematic constraints exist in the "
                "model.");
            // Do not add kinematic constraints, so we can return. This avoids
            // attempting to access the `enforce_constraint_derivatives`
            // property below, which is empty.
            return;
        } else {
            OPENSIM_THROW_IF(
                m_mocoTropterSolver
                .getProperty_enforce_constraint_derivatives().empty(),
                Exception, "Enabled kinematic constraints exist in the "
                "provided model. Please set the solver property "
                "'enforce_constraint_derivatives' to either 'true' or 'false'."
                );
        }
        
        int cid, mp, mv, ma;
        int numEquationsThisConstraint; 
        int multIndexThisConstraint;
        std::vector<MocoBounds> bounds;
        std::vector<std::string> labels;
        std::vector<KinematicLevel> kinLevels;
        bool enforceConstraintDerivs = 
            m_mocoTropterSolver.get_enforce_constraint_derivatives();
        for (const auto& kcName : kcNames) {
            const auto& kc = m_mocoProbRep.getKinematicConstraint(kcName);
            const auto& multInfos = m_mocoProbRep.getMultiplierInfos(kcName);
            cid = kc.getSimbodyConstraintIndex();
            mp = kc.getNumPositionEquations();
            mv = kc.getNumVelocityEquations();
            ma = kc.getNumAccelerationEquations();
            bounds = kc.getConstraintInfo().getBounds();
            labels = kc.getConstraintInfo().getConstraintLabels();
            kinLevels = kc.getKinematicLevels();

            // TODO only add velocity correction variables for holonomic
            // constraint derivatives? For now, disallow enforcing derivatives
            // if non-holonomic or acceleration constraints present.
            OPENSIM_THROW_IF(enforceConstraintDerivs && mv != 0, Exception,
                    format("Enforcing constraint derivatives is supported only for "
                           "holonomic (position-level) constraints. "
                           "There are %i velocity-level "
                           "scalar constraints associated with the model Constraint "
                           "at ConstraintIndex %i.", mv, cid));
            OPENSIM_THROW_IF(enforceConstraintDerivs && ma != 0, Exception,
                    format("Enforcing constraint derivatives is supported only for "
                           "holonomic (position-level) constraints. "
                           "There are %i acceleration-level "
                           "scalar constraints associated with the model Constraint "
                           "at ConstraintIndex %i.", ma, cid));

            m_total_mp += mp;
            m_total_mv += mv;
            m_total_ma += ma;

            // Loop through all scalar constraints associated with the model
            // constraint and corresponding path constraints to the optimal
            // control problem.
            //
            // We need a different index for the Lagrange multipliers since
            // they are only added if the current constraint equation is not a
            // derivative of a position- or velocity-level equation.
            multIndexThisConstraint = 0;
            numEquationsThisConstraint = 0;
            for (int i = 0; i < kc.getConstraintInfo().getNumEquations(); ++i) {
                
                // If the index for this path constraint represents an
                // a non-derivative scalar constraint equation, also add a
                // Lagrange multiplier to the problem.
                if (kinLevels[i] == KinematicLevel::Position ||
                        kinLevels[i] == KinematicLevel::Velocity ||
                        kinLevels[i] == KinematicLevel::Acceleration) {

                    // TODO name constraints based on model constraint names
                    // or coordinate names if a locked or prescribed coordinate
                    this->add_path_constraint(labels[i], 
                                              convertBounds(bounds[i]));

                    const auto& multInfo = multInfos[multIndexThisConstraint];
                    this->add_adjunct(multInfo.getName(),
                            convertBounds(multInfo.getBounds()),
                            convertBounds(multInfo.getInitialBounds()),
                            convertBounds(multInfo.getFinalBounds()));
                    // Add velocity correction variables if enforcing
                    // constraint equation derivatives.
                    if (enforceConstraintDerivs) {
                        // TODO this naming convention assumes that the 
                        // associated Lagrange multiplier name begins with
                        // "lambda", which may change in the future.
                        OPENSIM_THROW_IF(
                            multInfo.getName().substr(0, 6) != "lambda",
                            Exception,
                            format("Expected the multiplier name for this "
                                   "constraint to begin with 'lambda' but it "
                                   "begins with '%s'.",
                                   multInfo.getName().substr(0, 6)));
                        this->add_diffuse(std::string(
                                multInfo.getName()).replace(0, 6, "gamma"),
                            convertBounds(m_mocoTropterSolver
                                .get_velocity_correction_bounds()));
                    }
                    ++multIndexThisConstraint;
                    ++numEquationsThisConstraint;

                // If enforcing constraint derivatives, also add path 
                // constraints for kinematic constraint equation derivatives.
                } else if (enforceConstraintDerivs) {
                    this->add_path_constraint(labels[i], 
                                              convertBounds(bounds[i]));
                    ++numEquationsThisConstraint;
                }
            }

            m_numKinematicConstraintEquations += numEquationsThisConstraint;
        }

    }

    /// Add any generic path constraints included in the problem.
    void addGenericPathConstraints() {
        for (std::string pcName : m_mocoProbRep.createPathConstraintNames()) {
            const MocoPathConstraint& constraint =
                    m_mocoProbRep.getPathConstraint(pcName);
            auto pcInfo = constraint.getConstraintInfo();
            auto labels = pcInfo.getConstraintLabels();
            auto bounds = pcInfo.getBounds();
            for (int i = 0; i < pcInfo.getNumEquations(); ++i) {
                this->add_path_constraint(labels[i], convertBounds(bounds[i]));
            }
        }
        m_numPathConstraintEquations = 
            m_mocoProbRep.getNumPathConstraintEquations();
    }

    void addParameters() {
        for (std::string name : m_mocoProbRep.createParameterNames()) {
            const MocoParameter& parameter = m_mocoProbRep.getParameter(name);
            this->add_parameter(name, convertBounds(parameter.getBounds()));
        }
    }

    void initialize_on_iterate(const Eigen::VectorXd& parameters)
            const override final {
        // If they exist, apply parameter values to the model.
        this->applyParametersToModel(parameters);
    }

    void calc_integral_cost(const tropter::Input<T>& in,
            T& integrand) const override {
        // Unpack variables.
        const auto& time = in.time;
        const auto& states = in.states;
        const auto& controls = in.controls;
        const auto& adjuncts = in.adjuncts;

        // TODO would it make sense to a vector of States, one for each mesh
        // point, so that each can preserve their cache?
        m_state.setTime(time);
        std::copy_n(states.data(), states.size(),
                m_state.updY().updContiguousScalarData());

        // Set the controls for actuators in the OpenSim model.
        if (m_model.getNumControls()) {
            auto& osimControls = m_model.updControls(m_state);
            std::copy_n(controls.data(), controls.size(),
                    osimControls.updContiguousScalarData());
            m_model.realizePosition(m_state);
            m_model.setControls(m_state, osimControls);
        } else {
            m_model.realizePosition(m_state);
        }

        integrand = m_mocoProbRep.calcIntegralCost(m_state);

        if (m_mocoTropterSolver.get_minimize_lagrange_multipliers()) {
            // Add squared multiplers cost to the integrand.
            for (int i = 0; i < (m_total_mp + m_total_mv + m_total_ma); ++i) {
                integrand += 
                    m_mocoTropterSolver.get_lagrange_multiplier_weight()
                    * adjuncts[i] * adjuncts[i];
            }
        }
    }

    void calc_endpoint_cost(const T& final_time,
            const tropter::VectorX<T>& states,
            const tropter::VectorX<T>& /*parameters*/,
            T& cost) const override {
        // TODO avoid all of this if there are no endpoint costs.
        m_state.setTime(final_time);
        std::copy(states.data(), states.data() + states.size(),
                m_state.updY().updContiguousScalarData());
        // TODO cannot use control signals...
        m_model.updControls(m_state).setToNaN();
        cost = m_mocoProbRep.calcEndpointCost(m_state);
    }

    const MocoTropterSolver& m_mocoTropterSolver;
    const MocoProblemRep& m_mocoProbRep;
    const Model& m_model;
    mutable SimTK::State m_state;

    std::vector<std::string> m_svNamesInSysOrder;
    mutable SimTK::Vector_<SimTK::SpatialVec> constraintBodyForces;
    mutable SimTK::Vector constraintMobilityForces;
    mutable SimTK::Vector qdot;
    mutable SimTK::Vector qdotCorr;
    mutable SimTK::Vector udot;
    // This member variable avoids unnecessary extra allocation of memory for
    // spatial accelerations, which are incidental to the computation of
    // generalized accelerations when specifying the dynamics with model
    // constraints present.
    mutable SimTK::Vector_<SimTK::SpatialVec> A_GB;
    // The total number of scalar holonomic, non-holonomic, and acceleration 
    // constraint equations enabled in the model. This does not count equations                                                                     
    // for derivatives of holonomic and non-holonomic constraints. 
    mutable int m_total_mp = 0;
    mutable int m_total_mv = 0; 
    mutable int m_total_ma = 0;

    // The total number of scalar constraint equations associated with model
    // kinematic constraints that the solver is responsible for enforcing. This
    // number does include equations for constraint derivatives.
    mutable int m_numKinematicConstraintEquations = 0;
    // The total number of scalar constraint equations associated with
    // MocoPathConstraints added to the MocoProblem.
    mutable int m_numPathConstraintEquations = 0;

    void applyParametersToModel(const tropter::VectorX<T>& parameters) const
    {
        if (parameters.size()) {
            // Warning: memory borrowed, not copied (when third argument to
            // SimTK::Vector constructor is true)
            SimTK::Vector mocoParams(
                    (int)m_mocoProbRep.createParameterNames().size(),
                    parameters.data(), true);

            m_mocoProbRep.applyParametersToModel(mocoParams);
            // TODO: Avoid this const_cast.
            const_cast<Model&>(m_model).initSystem();
        }
    }

    void calcKinematicConstraintForces(const tropter::Input<T>& in,
            const SimTK::State& state,
            SimTK::Vector_<SimTK::SpatialVec>& constraintBodyForces,
            SimTK::Vector& constraintMobilityForces) const {
        // If enabled constraints exist in the model, compute accelerations
        // based on Lagrange multipliers.
        const auto& matter = m_model.getMatterSubsystem();

        // Multipliers are negated so constraint forces can be used like
        // applied forces.
        SimTK::Vector multipliers(m_numKinematicConstraintEquations,
                in.adjuncts.data(), true);
        matter.calcConstraintForcesFromMultipliers(state, -multipliers,
                constraintBodyForces, constraintMobilityForces);
    }

    void copyKinematicConstraintErrors(tropter::Output<T>& out) const {
        auto& simTKState = this->m_state;

        // Position-level errors.
        std::copy_n(simTKState.getQErr().getContiguousScalarData(),
            this->m_total_mp, out.path.data());
        if (this->m_mocoTropterSolver.get_enforce_constraint_derivatives()) {
            // Velocity-level errors.
            std::copy_n(
                simTKState.getUErr().getContiguousScalarData(),
                this->m_total_mp + this->m_total_mv,
                out.path.data() + this->m_total_mp);
            // Acceleration-level errors.
            std::copy_n(
                simTKState.getUDotErr().getContiguousScalarData(),
                this->m_total_mp + this->m_total_mv + this->m_total_ma,
                out.path.data() + 2 * this->m_total_mp + this->m_total_mv);
        } else {
            // Velocity-level errors. Skip derivatives of position-level
            // constraint equations.
            std::copy_n(
                simTKState.getUErr().getContiguousScalarData()
                + this->m_total_mp, this->m_total_mv,
                out.path.data() + this->m_total_mp);
            // Acceleration-level errors. Skip derivatives of velocity-
            // and position-level constraint equations.
            std::copy_n(
                simTKState.getUDotErr().getContiguousScalarData() +
                this->m_total_mp + this->m_total_mv, this->m_total_ma,
                out.path.data() + this->m_total_mp + this->m_total_mv);
        }
    }

    void calcPathConstraintErrors(const SimTK::State& state,
            double* errorsBegin) const {
        // Copy errors from generic path constraints into output struct.
        SimTK::Vector pathConstraintErrors(
                this->m_numPathConstraintEquations, errorsBegin, true);
        m_mocoProbRep.calcPathConstraintErrors(state, pathConstraintErrors);
    }

public:
    template <typename MocoIterateType, typename tropIterateType>
    MocoIterateType
    convertIterateTropterToMoco(const tropIterateType& tropSol) const;

    MocoIterate
    convertToMocoIterate(const tropter::Iterate& tropSol) const;

    MocoSolution
    convertToMocoSolution(const tropter::Solution& tropSol) const;

    tropter::Iterate
    convertToTropterIterate(const MocoIterate& mocoIter) const;
};


template <typename T>
class MocoTropterSolver::ExplicitTropterProblem
        : public MocoTropterSolver::TropterProblemBase<T> {
public:
    ExplicitTropterProblem(const MocoTropterSolver& solver)
            : MocoTropterSolver::TropterProblemBase<T>(solver) {
    }
    void initialize_on_mesh(const Eigen::VectorXd&) const override {
    }
    void calc_differential_algebraic_equations(
            const tropter::Input<T>& in,
            tropter::Output<T> out) const override {

        // TODO convert to implicit formulation.

        const auto& states = in.states;
        const auto& controls = in.controls;
        // const auto& adjuncts = in.adjuncts;
        const auto& diffuses = in.diffuses;

        auto& model = this->m_model;
        auto& simTKState = this->m_state;

        simTKState.setTime(in.time);
        std::copy_n(states.data(), states.size(),
                simTKState.updY().updContiguousScalarData());
        //
        // TODO do not copy? I think this will still make a copy:
        // TODO use m_state.updY() = SimTK::Vector(states.size(), states.data(), true);
        //m_state.setY(SimTK::Vector(states.size(), states.data(), true));

        // Set the controls for actuators in the OpenSim model.
        if (model.getNumControls()) {
            auto& osimControls = model.updControls(simTKState);
            std::copy_n(controls.data(), controls.size(),
                    osimControls.updContiguousScalarData());
            model.realizeVelocity(simTKState);
            model.setControls(simTKState, osimControls);
        }
        
        // TODO Antoine and Gil said realizing Dynamics is a lot costlier 
        // than realizing to Velocity and computing forces manually.
        model.realizeDynamics(simTKState);

        const SimTK::MultibodySystem& multibody =
                model.getMultibodySystem();
        const SimTK::Vector_<SimTK::SpatialVec>& appliedBodyForces =
                multibody.getRigidBodyForces(simTKState,
                        SimTK::Stage::Dynamics);
        const SimTK::Vector& appliedMobilityForces =
                multibody.getMobilityForces(simTKState,
                        SimTK::Stage::Dynamics);

        const SimTK::SimbodyMatterSubsystem& matter =
                model.getMatterSubsystem();

        this->constraintBodyForces.setToZero();
        this->constraintMobilityForces.setToZero();
        if (this->m_numKinematicConstraintEquations) {
            this->calcKinematicConstraintForces(in, simTKState,
                    this->constraintBodyForces, 
                    this->constraintMobilityForces);
        }
                
        matter.calcAccelerationIgnoringConstraints(simTKState,
                appliedMobilityForces + this->constraintMobilityForces,
                appliedBodyForces + this->constraintBodyForces,
                this->udot, A_GB);
                    
        // Apply velocity correction to qdot if at a mesh interval midpoint.
        // This correction modifies the dynamics to enable a projection of
        // the model coordinates back onto the constraint manifold whenever
        // they deviate.
        // Posa, Kuindersma, Tedrake, 2016. "Optimization and stabilization
        // of trajectories for constrained dynamical systems"
        // Note: Only supported for the Hermite-Simpson transcription 
        // scheme.
        if (diffuses.size() != 0) {
            SimTK::Vector gamma((int)diffuses.size(), diffuses.data());
            matter.multiplyByGTranspose(simTKState, gamma, this->qdotCorr);
            this->qdot = simTKState.getQDot() + this->qdotCorr;
        } else {
            this->qdot = simTKState.getQDot();
        }

        // Constraint errors.
        // TODO double-check that disabled constraints don't show up in
        // state
        if (out.path.size() != 0) {
            this->copyKinematicConstraintErrors(out);
        }

        // Copy state derivative values to output struct. We cannot simply
        // use getYDot() because that requires realizing to Acceleration.
        const int nq = simTKState.getQ().size();
        const int nu = this->udot.size();
        const int nz = simTKState.getZ().size();
        std::copy_n(this->qdot.getContiguousScalarData(),
                nq, out.dynamics.data());
        std::copy_n(this->udot.getContiguousScalarData(),
                this->udot.size(), out.dynamics.data() + nq);
        std::copy_n(simTKState.getZDot().getContiguousScalarData(), nz,
                out.dynamics.data() + nq + nu);
    
        // TODO move condition inside function
        if (out.path.size() != 0) {
            this->calcPathConstraintErrors(simTKState,
                    out.path.data() + this->m_numKinematicConstraintEquations);
        }
    }
};

template <typename T>
class MocoTropterSolver::ImplicitTropterProblem :
        public MocoTropterSolver::TropterProblemBase<T> {
public:
    ImplicitTropterProblem(const MocoTropterSolver& solver)
            : TropterProblemBase<T>(solver) {
        OPENSIM_THROW_IF(this->m_state.getNZ(), Exception,
                "Cannot use implicit dynamics mode if the system has auxiliary "
                "states.");
        // Add adjuncts for udot, which we call "w".
        int NU = this->m_state.getNU();
        OPENSIM_THROW_IF(NU != this->m_state.getNQ(), Exception,
                "Quaternions are not supported.");
        for (int iudot = 0; iudot < NU; ++iudot) {
            auto name = this->m_svNamesInSysOrder[iudot];
            auto leafpos = name.find("value");
            OPENSIM_THROW_IF(leafpos == std::string::npos, Exception,
                    "Internal error.");
            name.replace(leafpos, name.size(), "accel");
            // TODO: How to choose bounds on udot?
            this->add_adjunct(name, {-1000, 1000});
            this->add_path_constraint(name.substr(0, leafpos) + "residual", 0);
        }
    }
    void calc_differential_algebraic_equations(
            const tropter::Input<T>& in,
            tropter::Output<T> out) const override {

        const auto& states = in.states;
        const auto& controls = in.controls;
        const auto& adjuncts = in.adjuncts;
        const auto& diffuses = in.diffuses;

        auto& model = this->m_model;
        auto& simTKState = this->m_state;
        const SimTK::SimbodyMatterSubsystem& matter = 
            model.getMatterSubsystem();

        simTKState.setTime(in.time);
        const auto NQ = simTKState.getNQ(); // TODO we assume NQ = NU

        const auto& u = states.segment(NQ, NQ);
        const auto& w = adjuncts.segment(
            this->m_total_mp + this->m_total_mv + this->m_total_ma, NQ);

        // Kinematic differential equations
        // --------------------------------
        // qdot = u
        // TODO does not work for quaternions!
        if (diffuses.size() != 0) {
            model.realizeVelocity(simTKState);
            SimTK::Vector gamma((int)diffuses.size(), diffuses.data());
            matter.multiplyByGTranspose(simTKState, gamma, qdotCorr);
            SimTK::Vector qdot((int)u.size(), u.data());
            qdot += qdotCorr;

            std::copy_n(qdot.getContiguousScalarData(),
                NQ, out.dynamics.data());
        }
        else {
            out.dynamics.head(NQ) = u;
        }

        // Multibody dynamics: differential equations
        // ------------------------------------------
        // udot = w
        out.dynamics.segment(NQ, NQ) = w;


        // Multibody dynamics: "F - ma = 0"
        // --------------------------------
        std::copy_n(states.data(), states.size(),
                simTKState.updY().updContiguousScalarData());

        // TODO do not copy? I think this will still make a copy:
        // TODO use m_state.updY() = SimTK::Vector(states.size(), states.data(), true);
        //m_state.setY(SimTK::Vector(states.size(), states.data(), true));

        if (model.getNumControls()) {
            auto& osimControls = model.updControls(simTKState);
            std::copy_n(controls.data(), controls.size(),
                    osimControls.updContiguousScalarData());

            model.realizeVelocity(simTKState);
            model.setControls(simTKState, osimControls);
        }

        // TODO move condition inside path constraint function
        if (out.path.size() != 0) {
            // Multibody constraint errors.
            this->copyKinematicConstraintErrors(out);

            // Path constraint errors.
            double* pathConstraintErrorBegin =
                    out.path.data() + this->m_numKinematicConstraintEquations;
            this->calcPathConstraintErrors(simTKState, pathConstraintErrorBegin);
            OPENSIM_THROW_IF(
                    simTKState.getSystemStage() >= SimTK::Stage::Acceleration,
                    Exception,
                    "Cannot realize to Acceleration in implicit dynamics mode.");
        
            // Residuals.
            model.realizeDynamics(simTKState);
            const SimTK::MultibodySystem& multibody =
                model.getMultibodySystem();
            const SimTK::Vector_<SimTK::SpatialVec>& appliedBodyForces =
                multibody.getRigidBodyForces(simTKState,
                    SimTK::Stage::Dynamics);
            const SimTK::Vector& appliedMobilityForces =
                multibody.getMobilityForces(simTKState,
                    SimTK::Stage::Dynamics);

            this->constraintBodyForces.setToZero();
            this->constraintMobilityForces.setToZero();
            if (this->m_numKinematicConstraintEquations) {
                this->calcKinematicConstraintForces(in, simTKState,
                    this->constraintBodyForces, this->constraintMobilityForces);
            }

            SimTK::Vector udot((int)w.size(), w.data(), true);
            matter.calcResidualForceIgnoringConstraints(simTKState,
                appliedMobilityForces + constraintMobilityForces,
                appliedBodyForces + constraintBodyForces,
                udot, residual);

            double* residualBegin =
                pathConstraintErrorBegin +
                this->m_numPathConstraintEquations;
            std::copy_n(residual.getContiguousScalarData(), residual.size(),
                residualBegin);
        }

        // TODO Antoine and Gil said realizing Dynamics is a lot costlier than
        // realizing to Velocity and computing forces manually.

        /*
        if (SimTK::isNaN(in.adjuncts(0))) {
            std::cout << "DEBUG " << in.adjuncts << std::endl;
        }
        std::cout << "DEBUG dynamics\n" << out.dynamics << "\npath\n" << out.path << std::endl;
        std::cout << simTKState.getY() << std::endl;
        std::cout << residual << std::endl;
        std::cout << udot << std::endl;
        std::cout << "adjuncts " << adjuncts << std::endl;
        std::cout << "num multibody " << this->m_numKinematicConstraintEqs;
         */
    }
    void calc_integral_cost(const tropter::Input<T>& in,
            T& integrand) const override final {
        TropterProblemBase<T>::calc_integral_cost(in, integrand);
        OPENSIM_THROW_IF(
                this->m_state.getSystemStage() >= SimTK::Stage::Acceleration,
                Exception,
                "Cannot realize to Acceleration in implicit dynamics mode.");
    }
    void calc_endpoint_cost(const T& final_time,
            const tropter::VectorX<T>& states,
            const tropter::VectorX<T>& parameters,
            T& cost) const override final {
        TropterProblemBase<T>::calc_endpoint_cost(final_time, states,
                parameters, cost);
        OPENSIM_THROW_IF(
                this->m_state.getSystemStage() >= SimTK::Stage::Acceleration,
                Exception,
                "Cannot realize to Acceleration in implicit dynamics mode.");
    }
private:
    mutable SimTK::Vector residual;
};

} // namespace OpenSim


#endif // MOCO_TROPTERPROBLEM_H
