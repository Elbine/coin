/**************************************************************************\
 * 
 *  Copyright (C) 1998-1999 by Systems in Motion.  All rights reserved.
 *
 *  This file is part of the Coin library.
 *
 *  This file may be distributed under the terms of the Q Public License
 *  as defined by Troll Tech AS of Norway and appearing in the file
 *  LICENSE.QPL included in the packaging of this file.
 *
 *  If you want to use Coin in applications not covered by licenses
 *  compatible with the QPL, you can contact SIM to aquire a
 *  Professional Edition license for Coin.
 *
 *  Systems in Motion AS, Prof. Brochs gate 6, N-7030 Trondheim, NORWAY
 *  http://www.sim.no/ sales@sim.no Voice: +47 22114160 Fax: +47 67172912
 *
\**************************************************************************/

/*!
  \class SbTesselator SbTesselator.h Inventor/SbTesselator.h
  \brief The SbTesselator class is used to tesselate polygons into triangles. 
  \ingroup base
  
  FIXME: write doc
*/


#include <Inventor/SbTesselator.h>
#include <Inventor/SbHeap.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <float.h>

struct SbTVertex {
  float weight;
  int dirtyweight;
  int heapidx;
  SbTVertex *prev;
  SbTesselator *thisp;
  SbVec3f v;
  void *data;
  SbTVertex *next;
};


float 
SbTesselator::heap_evaluate(void *v)
{
  SbTVertex *vertex = (SbTVertex*)v;
  if (vertex->dirtyweight) {
    vertex->dirtyweight = 0;
    if (vertex->thisp->area(vertex) > FLT_EPSILON && 
	vertex->thisp->isTriangle(vertex) && 
	vertex->thisp->clippable(vertex))
      vertex->weight = vertex->thisp->circleSize(vertex);
    else
      vertex->weight = FLT_MAX;
  }
  return vertex->weight;
}

static int 
heap_get_index(void *v)
{
  return ((SbTVertex*)v)->heapidx;
}

static void 
heap_set_index(void *v, int idx)
{
  ((SbTVertex*)v)->heapidx = idx;
}

//projection enums
enum {OXY,OXZ,OYZ};

/*!
  Initializes a tesselator. The \a callback argument specifies
  a function which will be called for each triangle returned 
  by the tesselator. The callback function will get three pointers
  to each vertex and the \a userdata pointer. The vertex pointers
  are specified in the SbTesselator::addVertex() method.
*/
SbTesselator::SbTesselator(void (*callback)(void *v0, 
					    void *v1, 
					    void *v2, 
					    void *data),
			   void *userdata)
{
  this->setCallback(callback, userdata);
  this->headV = this->tailV = NULL;
  this->currVertex = 0;
  
  SbHeapFuncs hf;
  hf.eval_func = heap_evaluate;
  hf.get_index_func = heap_get_index;
  hf.set_index_func = heap_set_index;
  
  this->heap = new SbHeap(hf, 64);
}

/*!
  Destructor.
*/
SbTesselator::~SbTesselator()
{
  cleanUp();
  int i,n = this->vertexStorage.getLength();
  for (i = 0; i < n; i++) delete this->vertexStorage[i];
  delete this->heap;
}

/*!
  Initializes new polygon. This method can be used if you've 
  already calculated the normal of the polygon. If \a keepVerts
  is \e TRUE, all vertices will be included in the returned
  triangles, even though this might lead to triangles without area.
*/
void
SbTesselator::beginPolygon(const SbVec3f &normal, SbBool keepVerts)
{
  this->cleanUp();
  this->keepVertices = keepVerts;
  this->polyNormal = normal;
  this->hasNormal = TRUE;
  this->headV = this->tailV = NULL;
  this->numVerts=0;
}

/*
  \overload
*/
void
SbTesselator::beginPolygon(SbBool keepVerts)
{
  this->cleanUp();
  this->keepVertices = keepVerts;
  this->hasNormal = FALSE;
  this->headV = this->tailV = NULL;
  this->numVerts=0;
}

/*!
  Adds a new vertex to the polygon. \a data will be returned
  as a vertex in the callback-function.
*/
void
SbTesselator::addVertex(const SbVec3f &v,void *data)
{
  SbTVertex *newv = this->newVertex();
  newv->v = v;
  newv->data = data;
  newv->next = NULL;
  newv->dirtyweight = 1;
  newv->weight = FLT_MAX;
  newv->heapidx = -1;
  newv->prev = this->tailV;
  newv->thisp = this;
  if (!this->headV) this->headV = newv;
  if (this->tailV) this->tailV->next = newv;
  this->tailV = newv;
  this->numVerts++;
}

/*!
  Signals the tesselator to begin tesselating. The callback function
  specified in the constructor (or set using the SbTesselator::setCallback()
  method) will be called for each triangle before returning.
*/
void
SbTesselator::endPolygon()
{
  SbTVertex *v, *tmpv, *stv;
  int count;

  if (this->numVerts > 3) {
    calcPolygonNormal();
    
    // Find best projection plane
    int projection;
    if (fabs(polyNormal[0]) > fabs(polyNormal[1]))
      if (fabs(polyNormal[0]) > fabs(polyNormal[2]))
	projection=OYZ;
      else projection=OXY;
    else
      if (fabs(polyNormal[1]) > fabs(polyNormal[2]))
	projection=OXZ;
      else projection=OXY;
    
    switch (projection) {
    case OYZ:
      this->X=1;
      this->Y=2;
      polyDir=(int)(polyNormal[0]/fabs(polyNormal[0]));
      break;
    case OXY:
      this->X=0;
      this->Y=1;
      polyDir=(int)(polyNormal[2]/fabs(polyNormal[2]));
      break;
    case OXZ:
      this->X=2;
      this->Y=0;
      polyDir=(int)(polyNormal[1]/fabs(polyNormal[1]));
      break;
    }
    //Make loop
    this->tailV->next = this->headV;
    this->headV->prev = this->tailV;
    
    // add all vertices to heap.
    heap->emptyHeap();
    this->headV->heapidx = this->heap->add(this->headV);
    v = this->headV->next;
    while (v != this->headV) {
      v->heapidx = this->heap->add(v);
      v = v->next;
    }
    this->heap->buildHeap();

    while (this->numVerts >= 3) {
      v = (SbTVertex*) this->heap->getMin();
      if (heap_evaluate(v) == FLT_MAX) break;
      this->heap->remove(v->next->heapidx);

      this->emitTriangle(v); // will remove v->next
      this->numVerts--;
      
      // update heap
      v->prev->dirtyweight = 1;
      v->dirtyweight = 1;
      this->heap->newWeight(v->prev, v->prev->heapidx);
      this->heap->newWeight(v, v->heapidx);
    }
    
    // remember that headV and tailV are not valid anymore!
    
    // Emit the empty triangles that might lay around
    if (this->keepVertices) { 
      while (numVerts>=3) {
	this->emitTriangle(v);
	this->numVerts--;
      }
    }    
  }
  else if (this->numVerts==3) {   //only one triangle
    this->emitTriangle(headV);
  }
}

/*!
  Sets the callback function for this tesselator.
*/
void
SbTesselator::setCallback(void (*callback)(void *v0,
					   void *v1,
					   void *v2,
					   void *data),
			  void *data)
{
  this->callback = callback;
  this->callbackData = data;
}

//
// PRIVATE FUNCTIONS:
//

//
// Checks if the point p lies inside the triangle
// starting with t.
// Algorithm from comp.graphics.algorithms FAQ
//
SbBool
SbTesselator::pointInTriangle(SbTVertex *p, SbTVertex *t)
{
  float x,y;
  SbBool tst = FALSE;
  
  x = p->v[X];
  y = p->v[Y];

  SbVec3f &v1 = t->v;
  SbVec3f &v2 = t->next->next->v;
  if ((((v1[Y]<=y) && (y<v2[Y])) || ((v2[Y]<=y)  && (y<v1[Y]))) &&
      (x < (v2[X] - v1[X]) * (y - v1[Y]) /  (v2[Y] - v1[Y]) + v1[X]))
    tst = (tst==FALSE ? TRUE : FALSE);
  v2=v1;
  v1=t->next->v;
  if ((((v1[Y]<=y) && (y<v2[Y])) || ((v2[Y]<=y)  && (y<v1[Y]))) &&
      (x < (v2[X] - v1[X]) * (y - v1[Y]) /  (v2[Y] - v1[Y]) + v1[X]))
    tst = (tst==FALSE ? TRUE : FALSE);
  v2=v1;
  v1=t->next->next->v;
  if ((((v1[Y]<=y) && (y<v2[Y])) || ((v2[Y]<=y)  && (y<v1[Y]))) &&
      (x < (v2[X] - v1[X]) * (y - v1[Y]) /  (v2[Y] - v1[Y]) + v1[X]))
    tst = (tst==FALSE ? TRUE : FALSE);
  
  return tst;
}

//
// Check if v points to a legal triangle (normal is right way)
// (i.e convex or concave corner)
//
SbBool
SbTesselator::isTriangle(SbTVertex *v)
{
  if (((v->next->v[X]-v->v[X])*(v->next->next->v[Y]-v->v[Y])-
       (v->next->v[Y]-v->v[Y])*(v->next->next->v[X]-v->v[X]))*this->polyDir>0.0)
    return TRUE;
  else return FALSE;
}

//
// Check if there are no intersection to the triangle
// pointed to by v. (no other vertices are inside the triangle)
//
SbBool
SbTesselator::clippable(SbTVertex *v)
{
  SbTVertex *vtx=v->next->next->next;
  
  do {
    if (vtx!=v && vtx!=v->next && vtx!=v->next->next &&
	vtx->v!=v->v && vtx->v!=v->next->v && vtx->v!=v->next->next->v)
      if (pointInTriangle(vtx,v))
	return FALSE;
    vtx=vtx->next;
  } while (vtx!=v);
  return TRUE;
}

//
// Call the callback-function for the triangle starting with t
//
void
SbTesselator::emitTriangle(SbTVertex *t)
{
  SbTVertex *tmp;
  
  assert(t);
  assert(t->next);
  assert(t->next->next);
  assert(this->callback);

  this->callback(t->data, t->next->data, t->next->next->data,
		 this->callbackData);
  tmp = t->next;
  t->next = t->next->next;
  tmp->next->prev = t;
}

//
// Throw away triangle
//
void
SbTesselator::cutTriangle(SbTVertex *t)
{
  SbTVertex *tmp;
  tmp=t->next;
  t->next=t->next->next;
  tmp->next->prev = t;
}

//
// Return the area of the triangle starting with v
//
float
SbTesselator::area(SbTVertex *v)
{
  return fabs(((v->next->v[X]-v->v[X])*(v->next->next->v[Y]-v->v[Y])-
	       (v->next->v[Y]-v->v[Y])*(v->next->next->v[X]-v->v[X])));
}

//
// Returns the center of the circle through points a, b, c.
//
SbBool 
SbTesselator::circleCenter(const SbVec3f &a, const SbVec3f &b, 
			   const SbVec3f &c, float &cx, float &cy)
{
  float d1, d2, d3, c1, c2, c3;
  SbVec3f tmp1, tmp2;
  
  tmp1 = b - a;
  tmp2 = c - a;
  d1 = dot2D(tmp1, tmp2);
  
  tmp1 = b - c;
  tmp2 = a - c;
  d2 = dot2D(tmp1, tmp2);
  
  tmp1 = a - b;
  tmp2 = c - b;
  d3 = dot2D(tmp1, tmp2);

  c1 = d2 * d3;
  c2 = d3 * d1;
  c3 = d1 * d2;
  
  SbVec3f tmp4(c);
  tmp1 = a;
  tmp2 = b;
  tmp1 *= (c2+c3);
  tmp2 *= (c3+c1);
  tmp4 *= (c1+c2);
  tmp4 += tmp1;
  tmp4 += tmp2;

  float div = 2*(c1+c2+c3);
  if (div != 0.0f) {
    float val = 1.0f / div;
    cx = tmp4[this->X] * val;
    cy = tmp4[this->Y] * val;
    return TRUE;
  }
  return FALSE;
}

//
// Returns the square of the radius of the circle through a, b, c
//
float 
SbTesselator::circleSize(const SbVec3f &a, const SbVec3f &b, const SbVec3f &c)
{
  float cx, cy;
  if (circleCenter(a, b, c, cx, cy)) {  
    float t1, t2;
    t1 = a[this->X] - cx;
    t2 = a[this->Y] - cy;
    return t1*t1+t2*t2;
  }
  return FLT_MAX;
}   

float 
SbTesselator::circleSize(SbTVertex *v)
{
  return circleSize(v->v, v->next->v, v->next->next->v);
}

float 
SbTesselator::dot2D(const SbVec3f &v1, const SbVec3f &v2)
{
  return v1[this->X] * v2[this->X] + v1[this->Y] * v2[this->Y]; 
}

void 
SbTesselator::calcPolygonNormal()
{
  assert(this->numVerts > 3);
  
  this->polyNormal.setValue(0.0f, 0.0f, 0.0f);
  SbVec3f vert1, vert2;
  SbTVertex *currvertex = this->headV;
  vert2 = currvertex->v;
  
  while (currvertex->next != NULL && currvertex != tailV) {
    vert1 = vert2;
    vert2 = currvertex->next->v;
    this->polyNormal[0] += (vert1[1] - vert2[1]) * (vert1[2] + vert2[2]);
    this->polyNormal[1] += (vert1[2] - vert2[2]) * (vert1[0] + vert2[0]);
    this->polyNormal[2] += (vert1[0] - vert2[0]) * (vert1[1] + vert2[1]);
    currvertex = currvertex->next;
  }
  vert1 = vert2;
  vert2 = headV->v;
  polyNormal[0] += (vert1[1] - vert2[1]) * (vert1[2] + vert2[2]);
  polyNormal[1] += (vert1[2] - vert2[2]) * (vert1[0] + vert2[0]);
  polyNormal[2] += (vert1[0] - vert2[0]) * (vert1[1] + vert2[1]);      
  
  polyNormal.normalize();
}

//
// makes sure SbTVertexes are not allocated and deallocated
// all the time, by storing them in a growable array. This
// way, the SbTVertexes will not be deleted until the tesselator
// is destructed, and SbTVertexes can be reused.
//
struct SbTVertex *
SbTesselator::newVertex()
{
  assert(this->currVertex <= this->vertexStorage.getLength());
  if (this->currVertex == this->vertexStorage.getLength()) {
    struct SbTVertex *v = new SbTVertex;
    this->vertexStorage.append(v);
  }
  return this->vertexStorage[currVertex++];
}

void 
SbTesselator::cleanUp()
{
  this->headV = this->tailV = NULL;
  this->currVertex = 0;
}
