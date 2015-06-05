// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

//#include "../common/tutorial/tutorial_device.isph"

#include "linalg/math.isph"
#include "linalg/vec3f.isph"
#include "linalg/affinespace.isph"

#include <embree2/rtcore.isph>
//#include <embree2/rtcore_ray.isph>
#include "rtcray.isph"
#include "render.isph"
#include "spheres.isph"

// ======================================================================== //
//                     User defined sphere geometry                         //
// ======================================================================== //

void sphereBoundsFunc(const uniform RenderObjectData* uniform data, uniform size_t item, uniform RTCBounds* uniform bounds_o)
{
  const uniform Sphere& sphere = data->spheres[item];
  bounds_o->lower_x = sphere.p.x-sphere.r;
  bounds_o->lower_y = sphere.p.y-sphere.r;
  bounds_o->lower_z = sphere.p.z-sphere.r;
  bounds_o->upper_x = sphere.p.x+sphere.r;
  bounds_o->upper_y = sphere.p.y+sphere.r;
  bounds_o->upper_z = sphere.p.z+sphere.r;
}

void sphereIntersectFunc(const uniform RenderObjectData* uniform data, varying RTCRay& ray, uniform size_t item)
{
  const uniform Sphere& sphere = data->spheres[item];
  const Vec3f v = ray.org-sphere.p;
  const float A = dot(ray.dir,ray.dir);
  const float B = 2.0f*dot(v,ray.dir);
  const float C = dot(v,v) - sqr(sphere.r);
  const float D = B*B - 4.0f*A*C;
  if (D < 0.0f) return;
  const float Q = sqrt(D);
  const float rcpA = rcp(A);
  const float t0 = 0.5f*rcpA*(-B-Q);
  const float t1 = 0.5f*rcpA*(-B+Q);
  if ((ray.tnear < t0) & (t0 < ray.tfar)) {
    ray.u = 0.0f;
    ray.v = 0.0f;
    ray.tfar = t0;
    ray.geomID = data->geomId;
    ray.primID = item;
    ray.Ng = ray.org+t0*ray.dir-sphere.p;
  }
  if ((ray.tnear < t1) & (t1 < ray.tfar)) {
    ray.u = 0.0f;
    ray.v = 0.0f;
    ray.tfar = t1;
    ray.geomID = data->geomId;
    ray.primID = item;
    ray.Ng = ray.org+t1*ray.dir-sphere.p;
  }
}

void sphereOccludedFunc(const uniform RenderObjectData* uniform data, varying RTCRay& ray, uniform size_t item)
{
  const uniform Sphere& sphere = data->spheres[item];
  const Vec3f v = ray.org-sphere.p;
  const float A = dot(ray.dir,ray.dir);
  const float B = 2.0f*dot(v,ray.dir);
  const float C = dot(v,v) - sqr(sphere.r);
  const float D = B*B - 4.0f*A*C;
  if (D < 0.0f) return;
  const float Q = sqrt(D);
  const float rcpA = rcp(A);
  const float t0 = 0.5f*rcpA*(-B-Q);
  const float t1 = 0.5f*rcpA*(-B+Q);
  if ((ray.tnear < t0) & (t0 < ray.tfar)) {
    ray.geomID = 0;
  }
  if ((ray.tnear < t1) & (t1 < ray.tfar)) {
    ray.geomID = 0;
  }
}

export uniform unsigned int registerSpheres(uniform RTCScene scene, uniform RenderObjectData *uniform data, uniform size_t N) {

  assert(data->spheres);

  const uniform unsigned int geomID = rtcNewUserGeometry(scene,N);
  data->geomId = geomID;
  rtcSetUserData(scene, geomID, data);
  rtcSetBoundsFunction(scene, geomID, (RTCBoundsFunc)&sphereBoundsFunc);
  rtcSetIntersectFunction(scene, geomID, (RTCIntersectFuncVarying)&sphereIntersectFunc);
  rtcSetOccludedFunction(scene, geomID, (RTCOccludedFuncVarying)&sphereOccludedFunc);
  return geomID;
}