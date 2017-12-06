#ifndef MUSCOLLO_MUSCOLLOSANDBOXSHARED_H
#define MUSCOLLO_MUSCOLLOSANDBOXSHARED_H
/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: MuscolloSandboxShared.h                                  *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Nicholas Bianco, Chris Dembia                                   *
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

namespace OpenSim {

/// Similar to CoordinateActuator (simply produces a generalized force) but
/// with first-order linear activation dynamics. This actuator has one state
/// variable, `activation`, with \f$ \dot{a} = (u - a) / \tau \f$, where
/// \f$ a \f$ is activation, \f$ u $\f is excitation, and \f$ \tau \f$ is the
/// activation time constant (there is no separate deactivation time constant).
/// <b>Default %Property Values</b>
/// @verbatim
/// activation_time_constant: 0.01
/// default_activation: 0.5
/// @dverbatim
class /*OSIMMUSCOLLO_API*/
ActivationCoordinateActuator : public CoordinateActuator {
    OpenSim_DECLARE_CONCRETE_OBJECT(ActivationCoordinateActuator,
            CoordinateActuator);
public:
    OpenSim_DECLARE_PROPERTY(activation_time_constant, double,
    "Larger value means activation can change more rapidly (units: seconds).");

    OpenSim_DECLARE_PROPERTY(default_activation, double,
    "Value of activation in the default state returned by initSystem().");

    ActivationCoordinateActuator() {
        constructProperties();
    }

    void extendAddToSystem(SimTK::MultibodySystem& system) const override {
        Super::extendAddToSystem(system);
        addStateVariable("activation", SimTK::Stage::Dynamics);
    }

    void extendInitStateFromProperties(SimTK::State& s) const override {
        Super::extendInitStateFromProperties(s);
        setStateVariableValue(s, "activation", get_default_activation());
    }

    void extendSetPropertiesFromState(const SimTK::State& s) override {
        Super::extendSetPropertiesFromState(s);
        set_default_activation(getStateVariableValue(s, "activation"));
    }

    // TODO no need to do clamping, etc; CoordinateActuator is bidirectional.
    void computeStateVariableDerivatives(const SimTK::State& s) const override {
        const auto& tau = get_activation_time_constant();
        const auto& u = getControl(s);
        const auto& a = getStateVariableValue(s, "activation");
        const SimTK::Real adot = (u - a) / tau;
        setStateVariableDerivativeValue(s, "activation", adot);
    }

    double computeActuation(const SimTK::State& s) const override {
        return getStateVariableValue(s, "activation") * getOptimalForce();
    }
private:
    void constructProperties() {
        constructProperty_activation_time_constant(0.010);
        constructProperty_default_activation(0.5);
    }
};

class /*TODO OSIMMUSCOLLO_API*/AckermannVanDenBogert2010Force : public Force {
    OpenSim_DECLARE_CONCRETE_OBJECT(AckermannVanDenBogert2010Force, Force);
public:
    OpenSim_DECLARE_PROPERTY(stiffness, double, "TODO N/m^3");
    OpenSim_DECLARE_PROPERTY(dissipation, double, "TODO s/m");
    OpenSim_DECLARE_PROPERTY(friction_coefficient, double, "TODO");
    // TODO rename to transition_velocity
    OpenSim_DECLARE_PROPERTY(tangent_velocity_scaling_factor, double, "TODO");

    OpenSim_DECLARE_OUTPUT(force_on_station, SimTK::Vec3, calcContactForce,
            SimTK::Stage::Velocity);

    OpenSim_DECLARE_SOCKET(station, Station, "TODO");

    AckermannVanDenBogert2010Force() {
        constructProperties();
    }

    /// Compute the force applied to body to which the station is attached, at
    /// the station, expressed in ground.
    SimTK::Vec3 calcContactForce(const SimTK::State& s) const {
        SimTK::Vec3 force(0);
        const auto& pt = getConnectee<Station>("station");
        const auto& pos = pt.getLocationInGround(s);
        const auto& vel = pt.getVelocityInGround(s);
        const SimTK::Real y = pos[1];
        const SimTK::Real velNormal = vel[1];
        // TODO should project vel into ground.
        const SimTK::Real velSliding = vel[0];
        const SimTK::Real depth = 0 - y;
        const SimTK::Real depthRate = 0 - velNormal;
        const SimTK::Real a = get_stiffness();
        const SimTK::Real b = get_dissipation();
        if (depth > 0) {
            force[1] = fmax(0, a * pow(depth, 3) * (1 + b * depthRate));
        }
        const SimTK::Real voidStiffness = 1.0; // N/m
        force[1] += voidStiffness * depth;

        const SimTK::Real velSlidingScaling =
                get_tangent_velocity_scaling_factor();
        const SimTK::Real z0 = exp(-velSliding / velSlidingScaling);
        // TODO decide direction!!!

        const SimTK::Real frictionForce =
                -(1 - z0) / (1 + z0) * get_friction_coefficient() * force[1];

        force[0] = frictionForce;
        return force;
    }
    void computeForce(const SimTK::State& s,
            SimTK::Vector_<SimTK::SpatialVec>& bodyForces,
            SimTK::Vector& /*generalizedForces*/) const override {
        const SimTK::Vec3 force = calcContactForce(s);
        const auto& pt = getConnectee<Station>("station");
        const auto& pos = pt.getLocationInGround(s);
        const auto& frame = pt.getParentFrame();
        applyForceToPoint(s, frame, pt.get_location(), force, bodyForces);
        applyForceToPoint(s, getModel().getGround(), pos, -force, bodyForces);
    }

    OpenSim::Array<std::string> getRecordLabels() const override {
        OpenSim::Array<std::string> labels;
        const auto stationName = getConnectee("station").getName();
        labels.append(getName() + "." + stationName + ".force.X");
        labels.append(getName() + "." + stationName + ".force.Y");
        labels.append(getName() + "." + stationName + ".force.Z");
        return labels;
    }
    OpenSim::Array<double> getRecordValues(const SimTK::State& s)
    const override {
        OpenSim::Array<double> values;
        // TODO cache.
        const SimTK::Vec3 force = calcContactForce(s);
        values.append(force[0]);
        values.append(force[1]);
        values.append(force[2]);
        return values;
    }

    void generateDecorations(bool fixed, const ModelDisplayHints& hints,
            const SimTK::State& s,
            SimTK::Array_<SimTK::DecorativeGeometry>& geoms) const override {
        Super::generateDecorations(fixed, hints, s, geoms);
        if (!fixed) {
            getModel().realizeVelocity(s);
            // Normalize contact force vector by body weight so that the line
            // is 1 meter long if the contact force magnitude is equal to
            // body weight.
            const double mg =
                    getModel().getTotalMass(s) * getModel().getGravity().norm();
            // TODO avoid recalculating.
            const auto& pt = getConnectee<Station>("station");
            const auto pt1 = pt.getLocationInGround(s);
            const SimTK::Vec3 force = calcContactForce(s);
            // std::cout << "DEBUGgd force " << force << std::endl;
            const SimTK::Vec3 pt2 = pt1 + force / mg;
            SimTK::DecorativeLine line(pt1, pt2);
            line.setColor(SimTK::Green);
            line.setLineThickness(0.10);
            geoms.push_back(line);

            // TODO move to fixed.
            SimTK::DecorativeSphere sphere;
            sphere.setColor(SimTK::Green);
            sphere.setRadius(0.01);
            sphere.setBodyId(pt.getParentFrame().getMobilizedBodyIndex());
            sphere.setRepresentation(SimTK::DecorativeGeometry::DrawWireframe);
            sphere.setTransform(SimTK::Transform(pt.get_location()));
            geoms.push_back(sphere);
        }
    }

    // TODO potential energy.
private:
    void constructProperties() {
        constructProperty_friction_coefficient(1.0);
        constructProperty_stiffness(5e7);
        constructProperty_dissipation(1.0);
        constructProperty_tangent_velocity_scaling_factor(0.05);
    }
};

// TODO rename ContactForceTracking? ExternalForceTracking?
class MucoForceTrackingCost : public MucoCost {
    OpenSim_DECLARE_CONCRETE_OBJECT(MucoForceTrackingCost, MucoCost);
public:
    OpenSim_DECLARE_LIST_PROPERTY(forces, std::string, "TODO");

    MucoForceTrackingCost() {
        constructProperties();
    }
protected:
    void initializeImpl() const override {
        m_forces.clear();
        for (int i = 0; i < getProperty_forces().size(); ++i) {
            m_forces.emplace_back(
                    dynamic_cast<const AckermannVanDenBogert2010Force&>(
                            getModel().getComponent(get_forces(i))));
        }
        // TODO this is a complete hack!
        auto data = STOFileAdapter::read("walk_gait1018_subject01_grf.mot");

        auto time = data.getIndependentColumn();
        SimTK::Vector Fx = data.getDependentColumn("ground_force_vx");
        SimTK::Vector Fy = data.getDependentColumn("ground_force_vy");
        m_refspline_x = GCVSpline(5, (int)time.size(), time.data(), &Fx[0]);
        m_refspline_y = GCVSpline(5, (int)time.size(), time.data(), &Fy[0]);
    }

    void calcIntegralCostImpl(const SimTK::State& state, double& integrand)
    const override {
        SimTK::Vec3 netForce(0);
        SimTK::Vec3 ref(0);
        for (const auto& force : m_forces) {
            netForce += force->calcContactForce(state);
        }
        SimTK::Vector timeVec(1, state.getTime());
        ref[0] = m_refspline_x.calcValue(timeVec);
        ref[1] = m_refspline_y.calcValue(timeVec);
        integrand = (netForce - ref).normSqr();
    }
private:
    void constructProperties() {
        constructProperty_forces();
    }
    mutable
    std::vector<SimTK::ReferencePtr<const AckermannVanDenBogert2010Force>>
            m_forces;

    mutable GCVSpline m_refspline_x;
    mutable GCVSpline m_refspline_y;
};

} // namespace OpenSim

#endif // MUSCOLLO_MUSCOLLOSANDBOXSHARED_H
