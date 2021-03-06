#include <sstream>
#include <iomanip>
#define _USE_MATH_DEFINES
#include <cmath>

#include <boost/mpl/for_each.hpp>

#include <core/object.h>
#include <core/triangles.h>
#include <core/normals.h>
#include <core/polygons.h>
#include <core/spheres.h>
#include <core/tubes.h>
#include <core/texture1d.h>

#include "ToTriangles.h"

MODULE_MAIN(ToTriangles)

using namespace vistle;

ToTriangles::ToTriangles(const std::string &name, int moduleID, mpi::communicator comm)
   : Module("transform Polygons into Triangles", name, moduleID, comm) {

   createInputPort("grid_in");
   createOutputPort("grid_out");
}

ToTriangles::~ToTriangles() {

}

template<int Dim>
struct ReplicateData {
   DataBase::const_ptr object;
   DataBase::ptr &result;
   Index n;
   Index nElem;
   const Index *const el;
   Index nStart, nEnd;
   ReplicateData(DataBase::const_ptr obj, DataBase::ptr &result, Index n, Index nElem, Index *el, Index nStart, Index nEnd)
      : object(obj)
      , result(result)
      , n(n)
      , nElem(nElem)
      , el(el)
      , nStart(nStart)
      , nEnd(nEnd)
      {
         vassert(nElem==0 || el);
      }
   template<typename S> void operator()(S) {

      typedef Vec<S,Dim> V;
      typename V::const_ptr in(V::as(object));
      if (!in)
         return;

      typename V::ptr out(new V(in->getSize()*n+nElem*(nStart+nEnd)));
      for (int i=0; i<Dim; ++i) {
         auto din = &in->x(i)[0];
         auto dout = out->x(i).data();

         const Index N = in->getSize();
         if (el) {
            for (Index e=0; e<nElem; ++e) {
               const Index start = el[e], end = el[e+1];
               for (Index k=0; k<nStart; ++k) {
                  *dout++ = *din;
               }
               for (Index i=start; i<end; ++i) {
                  for (Index k=0; k<n; ++k) {
                     *dout++ = *din;
                  }
                  ++din;
               }
               for (Index k=0; k<nEnd; ++k) {
                  *dout++ = *(din-1);
               }
            }
         } else {
            for (Index j=0; j<N; ++j) {
               for (Index k=0; k<n; ++k) {
                  *dout++ = *din;
               }
               ++din;
            }
         }
      }
      result = out;
   }
};

DataBase::ptr replicateData(DataBase::const_ptr src, Index n, Index nElem=0, Index *el=nullptr, Index nStart=0, Index nEnd=0) {

   DataBase::ptr result;
   boost::mpl::for_each<Scalars>(ReplicateData<1>(src, result, n, nElem, el, nStart, nEnd));
   boost::mpl::for_each<Scalars>(ReplicateData<3>(src, result, n, nElem, el, nStart, nEnd));
   if (auto tex = Texture1D::as(src)) {
       auto vec1 = Vec<Scalar, 1>::as(Object::as(result));
       vassert(vec1);
       auto result2 = tex->clone();
       result2->d()->x[0] = vec1->d()->x[0];
       result = result2;
   }
   return result;
}

bool ToTriangles::compute() {

   auto data = expect<DataBase>("grid_in");
   if (!data) {
      return true;
   }
   auto obj = data->grid();
   if (!obj) {
      obj = data;
      data.reset();
   }

   // pass through triangles
   if (auto tri = Triangles::as(obj)) {

       passThroughObject("grid_out", tri);
       if (data)
          passThroughObject("data_out", data);
       return true;
   }

   // transform the rest, if possible
   Triangles::ptr tri;
   DataBase::ptr ndata;

   if (auto poly = Polygons::as(obj)) {

      Index nelem = poly->getNumElements();
      Index nvert = poly->getNumCorners();
      Index ntri = nvert-2*nelem;

      tri.reset(new Triangles(3*ntri, 0));
      for (int i=0; i<3; ++i)
         tri->d()->x[i] = poly->d()->x[i];

      Index i = 0;
      auto el = &poly->el()[0];
      auto cl = &poly->cl()[0];
      auto tcl = &tri->cl()[0];
      for (Index e=0; e<nelem; ++e) {
         const Index begin=el[e], end=el[e+1], last=end-1;
         const Index N = end - begin;
         for (Index v=0; v<N-2; ++v) {
            const Index v2 = v/2;
            if (v%2) {
               tcl[i++] = cl[begin+v2+1];
               tcl[i++] = cl[last-v2-1];
               tcl[i++] = cl[last-v2];
            } else {
               tcl[i++] = cl[begin+v2+1];
               tcl[i++] = cl[begin+v2];
               tcl[i++] = cl[last-v2];
            }
         }
      }
      vassert(i == 3*ntri);
   }  else  if (auto sphere = Spheres::as(obj)) {

      const int NumLat = 8;
      const int NumLong = 13;
      static_assert(NumLat >= 3, "too few vertices");
      static_assert(NumLong >= 3, "too few vertices");
      Index TriPerSphere = NumLong * (NumLat-2) * 2;
      Index CoordPerSphere = NumLong * (NumLat - 2) + 2;

      Index n = sphere->getNumSpheres();
      auto x = &sphere->x()[0];
      auto y = &sphere->y()[0];
      auto z = &sphere->z()[0];
      auto r = &sphere->r()[0];

      tri.reset(new Triangles(n*3*TriPerSphere, n*CoordPerSphere));
      auto tx = tri->x().data();
      auto ty = tri->y().data();
      auto tz = tri->z().data();
      auto ti = tri->cl().data();

      Normals::ptr norm(new Normals(n*CoordPerSphere));
      auto nx = norm->x().data();
      auto ny = norm->y().data();
      auto nz = norm->z().data();

      const float psi = M_PI / (NumLat-1);
      const float phi = M_PI * 2 / NumLong;
      for (Index i=0; i<n; ++i) {

         // create normals
         { 
            Index ci = i*CoordPerSphere;
            // south pole
            nx[ci] = ny[ci] = 0.f;
            nz[ci] = -1.f;
            ++ci;

            float Psi = -M_PI*0.5+psi;
            for (Index j=0; j<NumLat-2; ++j) {
               float Phi = j*0.5f*phi;
               for (Index k=0; k<NumLong; ++k) {
                  nx[ci] = sin(Phi) * cos(Psi);
                  ny[ci] = cos(Phi) * cos(Psi);
                  nz[ci] = sin(Psi);
                  ++ci;
                  Phi += phi;
               }
               Psi += psi;
            }
            // north pole
            nx[ci] = ny[ci] = 0.f;
            nz[ci] = 1.f;
            ++ci;
            vassert(ci == (i+1)*CoordPerSphere);
         }

         // create coordinates from normals
         for (Index ci = i*CoordPerSphere; ci<(i+1)*CoordPerSphere; ++ci) {
            tx[ci] = nx[ci] * r[i] + x[i];
            ty[ci] = ny[ci] * r[i] + y[i];
            tz[ci] = nz[ci] * r[i] + z[i];
         }

         // create index list
         {
            Index ii = i*3*TriPerSphere;
            // indices for ring around south pole
            Index ci = i*CoordPerSphere+1;
            for (Index k=0; k<NumLong; ++k) {
               ti[ii++] = ci+(k+1)%NumLong;
               ti[ii++] = ci+k;
               ti[ii++] = i*CoordPerSphere;
            }

            ci = i*CoordPerSphere+1;
            for (Index j=0; j<NumLat-3; ++j) {
               for (Index k=0; k<NumLong; ++k) {

                  ti[ii++] = ci+k;
                  ti[ii++] = ci+(k+1)%NumLong;
                  ti[ii++] = ci+k+NumLong;
                  ti[ii++] = ci+(k+1)%NumLong;
                  ti[ii++] = ci+NumLong+(k+1)%NumLong;
                  ti[ii++] = ci+k+NumLong;
               }
               ci += NumLong;
            }
            vassert(ci == i*CoordPerSphere + 1 + NumLong*(NumLat-3));
            vassert(ci + NumLong + 1 == (i+1)*CoordPerSphere);

            // indices for ring around north pole
            for (Index k=0; k<NumLong; ++k) {
               ti[ii++] = ci+k;
               ti[ii++] = ci+(k+1)%NumLong;
               ti[ii++] = (i+1)*CoordPerSphere-1;
            }
            vassert(ii == (i+1)*3*TriPerSphere);

            for (Index j=0; j<3*TriPerSphere; ++j) {
               vassert(ti[i*3*TriPerSphere + j] >= i*CoordPerSphere);
               vassert(ti[i*3*TriPerSphere + j] < (i+1)*CoordPerSphere);
            }
         }
      }
      norm->setMeta(obj->meta());
      tri->setNormals(norm);

      if (data) {
         ndata = replicateData(data, CoordPerSphere);
      }
   } else  if (auto tube = Tubes::as(obj)) {

      const int NumSect = 5;
      static_assert(NumSect >= 3, "too few sectors");
      Index TriPerSection = NumSect * 2;

      Index n = tube->getNumTubes();
      Index s = tube->getNumCoords();
      auto x = &tube->x()[0];
      auto y = &tube->y()[0];
      auto z = &tube->z()[0];
      auto r = &tube->r()[0];
      auto el = tube->components().data();
      const auto startStyle = tube->startStyle();
      const auto endStyle = tube->endStyle();

      Index numCoordStart = 0, numCoordEnd = 0;
      Index numIndStart = 0, numIndEnd = 0;
      if (startStyle != Tubes::Open) {
         numCoordStart = 1+NumSect;
         numIndStart = 3*NumSect;
      }
      if (endStyle == Tubes::Arrow) {
         numCoordEnd = 3*NumSect;
         numIndEnd = 3*3*NumSect;
      } else if (endStyle == Tubes::Flat) {
         numCoordEnd = 1+NumSect;
         numIndEnd = 3*NumSect;
      }

      const Index numSeg = (s-n)*3*TriPerSection+n*(numIndStart+numIndEnd);
      const Index numCoord = numSeg > 0 ? s*NumSect+n*(numCoordStart+numCoordEnd) : 0;
      tri.reset(new Triangles(numSeg, numCoord));
      auto tx = tri->x().data();
      auto ty = tri->y().data();
      auto tz = tri->z().data();
      auto ti = tri->cl().data();

      Normals::ptr norm(new Normals(numCoord));
      vassert(norm->getSize() == tri->getSize());
      auto nx = norm->x().data();
      auto ny = norm->y().data();
      auto nz = norm->z().data();

      Index ci = 0; // coord index
      Index ii = 0; // index index
      if (numCoord > 0) {
         for (Index i=0; i<n; ++i) {
            const Index begin = el[i], end = el[i+1];

            Vector normal;
            for (Index k=begin; k<end; ++k) {

               Vector cur(x[k], y[k], z[k]);
               Vector next = k+1<end ? Vector(x[k+1], y[k+1], z[k+1]) : Vector(0., 0., 0.);

               Vector l1 = next - cur;
               Vector dir;
               if (k == begin) {
                  dir = l1.normalized();
               } else {
                  Vector l2(x[k]-x[k-1], y[k]-y[k-1], z[k]-z[k-1]);
                  if (k+1 == end) {
                     dir = l2.normalized();
                  } else {
                     dir = (l1.normalized()+l2.normalized()).normalized();
                  }
               }

               if (k == begin) {
                  normal = dir.cross(Vector(0,0,1)).normalized();
               } else {
                  normal = (normal-dir.dot(normal)*dir).normalized();
               }

               Quaternion qrot(AngleAxis(2.*M_PI/NumSect, dir));
               const auto rot = qrot.toRotationMatrix();
               const auto rot2 = Quaternion(AngleAxis(M_PI/NumSect, dir)).toRotationMatrix();

               // start cap
               if (k == begin && startStyle != Tubes::Open) {
                  tx[ci] = cur[0];
                  ty[ci] = cur[1];
                  tz[ci] = cur[2];
                  nx[ci] = dir[0];
                  ny[ci] = dir[1];
                  nz[ci] = dir[2];
                  ++ci;

                  for (Index l=0; l<NumSect; ++l) {
                     ti[ii++] = ci-1;
                     ti[ii++] = ci+l;
                     ti[ii++] = ci+(l+1)%NumSect;
                  }

                  Vector rad = normal;
                  for (Index l=0; l<NumSect; ++l) {
                     nx[ci] = dir[0];
                     ny[ci] = dir[1];
                     nz[ci] = dir[2];
                     Vector p = cur+r[k]*rad;
                     rad = rot * rad;
                     tx[ci] = p[0];
                     ty[ci] = p[1];
                     tz[ci] = p[2];
                     ++ci;
                  }
               }

               // indices
               if (k+1 < end) {
                  for (Index l=0; l<NumSect; ++l) {
                     ti[ii++] = ci+l;
                     ti[ii++] = ci+(l+1)%NumSect;
                     ti[ii++] = ci+(l+1)%NumSect+NumSect;
                     ti[ii++] = ci+l;
                     ti[ii++] = ci+(l+1)%NumSect+NumSect;
                     ti[ii++] = ci+l+NumSect;
                  }
               }

               // coordinates and normals
               for (Index l=0; l<NumSect; ++l) {
                  nx[ci] = normal[0];
                  ny[ci] = normal[1];
                  nz[ci] = normal[2];
                  Vector p = cur+r[k]*normal;
                  normal = rot * normal;
                  tx[ci] = p[0];
                  ty[ci] = p[1];
                  tz[ci] = p[2];
                  ++ci;
               }

               // end cap/arrow
               if (k+1 == end) {
                  if (endStyle==Tubes::Arrow) {

                     Index tipStart = ci;
                     for (Index l=0; l<NumSect; ++l) {
                        tx[ci] = tx[ci-NumSect];
                        ty[ci] = ty[ci-NumSect];
                        tz[ci] = tz[ci-NumSect];
                        nx[ci] = dir[0];
                        ny[ci] = dir[1];
                        nz[ci] = dir[2];
                        ++ci;
                     }

                     Scalar tipSize = 2.0;

                     Vector tip = cur + tipSize*dir*r[k];
                     for (Index l=0; l<NumSect; ++l) {
                        Vector norm = (normal+dir).normalized();
                        Vector p = cur+tipSize*r[k]*normal;
                        normal = rot * normal;

                        nx[ci] = norm[0];
                        ny[ci] = norm[1];
                        nz[ci] = norm[2];
                        tx[ci] = p[0];
                        ty[ci] = p[1];
                        tz[ci] = p[2];
                        ++ci;
                     }

                     normal = rot2 * normal;
                     for (Index l=0; l<NumSect; ++l) {
                        Vector norm = (normal+dir).normalized();
                        normal = rot * normal;

                        nx[ci] = norm[0];
                        ny[ci] = norm[1];
                        nz[ci] = norm[2];
                        tx[ci] = tip[0];
                        ty[ci] = tip[1];
                        tz[ci] = tip[2];
                        ++ci;
                     }

                     for (Index l=0; l<NumSect; ++l) {
                        ti[ii++] = tipStart+l;
                        ti[ii++] = tipStart+(l+1)%NumSect;
                        ti[ii++] = tipStart+NumSect+(l+1)%NumSect;

                        ti[ii++] = tipStart+NumSect+(l+1)%NumSect;
                        ti[ii++] = tipStart+NumSect+l;
                        ti[ii++] = tipStart+l;

                        ti[ii++] = tipStart+NumSect+l;
                        ti[ii++] = tipStart+NumSect+(l+1)%NumSect;
                        ti[ii++] = tipStart+2*NumSect+l;
                     }
                  } else if (endStyle == Tubes::Flat) {

                     for (Index l=0; l<NumSect; ++l) {
                        tx[ci] = tx[ci-NumSect];
                        ty[ci] = ty[ci-NumSect];
                        tz[ci] = tz[ci-NumSect];
                        nx[ci] = dir[0];
                        ny[ci] = dir[1];
                        nz[ci] = dir[2];
                        ++ci;
                     }

                     tx[ci] = cur[0];
                     ty[ci] = cur[1];
                     tz[ci] = cur[2];
                     nx[ci] = dir[0];
                     ny[ci] = dir[1];
                     nz[ci] = dir[2];
                     for (Index l=0; l<NumSect; ++l) {
                        ti[ii++] = ci-NumSect+l;
                        ti[ii++] = ci-NumSect+(l+1)%NumSect;
                        ti[ii++] = ci;
                     }
                     ++ci;
                  }
               }
            }
         }
      }
      vassert(ci == numCoord);
      vassert(ii == numSeg);

      norm->setMeta(obj->meta());
      tri->setNormals(norm);

      if (data) {
         ndata = replicateData(data, NumSect, n, el, numCoordStart, numCoordEnd);
      }
   }

   if (tri) {
      tri->setMeta(obj->meta());
      tri->copyAttributes(obj);

      if (data) {
         if (!ndata)
            ndata = data->clone();
         ndata->setMeta(data->meta());
         ndata->copyAttributes(data);
         ndata->setGrid(tri);
         addObject("grid_out", ndata);
      } else {
         addObject("grid_out", tri);
      }
   }

   return true;
}
