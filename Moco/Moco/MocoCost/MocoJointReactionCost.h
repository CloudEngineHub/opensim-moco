#ifndef MOCO_MOCOJOINTREACTIONCOST_H
#define MOCO_MOCOJOINTREACTIONCOST_H
/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoJointReactionCost.h                                      *
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

#include "MocoCost.h"
#include "OpenSim\Simulation\SimbodyEngine\Joint.h"

namespace OpenSim {

/// Minimize the reaction loads on the child body of a specified joint. 
/// The norm of the reaction forces and moments integrated over the phase is 
/// the specific quantity minimized.
/// This cost requires realizing to the Acceleration stage.
/// @ingroup mococost
// TODO allow a list property of multiple joints?
// TODO allow specification of either child or parent reaction loads to 
//      to minimize.
class OSIMMOCO_API MocoJointReactionCost : public MocoCost {
OpenSim_DECLARE_CONCRETE_OBJECT(MocoJointReactionCost, MocoCost);
public: 
    MocoJointReactionCost();
    MocoJointReactionCost(std::string name) : MocoCost(std::move(name)) {
        constructProperties();
    }
    MocoJointReactionCost(std::string name, double weight)
            : MocoCost(std::move(name), weight) {
        constructProperties();
    }
    /// Provide a valid model path for joint whose reaction loads will be
    /// minimized. 
    // TODO when using implicit dynamics, we will need to revisit this cost.
    void setJointPath(const std::string& jointPath) 
    {   set_joint_path(jointPath); }
    void setExpressedInFramePath(const std::string& framePath) {
        set_expressed_in_frame_path(framePath);
    }
    void setReactionComponent(int comp) {
        set_reaction_component(comp);
    }

protected:
    void initializeOnModelImpl(const Model&) const override;
    void calcIntegralCostImpl(const SimTK::State& state,
            double& integrand) const override;

private:
    mutable SimTK::ReferencePtr<const Joint> m_joint;
    mutable SimTK::ReferencePtr<const Frame> m_frame;
    mutable int m_vec;
    mutable int m_elt;

    void constructProperties();
    OpenSim_DECLARE_PROPERTY(joint_path, std::string, "The model path for the "
            "joint with minimized reaction loads.");
    OpenSim_DECLARE_PROPERTY(expressed_in_frame_path, std::string, "The frame that "
            "the minimized reaction force is expressed in.");
    OpenSim_DECLARE_PROPERTY(reaction_component, int, "TODO");
};

} // namespace OpenSim

#endif // MOCO_MOCOJOINTREACTIONCOST_H
