/**
 * @file kinematic_group.cpp
 * @brief A kinematic group with forward and inverse kinematics methods.
 *
 * @author Levi Armstrong
 * @date Aug 20, 2021
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2021, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <console_bridge/console.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_kinematics/core/kinematic_group.h>
#include <tesseract_common/utils.h>

#include <tesseract_scene_graph/kdl_parser.h>

namespace tesseract_kinematics
{
KinematicGroup::KinematicGroup(std::string name,
                               InverseKinematics::UPtr inv_kin,
                               const tesseract_scene_graph::SceneGraph& scene_graph,
                               const tesseract_scene_graph::SceneState& scene_state)
  : JointGroup(std::move(name), inv_kin->getJointNames(), scene_graph, scene_state)
{
  inv_kin_ = std::move(inv_kin);

  std::vector<std::string> active_link_names = state_solver_->getActiveLinkNames();
  std::string working_frame = inv_kin_->getWorkingFrame();
  auto it = std::find(active_link_names.begin(), active_link_names.end(), working_frame);
  if (it == active_link_names.end())
  {
    working_frames_.insert(working_frames_.end(), static_link_names_.begin(), static_link_names_.end());
  }
  else
  {
    std::vector<std::string> child_link_names = scene_graph.getLinkChildrenNames(working_frame);
    working_frames_.push_back(working_frame);
    working_frames_.insert(working_frames_.end(), child_link_names.begin(), child_link_names.end());
  }

  for (const auto& tip_link : inv_kin_->getTipLinkNames())
  {
    inv_tip_links_map_[tip_link] = tip_link;
    for (const auto& child : scene_graph.getLinkChildrenNames(tip_link))
      inv_tip_links_map_[child] = tip_link;
  }

  inv_to_fwd_base_ = state_.link_transforms.at(inv_kin_->getBaseLinkName()).inverse() *
                     state_.link_transforms.at(state_solver_->getBaseLinkName());
}

KinematicGroup::KinematicGroup(const KinematicGroup& other) : JointGroup(other) { *this = other; }

KinematicGroup& KinematicGroup::operator=(const KinematicGroup& other)
{
  JointGroup::operator=(other);
  inv_kin_ = other.inv_kin_->clone();
  inv_to_fwd_base_ = other.inv_to_fwd_base_;
  working_frames_ = other.working_frames_;
  inv_tip_links_map_ = other.inv_tip_links_map_;
  return *this;
}

IKSolutions KinematicGroup::calcInvKin(const KinGroupIKInputs& tip_link_poses,
                                       const Eigen::Ref<const Eigen::VectorXd>& seed) const
{
  // Convert to IK Inputs
  tesseract_common::TransformMap ik_inputs;
  for (const auto& tip_link_pose : tip_link_poses)
  {
    assert(std::find(working_frames_.begin(), working_frames_.end(), tip_link_pose.working_frame) !=
           working_frames_.end());

    // The IK Solvers tip link and working frame
    std::string ik_solver_tip_link = inv_tip_links_map_.at(tip_link_pose.tip_link_name);
    std::string working_frame = inv_kin_->getWorkingFrame();

    // Get transform from working frame to user working frame (reference frame for the target IK pose)
    const Eigen::Isometry3d& world_to_user_wf = state_.link_transforms.at(tip_link_pose.working_frame);
    const Eigen::Isometry3d& world_to_wf = state_.link_transforms.at(working_frame);
    const Eigen::Isometry3d wf_to_user_wf = world_to_wf.inverse() * world_to_user_wf;

    // Get the transform from IK solver tip link to the user tip link
    const Eigen::Isometry3d& world_to_user_tl = state_.link_transforms.at(tip_link_pose.tip_link_name);
    const Eigen::Isometry3d& world_to_tl = state_.link_transforms.at(ik_solver_tip_link);
    const Eigen::Isometry3d tl_to_user_tl = world_to_tl.inverse() * world_to_user_tl;

    // Get the transformation from the IK solver working frame to the IK solver tip frame
    const Eigen::Isometry3d& user_wf_to_user_tl = tip_link_pose.pose;  // an unnecessary but helpful alias
    const Eigen::Isometry3d wf_to_tl = wf_to_user_wf * user_wf_to_user_tl * tl_to_user_tl.inverse();

    ik_inputs[ik_solver_tip_link] = wf_to_tl;
  }

  IKSolutions solutions = inv_kin_->calcInvKin(ik_inputs, seed);
  IKSolutions solutions_filtered;
  solutions_filtered.reserve(solutions.size());
  for (auto& solution : solutions)
  {
    if (tesseract_common::satisfiesPositionLimits(solution, limits_.joint_limits))
      solutions_filtered.push_back(solution);
  }

  return solutions_filtered;
}

std::vector<std::string> KinematicGroup::getAllValidWorkingFrames() const { return working_frames_; }

std::vector<std::string> KinematicGroup::getAllPossibleTipLinkNames() const
{
  std::vector<std::string> ik_tip_links;
  ik_tip_links.reserve(inv_tip_links_map_.size());
  for (const auto& pair : inv_tip_links_map_)
    ik_tip_links.push_back(pair.first);

  return ik_tip_links;
}
}  // namespace tesseract_kinematics
