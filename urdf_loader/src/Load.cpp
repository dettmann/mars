/*
 *  Copyright 2011, 2012, 2014, DFKI GmbH Robotics Innovation Center
 *
 *  This file is part of the MARS simulation framework.
 *
 *  MARS is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation, either version 3
 *  of the License, or (at your option) any later version.
 *
 *  MARS is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with MARS.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "Load.h"
#include "zipit.h"

#include <QtXml>
#include <QDomNodeList>

#include <mars/data_broker/DataBrokerInterface.h>

#include <mars/interfaces/sim/SimulatorInterface.h>
#include <mars/interfaces/sim/NodeManagerInterface.h>
#include <mars/interfaces/sim/JointManagerInterface.h>
#include <mars/interfaces/sim/SensorManagerInterface.h>
#include <mars/interfaces/sim/MotorManagerInterface.h>
#include <mars/interfaces/sim/ControllerManagerInterface.h>
#include <mars/interfaces/graphics/GraphicsManagerInterface.h>

#include <mars/interfaces/sim/EntityManagerInterface.h>
#include <mars/interfaces/sim/LoadSceneInterface.h>
#include <mars/utils/misc.h>
#include <mars/utils/mathUtils.h>

//#define DEBUG_PARSE_SENSOR 1

/*
 * remarks:
 *
 *   - we need some special handling because the representation
 *     in MARS is different then in URDF with is marked in the source with:
 *     ** special case handling **
 *
 *   - if we load and save a file we might lose names of
 *     collision and visual objects
 *
 */

namespace mars {
  namespace urdf_loader {

    using namespace std;
    using namespace interfaces;
    using namespace utils;

    Load::Load(std::string fileName, ControlCenter *c, std::string tmpPath_,
               const std::string &robotname) :
      mFileName(fileName), mRobotName(robotname),
      control(c), tmpPath(tmpPath_) {

      mFileSuffix = getFilenameSuffix(mFileName);
    }

    unsigned int Load::load() {

      if (!prepareLoad())
        return 0;
      if (!parseScene())
        return 0;
      return loadScene();
    }

    unsigned int Load::prepareLoad() {
      std::string filename = mFileName;

      nextGroupID = control->nodes->getMaxGroupID()+1;
      nextNodeID = 1;
      nextJointID = 1;
      nextMaterialID = 1;

      if (mRobotName != "") {
        control->entities->addEntity(mRobotName);
      }

      LOG_INFO("urdf_loader: prepare loading");

      // need to unzip into a temporary directory
      if (mFileSuffix == ".zsmurf") {
        if (unzip(tmpPath, mFileName) == 0) {
          return 0;
        }
        mFileSuffix = ".smurf";
      } else {
        // can parse file without unzipping
        tmpPath = getPathOfFile(mFileName);
      }

      removeFilenamePrefix(&filename);
      removeFilenameSuffix(&filename);

      mapIndex = control->loadCenter->getMappedSceneByName(mFileName);
      if (mapIndex == 0) {
        control->loadCenter->setMappedSceneName(mFileName);
        mapIndex = control->loadCenter->getMappedSceneByName(mFileName);
      }
      sceneFilename = tmpPath + filename + mFileSuffix;
      return 1;
    }

    unsigned int Load::unzip(const std::string& destinationDir,
                             const std::string& zipFilename) {
      if (!createDirectory(destinationDir))
        return 0;

      Zipit myZipFile(zipFilename);
      LOG_INFO("Load: unsmurfing zipped SMURF: %s", zipFilename.c_str());

      if (!myZipFile.unpackWholeZipTo(destinationDir))
        return 0;

      return 1;
    }

    void Load::handleInertial(ConfigMap *map,
                              const boost::shared_ptr<urdf::Link> &link) {
      if(link->inertial) {
        (*map)["mass"] = link->inertial->mass;
        // handle inertial
        (*map)["i00"] = link->inertial->ixx;
        (*map)["i01"] = link->inertial->ixy;
        (*map)["i02"] = link->inertial->ixz;
        (*map)["i10"] = link->inertial->ixy;
        (*map)["i11"] = link->inertial->iyy;
        (*map)["i12"] = link->inertial->iyz;
        (*map)["i20"] = link->inertial->ixz;
        (*map)["i21"] = link->inertial->iyz;
        (*map)["i22"] = link->inertial->izz;
        (*map)["inertia"] = true;
      }
      else {
        (*map)["inertia"] = false;
      }
    }

    void Load::calculatePose(ConfigMap *map,
                             const boost::shared_ptr<urdf::Link> &link) {
      urdf::Pose jointPose, parentInertialPose, inertialPose;
      urdf::Pose goalPose;

      if(link->parent_joint) {
        jointPose = link->parent_joint->parent_to_joint_origin_transform;
        if(link->getParent()->inertial) {
          parentInertialPose = link->getParent()->inertial->origin;
        }
        unsigned long parentID = nodeIDMap[link->getParent()->name];
        (*map)["relativeid"] = parentID;
      }
      else {
        (*map)["relativeid"] = 0ul;
      }

      if(link->inertial) {
        inertialPose = link->inertial->origin;
      }
      /** special case handling **/
      else if(link->collision) {
        // if we don't have an inertial but a collision (standard for MARS)
        // we place the node at the position of the collision
        inertialPose = link->collision->origin;
      }
      // we need the inverse of parentInertialPose.position
      parentInertialPose.position.x *= -1;
      parentInertialPose.position.y *= -1;
      parentInertialPose.position.z *= -1;

      goalPose.position = jointPose.position + parentInertialPose.position;
      goalPose.position = (goalPose.position +
                           jointPose.rotation * inertialPose.position);
      goalPose.position = (parentInertialPose.rotation.GetInverse()*
                           goalPose.position);
      goalPose.rotation = (parentInertialPose.rotation.GetInverse()*
                           inertialPose.rotation*jointPose.rotation);

      Vector v(goalPose.position.x, goalPose.position.y, goalPose.position.z);
      vectorToConfigItem(&(*map)["position"][0], &v);
      Quaternion q = quaternionFromMembers(goalPose.rotation);
      quaternionToConfigItem(&(*map)["rotation"][0], &q);
    }

    void Load::handleVisual(ConfigMap *map,
                            const boost::shared_ptr<urdf::Visual> &visual) {
      boost::shared_ptr<urdf::Geometry> tmpGeometry = visual->geometry;
      Vector size(0.0, 0.0, 0.0);
      Vector scale(1.0, 1.0, 1.0);
      urdf::Vector3 v;
      (*map)["filename"] = "PRIMITIVE";
      switch (tmpGeometry->type) {
      case urdf::Geometry::SPHERE:
        size.x() = ((urdf::Sphere*)tmpGeometry.get())->radius;
        (*map)["origname"] ="sphere";
        break;
      case urdf::Geometry::BOX:
        v = ((urdf::Box*)tmpGeometry.get())->dim;
        size = Vector(v.x, v.y, v.z);
        (*map)["origname"] = "box";
        break;
      case urdf::Geometry::CYLINDER:
        size.x() = ((urdf::Cylinder*)tmpGeometry.get())->radius;
        size.y() = ((urdf::Cylinder*)tmpGeometry.get())->length;
        (*map)["origname"] = "cylinder";
        break;
      case urdf::Geometry::MESH:
        v = ((urdf::Mesh*)tmpGeometry.get())->scale;
        scale = Vector(v.x, v.y, v.z);
        (*map)["filename"] = ((urdf::Mesh*)tmpGeometry.get())->filename;
        (*map)["origname"] = "";
        break;
      default:
        break;
      }
      vectorToConfigItem(&(*map)["visualsize"][0], &size);
      vectorToConfigItem(&(*map)["visualscale"][0], &scale);
      (*map)["materialName"] = visual->material_name;
    }

    void Load::convertPose(const urdf::Pose &pose,
                           const boost::shared_ptr<urdf::Link> &link,
                           Vector *v, Quaternion *q) {
      urdf::Pose toPose;

      if(link->inertial) {
        toPose = link->inertial->origin;
      }
      /** special case handling **/
      else if(link->collision) {
        // if we don't have an inertial but a collision (standard for MARS)
        // we place the node at the position of the collision
        toPose = link->collision->origin;
      }

      convertPose(pose, toPose, v, q);
    }

    void Load::convertPose(const urdf::Pose &pose,
                           const urdf::Pose &toPose,
                           Vector *v, Quaternion *q) {
      urdf::Pose pose_ = pose;
      urdf::Pose toPose_ = toPose;
      urdf::Vector3 p;
      urdf::Rotation r;

      // we need the inverse of toPose_.position
      toPose_.position.x *= -1;
      toPose_.position.y *= -1;
      toPose_.position.z *= -1;
      p = pose_.position + toPose_.position;
      p = toPose_.rotation * p;
      r = (toPose_.rotation.GetInverse() *
           pose_.rotation);
      *v = Vector(p.x, p.y, p.z);
      *q = quaternionFromMembers(r);
    }

    bool Load::isEqualPos(const urdf::Pose &p1, const urdf::Pose p2) {
      bool equal = true;
      double epsilon = 0.00000000001;
      if(fabs(p1.position.x - p2.position.x) > epsilon) equal = false;
      if(fabs(p1.position.y - p2.position.y) > epsilon) equal = false;
      if(fabs(p1.position.z - p2.position.z) > epsilon) equal = false;
      if(fabs(p1.rotation.x - p2.rotation.x) > epsilon) equal = false;
      if(fabs(p1.rotation.y - p2.rotation.y) > epsilon) equal = false;
      if(fabs(p1.rotation.z - p2.rotation.z) > epsilon) equal = false;
      if(fabs(p1.rotation.w - p2.rotation.w) > epsilon) equal = false;
      return equal;
    }

    void Load::handleCollision(ConfigMap *map,
                               const boost::shared_ptr<urdf::Collision> &c) {
      boost::shared_ptr<urdf::Geometry> tmpGeometry = c->geometry;
      Vector size(0.0, 0.0, 0.0);
      Vector scale(1.0, 1.0, 1.0);
      urdf::Vector3 v;
      switch (tmpGeometry->type) {
      case urdf::Geometry::SPHERE:
        size.x() = ((urdf::Sphere*)tmpGeometry.get())->radius;
        (*map)["physicmode"] ="sphere";
        break;
      case urdf::Geometry::BOX:
        v = ((urdf::Box*)tmpGeometry.get())->dim;
        size = Vector(v.x, v.y, v.z);
        (*map)["physicmode"] = "box";
        break;
      case urdf::Geometry::CYLINDER:
        size.x() = ((urdf::Cylinder*)tmpGeometry.get())->radius;
        size.y() = ((urdf::Cylinder*)tmpGeometry.get())->length;
        (*map)["physicmode"] = "cylinder";
        break;
      case urdf::Geometry::MESH:
        v = ((urdf::Mesh*)tmpGeometry.get())->scale;
        scale = Vector(v.x, v.y, v.z);
        (*map)["filename"] = ((urdf::Mesh*)tmpGeometry.get())->filename;
        (*map)["origname"] = "";
        (*map)["physicmode"] = "mesh";
        break;
      default:
        break;
      }
      vectorToConfigItem(&(*map)["extend"][0], &size);
      vectorToConfigItem(&(*map)["scale"][0], &scale);
      // todo: we need to deal correctly with the scale and size in MARS
      //       if we have a mesh here, as a first hack we use the scale as size
      if(tmpGeometry->type == urdf::Geometry::MESH) {
        vectorToConfigItem(&(*map)["extend"][0], &scale);
      }
    }

    void Load::createFakeMaterial() {
      ConfigMap config;

      config["id"] = nextMaterialID++;
      config["name"] = "_fakeMaterial";
      config["exists"] = true;
      config["diffuseFront"][0]["a"] = 1.0;
      config["diffuseFront"][0]["r"] = 1.0;
      config["diffuseFront"][0]["g"] = 0.0;
      config["diffuseFront"][0]["b"] = 0.0;
      config["texturename"] = "";
      config["cullMask"] = 0;
      debugMap["materials"] += config;
      materialList.push_back(config);
    }

    void Load::createFakeVisual(ConfigMap *map) {
      Vector size(0.01, 0.01, 0.01);
      Vector scale(1.0, 1.0, 1.0);
      (*map)["filename"] = "PRIMITIVE";
      (*map)["origname"] = "box";
      (*map)["materialName"] = "_fakeMaterial";
      vectorToConfigItem(&(*map)["visualsize"][0], &size);
      vectorToConfigItem(&(*map)["visualscale"][0], &scale);
    }

    void Load::createFakeCollision(ConfigMap *map) {
      Vector size(0.01, 0.01, 0.01);
      (*map)["physicmode"] = "box";
      (*map)["coll_bitmask"] = 0;
      vectorToConfigItem(&(*map)["extend"][0], &size);
    }

    void Load::handleKinematics(boost::shared_ptr<urdf::Link> link) {
      ConfigMap config;
      // holds the index of the next visual object to load
      int visualArrayIndex = 0;
      // holds the index of the next collision object to load
      int collisionArrayIndex = 0;
      bool loadVisual = link->visual;
      bool loadCollision = link->collision;
      Vector v;
      Quaternion q;

      config["name"] = link->name;
      config["index"] = nextNodeID++;

      nodeIDMap[link->name] = nextNodeID-1;

      // todo: if we don't have any joints connected we need some more
      //       special handling and change the handling below
      //       config["movable"] ?!?

      // we do most of the special case handling here:
      { /** special case handling **/
        bool needGroupID = false;
        if(link->visual_array.size() > 1 &&
           link->collision_array.size() > 1) {
          needGroupID = true;
        }
        if(link->collision && link->inertial) {
          if(!isEqualPos(link->collision->origin, link->inertial->origin)) {
            loadCollision = false;
            needGroupID = true;
          }
        }
        if(link->visual && link->collision) {
          if(loadCollision && link->collision->geometry->type == urdf::Geometry::MESH) {
            if(link->visual->geometry->type != urdf::Geometry::MESH) {
              loadVisual = false;
              needGroupID = true;
            }
            else {
              if(((urdf::Mesh*)link->collision->geometry.get())->filename !=
                 ((urdf::Mesh*)link->visual->geometry.get())->filename) {
                loadVisual = false;
                needGroupID = true;
              }
            }
          }
        }
        if(needGroupID) {
          // we need to group mars nodes
          config["groupid"] = nextGroupID++;
        }
        else {
          config["groupid"] = 0;
        }
      }

      // we always handle the inertial
      handleInertial(&config, link);

      // calculates the pose including all case handling
      calculatePose(&config, link);

      if(loadVisual) {
        handleVisual(&config, link->visual);
        // caculate visual position offset
        convertPose(link->visual->origin, link, &v, &q);
        vectorToConfigItem(&config["visualposition"][0], &v);
        quaternionToConfigItem(&config["visualrotation"][0], &q);
        // the first visual object is loaded
        visualArrayIndex = 1;
      }
      else {
        // we need a fake visual for the node
        createFakeVisual(&config);
      }

      if(loadCollision) {
        handleCollision(&config, link->collision);
        // the first visual object is loaded
        collisionArrayIndex = 1;
      }
      else {
        createFakeCollision(&config);
      }

      debugMap["links"] += config;
      nodeList.push_back(config);

      // now we have all information for the main node and can create additional
      // nodes for the collision and visual array
      while(collisionArrayIndex < link->collision_array.size()) {
        ConfigMap childNode;
        boost::shared_ptr<urdf::Collision> collision;
        boost::shared_ptr<urdf::Visual> visual;
        collision = link->collision_array[collisionArrayIndex];
        if(visualArrayIndex < link->visual_array.size()) {
          visual = link->visual_array[visualArrayIndex];
          // check wether we can load visual and collision together
          /** special case handling **/
          if(collision->geometry->type == urdf::Geometry::MESH) {
            if(visual->geometry->type != urdf::Geometry::MESH) {
              visual.reset();
            }
            else if(((urdf::Mesh*)collision->geometry.get())->filename !=
                    ((urdf::Mesh*)visual->geometry.get())->filename) {
              visual.reset();
            }
          }
        }

        childNode["index"] = nextNodeID++;
        childNode["relativeid"] = config["index"];
        if(collision->name.empty()) {
          childNode["name"] = ((std::string)config["name"][0])+"_child";
        }
        else {
          childNode["name"] = collision->name;
        }
        childNode["groupid"] = config["groupid"];
        // we add a collision node without mass
        childNode["mass"] = 0.0;
        childNode["density"] = 0.0;

        handleCollision(&childNode, collision);
        convertPose(collision->origin, link, &v, &q);
        vectorToConfigItem(&childNode["position"][0], &v);
        quaternionToConfigItem(&childNode["rotation"][0], &q);
        urdf::Pose p1;
        p1.position = urdf::Vector3(v.x(), v.y(), v.z());
        p1.rotation = urdf::Rotation(q.x(), q.y(), q.z(), q.w());
        collisionArrayIndex++;

        if(visual) {
          handleVisual(&childNode, visual);
          // convert the pose into the same coordinate system like as the node
          convertPose(visual->origin, link, &v, &q);
          urdf::Pose p2;
          p2.position = urdf::Vector3(v.x(), v.y(), v.z());
          p2.rotation = urdf::Rotation(q.x(), q.y(), q.z(), q.w());
          // then create the relative from node pose to visual pose
          convertPose(p2, p1, &v, &q);
          vectorToConfigItem(&childNode["visualposition"][0], &v);
          quaternionToConfigItem(&childNode["visualrotation"][0], &q);
          visualArrayIndex++;
        }
        else {
          createFakeVisual(&childNode);
        }
        debugMap["childNodes"] += childNode;
        nodeList.push_back(childNode);
      }

      while(visualArrayIndex < link->visual_array.size()) {
        ConfigMap childNode;
        boost::shared_ptr<urdf::Visual> visual;
        visual = link->visual_array[visualArrayIndex];

        childNode["index"] = nextNodeID++;
        childNode["relativeid"] = config["index"];
        if(visual->name.empty()) {
          childNode["name"] = ((std::string)config["name"][0])+"_child";
        }
        else {
          childNode["name"] = visual->name;
        }
        childNode["groupid"] = config["groupid"];
        childNode["noPhysical"] = true;
        childNode["mass"] = 0.0;
        childNode["density"] = 0.0;

        handleVisual(&childNode, visual);
        // todo: change NodeData.cpp not to need this:
        if(visual->geometry->type != urdf::Geometry::MESH) {
          childNode["physicmode"] = childNode["origname"];
        }

        // currently we need to set the extend because MARS uses it
        // also for primitiv visuals
        childNode["extend"] = childNode["visualsize"];
        convertPose(visual->origin, link, &v, &q);
        vectorToConfigItem(&childNode["position"][0], &v);
        quaternionToConfigItem(&childNode["rotation"][0], &q);
        visualArrayIndex++;
        debugMap["childNodes"] += childNode;
        nodeList.push_back(childNode);
      }

      // todo:  complete handle joint information
      if(link->parent_joint) {
        unsigned long id;
        ConfigMap joint;
        joint["name"] = link->parent_joint->name;
        joint["index"] = nextJointID++;
        joint["nodeindex1"] = nodeIDMap[link->parent_joint->parent_link_name];
        joint["nodeindex2"] = nodeIDMap[link->parent_joint->child_link_name];
        joint["anchorpos"] = 2;
        if(link->parent_joint->type == urdf::Joint::REVOLUTE) {
          joint["type"] = "hinge";
        }
        else if(link->parent_joint->type == urdf::Joint::PRISMATIC) {
          joint["type"] = "slider";
        }
        else if(link->parent_joint->type == urdf::Joint::FIXED) {
          joint["type"] = "fixed";
        }
        else {
          // we don't support the type yet and use a fixed joint
          joint["type"] = "fixed";
        }
        v = Vector(link->parent_joint->axis.x,
                   link->parent_joint->axis.y,
                   link->parent_joint->axis.z);
        vectorToConfigItem(&joint["axis1"][0], &v);

        debugMap["joints"] += joint;
        jointList.push_back(joint);
      }

      for (std::vector<boost::shared_ptr<urdf::Link> >::iterator it =
             link->child_links.begin(); it != link->child_links.end(); ++it) {
        handleKinematics(*it); //TODO: check if this is correct with shared_ptr
      }
    }

    void Load::handleMaterial(boost::shared_ptr<urdf::Material> material) {
      ConfigMap config;

      config["id"] = nextMaterialID++;
      config["name"] = material->name;
      config["exists"] = true;
      config["diffuseFront"][0]["a"] = (double)material->color.a;
      config["diffuseFront"][0]["r"] = (double)material->color.r;
      config["diffuseFront"][0]["g"] = (double)material->color.g;
      config["diffuseFront"][0]["b"] = (double)material->color.b;
      config["texturename"] = material->texture_filename;
      debugMap["materials"] += config;
      materialList.push_back(config);
    }


    unsigned int Load::parseScene() {
      //  HandleFileNames h_filenames;
      vector<string> v_filesToLoad;
      QString xmlErrorMsg = "";

      //creating a handle for the xmlfile
      QFile file(sceneFilename.c_str());

      QLocale::setDefault(QLocale::C);

      LOG_INFO("Load: loading scene: %s", sceneFilename.c_str());

      //test to open the xmlfile
      if (!file.open(QIODevice::ReadOnly)) {
        std::cout << "Error while opening scene file content " << sceneFilename
                  << " in Load.cpp->parseScene" << std::endl;
        std::cout << "Make sure your scenefile name corresponds to"
                  << " the name given to the enclosed .scene file" << std::endl;
        return 0;
      }


      boost::shared_ptr<urdf::ModelInterface> model;
      model = urdf::parseURDFFile(sceneFilename);
      if (!model) {
        return 0;
      }

      createFakeMaterial();
      std::map<std::string, boost::shared_ptr<urdf::Material> >::iterator it;
      for(it=model->materials_.begin(); it!=model->materials_.end(); ++it) {
        handleMaterial(it->second);
      }

      handleKinematics(model->root_link_);


      debugMap.toYamlFile("debugMap.yml");

      //    //the entire tree recursively anyway
      //    std::vector<boost::shared_ptr<urdf::Link>> urdflinklist;
      //    std::vector<boost::shared_ptr<urdf::Joint>> urdfjointlist;
      //

      //    model.getJoints(urdfjointlist);
      //    for (std::vector<boost::shared_ptr<urdf::Link>>::iterator it =
      //            urdfjointlist.begin(); it != urdfjointlist.end(); ++it) {
      //        getGenericConfig(&jointList, it);
      //    }


      return 1;
    }

    unsigned int Load::loadScene() {

      for (unsigned int i = 0; i < materialList.size(); ++i)
        if(!loadMaterial(materialList[i]))
          return 0;
      for (unsigned int i = 0; i < nodeList.size(); ++i)
        if (!loadNode(nodeList[i]))
          return 0;

      for (unsigned int i = 0; i < jointList.size(); ++i)
        if (!loadJoint(jointList[i]))
          return 0;

      return 1;
    }

    unsigned int Load::loadNode(utils::ConfigMap config) {
      NodeData node;
      config["mapIndex"].push_back(utils::ConfigItem(mapIndex));
      int valid = node.fromConfigMap(&config, tmpPath, control->loadCenter);
      if (!valid)
        return 0;

      if((std::string)config["materialName"][0] != std::string("")) {
        std::map<std::string, MaterialData>::iterator it;
        it = materialMap.find(config["materialName"][0]);
        if (it != materialMap.end()) {
          node.material = it->second;
        }
      }

      NodeId oldId = node.index;
      NodeId newId = control->nodes->addNode(&node);
      if (!newId) {
        LOG_ERROR("addNode returned 0");
        return 0;
      }
      control->loadCenter->setMappedID(oldId, newId, MAP_TYPE_NODE, mapIndex);
      if (mRobotName != "") {
        control->entities->addNode(mRobotName, node.index, node.name);
      }
      return 1;
    }

    unsigned int Load::loadMaterial(utils::ConfigMap config) {
      MaterialData material;

      int valid = material.fromConfigMap(&config, tmpPath);
      materialMap[config["name"][0]] = material;

      return valid;
    }

    unsigned int Load::loadJoint(utils::ConfigMap config) {
      JointData joint;
      config["mapIndex"].push_back(utils::ConfigItem(mapIndex));
      int valid = joint.fromConfigMap(&config, tmpPath,
                                      control->loadCenter);
      if(!valid) {
        fprintf(stderr, "Load: error while loading joint\n");
        return 0;
      }

      JointId oldId = joint.index;
      JointId newId = control->joints->addJoint(&joint);
      if(!newId) {
        LOG_ERROR("addJoint returned 0");
        return 0;
      }
      control->loadCenter->setMappedID(oldId, newId,
                                       MAP_TYPE_JOINT, mapIndex);

      if(mRobotName != "") {
        control->entities->addJoint(mRobotName, joint.index, joint.name);
      }
      return true;
    }

  }// end of namespace urdf_loader
}
// end of namespace mars
