#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
#include <tesseract_urdf/urdf_parser.h>
#include <tesseract_collision/bullet/bullet_discrete_bvh_manager.h>
#include <tesseract_collision/fcl/fcl_discrete_managers.h>
#include <tesseract_collision/bullet/bullet_cast_bvh_manager.h>
#include <tesseract_geometry/impl/box.h>
#include <tesseract_common/resource_locator.h>
#include <tesseract_common/utils.h>
#include <tesseract_state_solver/kdl/kdl_state_solver.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_environment/commands.h>
#include <tesseract_environment/environment.h>

using namespace tesseract_scene_graph;
using namespace tesseract_srdf;
using namespace tesseract_collision;
using namespace tesseract_environment;

std::string locateResource(const std::string& url)
{
  std::string mod_url = url;
  if (url.find("package://tesseract_support") == 0)
  {
    mod_url.erase(0, strlen("package://tesseract_support"));
    size_t pos = mod_url.find('/');
    if (pos == std::string::npos)
    {
      return std::string();
    }

    std::string package = mod_url.substr(0, pos);
    mod_url.erase(0, pos);
    std::string package_path = std::string(TESSERACT_SUPPORT_DIR);

    if (package_path.empty())
    {
      return std::string();
    }

    mod_url = package_path + mod_url;
  }

  return mod_url;
}

Eigen::Isometry3d tcpCallback(const tesseract_common::ManipulatorInfo&)
{
  Eigen::Isometry3d tcp = Eigen::Isometry3d::Identity();
  tcp.translation() = Eigen::Vector3d(0, 0, 0.1);
  return tcp;
}

SceneGraph::UPtr getSceneGraph()
{
  std::string path = std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.urdf";

  tesseract_common::SimpleResourceLocator locator(locateResource);
  return tesseract_urdf::parseURDFFile(path, locator);
}

SRDFModel::Ptr getSRDFModel(const SceneGraph& scene_graph)
{
  std::string path = std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.srdf";
  tesseract_common::SimpleResourceLocator locator(locateResource);

  auto srdf = std::make_shared<SRDFModel>();
  srdf->initFile(scene_graph, path, locator);

  return srdf;
}

SceneGraph::Ptr getSubSceneGraph()
{
  SceneGraph::Ptr subgraph = std::make_shared<SceneGraph>();
  subgraph->setName("subgraph");

  // Now add a link to empty environment
  auto visual = std::make_shared<Visual>();
  visual->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);
  auto collision = std::make_shared<Collision>();
  collision->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);

  const std::string link_name1 = "subgraph_base_link";
  const std::string link_name2 = "subgraph_link_1";
  const std::string joint_name1 = "subgraph_joint1";
  Link link_1(link_name1);
  link_1.visual.push_back(visual);
  link_1.collision.push_back(collision);
  Link link_2(link_name2);

  Joint joint_1(joint_name1);
  joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
  joint_1.parent_link_name = link_name1;
  joint_1.child_link_name = link_name2;
  joint_1.type = JointType::FIXED;

  subgraph->addLink(link_1);
  subgraph->addLink(link_2);
  subgraph->addJoint(joint_1);

  return subgraph;
}

void runGetLinkTransformsTest(Environment& env)
{
  StateSolver::Ptr state_solver = env.getStateSolver();
  for (int i = 0; i < 20; ++i)
  {
    SceneState random_state = state_solver->getRandomState();
    env.setState(random_state.joints);

    std::vector<std::string> link_names = env.getLinkNames();
    SceneState env_state = env.getState();
    tesseract_common::VectorIsometry3d link_transforms = env.getLinkTransforms();
    for (std::size_t i = 0; i < link_names.size(); ++i)
    {
      EXPECT_TRUE(env_state.link_transforms.at(link_names.at(i)).isApprox(link_transforms.at(i), 1e-6));
      EXPECT_TRUE(
          env_state.link_transforms.at(link_names.at(i)).isApprox(env.getLinkTransform(link_names.at(i)), 1e-6));
    }
  }
}

enum class EnvRegisterMethod
{
  MANUAL_REGISTAR = 0,
  ON_CONSTRUCTION = 1,
  CALL_DEFAULT_REGISTAR = 2
};

Environment::Ptr getEnvironment(EnvRegisterMethod register_contact_managers = EnvRegisterMethod::MANUAL_REGISTAR)
{
  tesseract_scene_graph::SceneGraph::Ptr scene_graph = getSceneGraph();
  EXPECT_TRUE(scene_graph != nullptr);

  // Check to make sure all links are enabled
  for (const auto& link : scene_graph->getLinks())
  {
    EXPECT_TRUE(scene_graph->getLinkCollisionEnabled(link->getName()));
    EXPECT_TRUE(scene_graph->getLinkVisibility(link->getName()));
  }

  auto srdf = getSRDFModel(*scene_graph);
  EXPECT_TRUE(srdf != nullptr);

  auto env = std::make_shared<Environment>(register_contact_managers == EnvRegisterMethod::ON_CONSTRUCTION);
  EXPECT_TRUE(env != nullptr);
  EXPECT_EQ(0, env->getRevision());
  EXPECT_EQ(0, env->getCommandHistory().size());
  EXPECT_FALSE(env->reset());
  EXPECT_FALSE(env->isInitialized());
  EXPECT_TRUE(env->clone() != nullptr);

  bool success = env->init(*scene_graph, srdf);
  Environment::ConstPtr env_const = env;
  EXPECT_TRUE(success);
  EXPECT_EQ(2, env->getRevision());
  EXPECT_TRUE(env->isInitialized());
  //  EXPECT_TRUE(env->getManipulatorManager() != nullptr);
  //  EXPECT_TRUE(env_const->getManipulatorManager() != nullptr);
  EXPECT_TRUE(env->getResourceLocator() == nullptr);

  env->setResourceLocator(std::make_shared<tesseract_common::SimpleResourceLocator>(locateResource));
  EXPECT_TRUE(env->getResourceLocator() != nullptr);

  // Check to make sure all links are enabled
  for (const auto& link : env->getSceneGraph()->getLinks())
  {
    EXPECT_TRUE(env->getSceneGraph()->getLinkCollisionEnabled(link->getName()));
    EXPECT_TRUE(env->getSceneGraph()->getLinkVisibility(link->getName()));
  }

  if (register_contact_managers == EnvRegisterMethod::MANUAL_REGISTAR)
  {
    // No contact managers should exist
    EXPECT_TRUE(env->getDiscreteContactManager(tesseract_collision_bullet::BulletDiscreteBVHManager::name()) ==
                nullptr);
    EXPECT_TRUE(env->getContinuousContactManager(tesseract_collision_bullet::BulletCastBVHManager::name()) == nullptr);

    EXPECT_TRUE(env->getDiscreteContactManager() == nullptr);
    EXPECT_TRUE(env->getContinuousContactManager() == nullptr);

    // Register contact manager
    EXPECT_TRUE(env->registerDiscreteContactManager(tesseract_collision_bullet::BulletDiscreteBVHManager::name(),
                                                    &tesseract_collision_bullet::BulletDiscreteBVHManager::create));
    EXPECT_TRUE(env->registerContinuousContactManager(tesseract_collision_bullet::BulletCastBVHManager::name(),
                                                      &tesseract_collision_bullet::BulletCastBVHManager::create));

    // Set active contact manager to one that does not exist
    EXPECT_FALSE(env->setActiveDiscreteContactManager("does_not_exist"));
    EXPECT_FALSE(env->setActiveContinuousContactManager("does_not_exist"));

    // Set Active contact manager
    EXPECT_TRUE(env->setActiveDiscreteContactManager(tesseract_collision_bullet::BulletDiscreteBVHManager::name()));
    EXPECT_TRUE(env->setActiveContinuousContactManager(tesseract_collision_bullet::BulletCastBVHManager::name()));

    // Contact managers should exist now
    EXPECT_TRUE(env->getDiscreteContactManager(tesseract_collision_bullet::BulletDiscreteBVHManager::name()) !=
                nullptr);
    EXPECT_TRUE(env->getContinuousContactManager(tesseract_collision_bullet::BulletCastBVHManager::name()) != nullptr);

    EXPECT_TRUE(env->getDiscreteContactManager() != nullptr);
    EXPECT_TRUE(env->getContinuousContactManager() != nullptr);
  }
  else
  {
    if (register_contact_managers == EnvRegisterMethod::CALL_DEFAULT_REGISTAR)
      env->registerDefaultContactManagers();

    // Contact managers should exist
    EXPECT_TRUE(env->getDiscreteContactManager(tesseract_collision_bullet::BulletDiscreteBVHManager::name()) !=
                nullptr);
    EXPECT_TRUE(env->getContinuousContactManager(tesseract_collision_bullet::BulletCastBVHManager::name()) != nullptr);
    EXPECT_TRUE(env->getDiscreteContactManager(tesseract_collision_fcl::FCLDiscreteBVHManager::name()) != nullptr);

    EXPECT_TRUE(env->getDiscreteContactManager() != nullptr);
    EXPECT_TRUE(env->getContinuousContactManager() != nullptr);
  }

  EXPECT_EQ(env->getFindTCPOffsetCallbacks().size(), 0);
  env->addFindTCPOffsetCallback(tcpCallback);
  EXPECT_EQ(env->getFindTCPOffsetCallbacks().size(), 1);

  return env;
}

TEST(TesseractEnvironmentUnit, EnvInitFailuresUnit)  // NOLINT
{
  auto env = std::make_shared<Environment>();
  EXPECT_TRUE(env != nullptr);
  EXPECT_EQ(0, env->getRevision());
  EXPECT_FALSE(env->reset());
  EXPECT_FALSE(env->isInitialized());
  EXPECT_TRUE(env->clone() != nullptr);

  // Test Empty commands
  Commands commands;
  EXPECT_FALSE(env->init(commands));
  EXPECT_FALSE(env->isInitialized());

  // Test scene graph is not first command
  commands.clear();
  auto cmd = std::make_shared<MoveJointCommand>("joint_name", "parent_link");
  commands.push_back(cmd);
  EXPECT_FALSE(env->init(commands));
  EXPECT_FALSE(env->isInitialized());
}

TEST(TesseractEnvironmentUnit, EnvCloneContactManagerUnit)  // NOLINT
{
  {  // Get the environment
    auto env = getEnvironment();

    // Test after clone if active list correct
    tesseract_collision::DiscreteContactManager::Ptr discrete_manager = env->getDiscreteContactManager();
    const std::vector<std::string>& e_active_list = env->getActiveLinkNames();
    const std::vector<std::string>& d_active_list = discrete_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), d_active_list.begin()));

    tesseract_collision::ContinuousContactManager::Ptr cast_manager = env->getContinuousContactManager();
    const std::vector<std::string>& c_active_list = cast_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), c_active_list.begin()));
  }

  {  // Get the environment with registered default contact managers
    auto env = getEnvironment(EnvRegisterMethod::ON_CONSTRUCTION);

    // Test after clone if active list correct
    tesseract_collision::DiscreteContactManager::Ptr discrete_manager = env->getDiscreteContactManager();
    const std::vector<std::string>& e_active_list = env->getActiveLinkNames();
    const std::vector<std::string>& d_active_list = discrete_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), d_active_list.begin()));

    tesseract_collision::ContinuousContactManager::Ptr cast_manager = env->getContinuousContactManager();
    const std::vector<std::string>& c_active_list = cast_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), c_active_list.begin()));
  }

  {  // Get the environment with registered default contact managers function
    auto env = getEnvironment(EnvRegisterMethod::CALL_DEFAULT_REGISTAR);

    // Test after clone if active list correct
    tesseract_collision::DiscreteContactManager::Ptr discrete_manager = env->getDiscreteContactManager();
    const std::vector<std::string>& e_active_list = env->getActiveLinkNames();
    const std::vector<std::string>& d_active_list = discrete_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), d_active_list.begin()));

    tesseract_collision::ContinuousContactManager::Ptr cast_manager = env->getContinuousContactManager();
    const std::vector<std::string>& c_active_list = cast_manager->getActiveCollisionObjects();
    EXPECT_TRUE(std::equal(e_active_list.begin(), e_active_list.end(), c_active_list.begin()));
  }
}

void runFindTCPTest()
{
  // Get the environment
  auto env = getEnvironment();

  // Add link to tip of kinematic chain
  Eigen::Isometry3d tcp_link_tf = Eigen::Isometry3d::Identity();
  tcp_link_tf.translation() = Eigen::Vector3d(0, 0, 0.35);
  Link tcp_link("tcp_link");
  Joint tcp_joint("tcp_joint");
  tcp_joint.parent_link_name = "tool0";
  tcp_joint.child_link_name = "tcp_link";
  tcp_joint.parent_to_joint_origin_transform = tcp_link_tf;
  tcp_joint.type = JointType::FIXED;
  env->applyCommand(std::make_shared<AddLinkCommand>(tcp_link, tcp_joint));

  // Add external tcp
  Eigen::Isometry3d external_tcp_link_tf = Eigen::Isometry3d::Identity();
  external_tcp_link_tf.translation() = Eigen::Vector3d(1, 0, 1);
  Link external_tcp_link("external_tcp_link");
  Joint external_tcp_joint("external_tcp_joint");
  external_tcp_joint.parent_link_name = "base_link";
  external_tcp_joint.child_link_name = "external_tcp_link";
  external_tcp_joint.parent_to_joint_origin_transform = external_tcp_link_tf;
  external_tcp_joint.type = JointType::FIXED;
  env->applyCommand(std::make_shared<AddLinkCommand>(external_tcp_link, external_tcp_joint));

  {  // Should return the solution form the provided callback
    Eigen::Isometry3d tcp = Eigen::Isometry3d::Identity();
    tcp.translation() = Eigen::Vector3d(0, 0, 0.1);
    tesseract_common::ManipulatorInfo manip_info("manipulator", "unknown", "unknown", tcp);
    Eigen::Isometry3d found_tcp = env->findTCPOffset(manip_info);
    EXPECT_TRUE(std::get<Eigen::Isometry3d>(manip_info.tcp_offset).isApprox(found_tcp, 1e-6));
  }

  {  // Empty tcp should return identity
    tesseract_common::ManipulatorInfo manip_info("manipulator", "", "");

    Eigen::Isometry3d tcp = Eigen::Isometry3d::Identity();
    Eigen::Isometry3d found_tcp = env->findTCPOffset(manip_info);
    EXPECT_TRUE(tcp.isApprox(found_tcp, 1e-6));
  }

  {  // The tcp is a link attached to the tip of the kinematic chain
    tesseract_common::ManipulatorInfo manip_info("manipulator", "unknown", "tcp_link");
    Eigen::Isometry3d found_tcp = env->findTCPOffset(manip_info);
    EXPECT_TRUE(tcp_link_tf.isApprox(found_tcp, 1e-6));
  }

  {  // The tcp is external link name
    tesseract_common::ManipulatorInfo manip_info("manipulator", "unknown", "external_tcp_link");
    Eigen::Isometry3d found_tcp = env->findTCPOffset(manip_info);
    EXPECT_TRUE(external_tcp_link_tf.isApprox(found_tcp, 1e-6));
  }

  {  // If the manipulator has a tcp transform then it should be returned
    Eigen::Isometry3d tcp = Eigen::Isometry3d::Identity();
    tcp.translation() = Eigen::Vector3d(0, 0, 0.25);
    tesseract_common::ManipulatorInfo manip_info("manipulator", "", "", tcp);
    Eigen::Isometry3d found_tcp = env->findTCPOffset(manip_info);
    EXPECT_TRUE(std::get<Eigen::Isometry3d>(manip_info.tcp_offset).isApprox(found_tcp, 1e-6));
  }

  {  // If the manipulator does not exist it should throw an exception
    tesseract_common::ManipulatorInfo manip_info("missing_manipulator", "unknown", "unknown");
    EXPECT_ANY_THROW(env->findTCPOffset(manip_info));  // NOLINT
  }
}

TEST(TesseractEnvironmentUnit, EnvAddAndRemoveAllowedCollisionCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  std::string l1 = "link_1";
  std::string l2 = "link_6";
  std::string r = "Unit Test";

  AllowedCollisionMatrix::ConstPtr acm = env->getAllowedCollisionMatrix();
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "base_link"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_2"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_3"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_4"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_5"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_6"));
  EXPECT_TRUE(acm->isCollisionAllowed(l1, "link_7"));
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  // Remove allowed collision
  auto cmd_remove = std::make_shared<RemoveAllowedCollisionCommand>(l1, l2);
  EXPECT_EQ(cmd_remove->getType(), CommandType::REMOVE_ALLOWED_COLLISION);
  EXPECT_EQ(cmd_remove->getLinkName1(), l1);
  EXPECT_EQ(cmd_remove->getLinkName2(), l2);

  EXPECT_TRUE(env->applyCommand(cmd_remove));

  EXPECT_FALSE(acm->isCollisionAllowed(l1, l2));
  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_EQ(env->getCommandHistory().back(), cmd_remove);

  // Add allowed collision back
  auto cmd_add = std::make_shared<AddAllowedCollisionCommand>(l1, l2, r);
  EXPECT_EQ(cmd_add->getType(), CommandType::ADD_ALLOWED_COLLISION);
  EXPECT_EQ(cmd_add->getLinkName1(), l1);
  EXPECT_EQ(cmd_add->getLinkName2(), l2);
  EXPECT_EQ(cmd_add->getReason(), r);

  EXPECT_TRUE(env->applyCommand(cmd_add));

  EXPECT_TRUE(acm->isCollisionAllowed(l1, l2));
  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);
  EXPECT_EQ(env->getCommandHistory().back(), cmd_add);

  // Remove allowed collision
  auto cmd_remove_link = std::make_shared<RemoveAllowedCollisionLinkCommand>(l1);
  EXPECT_EQ(cmd_remove_link->getType(), CommandType::REMOVE_ALLOWED_COLLISION_LINK);
  EXPECT_EQ(cmd_remove_link->getLinkName(), l1);

  EXPECT_TRUE(env->applyCommand(cmd_remove_link));

  EXPECT_FALSE(acm->isCollisionAllowed(l1, "base_link"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_2"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_3"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_4"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_5"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_6"));
  EXPECT_FALSE(acm->isCollisionAllowed(l1, "link_7"));
  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);
  EXPECT_EQ(env->getCommandHistory().back(), cmd_remove_link);
}

TEST(TesseractEnvironmentUnit, EnvAddandRemoveLink)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  auto visual = std::make_shared<Visual>();
  visual->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);
  auto collision = std::make_shared<Collision>();
  collision->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);

  const std::string link_name1 = "link_n1";
  const std::string link_name2 = "link_n2";
  const std::string joint_name1 = "joint_n1";
  Link link_1(link_name1);
  link_1.visual.push_back(visual);
  link_1.collision.push_back(collision);
  Link link_2(link_name2);

  Joint joint_1(joint_name1);
  joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
  joint_1.parent_link_name = link_name1;
  joint_1.child_link_name = link_name2;
  joint_1.type = JointType::FIXED;

  {
    auto cmd = std::make_shared<AddLinkCommand>(link_1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_LINK);
    EXPECT_TRUE(cmd->getLink() != nullptr);
    EXPECT_TRUE(cmd->getJoint() == nullptr);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_TRUE(env->getDiscreteContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getDiscreteContactManager()->hasCollisionObject(link_name2));
  EXPECT_TRUE(env->getContinuousContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getContinuousContactManager()->hasCollisionObject(link_name2));

  std::vector<std::string> link_names = env->getLinkNames();
  std::vector<std::string> joint_names = env->getJointNames();
  tesseract_scene_graph::SceneState state = env->getState();
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), "joint_" + link_name1) != joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("joint_" + link_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("joint_" + link_name1) == state.joints.end());

  {
    auto cmd = std::make_shared<AddLinkCommand>(link_2, joint_1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_LINK);
    EXPECT_TRUE(cmd->getLink() != nullptr);
    EXPECT_TRUE(cmd->getJoint() != nullptr);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);

  link_names = env->getLinkNames();
  joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) != joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "before_remove_link_unit.dot");

  {
    auto cmd = std::make_shared<RemoveLinkCommand>(link_name1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::REMOVE_LINK);
    EXPECT_EQ(cmd->getLinkName(), link_name1);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);
  EXPECT_FALSE(env->getDiscreteContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getDiscreteContactManager()->hasCollisionObject(link_name2));
  EXPECT_FALSE(env->getContinuousContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getContinuousContactManager()->hasCollisionObject(link_name2));

  link_names = env->getLinkNames();
  joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) == link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), "joint_" + link_name1) == joint_names.end());
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) == link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) == joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name1) == state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("joint_" + link_name1) == state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("joint_" + link_name1) == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) == state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) == state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "after_remove_link_unit.dot");

  // Test against double removing
  {
    auto cmd = std::make_shared<RemoveLinkCommand>(link_name1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::REMOVE_LINK);
    EXPECT_EQ(cmd->getLinkName(), link_name1);
    EXPECT_FALSE(env->applyCommand(cmd));
  }
  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);

  {
    auto cmd = std::make_shared<RemoveLinkCommand>(link_name2);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::REMOVE_LINK);
    EXPECT_EQ(cmd->getLinkName(), link_name2);
    EXPECT_FALSE(env->applyCommand(cmd));
  }
  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);

  {
    auto cmd = std::make_shared<RemoveJointCommand>(joint_name1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::REMOVE_JOINT);
    EXPECT_EQ(cmd->getJointName(), joint_name1);
    EXPECT_FALSE(env->applyCommand(cmd));
  }
  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);

  {
    auto cmd = std::make_shared<RemoveJointCommand>("joint_" + link_name1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::REMOVE_JOINT);
    EXPECT_EQ(cmd->getJointName(), "joint_" + link_name1);
    EXPECT_FALSE(env->applyCommand(cmd));
  }
  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);
}

TEST(TesseractEnvironmentUnit, EnvAddKinematicsInformationCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  KinematicsInformation kin_info;

  auto cmd = std::make_shared<AddKinematicsInformationCommand>(kin_info);

  EXPECT_TRUE(cmd != nullptr);
  EXPECT_EQ(cmd->getType(), CommandType::ADD_KINEMATICS_INFORMATION);
  KinematicsInformation kin_info2 = cmd->getKinematicsInformation();
  EXPECT_TRUE(kin_info2.group_names == kin_info.group_names);
  EXPECT_TRUE(kin_info2.group_names == kin_info.group_names);
  EXPECT_TRUE(kin_info2.chain_groups == kin_info.chain_groups);
  EXPECT_TRUE(kin_info2.joint_groups == kin_info.joint_groups);
  EXPECT_TRUE(kin_info2.link_groups == kin_info.link_groups);
  EXPECT_TRUE(kin_info2.group_states == kin_info.group_states);
  EXPECT_TRUE(env->applyCommand(cmd));

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
}

TEST(TesseractEnvironmentUnit, EnvAddSceneGraphCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  SceneGraph::Ptr subgraph = std::make_shared<SceneGraph>();
  subgraph->setName("subgraph");

  {  // Adding an empty scene graph which should fail
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_FALSE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  Joint joint_1("provided_subgraph_joint");
  joint_1.parent_link_name = "base_link";
  joint_1.child_link_name = "prefix_subgraph_base_link";
  joint_1.type = JointType::FIXED;

  {  // Adding an empty scene graph which should fail with joint
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph, joint_1);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_FALSE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  subgraph = getSubSceneGraph();

  const std::string link_name1 = "subgraph_base_link";
  const std::string link_name2 = "subgraph_link_1";
  const std::string joint_name1 = "subgraph_joint1";

  {
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_TRUE(env->getDiscreteContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getDiscreteContactManager()->hasCollisionObject(link_name2));
  EXPECT_TRUE(env->getContinuousContactManager()->hasCollisionObject(link_name1));
  EXPECT_FALSE(env->getContinuousContactManager()->hasCollisionObject(link_name2));

  tesseract_scene_graph::SceneState state = env->getState();
  EXPECT_TRUE(env->getJoint("subgraph_joint") != nullptr);
  EXPECT_TRUE(env->getLink(link_name1) != nullptr);
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("subgraph_joint") != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("subgraph_joint") == state.joints.end());

  {  // Adding twice with the same name should fail
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_FALSE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);

  // Add subgraph with prefix
  {
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph, "prefix_");
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);

  state = env->getState();
  EXPECT_TRUE(env->getJoint("prefix_subgraph_joint") != nullptr);
  EXPECT_TRUE(env->getLink("prefix_subgraph_base_link") != nullptr);
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("subgraph_joint") != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("subgraph_joint") == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find("prefix_subgraph_base_link") != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("prefix_subgraph_joint") != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("prefix_subgraph_joint") == state.joints.end());

  // Add subgraph with prefix and joint
  joint_1.child_link_name = "prefix2_subgraph_base_link";
  {
    auto cmd = std::make_shared<AddSceneGraphCommand>(*subgraph, joint_1, "prefix2_");
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::ADD_SCENE_GRAPH);
    EXPECT_TRUE(cmd->getSceneGraph() != nullptr);
    EXPECT_TRUE(env->applyCommand(cmd));
  }

  EXPECT_EQ(env->getRevision(), 5);
  EXPECT_EQ(env->getCommandHistory().size(), 5);

  state = env->getState();
  EXPECT_TRUE(env->getJoint("provided_subgraph_joint") != nullptr);
  EXPECT_TRUE(env->getLink("prefix2_subgraph_base_link") != nullptr);
  EXPECT_TRUE(state.link_transforms.find("prefix2_subgraph_base_link") != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find("provided_subgraph_joint") != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find("provided_subgraph_joint") == state.joints.end());
}

TEST(TesseractEnvironmentUnit, EnvChangeJointLimitsCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  {
    JointLimits::ConstPtr limits = env->getJointLimits("not_in_graph");
    EXPECT_TRUE(limits == nullptr);
  }

  {  // Note that this will fail artificially if the urdf is changed for some reason
    JointLimits::ConstPtr limits = env->getJointLimits("joint_a1");
    EXPECT_NEAR(limits->lower, -2.9668, 1e-5);
    EXPECT_NEAR(limits->upper, 2.9668, 1e-5);
    EXPECT_NEAR(limits->velocity, 1.4834, 1e-5);
    EXPECT_NEAR(limits->effort, 0, 1e-5);
  }

  {
    double new_lower = 1.0;
    double new_upper = 2.0;
    double new_velocity = 3.0;
    double new_acceleration = 4.0;

    int revision = env->getRevision();
    auto cmd_jpl = std::make_shared<ChangeJointPositionLimitsCommand>("joint_a1", new_lower, new_upper);
    EXPECT_EQ(cmd_jpl->getType(), CommandType::CHANGE_JOINT_POSITION_LIMITS);
    EXPECT_EQ(cmd_jpl->getLimits().size(), 1);
    auto it_jpl = cmd_jpl->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jpl != cmd_jpl->getLimits().end());
    EXPECT_EQ(it_jpl->first, "joint_a1");
    EXPECT_NEAR(it_jpl->second.first, new_lower, 1e-6);
    EXPECT_NEAR(it_jpl->second.second, new_upper, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jpl));
    EXPECT_EQ(revision + 1, env->getRevision());
    EXPECT_EQ(revision + 1, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jpl);

    auto cmd_jvl = std::make_shared<ChangeJointVelocityLimitsCommand>("joint_a1", new_velocity);
    EXPECT_EQ(cmd_jvl->getType(), CommandType::CHANGE_JOINT_VELOCITY_LIMITS);
    EXPECT_EQ(cmd_jvl->getLimits().size(), 1);
    auto it_jvl = cmd_jvl->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jvl != cmd_jvl->getLimits().end());
    EXPECT_EQ(it_jvl->first, "joint_a1");
    EXPECT_NEAR(it_jvl->second, new_velocity, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jvl));
    EXPECT_EQ(revision + 2, env->getRevision());
    EXPECT_EQ(revision + 2, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jvl);

    auto cmd_jal = std::make_shared<ChangeJointAccelerationLimitsCommand>("joint_a1", new_acceleration);
    EXPECT_EQ(cmd_jal->getType(), CommandType::CHANGE_JOINT_ACCELERATION_LIMITS);
    EXPECT_EQ(cmd_jal->getLimits().size(), 1);
    auto it_jal = cmd_jal->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jal != cmd_jal->getLimits().end());
    EXPECT_EQ(it_jal->first, "joint_a1");
    EXPECT_NEAR(it_jal->second, new_acceleration, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jal));
    EXPECT_EQ(revision + 3, env->getRevision());
    EXPECT_EQ(revision + 3, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jal);

    // Check that the environment returns the correct limits
    JointLimits new_limits = *(env->getJointLimits("joint_a1"));
    EXPECT_NEAR(new_limits.lower, new_lower, 1e-5);
    EXPECT_NEAR(new_limits.upper, new_upper, 1e-5);
    EXPECT_NEAR(new_limits.velocity, new_velocity, 1e-5);
    EXPECT_NEAR(new_limits.acceleration, new_acceleration, 1e-5);
  }
  {  // call map method
    double new_lower = 1.0;
    double new_upper = 2.0;
    double new_velocity = 3.0;
    double new_acceleration = 4.0;

    std::unordered_map<std::string, std::pair<double, double> > position_limit_map;
    position_limit_map["joint_a1"] = std::make_pair(new_lower, new_upper);

    std::unordered_map<std::string, double> velocity_limit_map;
    velocity_limit_map["joint_a1"] = new_velocity;

    std::unordered_map<std::string, double> acceleration_limit_map;
    acceleration_limit_map["joint_a1"] = new_acceleration;

    int revision = env->getRevision();
    auto cmd_jpl = std::make_shared<ChangeJointPositionLimitsCommand>(position_limit_map);
    EXPECT_EQ(cmd_jpl->getType(), CommandType::CHANGE_JOINT_POSITION_LIMITS);
    EXPECT_EQ(cmd_jpl->getLimits().size(), 1);
    auto it_jpl = cmd_jpl->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jpl != cmd_jpl->getLimits().end());
    EXPECT_EQ(it_jpl->first, "joint_a1");
    EXPECT_NEAR(it_jpl->second.first, new_lower, 1e-6);
    EXPECT_NEAR(it_jpl->second.second, new_upper, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jpl));
    EXPECT_EQ(revision + 1, env->getRevision());
    EXPECT_EQ(revision + 1, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jpl);

    auto cmd_jvl = std::make_shared<ChangeJointVelocityLimitsCommand>(velocity_limit_map);
    EXPECT_EQ(cmd_jvl->getType(), CommandType::CHANGE_JOINT_VELOCITY_LIMITS);
    EXPECT_EQ(cmd_jvl->getLimits().size(), 1);
    auto it_jvl = cmd_jvl->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jvl != cmd_jvl->getLimits().end());
    EXPECT_EQ(it_jvl->first, "joint_a1");
    EXPECT_NEAR(it_jvl->second, new_velocity, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jvl));
    EXPECT_EQ(revision + 2, env->getRevision());
    EXPECT_EQ(revision + 2, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jvl);

    auto cmd_jal = std::make_shared<ChangeJointAccelerationLimitsCommand>(acceleration_limit_map);
    EXPECT_EQ(cmd_jal->getType(), CommandType::CHANGE_JOINT_ACCELERATION_LIMITS);
    EXPECT_EQ(cmd_jal->getLimits().size(), 1);
    auto it_jal = cmd_jal->getLimits().find("joint_a1");
    EXPECT_TRUE(it_jal != cmd_jal->getLimits().end());
    EXPECT_EQ(it_jal->first, "joint_a1");
    EXPECT_NEAR(it_jal->second, new_acceleration, 1e-6);
    EXPECT_TRUE(env->applyCommand(cmd_jal));
    EXPECT_EQ(revision + 3, env->getRevision());
    EXPECT_EQ(revision + 3, env->getCommandHistory().size());
    EXPECT_EQ(env->getCommandHistory().back(), cmd_jal);

    // Check that the environment returns the correct limits
    JointLimits new_limits = *(env->getJointLimits("joint_a1"));
    EXPECT_NEAR(new_limits.lower, new_lower, 1e-5);
    EXPECT_NEAR(new_limits.upper, new_upper, 1e-5);
    EXPECT_NEAR(new_limits.velocity, new_velocity, 1e-5);
    EXPECT_NEAR(new_limits.acceleration, new_acceleration, 1e-5);
  }
}

TEST(TesseractEnvironmentUnit, EnvChangeJointOriginCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  const std::string link_name1 = "link_n1";
  const std::string joint_name1 = "joint_n1";
  Link link_1(link_name1);

  Joint joint_1(joint_name1);
  joint_1.parent_link_name = env->getRootLinkName();
  joint_1.child_link_name = link_name1;
  joint_1.type = JointType::FIXED;

  env->applyCommand(std::make_shared<AddLinkCommand>(link_1, joint_1));
  tesseract_scene_graph::SceneState state = env->getState();
  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "before_change_joint_origin_unit.dot");

  Eigen::Isometry3d new_origin = Eigen::Isometry3d::Identity();
  new_origin.translation()(0) += 1.234;

  auto cmd = std::make_shared<ChangeJointOriginCommand>(joint_name1, new_origin);
  EXPECT_EQ(cmd->getType(), CommandType::CHANGE_JOINT_ORIGIN);
  EXPECT_EQ(cmd->getJointName(), joint_name1);
  EXPECT_TRUE(new_origin.isApprox(cmd->getOrigin()));
  EXPECT_TRUE(env->applyCommand(cmd));
  EXPECT_EQ(env->getCommandHistory().back(), cmd);

  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);

  // Check that the origin got updated
  state = env->getState();
  EXPECT_TRUE(env->getJoint(joint_name1)->parent_to_joint_origin_transform.isApprox(new_origin));
  EXPECT_TRUE(state.link_transforms.at(link_name1).isApprox(new_origin));
  EXPECT_TRUE(state.joint_transforms.at(joint_name1).isApprox(new_origin));

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "after_change_joint_origin_unit.dot");
}

TEST(TesseractEnvironmentUnit, EnvChangeLinkOriginCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  std::string link_name = "base_link";
  Eigen::Isometry3d new_origin = Eigen::Isometry3d::Identity();
  new_origin.translation()(0) += 1.234;

  auto cmd = std::make_shared<ChangeLinkOriginCommand>(link_name, new_origin);
  EXPECT_EQ(cmd->getType(), CommandType::CHANGE_LINK_ORIGIN);
  EXPECT_EQ(cmd->getLinkName(), link_name);
  EXPECT_TRUE(new_origin.isApprox(cmd->getOrigin()));
  EXPECT_ANY_THROW(env->applyCommand(cmd));  // NOLINT
}

TEST(TesseractEnvironmentUnit, EnvChangeLinkCollisionEnabledCommandUnit)  // NOLINT
{
  // Get the environment
  /** @todo update contact manager to have function to check collision object enabled state */
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  std::string link_name = "link_1";
  EXPECT_TRUE(env->getSceneGraph()->getLinkCollisionEnabled(link_name));

  {  // Disable Link collision
    auto cmd = std::make_shared<ChangeLinkCollisionEnabledCommand>(link_name, false);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::CHANGE_LINK_COLLISION_ENABLED);
    EXPECT_EQ(cmd->getEnabled(), false);
    EXPECT_EQ(cmd->getLinkName(), link_name);
    EXPECT_TRUE(env->applyCommand(cmd));
    EXPECT_EQ(env->getCommandHistory().back(), cmd);
  }

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_FALSE(env->getSceneGraph()->getLinkCollisionEnabled(link_name));

  {  // Enable Link collision
    auto cmd = std::make_shared<ChangeLinkCollisionEnabledCommand>(link_name, true);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::CHANGE_LINK_COLLISION_ENABLED);
    EXPECT_EQ(cmd->getEnabled(), true);
    EXPECT_EQ(cmd->getLinkName(), link_name);
    EXPECT_TRUE(env->applyCommand(cmd));
    EXPECT_EQ(env->getCommandHistory().back(), cmd);
  }

  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);
  EXPECT_TRUE(env->getSceneGraph()->getLinkCollisionEnabled(link_name));
}

TEST(TesseractEnvironmentUnit, EnvChangeLinkVisibilityCommandUnit)  // NOLINT
{
  // Get the environment
  /** @todo update contact manager to have function to check collision object enabled state */
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  std::string link_name = "link_1";
  EXPECT_TRUE(env->getSceneGraph()->getLinkVisibility(link_name));

  auto cmd = std::make_shared<ChangeLinkVisibilityCommand>(link_name, false);
  EXPECT_TRUE(cmd != nullptr);
  EXPECT_EQ(cmd->getType(), CommandType::CHANGE_LINK_VISIBILITY);
  EXPECT_EQ(cmd->getEnabled(), false);
  EXPECT_EQ(cmd->getLinkName(), link_name);
  EXPECT_TRUE(env->applyCommand(cmd));
  EXPECT_EQ(env->getCommandHistory().back(), cmd);

  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);
  EXPECT_FALSE(env->getSceneGraph()->getLinkVisibility(link_name));
}

TEST(TesseractEnvironmentUnit, EnvChangeCollisionMarginsCommandUnit)  // NOLINT
{
  {  // MODIFY_PAIR_MARGIN  and OVERRIDE_PAIR_MARGIN Unit Test
    std::string link_name1 = "link_1";
    std::string link_name2 = "link_2";
    double margin = 0.1;

    auto env = getEnvironment();
    EXPECT_EQ(env->getRevision(), 2);
    EXPECT_EQ(env->getCommandHistory().size(), 2);
    EXPECT_NEAR(
        env->getDiscreteContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        0.0,
        1e-6);
    EXPECT_NEAR(
        env->getContinuousContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        0.0,
        1e-6);

    // Test MODIFY_PAIR_MARGIN
    tesseract_common::CollisionMarginData collision_margin_data;
    collision_margin_data.setPairCollisionMargin(link_name1, link_name2, margin);
    tesseract_common::CollisionMarginOverrideType overrid_type =
        tesseract_common::CollisionMarginOverrideType::MODIFY_PAIR_MARGIN;

    auto cmd = std::make_shared<ChangeCollisionMarginsCommand>(collision_margin_data, overrid_type);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::CHANGE_COLLISION_MARGINS);
    EXPECT_EQ(cmd->getCollisionMarginData(), collision_margin_data);
    EXPECT_EQ(cmd->getCollisionMarginOverrideType(), tesseract_common::CollisionMarginOverrideType::MODIFY_PAIR_MARGIN);
    EXPECT_TRUE(env->applyCommand(cmd));

    EXPECT_EQ(env->getRevision(), 3);
    EXPECT_EQ(env->getCommandHistory().size(), 3);
    EXPECT_EQ(env->getCommandHistory().back(), cmd);
    EXPECT_NEAR(
        env->getDiscreteContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        margin,
        1e-6);
    EXPECT_NEAR(
        env->getContinuousContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        margin,
        1e-6);

    // Test
    std::string link_name3 = "link_3";
    std::string link_name4 = "link_4";
    margin = 0.2;
    collision_margin_data = tesseract_common::CollisionMarginData();
    collision_margin_data.setPairCollisionMargin(link_name3, link_name4, margin);
    overrid_type = tesseract_common::CollisionMarginOverrideType::OVERRIDE_PAIR_MARGIN;

    cmd = std::make_shared<ChangeCollisionMarginsCommand>(collision_margin_data, overrid_type);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::CHANGE_COLLISION_MARGINS);
    EXPECT_EQ(cmd->getCollisionMarginData(), collision_margin_data);
    EXPECT_EQ(cmd->getCollisionMarginOverrideType(),
              tesseract_common::CollisionMarginOverrideType::OVERRIDE_PAIR_MARGIN);
    EXPECT_TRUE(env->applyCommand(cmd));

    EXPECT_EQ(env->getRevision(), 4);
    EXPECT_EQ(env->getCommandHistory().size(), 4);
    EXPECT_EQ(env->getCommandHistory().back(), cmd);
    // Link1 and Link2 should be reset
    EXPECT_NEAR(
        env->getDiscreteContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        0,
        1e-6);
    EXPECT_NEAR(
        env->getContinuousContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name1, link_name2),
        0,
        1e-6);
    EXPECT_NEAR(
        env->getDiscreteContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name3, link_name4),
        margin,
        1e-6);
    EXPECT_NEAR(
        env->getContinuousContactManager()->getCollisionMarginData().getPairCollisionMargin(link_name3, link_name4),
        margin,
        1e-6);
  }

  {  // OVERRIDE_DEFAULT_MARGIN Unit Test
    auto env = getEnvironment();
    EXPECT_EQ(env->getRevision(), 2);
    EXPECT_EQ(env->getCommandHistory().size(), 2);
    EXPECT_NEAR(env->getDiscreteContactManager()->getCollisionMarginData().getDefaultCollisionMargin(), 0.0, 1e-6);
    EXPECT_NEAR(env->getContinuousContactManager()->getCollisionMarginData().getDefaultCollisionMargin(), 0.0, 1e-6);

    tesseract_common::CollisionMarginData collision_margin_data(0.1);
    tesseract_common::CollisionMarginOverrideType overrid_type =
        tesseract_common::CollisionMarginOverrideType::OVERRIDE_DEFAULT_MARGIN;

    auto cmd = std::make_shared<ChangeCollisionMarginsCommand>(collision_margin_data, overrid_type);
    EXPECT_TRUE(cmd != nullptr);
    EXPECT_EQ(cmd->getType(), CommandType::CHANGE_COLLISION_MARGINS);
    EXPECT_EQ(cmd->getCollisionMarginData(), collision_margin_data);
    EXPECT_EQ(cmd->getCollisionMarginOverrideType(),
              tesseract_common::CollisionMarginOverrideType::OVERRIDE_DEFAULT_MARGIN);
    EXPECT_TRUE(env->applyCommand(cmd));

    EXPECT_EQ(env->getRevision(), 3);
    EXPECT_EQ(env->getCommandHistory().size(), 3);
    EXPECT_EQ(env->getCommandHistory().back(), cmd);
    EXPECT_NEAR(env->getDiscreteContactManager()->getCollisionMarginData().getDefaultCollisionMargin(), 0.1, 1e-6);
    EXPECT_NEAR(env->getContinuousContactManager()->getCollisionMarginData().getDefaultCollisionMargin(), 0.1, 1e-6);
  }
}

TEST(TesseractEnvironmentUnit, EnvMoveJointCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  const std::string link_name1 = "link_n1";
  const std::string link_name2 = "link_n2";
  const std::string joint_name1 = "joint_n1";
  const std::string joint_name2 = "joint_n2";
  Link link_1(link_name1);
  Link link_2(link_name2);

  Joint joint_1(joint_name1);
  joint_1.parent_link_name = env->getRootLinkName();
  joint_1.child_link_name = link_name1;
  joint_1.type = JointType::FIXED;

  Joint joint_2(joint_name2);
  joint_2.parent_to_joint_origin_transform.translation()(0) = 1.25;
  joint_2.parent_link_name = link_name1;
  joint_2.child_link_name = link_name2;
  joint_2.type = JointType::FIXED;

  env->applyCommand(std::make_shared<AddLinkCommand>(link_1, joint_1));
  tesseract_scene_graph::SceneState state = env->getState();
  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);

  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());

  env->applyCommand(std::make_shared<AddLinkCommand>(link_2, joint_2));
  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);

  std::vector<std::string> link_names = env->getLinkNames();
  std::vector<std::string> joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) != link_names.end());
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) != joint_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name2) != joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name2) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name2) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "before_move_joint_unit.dot");

  auto cmd = std::make_shared<MoveJointCommand>(joint_name1, "tool0");
  EXPECT_TRUE(cmd != nullptr);
  EXPECT_EQ(cmd->getType(), CommandType::MOVE_JOINT);
  EXPECT_EQ(cmd->getJointName(), joint_name1);
  EXPECT_EQ(cmd->getParentLink(), "tool0");
  EXPECT_TRUE(env->applyCommand(cmd));
  EXPECT_EQ(env->getCommandHistory().back(), cmd);

  link_names = env->getLinkNames();
  joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(env->getJoint(joint_name1)->parent_link_name == "tool0");
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) != link_names.end());
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) != joint_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name2) != joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name2) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name2) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "after_move_joint_unit.dot");
}

TEST(TesseractEnvironmentUnit, EnvMoveLinkCommandUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getRevision(), 2);
  EXPECT_EQ(env->getCommandHistory().size(), 2);

  const std::string link_name1 = "link_n1";
  const std::string link_name2 = "link_n2";
  const std::string joint_name1 = "joint_n1";
  const std::string joint_name2 = "joint_n2";
  Link link_1(link_name1);
  Link link_2(link_name2);

  Joint joint_1(joint_name1);
  joint_1.parent_link_name = env->getRootLinkName();
  joint_1.child_link_name = link_name1;
  joint_1.type = JointType::FIXED;

  Joint joint_2(joint_name2);
  joint_2.parent_to_joint_origin_transform.translation()(0) = 1.25;
  joint_2.parent_link_name = link_name1;
  joint_2.child_link_name = link_name2;
  joint_2.type = JointType::FIXED;

  env->applyCommand(std::make_shared<AddLinkCommand>(link_1, joint_1));
  tesseract_scene_graph::SceneState state = env->getState();
  EXPECT_EQ(env->getRevision(), 3);
  EXPECT_EQ(env->getCommandHistory().size(), 3);

  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());

  env->applyCommand(std::make_shared<AddLinkCommand>(link_2, joint_2));
  EXPECT_EQ(env->getRevision(), 4);
  EXPECT_EQ(env->getCommandHistory().size(), 4);

  std::vector<std::string> link_names = env->getLinkNames();
  std::vector<std::string> joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) != link_names.end());
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) != joint_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name2) != joint_names.end());
  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name2) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name2) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "before_move_link_unit.dot");

  std::string moved_joint_name = joint_name1 + "_moved";
  Joint move_link_joint = joint_1.clone(moved_joint_name);
  move_link_joint.parent_link_name = "tool0";

  auto cmd = std::make_shared<MoveLinkCommand>(move_link_joint);
  EXPECT_TRUE(cmd != nullptr);
  EXPECT_EQ(cmd->getType(), CommandType::MOVE_LINK);
  EXPECT_TRUE(cmd->getJoint() != nullptr);
  EXPECT_TRUE(env->applyCommand(cmd));
  EXPECT_EQ(env->getCommandHistory().back(), cmd);

  link_names = env->getLinkNames();
  joint_names = env->getJointNames();
  state = env->getState();
  EXPECT_TRUE(env->getJoint(moved_joint_name)->parent_link_name == "tool0");
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name1) != link_names.end());
  EXPECT_TRUE(std::find(link_names.begin(), link_names.end(), link_name2) != link_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name1) == joint_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), moved_joint_name) != joint_names.end());
  EXPECT_TRUE(std::find(joint_names.begin(), joint_names.end(), joint_name2) != joint_names.end());

  EXPECT_TRUE(state.link_transforms.find(link_name1) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name1) == state.joint_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(moved_joint_name) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name1) == state.joints.end());
  EXPECT_TRUE(state.link_transforms.find(link_name2) != state.link_transforms.end());
  EXPECT_TRUE(state.joint_transforms.find(joint_name2) != state.joint_transforms.end());
  EXPECT_TRUE(state.joints.find(joint_name2) == state.joints.end());

  env->getSceneGraph()->saveDOT(tesseract_common::getTempPath() + "after_move_link_unit.dot");
}

TEST(TesseractEnvironmentUnit, EnvCurrentStatePreservedWhenEnvChanges)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  // Check if visibility and collision enabled
  for (const auto& link_name : env->getLinkNames())
  {
    EXPECT_TRUE(env->getLinkCollisionEnabled(link_name));
    EXPECT_TRUE(env->getLinkVisibility(link_name));
  }

  // Set the initial state of the robot
  std::unordered_map<std::string, double> joint_states;
  joint_states["joint_a1"] = 0.0;
  joint_states["joint_a2"] = 0.0;
  joint_states["joint_a3"] = 0.0;
  joint_states["joint_a4"] = -1.57;
  joint_states["joint_a5"] = 0.0;
  joint_states["joint_a6"] = 0.0;
  joint_states["joint_a7"] = 0.0;
  env->setState(joint_states);

  SceneState state = env->getState();
  for (auto& joint_state : joint_states)
  {
    EXPECT_NEAR(state.joints.at(joint_state.first), joint_state.second, 1e-5);
  }

  Link link("link_n1");

  Joint joint("joint_n1");
  joint.parent_link_name = env->getRootLinkName();
  joint.child_link_name = "link_n1";
  joint.type = JointType::FIXED;

  env->applyCommand(std::make_shared<AddLinkCommand>(link, joint));

  state = env->getState();
  for (auto& joint_state : joint_states)
  {
    EXPECT_NEAR(state.joints.at(joint_state.first), joint_state.second, 1e-5);
  }

  // Check if visibility and collision enabled
  for (const auto& link_name : env->getLinkNames())
  {
    EXPECT_TRUE(env->getLinkCollisionEnabled(link_name));
    EXPECT_TRUE(env->getLinkVisibility(link_name));
  }
}

TEST(TesseractEnvironmentUnit, EnvResetUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  // Check if visibility and collision enabled
  for (const auto& link_name : env->getLinkNames())
  {
    EXPECT_TRUE(env->getLinkCollisionEnabled(link_name));
    EXPECT_TRUE(env->getLinkVisibility(link_name));
  }

  int init_rev = env->getRevision();

  Link link("link_n1");
  Joint joint("joint_n1");
  joint.parent_link_name = env->getRootLinkName();
  joint.child_link_name = "link_n1";
  joint.type = JointType::FIXED;

  env->applyCommand(std::make_shared<AddLinkCommand>(link, joint));
  EXPECT_EQ(env->getRevision(), init_rev + 1);
  EXPECT_EQ(env->getCommandHistory().size(), init_rev + 1);

  Commands commands = env->getCommandHistory();

  // Check reset
  EXPECT_TRUE(env->reset());
  EXPECT_EQ(env->getRevision(), init_rev);
  EXPECT_EQ(env->getCommandHistory().size(), init_rev);
  EXPECT_TRUE(env->isInitialized());

  // Check if visibility and collision enabled
  for (const auto& link_name : env->getLinkNames())
  {
    EXPECT_TRUE(env->getLinkCollisionEnabled(link_name));
    EXPECT_TRUE(env->getLinkVisibility(link_name));
  }

  // Check reinit
  EXPECT_TRUE(env->init(commands));
  EXPECT_EQ(env->getRevision(), init_rev + 1);
  EXPECT_EQ(env->getCommandHistory().size(), init_rev + 1);
  EXPECT_TRUE(env->isInitialized());

  // Check if visibility and collision enabled
  for (const auto& link_name : env->getLinkNames())
  {
    EXPECT_TRUE(env->getLinkCollisionEnabled(link_name));
    EXPECT_TRUE(env->getLinkVisibility(link_name));
  }
}

TEST(TesseractEnvironmentUnit, EnvChangeNameUnit)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();
  EXPECT_EQ(env->getName(), "kuka_lbr_iiwa_14_r820");

  std::string env_changed_name = "env_unit_change_name";
  env->setName(env_changed_name);
  EXPECT_EQ(env->getName(), env_changed_name);
  EXPECT_EQ(env->getSceneGraph()->getName(), env_changed_name);
}

void runCompareSceneStates(const SceneState& base_state, const SceneState& compare_state)
{
  EXPECT_EQ(base_state.joints.size(), compare_state.joints.size());
  EXPECT_EQ(base_state.joint_transforms.size(), compare_state.joint_transforms.size());
  EXPECT_EQ(base_state.link_transforms.size(), compare_state.link_transforms.size());

  for (const auto& pair : base_state.joints)
  {
    EXPECT_NEAR(pair.second, compare_state.joints.at(pair.first), 1e-6);
  }

  for (const auto& pair : base_state.joint_transforms)
  {
    EXPECT_TRUE(pair.second.isApprox(compare_state.joint_transforms.at(pair.first), 1e-6));
  }

  for (const auto& link_pair : base_state.link_transforms)
  {
    EXPECT_TRUE(link_pair.second.isApprox(compare_state.link_transforms.at(link_pair.first), 1e-6));
  }
}

void runCompareStateSolver(const StateSolver& base_solver, StateSolver& comp_solver)
{
  EXPECT_EQ(base_solver.getBaseLinkName(), comp_solver.getBaseLinkName());
  EXPECT_TRUE(tesseract_common::isIdentical(base_solver.getJointNames(), comp_solver.getJointNames(), false));
  EXPECT_TRUE(
      tesseract_common::isIdentical(base_solver.getActiveJointNames(), comp_solver.getActiveJointNames(), false));
  EXPECT_TRUE(tesseract_common::isIdentical(base_solver.getLinkNames(), comp_solver.getLinkNames(), false));
  EXPECT_TRUE(tesseract_common::isIdentical(base_solver.getActiveLinkNames(), comp_solver.getActiveLinkNames(), false));
  EXPECT_TRUE(tesseract_common::isIdentical(base_solver.getStaticLinkNames(), comp_solver.getStaticLinkNames(), false));

  for (int i = 0; i < 10; ++i)
  {
    SceneState base_random_state = base_solver.getRandomState();
    SceneState comp_state_const = comp_solver.getState(base_random_state.joints);
    comp_solver.setState(base_random_state.joints);
    const SceneState& comp_state = comp_solver.getState();

    runCompareSceneStates(base_random_state, comp_state_const);
    runCompareSceneStates(base_random_state, comp_state);
  }
}

TEST(TesseractEnvironmentUnit, EnvApplyCommandsStateSolverCompareUnit)  // NOLINT
{
  // This is testing commands that modify the connectivity of scene graph
  // It checks that the state solver are updated correctly

  {  // Add new link no joint
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_n1");
    Commands commands{ std::make_shared<AddLinkCommand>(link_1) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Add new link with joint
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_n1");
    Joint joint_1("joint_link_n1");
    joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    joint_1.parent_link_name = "base_link";
    joint_1.child_link_name = "link_n1";
    joint_1.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, joint_1) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace link
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_1");

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, true) };
    EXPECT_TRUE(compare_env->applyCommands(commands));
    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();

    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }
  {  // Replace link and joint which is allowed
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_1");
    Joint joint_1("joint_a1");
    joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    joint_1.parent_link_name = "base_link";
    joint_1.child_link_name = "link_1";
    joint_1.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, joint_1, true) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace link and joint which is not allowed
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_1");
    Joint joint_1("joint_a1");
    joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    joint_1.parent_link_name = "base_link";
    joint_1.child_link_name = "link_1";
    joint_1.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, joint_1, false) };
    EXPECT_FALSE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint which exist but the link does not which should fail
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_2_does_not_exist");
    Joint joint_1("joint_a1");
    joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    joint_1.parent_link_name = "base_link";
    joint_1.child_link_name = "link_2_does_not_exist";
    joint_1.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, joint_1, true) };
    EXPECT_FALSE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace link and joint which is not allowed but they are not currently linked
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_2");
    Joint joint_1("joint_a1");
    joint_1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    joint_1.parent_link_name = "base_link";
    joint_1.child_link_name = "link_2";
    joint_1.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, joint_1, false) };
    EXPECT_FALSE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace link which is not allowed
    // Get the environment
    auto compare_env = getEnvironment();

    Link link_1("link_1");

    Commands commands{ std::make_shared<AddLinkCommand>(link_1, false) };
    EXPECT_FALSE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint but not move and same type
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a1 = compare_env->getJoint("joint_a1")->clone();
    new_joint_a1.parent_to_joint_origin_transform.translation()(0) = 1.25;

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a1) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint but not move with different type (Fixed)
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a1 = compare_env->getJoint("joint_a1")->clone();
    new_joint_a1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    new_joint_a1.type = JointType::FIXED;

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a1) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint but not move with different type (Continuous)
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a1 = compare_env->getJoint("joint_a1")->clone();
    new_joint_a1.parent_to_joint_origin_transform.translation()(0) = 1.25;
    new_joint_a1.type = JointType::CONTINUOUS;

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a1) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint and move with same type
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a3 = compare_env->getJoint("joint_a3")->clone();
    new_joint_a3.parent_link_name = "base_link";

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a3) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint and move with different type (Fixed)
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a3 = compare_env->getJoint("joint_a3")->clone();
    new_joint_a3.parent_link_name = "base_link";
    new_joint_a3.type = JointType::FIXED;

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a3) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Replace joint and move with different type (Prismatic)
    // Get the environment
    auto compare_env = getEnvironment();

    Joint new_joint_a3 = compare_env->getJoint("joint_a3")->clone();
    new_joint_a3.parent_link_name = "base_link";
    new_joint_a3.type = JointType::PRISMATIC;

    Commands commands{ std::make_shared<ReplaceJointCommand>(new_joint_a3) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // move link
    auto compare_env = getEnvironment();

    Joint new_joint_a3 = compare_env->getJoint("joint_a3")->clone();
    new_joint_a3.parent_link_name = "base_link";
    new_joint_a3.type = JointType::FIXED;

    Commands commands{ std::make_shared<MoveLinkCommand>(new_joint_a3) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // move joint
    auto compare_env = getEnvironment();

    Commands commands{ std::make_shared<MoveJointCommand>("joint_a3", "base_link") };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // remove link
    auto compare_env = getEnvironment();

    Commands commands{ std::make_shared<RemoveLinkCommand>("link_4") };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // remove link
    auto compare_env = getEnvironment();

    Commands commands{ std::make_shared<RemoveJointCommand>("joint_a3") };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Change Joint Origin
    auto compare_env = getEnvironment();

    Eigen::Isometry3d joint_origin = compare_env->getJoint("joint_a3")->parent_to_joint_origin_transform;
    joint_origin.translation() = joint_origin.translation() + Eigen::Vector3d(0, 0, 1);
    Commands commands{ std::make_shared<ChangeJointOriginCommand>("joint_a3", joint_origin) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Add SceneGraph case 1
    auto compare_env = getEnvironment();

    auto subgraph = getSubSceneGraph();

    Commands commands{ std::make_shared<AddSceneGraphCommand>(*subgraph) };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Add SceneGraph case 2
    auto compare_env = getEnvironment();

    auto subgraph = getSceneGraph();
    Commands commands{ std::make_shared<AddSceneGraphCommand>(*subgraph, "prefix_") };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }

  {  // Add SceneGraph case 3
    auto compare_env = getEnvironment();

    auto subgraph = getSceneGraph();
    Joint attach_joint("prefix_base_link_joint");
    attach_joint.parent_link_name = "tool0";
    attach_joint.child_link_name = "prefix_base_link";
    attach_joint.type = JointType::FIXED;

    Commands commands{ std::make_shared<AddSceneGraphCommand>(*subgraph, attach_joint, "prefix_") };
    EXPECT_TRUE(compare_env->applyCommands(commands));

    auto base_state_solver = std::make_unique<KDLStateSolver>(*compare_env->getSceneGraph());
    auto compare_state_solver = compare_env->getStateSolver();
    runCompareStateSolver(*base_state_solver, *compare_state_solver);
    runGetLinkTransformsTest(*compare_env);
  }
}

TEST(TesseractEnvironmentUnit, EnvMultithreadedApplyCommandsTest)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

#pragma omp parallel for num_threads(10) shared(env)
  for (long i = 0; i < 10; ++i)  // NOLINT
  {
    const int tn = omp_get_thread_num();
    CONSOLE_BRIDGE_logDebug("Thread (ID: %i): %i of %i", tn, i, 10);

    auto visual = std::make_shared<Visual>();
    visual->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);
    auto collision = std::make_shared<Collision>();
    collision->geometry = std::make_shared<tesseract_geometry::Box>(1, 1, 1);

    const std::string link_name1 = "link_n" + std::to_string(i);
    Link link_1(link_name1);
    link_1.visual.push_back(visual);
    link_1.collision.push_back(collision);

    for (int idx = 0; idx < 10; idx++)
    {
      // addLink
      EXPECT_TRUE(env->applyCommand(std::make_shared<AddLinkCommand>(link_1)));

      // removeLink
      EXPECT_TRUE(env->applyCommand(std::make_shared<RemoveLinkCommand>(link_1.getName())));
    }
  }
}

TEST(TesseractEnvironmentUnit, EnvClone)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  // Modifying collision margin from default
  tesseract_common::CollisionMarginData collision_margin_data(0.1);
  collision_margin_data.setPairCollisionMargin("link_1", "link_2", 0.1);
  tesseract_common::CollisionMarginOverrideType overrid_type = tesseract_common::CollisionMarginOverrideType::REPLACE;

  auto cmd = std::make_shared<ChangeCollisionMarginsCommand>(collision_margin_data, overrid_type);
  env->applyCommand(cmd);

  // Clone the environment
  auto clone = env->clone();

  // Check the basics
  EXPECT_EQ(clone->getName(), env->getName());
  EXPECT_EQ(clone->getRevision(), env->getRevision());

  // Check that all links got cloned
  std::vector<std::string> link_names = env->getLinkNames();
  std::vector<std::string> clone_link_names = clone->getLinkNames();
  for (const auto& name : link_names)
    EXPECT_TRUE(std::find(clone_link_names.begin(), clone_link_names.end(), name) != clone_link_names.end());

  // Check that all joints got cloned
  std::vector<std::string> joint_names = env->getJointNames();
  std::vector<std::string> clone_joint_names = clone->getJointNames();
  for (const auto& name : joint_names)
    EXPECT_TRUE(std::find(clone_joint_names.begin(), clone_joint_names.end(), name) != clone_joint_names.end());

  // Check that the command history is preserved
  auto history = env->getCommandHistory();
  auto clone_history = clone->getCommandHistory();
  ASSERT_EQ(history.size(), clone_history.size());
  for (std::size_t i = 0; i < history.size(); i++)
  {
    EXPECT_EQ(history[i]->getType(), clone_history[i]->getType());
  }

  {
    // Check active links
    std::vector<std::string> active_link_names = env->getActiveLinkNames();
    std::vector<std::string> clone_active_link_names = clone->getActiveLinkNames();
    EXPECT_EQ(active_link_names.size(), clone_active_link_names.size());
    for (const auto& name : active_link_names)
      EXPECT_TRUE(std::find(clone_active_link_names.begin(), clone_active_link_names.end(), name) !=
                  clone_active_link_names.end());

    // Check static links
    std::vector<std::string> static_link_names = env->getStaticLinkNames();
    std::vector<std::string> clone_static_link_names = clone->getStaticLinkNames();
    EXPECT_EQ(static_link_names.size(), clone_static_link_names.size());
    for (const auto& name : static_link_names)
      EXPECT_TRUE(std::find(clone_static_link_names.begin(), clone_static_link_names.end(), name) !=
                  clone_static_link_names.end());
  }
  {
    // Check active links with joint names
    std::vector<std::string> active_link_names = env->getActiveLinkNames(env->getActiveJointNames());
    EXPECT_TRUE(tesseract_common::isIdentical(active_link_names, env->getActiveLinkNames(), false));

    std::vector<std::string> clone_active_link_names = clone->getActiveLinkNames(env->getActiveJointNames());
    EXPECT_TRUE(tesseract_common::isIdentical(clone_active_link_names, clone->getActiveLinkNames(), false));

    EXPECT_EQ(active_link_names.size(), clone_active_link_names.size());
    for (const auto& name : active_link_names)
      EXPECT_TRUE(std::find(clone_active_link_names.begin(), clone_active_link_names.end(), name) !=
                  clone_active_link_names.end());

    // Check static links with joint names
    std::vector<std::string> static_link_names = env->getStaticLinkNames(env->getActiveJointNames());
    EXPECT_TRUE(tesseract_common::isIdentical(static_link_names, env->getStaticLinkNames(), false));

    std::vector<std::string> clone_static_link_names = clone->getStaticLinkNames(env->getActiveJointNames());
    EXPECT_TRUE(tesseract_common::isIdentical(clone_static_link_names, clone->getStaticLinkNames(), false));

    EXPECT_EQ(static_link_names.size(), clone_static_link_names.size());
    for (const auto& name : static_link_names)
      EXPECT_TRUE(std::find(clone_static_link_names.begin(), clone_static_link_names.end(), name) !=
                  clone_static_link_names.end());
  }

  // Check active joints
  std::vector<std::string> active_joint_names = env->getActiveJointNames();
  std::vector<std::string> clone_active_joint_names = clone->getActiveJointNames();
  for (const auto& name : active_joint_names)
    EXPECT_TRUE(std::find(clone_active_joint_names.begin(), clone_active_joint_names.end(), name) !=
                clone_active_joint_names.end());

  // Check that the state is preserved
  Eigen::VectorXd joint_vals = env->getState().getJointValues(active_joint_names);
  Eigen::VectorXd clone_joint_vals = clone->getState().getJointValues(active_joint_names);
  EXPECT_TRUE(joint_vals.isApprox(clone_joint_vals));

  // Check that the collision margin data is preserved
  EXPECT_EQ(env->getCollisionMarginData(), clone->getCollisionMarginData());
}

TEST(TesseractEnvironmentUnit, EnvSetState)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  //////////////////////////////////////////////////////////////////
  // Test forward kinematics when tip link is the base of the chain
  //////////////////////////////////////////////////////////////////
  Eigen::Isometry3d pose;
  std::vector<double> std_jvals = { 0, 0, 0, 0, 0, 0, 0 };
  std::unordered_map<std::string, double> map_jvals;
  Eigen::VectorXd jvals;
  jvals.resize(7);
  jvals.setZero();

  // Check current joint values
  Eigen::VectorXd cjv = env->getCurrentJointValues();
  EXPECT_TRUE(cjv.isApprox(jvals, 1e-6));

  std::vector<std::string> joint_names = { "base_link-base", "joint_a1", "joint_a2", "joint_a3",      "joint_a4",
                                           "joint_a5",       "joint_a6", "joint_a7", "joint_a7-tool0" };
  std::vector<std::string> link_names = { "base",   "base_link", "link_1", "link_2", "link_3",
                                          "link_4", "link_5",    "link_6", "link_7", "tool0" };
  std::vector<std::string> active_joint_names = { "joint_a1", "joint_a2", "joint_a3", "joint_a4",
                                                  "joint_a5", "joint_a6", "joint_a7" };

  for (const auto& jn : active_joint_names)
    map_jvals[jn] = 0;

  std::vector<SceneState> states;

  env->setState(active_joint_names, jvals);
  states.push_back(env->getState());

  // Set the environment to a random state
  env->setState(env->getStateSolver()->getRandomState().joints);
  cjv = env->getCurrentJointValues(active_joint_names);
  EXPECT_FALSE(cjv.isApprox(jvals, 1e-6));

  states.push_back(env->getState(active_joint_names, jvals));
  states.push_back(env->getState(map_jvals));

  env->setState(env->getStateSolver()->getRandomState().joints);
  cjv = env->getCurrentJointValues(active_joint_names);
  EXPECT_FALSE(cjv.isApprox(jvals, 1e-6));
  env->setState(active_joint_names, jvals);
  cjv = env->getCurrentJointValues();
  EXPECT_TRUE(cjv.isApprox(jvals, 1e-6));
  states.push_back(env->getState());

  env->setState(env->getStateSolver()->getRandomState().joints);
  cjv = env->getCurrentJointValues();
  EXPECT_FALSE(cjv.isApprox(jvals, 1e-6));
  env->setState(active_joint_names, jvals);
  cjv = env->getCurrentJointValues(active_joint_names);
  EXPECT_TRUE(cjv.isApprox(jvals, 1e-6));
  states.push_back(env->getState());

  env->setState(env->getStateSolver()->getRandomState().joints);
  cjv = env->getCurrentJointValues(active_joint_names);
  EXPECT_FALSE(cjv.isApprox(jvals, 1e-6));
  env->setState(map_jvals);
  cjv = env->getCurrentJointValues();
  EXPECT_TRUE(cjv.isApprox(jvals, 1e-6));
  states.push_back(env->getState());

  for (auto& current_state : states)
  {
    // Check joints and links size
    EXPECT_EQ(current_state.joint_transforms.size(), 9);
    EXPECT_EQ(current_state.link_transforms.size(), 10);
    EXPECT_EQ(current_state.joints.size(), 7);

    // Check joints and links names
    for (const auto& joint_name : joint_names)
    {
      EXPECT_TRUE(current_state.joint_transforms.find(joint_name) != current_state.joint_transforms.end());
    }

    for (const auto& link_name : link_names)
    {
      EXPECT_TRUE(current_state.link_transforms.find(link_name) != current_state.link_transforms.end());
    }

    for (const auto& joint_name : active_joint_names)
    {
      EXPECT_TRUE(current_state.joints.find(joint_name) != current_state.joints.end());
    }

    EXPECT_TRUE(current_state.link_transforms.at("base_link").isApprox(Eigen::Isometry3d::Identity()));

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity();
      EXPECT_TRUE(current_state.link_transforms.at("link_1").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(-0.00043624, 0, 0.36);
      EXPECT_TRUE(current_state.link_transforms.at("link_2").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(-0.00043624, 0, 0.36);
      EXPECT_TRUE(current_state.link_transforms.at("link_3").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(0, 0, 0.36 + 0.42);
      EXPECT_TRUE(current_state.link_transforms.at("link_4").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(0, 0, 0.36 + 0.42);
      EXPECT_TRUE(current_state.link_transforms.at("link_5").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(0, 0, 0.36 + 0.42 + 0.4);
      EXPECT_TRUE(current_state.link_transforms.at("link_6").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(0, 0, 0.36 + 0.42 + 0.4);
      EXPECT_TRUE(current_state.link_transforms.at("link_7").isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity() * Eigen::Translation3d(0, 0, 1.306);
      EXPECT_TRUE(current_state.link_transforms.at("tool0").isApprox(result));
    }
  }
}

TEST(TesseractEnvironmentUnit, EnvSetState2)  // NOLINT
{
  // Get the environment
  auto env = getEnvironment();

  //////////////////////////////////////////////////////////////////
  // Test forward kinematics when tip link is the base of the chain
  //////////////////////////////////////////////////////////////////
  Eigen::Isometry3d pose;
  Eigen::VectorXd jvals;
  jvals.resize(7);
  jvals.setZero();
  std::vector<std::string> joint_names = { "base_link-base", "joint_a1", "joint_a2", "joint_a3",      "joint_a4",
                                           "joint_a5",       "joint_a6", "joint_a7", "joint_a7-tool0" };
  std::vector<std::string> link_names = { "base",   "base_link", "link_1", "link_2", "link_3",
                                          "link_4", "link_5",    "link_6", "link_7", "tool0" };
  std::vector<std::string> active_joint_names = { "joint_a1", "joint_a2", "joint_a3", "joint_a4",
                                                  "joint_a5", "joint_a6", "joint_a7" };

  Eigen::Vector3d axis(0, 1, 0);
  jvals(1) = M_PI_2;
  std::vector<SceneState> states;

  env->setState(active_joint_names, jvals);
  states.push_back(env->getState());
  states.push_back(env->getState(active_joint_names, jvals));

  for (auto& current_state : states)
  {
    // Check joints and links size
    EXPECT_EQ(current_state.joint_transforms.size(), 9);
    EXPECT_EQ(current_state.link_transforms.size(), 10);
    EXPECT_EQ(current_state.joints.size(), 7);

    // Check joints and links names
    for (const auto& joint_name : joint_names)
    {
      EXPECT_TRUE(current_state.joint_transforms.find(joint_name) != current_state.joint_transforms.end());
    }

    for (const auto& link_name : link_names)
    {
      EXPECT_TRUE(current_state.link_transforms.find(link_name) != current_state.link_transforms.end());
    }

    int cnt = 0;
    for (const auto& joint_name : active_joint_names)
    {
      EXPECT_TRUE(current_state.joints.find(joint_name) != current_state.joints.end());
      EXPECT_NEAR(current_state.joints[joint_name], jvals(cnt++), 1e-5);
    }

    EXPECT_TRUE(current_state.link_transforms["base_link"].isApprox(Eigen::Isometry3d::Identity()));
    EXPECT_TRUE(current_state.link_transforms["base"].isApprox(Eigen::Isometry3d::Identity()));

    {
      Eigen::Isometry3d result = Eigen::Isometry3d::Identity();
      EXPECT_TRUE(current_state.link_transforms["link_1"].isApprox(result));
    }

    {
      Eigen::Isometry3d result = Eigen::Translation3d(-0.00043624, 0, 0.36) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_2"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result = Eigen::Translation3d(-0.00043624, 0, 0.36) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_3"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result =
          Eigen::Translation3d(0.42 - 0.00043624, 0, 0.36 - 0.00043624) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_4"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result =
          Eigen::Translation3d(0.42 - 0.00043624, 0, 0.36 - 0.00043624) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_5"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result =
          Eigen::Translation3d(0.42 + 0.4 - 0.00043624, 0, 0.36 - 0.00043624) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_6"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result =
          Eigen::Translation3d(0.42 + 0.4 - 0.00043624, 0, 0.36 - 0.00043624) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["link_7"].isApprox(result, 1e-4));
    }

    {
      Eigen::Isometry3d result =
          Eigen::Translation3d(1.306 - 0.36 - 0.00043624, 0, 0.36 - 0.00043624) * Eigen::AngleAxisd(M_PI_2, axis);
      EXPECT_TRUE(current_state.link_transforms["tool0"].isApprox(result, 1e-4));
    }
  }
}

// TEST(TesseractEnvironmentUnit, EnvFindTCPUnit)  // NOLINT
//{
//  runFindTCPTest();
//}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
