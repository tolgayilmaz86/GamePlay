#include "framework/Base.h"
#include "graphics/MeshSkin.h"
#include "animation/Joint.h"
#include "graphics/Model.h"

// The number of rows in each palette matrix.
#define PALETTE_ROWS 3

namespace gameplay
{

  MeshSkin::MeshSkin()
    : _rootJoint(nullptr), _rootNode(nullptr), _matrixPalette(nullptr), _model(nullptr)
  {
  }

  MeshSkin::~MeshSkin()
  {
    clearJoints();

    SAFE_DELETE_ARRAY(_matrixPalette);
  }

  const Matrix& MeshSkin::getBindShape() const
  {
    return _bindShape;
  }

  void MeshSkin::setBindShape(const float* matrix)
  {
    _bindShape.set(matrix);
  }

  unsigned int MeshSkin::getJointCount() const
  {
    return (unsigned int)_joints.size();
  }

  std::shared_ptr<Joint> MeshSkin::getJoint(unsigned int index) const
  {
    assert(index < _joints.size());
    return _joints[index];
  }

  std::shared_ptr<Joint> MeshSkin::getJoint(const char* id) const
  {
    assert(id);

    for (size_t i = 0, count = _joints.size(); i < count; ++i)
    {
      std::shared_ptr<Joint> j = _joints[i];
      if (j && j->getId() != nullptr && strcmp(j->getId(), id) == 0)
      {
        return j;
      }
    }

    return nullptr;
  }

  MeshSkin* MeshSkin::clone(NodeCloneContext& context) const
  {
    MeshSkin* skin = new MeshSkin();
    skin->_bindShape = _bindShape;
    if (_rootNode && _rootJoint)
    {
      const unsigned int jointCount = getJointCount();
      skin->setJointCount(jointCount);

      assert(skin->_rootNode == nullptr);

      // Check if the root node has already been cloned.
      if (std::shared_ptr<Node> rootNode = context.findClonedNode(_rootNode.get()))
      {
        skin->_rootNode = rootNode;
      }
      else
      {
        skin->_rootNode = _rootNode->cloneRecursive(context);
      }

      std::shared_ptr<Node> node = nullptr;
      if (strcmp(skin->_rootNode->getId(), _rootJoint->getId()) == 0)
      {
        node = skin->_rootNode;
      }
      else
      {
        node = skin->_rootNode->findNode(_rootJoint->getId());
      }
      assert(node);
      skin->_rootJoint = std::dynamic_pointer_cast<Joint>(node);
      for (unsigned int i = 0; i < jointCount; ++i)
      {
        std::shared_ptr<Joint> oldJoint = getJoint(i);
        assert(oldJoint);

        std::shared_ptr<Joint> newJoint = std::dynamic_pointer_cast<Joint>(skin->_rootNode->findNode(oldJoint->getId()));
        if (!newJoint)
        {
          if (strcmp(skin->_rootJoint->getId(), oldJoint->getId()) == 0)
            newJoint = std::dynamic_pointer_cast<Joint>(skin->_rootJoint);
        }
        assert(newJoint);
        skin->setJoint(newJoint, i);
      }
    }
    return skin;
  }

  void MeshSkin::setJointCount(unsigned int jointCount)
  {
    // Erase the joints vector and release all joints.
    clearJoints();

    // Resize the joints vector and initialize to nullptr.
    _joints.resize(jointCount);
    for (unsigned int i = 0; i < jointCount; i++)
    {
      _joints[i] = nullptr;
    }

    // Rebuild the matrix palette. Each matrix is 3 rows of Vector4.
    SAFE_DELETE_ARRAY(_matrixPalette);

    if (jointCount > 0)
    {
      _matrixPalette = new Vector4[jointCount * PALETTE_ROWS];
      for (unsigned int i = 0; i < jointCount * PALETTE_ROWS; i += PALETTE_ROWS)
      {
        _matrixPalette[i + 0].set(1.0f, 0.0f, 0.0f, 0.0f);
        _matrixPalette[i + 1].set(0.0f, 1.0f, 0.0f, 0.0f);
        _matrixPalette[i + 2].set(0.0f, 0.0f, 1.0f, 0.0f);
      }
    }
  }

  void MeshSkin::setJoint(std::shared_ptr<Joint> joint, unsigned int index)
  {
    assert(index < _joints.size());

    if (_joints[index])
    {
      _joints[index]->removeSkin(this);
    }

    _joints[index] = joint;

    if (joint)
    {
      joint->addSkin(this);
    }
  }

  Vector4* MeshSkin::getMatrixPalette() const
  {
    assert(_matrixPalette);

    for (size_t i = 0, count = _joints.size(); i < count; i++)
    {
      assert(_joints[i]);
      _joints[i]->updateJointMatrix(getBindShape(), &_matrixPalette[i * PALETTE_ROWS]);
    }
    return _matrixPalette;
  }

  unsigned int MeshSkin::getMatrixPaletteSize() const
  {
    return (unsigned int)_joints.size() * PALETTE_ROWS;
  }

  Model* MeshSkin::getModel() const
  {
    return _model;
  }

  std::shared_ptr<Joint> MeshSkin::getRootJoint() const
  {
    return _rootJoint;
  }

  void MeshSkin::setRootJoint(std::shared_ptr<Joint> joint)
  {
    if (_rootJoint)
    {
      if (_rootJoint->getParent())
      {
        _rootJoint->getParent()->removeListener(this);
      }
    }

    _rootJoint = joint;

    // If the root joint has a parent node, register for its transformChanged event
    if (_rootJoint && _rootJoint->getParent())
    {
      _rootJoint->getParent()->addListener(this, 1);
    }

    std::shared_ptr<Node> newRootNode = _rootJoint;
    if (newRootNode)
    {
      // Find the top level parent node of the root joint
      for (auto node = newRootNode->getParent(); node != nullptr; node = node->getParent())
      {
        if (node->getParent() == nullptr)
        {
          newRootNode = node;
          break;
        }
      }
    }
    setRootNode(newRootNode);
  }

  void MeshSkin::transformChanged(Transform* transform, long cookie)
  {
    switch (cookie)
    {
    case 1:
      // The direct parent of our joint hierarchy has changed.
      // Dirty the bounding volume for our model's node. This special
      // case allows us to have much tighter bounding volumes for
      // skinned meshes by only considering local skin/joint transformations
      // during bounding volume computation instead of fully resolved
      // joint transformations.
      if (_model && _model->getNode())
      {
        _model->getNode()->setBoundsDirty();
      }
      break;
    }
  }

  int MeshSkin::getJointIndex(std::shared_ptr<Joint> joint) const
  {
    for (size_t i = 0, count = _joints.size(); i < count; ++i)
    {
      if (_joints[i].get() == joint.get())
      {
        return (int)i;
      }
    }

    return -1;
  }

  void MeshSkin::setRootNode(std::shared_ptr<Node> node)
  {
    if (_rootNode.get() != node.get())
    {
      _rootNode.reset();
      _rootNode = node;
    }
  }

  void MeshSkin::clearJoints()
  {
    setRootJoint(nullptr);

    //for (size_t i = 0, count = _joints.size(); i < count; ++i)
    //{
    //    SAFE_RELEASE(_joints[i]);
    //}
    _joints.clear();
  }

}
