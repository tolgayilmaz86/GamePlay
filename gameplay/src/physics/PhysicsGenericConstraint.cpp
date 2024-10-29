#include "framework/Base.h"
#include "physics/PhysicsGenericConstraint.h"
#include "scene/Node.h"
#include "physics/PhysicsRigidBody.h"

namespace gameplay
{

  PhysicsGenericConstraint::PhysicsGenericConstraint()
    : PhysicsConstraint(nullptr, nullptr), _rotationOffsetA(nullptr), _rotationOffsetB(nullptr),
    _translationOffsetA(nullptr), _translationOffsetB(nullptr)
  {
    // Not used.
  }

  PhysicsGenericConstraint::PhysicsGenericConstraint(PhysicsRigidBody* a, PhysicsRigidBody* b)
    : PhysicsConstraint(a, b), _rotationOffsetA(nullptr), _rotationOffsetB(nullptr),
    _translationOffsetA(nullptr), _translationOffsetB(nullptr)
  {
    assert(a && a->_body && a->getNode());

    if (b)
    {
      assert(b->_body && b->getNode());
      Vector3 origin = centerOfMassMidpoint(a->getNode(), b->getNode());
      _constraint = bullet_new<btGeneric6DofConstraint>(*a->_body, *b->_body, getTransformOffset(a->getNode(), origin), getTransformOffset(b->getNode(), origin), true);
    }
    else
    {
      _constraint = bullet_new<btGeneric6DofConstraint>(*a->_body, btTransform::getIdentity(), true);
    }
  }

  PhysicsGenericConstraint::PhysicsGenericConstraint(PhysicsRigidBody* a, const Quaternion& rotationOffsetA, const Vector3& translationOffsetA,
    PhysicsRigidBody* b, const Quaternion& rotationOffsetB, const Vector3& translationOffsetB)
    : PhysicsConstraint(a, b), _rotationOffsetA(nullptr), _rotationOffsetB(nullptr), _translationOffsetA(nullptr), _translationOffsetB(nullptr)
  {
    assert(a && a->_body && a->getNode());

    // Take scale into account for the first node's translation offset.
    Vector3 sA;
    a->getNode()->getWorldMatrix().getScale(&sA);
    Vector3 tA(translationOffsetA.x * sA.x, translationOffsetA.y * sA.y, translationOffsetA.z * sA.z);

    if (b)
    {
      assert(b->_body && b->getNode());

      // Take scale into account for the second node's translation offset.
      Vector3 sB;
      b->getNode()->getWorldMatrix().getScale(&sB);
      Vector3 tB(translationOffsetB.x * sB.x, translationOffsetB.y * sB.y, translationOffsetB.z * sB.z);

      btTransform frameInA(BQ(rotationOffsetA), BV(tA));
      btTransform frameInB(BQ(rotationOffsetB), BV(tB));
      _constraint = bullet_new<btGeneric6DofConstraint>(*a->_body, *b->_body, frameInA, frameInB, true);
    }
    else
    {
      btTransform frameInA(BQ(rotationOffsetA), BV(tA));
      _constraint = bullet_new<btGeneric6DofConstraint>(*a->_body, frameInA, true);
    }
  }

  PhysicsGenericConstraint::~PhysicsGenericConstraint()
  {
    SAFE_DELETE(_rotationOffsetA);
    SAFE_DELETE(_rotationOffsetB);
    SAFE_DELETE(_translationOffsetA);
    SAFE_DELETE(_translationOffsetB);
  }

}
