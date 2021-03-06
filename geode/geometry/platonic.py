'''Convenience functions for generating platonic solids
   See also platonic.h'''

from __future__ import (division,absolute_import)
from geode import *

def sphere_mesh(refinements,center=0,radius=1):
  return sphere_mesh_py(refinements,center,radius)

def tetrahedron_mesh():
  X = array([[ 1, 1, 1],[-1,-1, 1],[-1,+1,-1],[+1,-1,-1]],dtype=real)
  mesh = TriangleSoup([[1,2,3],[3,2,0],[3,0,1],[0,2,1]])
  return mesh,X

def cube_mesh(pmin = [0,0,0], pmax=[1,1,1]):
  X = array([pmin,[pmin[0],pmin[1],pmax[2]],[pmin[0],pmax[1],pmin[2]],[pmin[0],pmax[1],pmax[2]],
            [pmax[0],pmin[1],pmin[2]],[pmax[0],pmin[1],pmax[2]],[pmax[0],pmax[1],pmin[2]],pmax],dtype=real)
  mesh = TriangleSoup([[0,1,2], [2,1,3],
                       [1,0,5], [5,0,4],
                       [3,1,7], [7,1,5],
                       [0,2,4], [4,2,6],
                       [2,3,6], [6,3,7],
                       [5,6,7], [6,5,4]])
  return mesh,X

def circle_mesh(n,center=0,radius=1):
  i = arange(n,dtype=int32)
  segments = empty((n,2),dtype=int32)
  segments[:,0] = i
  segments[:-1,1] = i[1:]
  segments[-1,1] = 0
  mesh = SegmentSoup(segments)
  if center is None:
    return mesh
  theta = 2*pi/n*i
  return mesh,(radius*vstack([cos(theta),sin(theta)])).T.copy()

def grid_topology(nx,ny):
  '''Construct a rectangular grid TriangleSoup with nx+1 by ny+1 vertices.'''
  i = (ny+1)*arange(nx).reshape(-1,1)
  j = arange(ny)
  ip = i+ny+1
  jp = j+1
  tris = empty((nx,ny,2,3),dtype=int32)
  tris[:,:,0,0] = tris[:,:,1,0] = i+jp
  tris[:,:,0,1] = i+j
  tris[:,:,0,2] = tris[:,:,1,1] = ip+j
  tris[:,:,1,2] = ip+jp
  return TriangleSoup(tris.reshape(-1,3))

def torus_topology(nx,ny):
  '''Construct a torus TriangleSoup with nx (along the major dimension, i.e. around the hole)
  by ny (along the minor dimension, i.e. through the hole) vertices.
  A matching position array would have shape (nx,ny,3), and list vertices
  sorted primarily by major dimension and secondarily by minor dimension.
  For example, array([ vertex(i, j) for i in range(0, nx) for j in range(0, ny) ]).
  If you want geometry too, consider using surface_of_revolution with periodic=True.'''
  i = ny*arange(nx).reshape(-1,1)
  j = arange(ny)
  ip = (i+ny)%(nx*ny)
  jp = (j+1)%ny
  tris = empty((nx,ny,2,3),dtype=int32)
  tris[:,:,0,0] = tris[:,:,1,0] = i+jp
  tris[:,:,0,1] = i+j
  tris[:,:,0,2] = tris[:,:,1,1] = ip+j
  tris[:,:,1,2] = ip+jp
  return TriangleSoup(tris.reshape(-1,3))

def cylinder_topology(nz,na,closed=False):
  '''Construct a open cylinder TriangleSoup with na triangles around and nz along.
  closed can be either a single bool or an array of two bools (one for each end).'''
  closed = asarray(closed)
  c0,c1 = closed if closed.ndim else (closed,closed)
  i = arange(na)
  j = arange(nz).reshape(-1,1)
  tris = empty((nz,na,2,3),dtype=int32)
  ip = (i+1)%na
  tris[:,:,0,0] = tris[:,:,1,0] = na*j+ip
  tris[:,:,0,1] = na*j+i
  tris[:,:,0,2] = tris[:,:,1,1] = na*(j+1)+i
  tris[:,:,1,2] = na*(j+1)+ip
  if c0 and c1: tris = concatenate([tris[0,:,1],tris[1:-1].reshape(-1,3),tris[-1,:,0]])
  elif c0:      tris = concatenate([tris[0,:,1],tris[1:  ].reshape(-1,3)])
  elif c1:      tris = concatenate([            tris[ :-1].reshape(-1,3),tris[-1,:,0]])
  if c1: tris = minimum(tris.ravel(),na*nz)
  if c0: tris = maximum(0,tris.ravel()-(na-1))
  return TriangleSoup(tris.reshape(-1,3))

def surface_of_revolution(base,axis,radius,height,resolution,closed=False,periodic=False):
  '''Construct a surface of revolution with given radius and height curves.
  closed can be either a single bool or an array of two bools (one for each end).
  For each closed end, height should have one more point than radius.
  If periodic is true, toroidal topology is used.'''
  closed = asarray(closed,dtype=int32)
  c0,c1 = closed if closed.ndim else (closed,closed)
  radius = asarray(radius)
  height = asarray(height)
  assert radius.ndim<=1 and height.ndim<=1
  assert height.size>=1+c0+c1
  assert not periodic or (not c0 and not c1)
  height = height.reshape(-1)
  axis = asarray(axis)
  x = unit_orthogonal_vector(axis)
  y = normalized(cross(axis,x))
  a = 2*pi/resolution*arange(resolution)
  circle = x*cos(a).reshape(-1,1)-y*sin(a).reshape(-1,1)
  X = base+radius[...,None,None]*circle+height[c0:len(height)-c1,None,None]*axis
  X = concatenate(([[base+height[0]*axis]] if c0 else []) + [X.reshape(-1,3)] + ([[base+height[-1]*axis]] if c1 else []))
  if periodic:
    return torus_topology(len(height),resolution),X
  else:
    return cylinder_topology(len(height)-1,resolution,closed=closed),X


def revolve_around_curve(curve,radius,resolution,tangent=None,closed=False,periodic=False):
  '''Construct a surface via variable radius thickening of a curve.
  closed can be either a single bool or an array of two bools (one for each end).
  For each closed end, curve should have one more point than radius.'''
  closed = asarray(closed,dtype=int32)
  c0,c1 = closed if closed.ndim else (closed,closed)
  curve = asarray(curve)
  n = len(curve)
  radius = asarray(radius)
  assert radius.ndim==0 or radius.shape==(n-c0-c1,)
  if tangent is not None:
    assert len(tangent)==n-c0-c1
    tangent = normalized(tangent)
  else:
    tangent = normalized(curve[1:]-curve[:-1])
    tangent = concatenate(([] if c0 else [[tangent[0]]]) + [normalized(tangent[:-1]+tangent[1:])] + ([] if c1 else [[tangent[-1]]]))
  x = unit_orthogonal_vector(tangent)
  y = cross(x,tangent)
  roll = atan2(dots(x[:-1],y[1:]),dots(x[:-1],x[1:]))
  roll = hstack([0,cumsum(roll)])[:,None,None]
  a = 2*pi/resolution*arange(resolution)[:,None]+roll
  X = curve[c0:n-c1,None]+radius[...,None,None]*(x[:,None]*cos(a)+y[:,None]*sin(a))
  X = concatenate(([[curve[0]]] if c0 else []) + [X.reshape(-1,3)] + ([[curve[-1]]] if c1 else []))
  if periodic:
    return torus_topology(n,resolution),X
  else:
    return cylinder_topology(n-1,resolution,closed=closed),X

def open_cylinder_mesh(x0,x1,radius,na,nz=None):
  '''radius may be a scalar or a 1d array'''
  radius = asarray(radius)
  if nz is None:
    assert radius.ndim<2
    if radius.ndim:
      assert len(radius)>1
      nz = len(radius)-1
    else:
      nz = 1
  else:
    assert radius.shape in ((),(nz+1,))
  x0 = asarray(x0)
  x1 = asarray(x1)
  z = normalized(x1-x0)
  x = unit_orthogonal_vector(z)
  y = cross(z,x)
  i = arange(na)
  a = 2*pi/na*i
  circle = x*cos(a).reshape(-1,1)-y*sin(a).reshape(-1,1)
  height = arange(nz+1)/(nz+1)
  X = x0+radius[...,None,None]*circle+arange(nz+1).reshape(-1,1,1)/nz*(x1-x0)
  return cylinder_topology(nz,na),X.reshape(-1,3)

def capsule_mesh(x0,x1,radius,n=30):
  x0 = asarray(x0)
  length,axis = magnitudes_and_normalized(x1-x0)
  theta = linspace(0,pi/2,(n+1)//2)
  r = radius*cos(theta[:-1])
  h = radius*sin(theta)
  r = hstack([r[::-1],r])
  h = hstack([-h[::-1],h+length])
  return surface_of_revolution(x0,axis,r,h,n,closed=True)
