// A corner data structure representing oriented triangle meshes.
// This class represents topology only, not geometry.
#pragma once

#include <geode/mesh/TriangleSoup.h>
#include <geode/mesh/ids.h>
#include <geode/array/Field.h>
#include <geode/array/UntypedArray.h>
#include <geode/structure/Hashtable.h>
#include <geode/structure/Tuple.h>
#include <geode/utility/range.h>
#include <exception>
namespace geode {

struct TriangleTopologyOutgoing;
struct TriangleTopologyIncoming;
template<class Id> struct TriangleTopologyIter;
class MutableTriangleTopology;

// A TriangleTopology consists of vertices and faces connected into an oriented manifold,
// plus special boundary halfedges along each boundary curve.  Treating boundary
// halfedges specially removes a bit of the elegance of the corner mesh, but has the
// huge advantage that code written for halfedge meshes can be ported relatively easily.
//
// Internal halfedges are represented implicitly as HalfedgeId(3*f+i), where f is the face
// and i in [0,3) is the index of the source vertex in the face.  Boundary halfedges are
// represented by -1-b where b is the boundary id.
//
// Warning: Since boundary halfedges are treated specially, fields indexed by HalfedgeId
// do not contain entries for boundary halfedges.
//
// The data structure is as follows:
// 1. Each finite vertex v has a pointer to one of its outgoing halfedges, named halfedge(v).
//    If v is a boundary vertex, halfedge(v) is a boundary halfedge.
// 2. Each triangle f has three pointers to vertices and three pointers to neighbor halfedges
//    in neighbor faces, named vertices(f) and neighbors(f).
// 4. Each boundary edge stores its source vertex, prev and next boundary edges, and its reverse.
//
// Important properties and invariants:
// 1. The topology is always locally manifold, with the proviso that a vertex may
//    have multiple disjoint boundary curves (this is common when triangles are
//    added from triangle soup).
// 2. If a vertex is a boundary vertex, halfedge(v) is a boundary halfedge.
// 3. If e is a halfedge, either e or reverse(e) is not a boundary.
// 4. There are no self loops: src(e) != dst(e).
// 5. There is at most one edge between each pair of vertices.
// 6. For any face f, src(3*f+i) = faces_[f].vertices[i]
//
// Performance considerations:
// 1. IMPORTANT: Since boundary halfedges are stored specially, removing a single face in the interior
//    of a mesh will result in three new halfedge structs, increasing the storage costs.  If you plan
//    to fill in the missing space with further triangles, use alternative modification primitives
//    such as split_face or flip_edge (or new routines written on request).
// 2. Similarly, adding triangles one by one using add_face is much more expensive than a single
//    bulk call to add_faces.
// 3. For a mesh with v vertices, f faces, and b boundary edges, the storage costs are roughly
//    1v + 6f + 4b = 13 ints/vertex assuming low genus and few boundary vertices.
//
// TriangleTopology deliberately exposes the arrays containing the data structure details as public API,
// so that users with special requirements may perform surgery on the raw structure.  Normal use
// should go through the high level interface.
//
// TODO:
// - Make add_faces that actually batch inserts.
// - Make a more efficient version of erased(VertexId) and erase(HalfedgeId)
// - Check in add_face/add_vertex whether we exceed the data structure limits.
// - Make a field class for boundary halfedges

class TriangleTopology : public Object {
public:
  GEODE_NEW_FRIEND
  typedef Object Base;

  // Various feature counts, exluding erased entries
  const int n_vertices_;
  const int n_faces_;
  const int n_boundary_edges_;

  // Flat arrays describing the mesh structure.  Do not use these directly unless you have a good reason.
  struct FaceInfo {
    Vector<VertexId,3> vertices; // vertices.x==erased_id if the face is erased
    Vector<HalfedgeId,3> neighbors;
  };
  struct BoundaryInfo {
    HalfedgeId prev, next; // These always point to boundaries.  Erased boundary edges are connected into a linked list via next.
    HalfedgeId reverse; // Always points to an interior halfedge
    VertexId src; // If erased, src = erased_id
  };
  const Field<const FaceInfo,FaceId> faces_;
  const Field<const HalfedgeId,VertexId> vertex_to_edge_; // outgoing halfedge, invalid if isolated, erased_id if vertex erased
  const Array<const BoundaryInfo> boundaries_; // If HalfedgeId(-1-b) is a boundary halfedge, boundaries_[b] is its info

  // The linked list of erased boundary edges
  const HalfedgeId erased_boundaries_;

protected:

  // These functions are needed for the constructors, but are protected because we are not publically mutable.

  // Link two boundary edges together (without ensuring consistency)
  void unsafe_boundary_link(HalfedgeId p, HalfedgeId n) {
    assert(p.id<0 && n.id<0);
    boundaries_.const_cast_()[-1-p.id].next = n;
    boundaries_.const_cast_()[-1-n.id].prev = p;
  }

  // Link two interior edges together (without ensuring consistency)
  void unsafe_interior_link(const HalfedgeId e0, const HalfedgeId e1) {
    const auto& faces = faces_.const_cast_().flat;
    const int f0 = e0.id/3,
              f1 = e1.id/3;
    faces[f0].neighbors[e0.id-3*f0] = e1;
    faces[f1].neighbors[e1.id-3*f1] = e0;
  }

  // Link an interior halfedge with an arbitrary opposite halfedge (without ensuring consistency)
  inline void unsafe_set_reverse(FaceId f, int i, HalfedgeId r) {
    faces_.const_cast_()[f].neighbors[i] = r;
    if (r.id>=0) {
      const int f1 = r.id/3;
      faces_.const_cast_().flat[f1].neighbors[r.id-3*f1] = HalfedgeId(3*f.id+i);
    } else
      boundaries_.const_cast_()[-1-r.id].reverse = HalfedgeId(3*f.id+i);
  }

  // make a new boundary at src, opposite of reverse. Does not ensure consistency
  HalfedgeId unsafe_new_boundary(const VertexId src, const HalfedgeId reverse);

  // Mark features as erased (takes care of element counts, but without ensuring consistency)
  inline void unsafe_set_erased(VertexId v); // safe if the vertex is isolated
  inline void unsafe_set_erased(FaceId f);
  inline void unsafe_set_erased(HalfedgeId b); // Must be a boundary edge

  // Add a new isolated vertex and return its id
  GEODE_CORE_EXPORT VertexId internal_add_vertex();

  // Add n isolated vertices and return the first id (new ids are contiguous)
  GEODE_CORE_EXPORT VertexId internal_add_vertices(int n);

  // Add a new face.  If the result would not be manifold, no change is made and ValueError is thrown (TODO: throw a better exception).
  GEODE_CORE_EXPORT FaceId internal_add_face(Vector<VertexId,3> v);

  // Add many new faces (return the first id, new ids are contiguous)
  GEODE_CORE_EXPORT FaceId internal_add_faces(RawArray<const Vector<int,3>> vs);

  // Collect unused boundary halfedges.  Returns old_to_new map.  This can be called after construction
  // from triangle soup, since unordered face addition leaves behind garbage boundary halfedges.
  // The complexity is linear in the size of the boundary (including garbage).
  GEODE_CORE_EXPORT Array<int> internal_collect_boundary_garbage();

  GEODE_CORE_EXPORT TriangleTopology();
  GEODE_CORE_EXPORT TriangleTopology(const TriangleTopology& mesh, bool copy = false);
  GEODE_CORE_EXPORT explicit TriangleTopology(TriangleSoup const &soup);
  GEODE_CORE_EXPORT explicit TriangleTopology(RawArray<const Vector<int,3>> faces);

public:
  ~TriangleTopology();

  // Copy the mesh
  GEODE_CORE_EXPORT Ref<TriangleTopology> copy() const;
  GEODE_CORE_EXPORT Ref<MutableTriangleTopology> mutate() const;

  // Count various features, excluding erased ids.  If you want to include deletions, use faces_.size() and such.
  int n_vertices()       const { return n_vertices_; }
  int n_faces()          const { return n_faces_; }
  int n_edges()          const { return (3*n_faces_+n_boundary_edges_)>>1; }
  int n_boundary_edges() const { return n_boundary_edges_; }

  // Check if vertices, faces, and boundary edges are garbage collected, ensuring contiguous indices.
  GEODE_CORE_EXPORT bool is_garbage_collected() const;

  // Walk around the mesh.  These always succeed given valid ids, but may return invalid ids as a result (e.g., the face of a boundary halfedge).
  inline HalfedgeId halfedge(VertexId v)           const;
  inline HalfedgeId prev    (HalfedgeId e)         const;
  inline HalfedgeId next    (HalfedgeId e)         const;
  inline HalfedgeId reverse (HalfedgeId e)         const;
  inline VertexId   src     (HalfedgeId e)         const;
  inline VertexId   dst     (HalfedgeId e)         const;
  inline FaceId     face    (HalfedgeId e)         const;
  inline VertexId   vertex  (FaceId f, int i=0)    const;
  inline HalfedgeId halfedge(FaceId f, int i=0)    const;
  inline HalfedgeId left    (HalfedgeId e)         const;
  inline HalfedgeId right   (HalfedgeId e)         const;
  inline VertexId   opposite(HalfedgeId e)         const;
  inline HalfedgeId opposite(FaceId f, VertexId v) const;

  // Check id validity or deletion.  A erased id is considered invalid.
  inline bool valid(VertexId v)   const;
  inline bool valid(HalfedgeId e) const;
  inline bool valid(FaceId f)     const;
  inline bool erased(VertexId v)   const;
  inline bool erased(HalfedgeId e) const;
  inline bool erased(FaceId f)     const;

  // Check for boundaries
  inline bool is_boundary(HalfedgeId e) const;
  inline bool is_boundary(VertexId v)   const;
  inline bool isolated   (VertexId v)   const;
  GEODE_CORE_EXPORT bool has_boundary() const; // O(1) time
  GEODE_CORE_EXPORT bool is_manifold() const; // O(1) time
  GEODE_CORE_EXPORT bool is_manifold_with_boundary() const; // O(n) time
  GEODE_CORE_EXPORT bool has_isolated_vertices() const; // O(n) time

  // Tuples or iterable ranges of neighbors
  inline Vector<HalfedgeId,3> halfedges(FaceId f) const;
  inline Vector<VertexId,2> vertices(HalfedgeId e) const;
  inline Vector<VertexId,3> vertices(FaceId f) const;
  inline Vector<FaceId,3> faces(FaceId f) const;
  inline Range<TriangleTopologyOutgoing> outgoing(VertexId v) const;
  inline Range<TriangleTopologyIncoming> incoming(VertexId v) const;
  inline Vector<FaceId,2> faces(HalfedgeId e) const; // vec(face(e), face(reverse(e)))

  Array<VertexId> vertex_one_ring(VertexId v) const;
  Array<FaceId> incident_faces(VertexId v) const;

  // Iterate over vertices, edges, or faces, skipping erased entries.
  inline Range<TriangleTopologyIter<VertexId>>   vertices()           const;
  inline Range<TriangleTopologyIter<FaceId>>     faces()              const;
  inline Range<TriangleTopologyIter<HalfedgeId>> halfedges()          const;
  inline Range<TriangleTopologyIter<HalfedgeId>> boundary_edges()     const;
  inline Range<TriangleTopologyIter<HalfedgeId>> interior_halfedges() const;

  // Find a halfedge between two vertices, or return an invalid id if none exists.
  GEODE_CORE_EXPORT HalfedgeId halfedge(VertexId v0, VertexId v1) const;

  // Find the halfedge between two faces, or return an invalid id if none exists.  The halfedge belongs to the first face.
  // This function works correctly if the input faces are invalid.
  GEODE_CORE_EXPORT HalfedgeId common_halfedge(FaceId f0, FaceId f1) const;

  // Extract all triangles as a flat array
  Array<Vector<int,3>> elements() const;

  // Compute the edge degree of a vertex in O(degree) time.
  GEODE_CORE_EXPORT int degree(VertexId v) const;

  // Compute all boundary loops
  Nested<HalfedgeId> boundary_loops() const;

  // Compute the Euler characteristic.
  int chi() const {
    return n_vertices()-n_edges()+n_faces();
  }

  // Check whether an edge flip would result in a manifold mesh
  GEODE_CORE_EXPORT bool is_flip_safe(HalfedgeId e) const;

  // Run an expensive internal consistency check.  Safe to call even if the structure arrays are random noise.
  GEODE_CORE_EXPORT void assert_consistent() const;

  // Print internal structure to Log::cout.  Safe to call even if the structure arrays are random noise.
  GEODE_CORE_EXPORT void dump_internals() const;

  // Iterate over vertices, edges, or faces *without* skipping erased entries.
  inline Range<IdIter<VertexId>>   all_vertices()       const;
  inline Range<IdIter<FaceId>>     all_faces()          const;
  inline Range<IdIter<HalfedgeId>> all_halfedges() const;
  inline Range<IdIter<HalfedgeId>> all_interior_halfedges() const;
  inline Range<IdIter<HalfedgeId>> all_boundary_edges() const;

  // Safe versions of functions for python
  HalfedgeId safe_halfedge(VertexId v) const;
  HalfedgeId safe_prev    (HalfedgeId e) const;
  HalfedgeId safe_next    (HalfedgeId e) const;
  VertexId   safe_src     (HalfedgeId e) const;
  VertexId   safe_dst     (HalfedgeId e) const;
  FaceId     safe_face    (HalfedgeId e) const;
  HalfedgeId safe_left    (HalfedgeId e) const;
  HalfedgeId safe_right   (HalfedgeId e) const;
  Vector<VertexId,3>   safe_face_vertices    (FaceId f) const;
  Vector<HalfedgeId,3> safe_face_halfedges   (FaceId f) const;
  Vector<VertexId,2>   safe_halfedge_vertices(HalfedgeId e) const;
  Vector<FaceId,3>     safe_face_faces       (FaceId f) const;
  Vector<FaceId,2>     safe_halfedge_faces   (HalfedgeId e) const;
  Range<TriangleTopologyOutgoing> safe_outgoing(VertexId v) const;
  Range<TriangleTopologyIncoming> safe_incoming(VertexId v) const;
  HalfedgeId safe_halfedge_between(VertexId v0, VertexId v1) const;
};

// A mutable topology, with attached fields on vertices, faces, or halfedges, which are maintained through
// topological operations using user-defined schemes.
class MutableTriangleTopology: public TriangleTopology {
public:
  GEODE_NEW_FRIEND
  typedef TriangleTopology Base;

  typedef Base::FaceInfo FaceInfo;
  typedef Base::BoundaryInfo BoundaryInfo;

protected:

  int &mutable_n_vertices_;
  int &mutable_n_faces_;
  int &mutable_n_boundary_edges_;

  Field<FaceInfo,FaceId>& mutable_faces_;
  Field<HalfedgeId,VertexId>& mutable_vertex_to_edge_; // outgoing halfedge, invalid if isolated, erased_id if vertex erased
  Array<BoundaryInfo>& mutable_boundaries_; // If HalfedgeId(-1-b) is a boundary halfedge, boundaries_[b] is its info

  // The linked list of erased boundary edges
  HalfedgeId& mutable_erased_boundaries_;

  vector<UntypedArray> vertex_fields,
                       face_fields,
                       halfedge_fields;
  Hashtable<int,int> id_to_vertex_field,
                     id_to_face_field,
                     id_to_halfedge_field;
  int next_field_id;

  GEODE_CORE_EXPORT MutableTriangleTopology();
  GEODE_CORE_EXPORT MutableTriangleTopology(const TriangleTopology& mesh, bool copy = false);
  GEODE_CORE_EXPORT MutableTriangleTopology(const MutableTriangleTopology& mesh, bool copy = false);
  GEODE_CORE_EXPORT MutableTriangleTopology(TriangleSoup const &soup);
  GEODE_CORE_EXPORT MutableTriangleTopology(RawArray<const Vector<int,3>> faces);

public:

  ~MutableTriangleTopology();

  // Field management
#define FIELD_ACCESS_FUNCTIONS(prim, Id, size_expr) \
  template<class T> FieldId<T,Id> add_##prim##_field(int id = invalid_id) { \
    if (id == invalid_id) \
      id = next_field_id++; \
    else \
      next_field_id = max(next_field_id,id+1); \
    GEODE_ASSERT(!id_to_##prim##_field.contains(id)); \
    prim##_fields.push_back(UntypedArray(Types<T>(),(size_expr))); \
    id_to_##prim##_field.set(id,int(prim##_fields.size()-1)); \
    return FieldId<T,Id>(id); \
  } \
  template<class T> bool has_##prim##_field(const FieldId<T,Id> id) const { \
    return id.valid() && id_to_##prim##_field.contains(id); \
  } \
  template<class T> void remove_##prim##_field(const FieldId<T,Id> id) { \
    remove_field_helper(id_to_##prim##_field,prim##_fields,id.id); \
  } \
  template<class T> const Field<T,Id>& field(const FieldId<T,Id> id) { \
    return prim##_fields[id_to_##prim##_field.get(id)].template get<T,Id>(); \
  } \
  template<class T> const Field<const T,Id> field(const FieldId<T,Id> id) const { \
    return prim##_fields[id_to_##prim##_field.get(id)].template get<const T,Id>(); \
  }
  FIELD_ACCESS_FUNCTIONS(vertex,   VertexId,   vertex_to_edge_.size())
  FIELD_ACCESS_FUNCTIONS(face,     FaceId,     faces_.size())
  FIELD_ACCESS_FUNCTIONS(halfedge, HalfedgeId, faces_.size()*3)
  #undef FIELD_ACCESS_FUNCTIONS

  #ifdef GEODE_PYTHON
    PyObject* add_vertex_field_py(PyObject* dtype, const int id);
    PyObject* add_face_field_py(PyObject* dtype, const int id);
    PyObject* add_halfedge_field_py(PyObject* dtype, const int id);
    bool has_field_py(const PyFieldId& id) const;
    void remove_field_py(const PyFieldId& id);
    PyObject* field_py(const PyFieldId& id);
  #endif

  // publish the hidden construction methods from TriangleTopology
  using TriangleTopology::unsafe_boundary_link;
  using TriangleTopology::unsafe_set_erased;
  using TriangleTopology::unsafe_set_reverse;

  // we have to overwrite this -- it needs to take care of fields for us
  HalfedgeId unsafe_new_boundary(const VertexId src, const HalfedgeId reverse);

  // return a deep copy as a new TriangleTopology
  Ref<MutableTriangleTopology> copy() const;

  // these methods take care of your fields for you.

  // Add a new isolated vertex and return its id.
  GEODE_CORE_EXPORT VertexId add_vertex();

  // Add n isolated vertices and return the first id (new ids are contiguous)
  GEODE_CORE_EXPORT VertexId add_vertices(int n);

  // Add a new face.  If the result would not be manifold, no change is made and ValueError is thrown.
  // TODO: throw a better exception.
  GEODE_CORE_EXPORT FaceId add_face(Vector<VertexId,3> v);

  // Add many new faces (return the first id, new ids are contiguous)
  GEODE_CORE_EXPORT FaceId add_faces(RawArray<const Vector<int,3>> vs);

  // Flip the two triangles adjacent to a given halfedge.  The routines throw an exception if is_flip_safe fails;
  // call unsafe_flip_edge if you've already checked.  WARNING: The halfedge ids in the two adjacent faces are
  // changed, and the new id of the argument edge is returned.  The returned edge is e topologically rotated right.
  GEODE_CORE_EXPORT HalfedgeId flip_edge(HalfedgeId e) GEODE_WARN_UNUSED_RESULT;

  // Permute vertices: vertex v becomes vertex permutation[v]
  GEODE_CORE_EXPORT void permute_vertices(RawArray<const int> permutation, bool check=false);

  // Add another TriangleTopology, assuming the vertex sets are disjoint.
  // Returns the offsets of the other vertex, face, and boundary ids in the new arrays.
  GEODE_CORE_EXPORT Vector<int,3> add(MutableTriangleTopology const &other);

  // split the halfedge h with a new vertex c, which splits the adjacent face
  // into itself and a face nf. The halfedge h (for which now dst(.) == c), and
  // the halfedge in nf for which now src(.) == c are returned in a vector, and
  // still have to be connected to the outside world with unsafe_set_reverse.
  Vector<HalfedgeId,2> unsafe_split_halfedge(HalfedgeId h, FaceId nf, VertexId c);

  // Split a face into three by inserting a new vertex. Two new faces are created.
  // The id of the new vertex is returned. The two new faces (added at the end)
  // have field that are newly initialized; the existing face's area changes
  // but its field are not touched. The halfedge fields of the original
  // face are copied to the halfedges that are their obvious equivalent (their
  // indices change). The new halfedges (all those connected to the new vertex)
  // are newly initialized.
  GEODE_CORE_EXPORT VertexId split_face(FaceId f);

  // Split a face into three by inserting an existing isolated vertex in the center.
  // Afterwards, face(halfedge(c))==f. The two new faces (added at the end)
  // have fields that are newly initialized; the existing face's area changes
  // but its fields are not touched. The halfedge fields of the original
  // face are copied to the halfedges that are their obivous equivalent (their
  // indices change). The new halfedges (all those connected to the new vertex)
  // are newly initialized.
  GEODE_CORE_EXPORT void split_face(FaceId f, VertexId c);

  // Split a face into three by inserting a new vertex.
  GEODE_CORE_EXPORT VertexId split_edge(HalfedgeId e);

  // Split an edge by inserting an existing isolated vertex in the center.
  GEODE_CORE_EXPORT void split_edge(HalfedgeId h, VertexId c);

  // Erase the given vertex. Erases all incident faces. If erase_isolated is true, also erase other vertices that are now isolated.
  GEODE_CORE_EXPORT void erase(VertexId id, bool erase_isolated = false);

  // Erase all faces indicent to the given halfedge. If erase_isolated is true, also erase incident vertices that are now isolated.
  GEODE_CORE_EXPORT void erase(HalfedgeId id, bool erase_isolated = false);

  // Erase the given face. If erase_isolated is true, also erases incident vertices that are now isolated.
  GEODE_CORE_EXPORT void erase(FaceId id, bool erase_isolated = false);

  // Compact the data structure, removing all erased primitives. Returns a tuple of permutations for
  // vertices, faces, and boundary halfedges, such that the old primitive i now has index permutation[i].
  // For any field f, use f.permute() to create a field that works with the new ids
  GEODE_CORE_EXPORT Vector<Array<int>,3> collect_garbage();

  // Collect unused boundary halfedges.  Returns old_to_new map.  This can be called after construction
  // from triangle soup, since unordered face addition leaves behind garbage boundary halfedges.
  // The complexity is linear in the size of the boundary (including garbage).
  GEODE_CORE_EXPORT Array<int> collect_boundary_garbage();

  // The remaining functions are mainly for internal use, or for external routines that perform direct surgery
  // on the internal structure.  Use with caution!

  // Flip an edge assuming is_flip_safe(e). If that is not the case, this may leave the mesh in a broken state.
  // WARNING: all halfedge ids in the two adjacent faces are changed, and the new id of the argument edge is returned.
  // Both faces keep their fields. The halfedge fields on the outer halfedges are
  // moved to their corresponding new indices, but the interior halfedges (on the
  // flipped edge) keep their fields, the interior halfedge of face 1 before
  // will have the same fields before and after.
  GEODE_CORE_EXPORT HalfedgeId unsafe_flip_edge(HalfedgeId e) GEODE_WARN_UNUSED_RESULT;

  // Remove a face from the mesh, shuffling face and halfedge ids in the process.
  // Vertex ids are untouched, and in particular isolated vertices are not erased.
  GEODE_CORE_EXPORT void erase_face_with_reordering(FaceId f);

  // Remove the last vertex from the mesh, shuffling face and halfedge ids in the process.
  // This exists solely to erase sentinel vertices created by Delaunay.
  GEODE_CORE_EXPORT void erase_last_vertex_with_reordering();

  // Safe versions of functions for Python
  void safe_erase_face    (FaceId f,     bool erase_isolated=false);
  void safe_erase_vertex  (VertexId v,   bool erase_isolated=false);
  void safe_erase_halfedge(HalfedgeId e, bool erase_isolated=false);
};


// Mesh walking routines
inline HalfedgeId TriangleTopology::halfedge(VertexId v) const {
  assert(valid(v));
  return vertex_to_edge_[v];
}
inline VertexId TriangleTopology::vertex(FaceId f, int i) const {
  assert(valid(f));
  return faces_[f].vertices[i];
}
inline HalfedgeId TriangleTopology::halfedge(FaceId f, int i) const {
  assert(valid(f) && unsigned(i)<3);
  return HalfedgeId(3*f.id+i);
}
inline HalfedgeId TriangleTopology::prev(HalfedgeId e) const {
  assert(valid(e));
  return e.id>=0 ? HalfedgeId(e.id+(e.id%3==0?2:-1)) : boundaries_[-1-e.id].prev;
}
inline HalfedgeId TriangleTopology::next(HalfedgeId e) const {
  assert(valid(e));
  return e.id>=0 ? HalfedgeId(e.id+(e.id%3==2?-2:1)) : boundaries_[-1-e.id].next;
}
inline HalfedgeId TriangleTopology::reverse(HalfedgeId e) const {
  assert(valid(e));
  if (e.id>=0) {
    const int f = e.id/3;
    return faces_.flat[f].neighbors[e.id-3*f];
  }
  return boundaries_[-1-e.id].reverse;
}
inline VertexId TriangleTopology::src(HalfedgeId e) const {
  assert(valid(e));
  if (e.id>=0) {
    const int f = e.id/3;
    return faces_.flat[f].vertices[e.id-3*f];
  } else
    return boundaries_[-1-e.id].src;
}
inline VertexId TriangleTopology::dst(HalfedgeId e) const {
  assert(valid(e));
  if (e.id>=0) {
    const int f = e.id/3,
              i = e.id-3*f;
    return faces_.flat[f].vertices[i==2?0:i+1];
  } else
    return boundaries_[-1-boundaries_[-1-e.id].next.id].src;
}
inline FaceId TriangleTopology::face(HalfedgeId e) const {
  assert(valid(e));
  return e.id>=0 ? FaceId(e.id/3) : FaceId();
}
inline HalfedgeId TriangleTopology::left(HalfedgeId e)  const { return reverse(prev(e)); }
inline HalfedgeId TriangleTopology::right(HalfedgeId e) const { return next(reverse(e)); }

inline VertexId TriangleTopology::opposite(HalfedgeId e) const {
  assert(!is_boundary(e));
  return dst(next(e));
}

inline HalfedgeId TriangleTopology::opposite(FaceId f, VertexId v) const {
  assert(valid(f) && faces_[f].vertices.contains(v));
  const auto& vs = faces_[f].vertices;
  return HalfedgeId(3*f.id+(vs.x==v?1:vs.y==v?2:0));
}

// Check id validity or deletion.  A erased id is considered invalid.
inline bool TriangleTopology::valid(VertexId v) const {
  return vertex_to_edge_.valid(v) && !erased(v);
}
inline bool TriangleTopology::valid(HalfedgeId e) const {
  return e.id>=0 ? valid(FaceId(e.id/3))
                 : boundaries_.valid(-1-e.id) && boundaries_[-1-e.id].src.id!=erased_id;
}
inline bool TriangleTopology::valid(FaceId f) const {
  return faces_.valid(f) && !erased(f);
}
inline bool TriangleTopology::erased(VertexId v) const {
  return vertex_to_edge_[v].id==erased_id;
}
inline bool TriangleTopology::erased(HalfedgeId e) const {
  return e.id>=0 ? faces_.flat[e.id/3].vertices.x.id==erased_id
                 : boundaries_[-1-e.id].src.id==erased_id;
}
inline bool TriangleTopology::erased(FaceId f) const {
  return faces_[f].vertices.x.id==erased_id;
}

// Mark features as erased
inline void TriangleTopology::unsafe_set_erased(VertexId v) {
  vertex_to_edge_.const_cast_()[v].id = erased_id;
  const_cast_(n_vertices_)--;
}
inline void TriangleTopology::unsafe_set_erased(FaceId f) {
  faces_.const_cast_()[f].vertices.x.id = erased_id;
  const_cast_(n_faces_)--;
}
inline void TriangleTopology::unsafe_set_erased(HalfedgeId b) {
  assert(b.id < 0); // make sure this is a boundary edge
  boundaries_.const_cast_()[-1-b.id].src.id = erased_id;
  boundaries_.const_cast_()[-1-b.id].next = erased_boundaries_;
  const_cast_(erased_boundaries_) = b;
  const_cast_(n_boundary_edges_)--;
}

// Check for boundaries
inline bool TriangleTopology::is_boundary(HalfedgeId e) const { assert(valid(e)); return e.id<0; }
inline bool TriangleTopology::is_boundary(VertexId v)   const { assert(valid(v)); return halfedge(v).id<0; }
inline bool TriangleTopology::isolated   (VertexId v)   const { assert(valid(v)); return !halfedge(v).valid(); }

// Use only through TriangleTopology::outgoing()
struct TriangleTopologyOutgoing {
  const TriangleTopology& mesh;
  HalfedgeId e;
  bool first;
  TriangleTopologyOutgoing(const TriangleTopology& mesh, HalfedgeId e, bool first) : mesh(mesh), e(e), first(first) {}
  void operator++() { e = mesh.left(e); first = false; }
  bool operator!=(TriangleTopologyOutgoing o) { return first || e!=o.e; } // For use only inside range-based for loops
  HalfedgeId operator*() const { return e; }
};

struct TriangleTopologyIncoming {
  const TriangleTopology& mesh;
  HalfedgeId e;
  bool first;
  TriangleTopologyIncoming(const TriangleTopology& mesh, HalfedgeId e, bool first) : mesh(mesh), e(e), first(first) {}
  void operator++() { e = mesh.left(e); first = false; }
  bool operator!=(TriangleTopologyIncoming o) { return first || e!=o.e; } // For use only inside range-based for loops
  HalfedgeId operator*() const { return mesh.reverse(e); }
};

// Tuples or iterable ranges of neighbors
inline Vector<HalfedgeId,3> TriangleTopology::halfedges(FaceId f) const {
  return vec(HalfedgeId(3*f.id+0),
             HalfedgeId(3*f.id+1),
             HalfedgeId(3*f.id+2));
}

inline Vector<FaceId,3> TriangleTopology::faces(FaceId f) const {
  return vec(face(faces_[f].neighbors[0]),
             face(faces_[f].neighbors[1]),
             face(faces_[f].neighbors[2]));
}

inline Vector<VertexId,2> TriangleTopology::vertices(HalfedgeId e) const {
  return vec(src(e),dst(e));
}

inline Vector<VertexId,3> TriangleTopology::vertices(FaceId f) const {
  assert(valid(f));
  return faces_[f].vertices;
}

inline Vector<FaceId,2> TriangleTopology::faces(HalfedgeId e) const {
  return vec(face(e), face(reverse(e)));
}

inline Range<TriangleTopologyOutgoing> TriangleTopology::outgoing(VertexId v) const {
  const auto e = halfedge(v);
  const TriangleTopologyOutgoing c(*this,e,e.valid());
  return Range<TriangleTopologyOutgoing>(c,c);
}

inline Range<TriangleTopologyIncoming> TriangleTopology::incoming(VertexId v) const {
  const auto e = halfedge(v);
  const TriangleTopologyIncoming c(*this,e,e.valid());
  return Range<TriangleTopologyIncoming>(c,c);
}

// Use only throw vertices(), faces(), or boundary_edges()
template<class Id> struct TriangleTopologyIter {
  const TriangleTopology& mesh;
  Id i;
  Id end;

  TriangleTopologyIter(TriangleTopologyIter<Id> const &i)
    : mesh(i.mesh), i(i.i), end(i.end) {
  }

  TriangleTopologyIter(const TriangleTopology& mesh, Id i_, Id end)
    : mesh(mesh), i(i_), end(end) {
    while (i!=end && mesh.erased(i)) i.id++;
  }

  TriangleTopologyIter &operator=(const TriangleTopologyIter &it) {
    if (&mesh != &it.mesh)
      throw std::runtime_error("Cannot assign iterators of different meshes.");
    i = it.i;
    end = it.end;
    return *this;
  }

  // prefix
  TriangleTopologyIter &operator++() {
    assert(i != end); // don't let them iterate past the end
    i.id++;
    while (i!=end && mesh.erased(i)) i.id++;
    return *this;
  }

  // postfix
  TriangleTopologyIter operator++(int) {
    assert(i != end);
    TriangleTopologyIter old = *this;
    ++*this;
    return old;
  }

  bool operator!=(TriangleTopologyIter o) const { return i!=o.i; } // Assume &mesh==&o.mesh
  bool operator==(TriangleTopologyIter o) const { return i==o.i; } // Assume &mesh==&o.mesh
  Id operator*() const { return i; }
};

// Iterate over vertices, edges, or faces, skipping erased entries
inline Range<TriangleTopologyIter<VertexId>> TriangleTopology::vertices() const {
  const VertexId end(vertex_to_edge_.size());
  return Range<TriangleTopologyIter<VertexId>>(TriangleTopologyIter<VertexId>(*this,VertexId(0),end),TriangleTopologyIter<VertexId>(*this,end,end));
}
inline Range<TriangleTopologyIter<FaceId>> TriangleTopology::faces() const {
  const FaceId end(faces_.size());
  return Range<TriangleTopologyIter<FaceId>>(TriangleTopologyIter<FaceId>(*this,FaceId(0),end),TriangleTopologyIter<FaceId>(*this,end,end));
}
inline Range<TriangleTopologyIter<HalfedgeId>> TriangleTopology::boundary_edges() const {
  const HalfedgeId end(0);
  return Range<TriangleTopologyIter<HalfedgeId>>(TriangleTopologyIter<HalfedgeId>(*this,HalfedgeId(-boundaries_.size()),end),TriangleTopologyIter<HalfedgeId>(*this,end,end));
}
inline Range<TriangleTopologyIter<HalfedgeId>> TriangleTopology::interior_halfedges() const {
  const HalfedgeId end(3*faces_.size());
  return Range<TriangleTopologyIter<HalfedgeId>>(TriangleTopologyIter<HalfedgeId>(*this,HalfedgeId(0),end),TriangleTopologyIter<HalfedgeId>(*this,end,end));
}
inline Range<TriangleTopologyIter<HalfedgeId>> TriangleTopology::halfedges() const {
  const HalfedgeId end(3*faces_.size());
  return Range<TriangleTopologyIter<HalfedgeId>>(TriangleTopologyIter<HalfedgeId>(*this,HalfedgeId(-boundaries_.size()),end),TriangleTopologyIter<HalfedgeId>(*this,end,end));
}

// Iterate over vertices, edges, or faces *without* skipping erased entries
inline Range<IdIter<VertexId>> TriangleTopology::all_vertices() const {
  return Range<IdIter<VertexId>>(VertexId(0),VertexId(vertex_to_edge_.size()));
}
inline Range<IdIter<FaceId>> TriangleTopology::all_faces() const {
  return Range<IdIter<FaceId>>(FaceId(0),FaceId(faces_.size()));
}
inline Range<IdIter<HalfedgeId>> TriangleTopology::all_boundary_edges() const {
  return Range<IdIter<HalfedgeId>>(HalfedgeId(-boundaries_.size()),HalfedgeId(0));
}
inline Range<IdIter<HalfedgeId>> TriangleTopology::all_halfedges() const {
  return Range<IdIter<HalfedgeId>>(HalfedgeId(-boundaries_.size()),HalfedgeId(faces_.size()*3));
}
inline Range<IdIter<HalfedgeId>> TriangleTopology::all_interior_halfedges() const {
  return Range<IdIter<HalfedgeId>>(HalfedgeId(0),HalfedgeId(faces_.size()*3));
}

GEODE_EXPORT void remove_field_helper(Hashtable<int,int>& id_to_field, vector<UntypedArray>& fields, const int id);

}