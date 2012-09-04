//#####################################################################
// Header Geometry/Forward
//#####################################################################
#pragma once

#include <other/core/utility/forward.h>
#include <other/core/vector/ScalarPolicy.h>
namespace other {

template<class TV> class Box;
template<class TV> class Segment;
template<class TV> class Triangle;
template<class T> class Plane;
template<class TV> class Ray;
template<class TV> class Sphere;
class Cylinder;

template<class TV> class BoxTree;
template<class TV> class ParticleTree;
template<class TV,int d> class SimplexTree;

template<class TV> class Implicit;

template<class TV> struct IsScalarBlock<Box<TV>> : public IsScalarBlock<TV>{};
template<class TV> struct is_packed_pod<Box<TV>> : public is_packed_pod<TV>{};

}
