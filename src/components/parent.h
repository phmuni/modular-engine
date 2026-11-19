#pragma once
// Makes an entity follow another one: a wheel on a car, a lamp on a post, a weapon in a hand.
//
// The entity's Transform stays its place in the world, so everything that reads Transforms works on children as on
// any entity. Each frame the TransformSystem sets it to the parent's transform combined with `local`, the child's
// position, rotation and scale relative to the parent. Either side can be changed: moving the child's Transform (the
// editor's gizmo, game code placing it in the world) updates `local`; changing `local` moves the child.
//
// Set it with Engine::setParent (or EntityBuilder::withParent), not by adding the component: those keep the child
// where it is, refuse loops, and fill in the bookkeeping below. Destroying a parent destroys its children.

#include "components/transform.h"
#include "foundation/ecs/entityManager.h"

struct Parent {
  Entity entity = -1;
  Transform local; // relative to the parent

  // What the TransformSystem last wrote, to tell which side changed since
  Transform lastWorld;
  Transform lastLocal;
  Transform lastParent;
};
