/**
 * @file convex_mesh.cpp
 * @brief Parse convex_mesh from xml string
 *
 * @author Levi Armstrong
 * @date September 1, 2019
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2019, Southwest Research Institute
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
#include <stdexcept>
#include <tesseract_common/utils.h>
#include <tinyxml2.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract_urdf/convex_mesh.h>
#include <tesseract_urdf/mesh_writer.h>
#include <tesseract_scene_graph/utils.h>
#include <tesseract_geometry/mesh_parser.h>
#include <tesseract_geometry/impl/mesh.h>
#include <tesseract_collision/core/common.h>
#include <tesseract_common/resource_locator.h>

std::vector<tesseract_geometry::ConvexMesh::Ptr>
tesseract_urdf::parseConvexMesh(const tinyxml2::XMLElement* xml_element,
                                const tesseract_common::ResourceLocator& locator,
                                bool visual,
                                int /*version*/)
{
  std::vector<tesseract_geometry::ConvexMesh::Ptr> meshes;

  std::string filename;
  if (tesseract_common::QueryStringAttribute(xml_element, "filename", filename) != tinyxml2::XML_SUCCESS)
    std::throw_with_nested(std::runtime_error("ConvexMesh: Missing or failed parsing attribute 'filename'!"));

  std::string scale_string;
  Eigen::Vector3d scale(1, 1, 1);
  if (tesseract_common::QueryStringAttribute(xml_element, "scale", scale_string) == tinyxml2::XML_SUCCESS)
  {
    std::vector<std::string> tokens;
    boost::split(tokens, scale_string, boost::is_any_of(" "), boost::token_compress_on);
    if (tokens.size() != 3 || !tesseract_common::isNumeric(tokens))
      std::throw_with_nested(std::runtime_error("ConvexMesh: Failed parsing attribute 'scale'!"));

    double sx{ 0 }, sy{ 0 }, sz{ 0 };
    // No need to check return values because the tokens are verified above
    tesseract_common::toNumeric<double>(tokens[0], sx);
    tesseract_common::toNumeric<double>(tokens[1], sy);
    tesseract_common::toNumeric<double>(tokens[2], sz);

    if (!(sx > 0))
      std::throw_with_nested(std::runtime_error("ConvexMesh: Scale x must be greater than zero!"));

    if (!(sy > 0))
      std::throw_with_nested(std::runtime_error("ConvexMesh: Scale y must be greater than zero!"));

    if (!(sz > 0))
      std::throw_with_nested(std::runtime_error("ConvexMesh: Scale z must be greater than zero!"));

    scale = Eigen::Vector3d(sx, sy, sz);
  }

  bool convert = false;
  xml_element->QueryBoolAttribute("convert", &convert);

  if (visual)
    meshes = tesseract_geometry::createMeshFromResource<tesseract_geometry::ConvexMesh>(
        locator.locateResource(filename), scale, true, true, true, true, true);
  else
  {
    if (!convert)
    {
      meshes = tesseract_geometry::createMeshFromResource<tesseract_geometry::ConvexMesh>(
          locator.locateResource(filename), scale, false, false);
    }
    else
    {
      std::vector<tesseract_geometry::Mesh::Ptr> temp_meshes =
          tesseract_geometry::createMeshFromResource<tesseract_geometry::Mesh>(
              locator.locateResource(filename), scale, true, false);
      for (auto& mesh : temp_meshes)
        meshes.push_back(tesseract_collision::makeConvexMesh(*mesh));
    }
  }

  if (meshes.empty())
    std::throw_with_nested(std::runtime_error("ConvexMesh: Error importing meshes from filename: '" + filename + "'!"));

  return meshes;
}

tinyxml2::XMLElement* tesseract_urdf::writeConvexMesh(const std::shared_ptr<const tesseract_geometry::ConvexMesh>& mesh,
                                                      tinyxml2::XMLDocument& doc,
                                                      const std::string& directory,
                                                      const std::string& filename)
{
  if (mesh == nullptr)
    std::throw_with_nested(std::runtime_error("Mesh is nullptr and cannot be converted to XML"));
  tinyxml2::XMLElement* xml_element = doc.NewElement("convex_mesh");

  try
  {
    writeMeshToFile(mesh, directory + filename);
  }
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("Failed to write convex mesh to file: " + directory + filename));
  }

  xml_element->SetAttribute("filename", filename.c_str());

  std::string scale_string = std::to_string(mesh->getScale().x()) + " " + std::to_string(mesh->getScale().y()) + " " +
                             std::to_string(mesh->getScale().z());

  xml_element->SetAttribute("scale", scale_string.c_str());

  xml_element->SetAttribute("convert", false);

  return xml_element;
}
