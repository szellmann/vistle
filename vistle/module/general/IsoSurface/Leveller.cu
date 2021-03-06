﻿//
//This code is used for both IsoCut and IsoSurface!
//

#include <sstream>
#include <iomanip>
#include <core/index.h>
#include <core/scalar.h>
#include <core/unstr.h>
#include <core/triangles.h>
#include <core/shm.h>
#include <thrust/execution_policy.h>
#include <thrust/device_vector.h>
#include <thrust/transform.h>
#include <thrust/for_each.h>
#include <thrust/scan.h>
#include <thrust/sequence.h>
#include <thrust/copy.h>
#include <thrust/count.h>
#include <thrust/iterator/zip_iterator.h>
#include <thrust/iterator/constant_iterator.h>
#include <thrust/tuple.h>
#include "tables.h"


#include "Leveller.h"

using namespace vistle;


const int MaxNumData = 6;


struct HostData {

   Scalar m_isovalue;
   int m_numInVertData, m_numInVertDataI;
   int m_numInCellData, m_numInCellDataI;
   IsoDataFunctor m_isoFunc;
   const Index *m_el;
   const Index *m_cl;
   const unsigned char *m_tl;
   std::vector<Index> m_caseNums;
   std::vector<Index> m_numVertices;
   std::vector<Index> m_LocationList;
   std::vector<Index> m_ValidCellVector;
   Index m_nvert[3];
   Index m_nghost[3][2];
   std::vector<vistle::shm_ref<vistle::shm_array<Scalar, shm<Scalar>::allocator>>> m_outVertData, m_outCellData;
   std::vector<vistle::shm_ref<vistle::shm_array<Index, shm<Index>::allocator>>> m_outVertDataI, m_outCellDataI;
   std::vector<const Scalar*> m_inVertPtr, m_inCellPtr;
   std::vector<const Index*> m_inVertPtrI, m_inCellPtrI;
   std::vector<Scalar*> m_outVertPtr, m_outCellPtr;
   std::vector<Index *> m_outVertPtrI, m_outCellPtrI;
   bool m_haveCoords;
   bool m_computeNormals;

   typedef const u_char *TypeIterator;
   typedef const Index *IndexIterator;
   typedef std::vector<Index>::iterator VectorIndexIterator;

   HostData(Scalar isoValue
            , IsoDataFunctor isoFunc
            , const Index *el
            , const unsigned char *tl
            , const Index *cl
            , const Scalar *x
            , const Scalar *y
            , const Scalar *z
            )
      : m_isovalue(isoValue)
      , m_numInVertData(0)
      , m_numInVertDataI(0)
      , m_numInCellData(0)
      , m_numInCellDataI(0)
      , m_isoFunc(isoFunc)
      , m_el(el)
      , m_cl(cl)
      , m_tl(tl)
      , m_nvert{0,0,0}
      , m_nghost{{0,0}, {0,0}, {0,0}}
      , m_haveCoords(true)
      , m_computeNormals(false)
   {
      addmappeddata(x ? &x[0] : (Scalar *)nullptr);
      addmappeddata(y ? &y[0] : (Scalar *)nullptr);
      addmappeddata(z ? &z[0] : (Scalar *)nullptr);
   }

   HostData(Scalar isoValue
            , IsoDataFunctor isoFunc
            , Index nx
            , Index ny
            , Index nz
            , const Scalar *x
            , const Scalar *y
            , const Scalar *z
            )
      : m_isovalue(isoValue)
      , m_numInVertData(0)
      , m_numInVertDataI(0)
      , m_numInCellData(0)
      , m_numInCellDataI(0)
      , m_isoFunc(isoFunc)
      , m_el(nullptr)
      , m_cl(nullptr)
      , m_tl(nullptr)
      , m_nvert{nx,ny,nz}
      , m_nghost{{0,0}, {0,0}, {0,0}}
      , m_haveCoords(false)
      , m_computeNormals(false)
   {
      // allocate storage for normals
      addmappeddata((Scalar *)nullptr);
      addmappeddata((Scalar *)nullptr);
      addmappeddata((Scalar *)nullptr);

      addmappeddata(x ? &x[0] : (Scalar *)nullptr);
      addmappeddata(y ? &y[0] : (Scalar *)nullptr);
      addmappeddata(z ? &z[0] : (Scalar *)nullptr);
   }

   void setHaveCoords(bool val) {
       m_haveCoords = val;
   }

   void setGhostLayers(Index ghost[3][2]) {
       for (int c=0; c<3; ++c) {
           for (int i=0; i<2; ++i)
               m_nghost[c][i] = ghost[c][i];
       }
   }

   void setComputeNormals(bool val) {
       m_computeNormals = val;
   }

   void addmappeddata(const Scalar *mapdata){

      m_inVertPtr.push_back(mapdata);
      m_outVertData.emplace_back(vistle::ShmVector<Scalar>::create(0));
      m_outVertPtr.push_back(NULL);
      m_numInVertData = m_inVertPtr.size();
   }

   void addmappeddata(const Index *mapdata){

      m_inVertPtrI.push_back(mapdata);
      m_outVertDataI.push_back(vistle::ShmVector<Index>::create(0));
      m_outVertPtrI.push_back(NULL);
      m_numInVertDataI = m_inVertPtrI.size();
   }

   void addcelldata(const Scalar *mapdata){

      m_inCellPtr.push_back(mapdata);
      m_outCellData.push_back(vistle::ShmVector<Scalar>::create(0));
      m_outCellPtr.push_back(NULL);
      m_numInCellData = m_inCellPtr.size();
   }

   void addcelldata(const Index *mapdata){

      m_inCellPtrI.push_back(mapdata);
      m_outCellDataI.push_back(vistle::ShmVector<Index>::create(0));
      m_outCellPtrI.push_back(NULL);
      m_numInCellDataI = m_inCellPtrI.size();
   }
};

struct DeviceData {

   Scalar m_isovalue;
   int m_numInVertData, m_numInVertDataI;
   int m_numInCellData, m_numInCellDataI;
   IsoDataFunctor m_isoFunc;
   thrust::device_vector<Index> m_el;
   thrust::device_vector<Index> m_cl;
   thrust::device_vector<unsigned char> m_tl;
   thrust::device_vector<Index> m_caseNums;
   thrust::device_vector<Index> m_numVertices;
   thrust::device_vector<Index> m_LocationList;
   thrust::device_vector<Index> m_ValidCellVector;
   thrust::device_vector<Scalar> m_x;
   thrust::device_vector<Scalar> m_y;
   thrust::device_vector<Scalar> m_z;
   std::vector<thrust::device_vector<Scalar> *> m_outVertData, m_outCellData;
   std::vector<thrust::device_vector<Index> *> m_outVertDataI, m_outCellDataI;
   std::vector<thrust::device_ptr<Scalar> > m_inVertPtr, m_inCellPtr;
   std::vector<thrust::device_ptr<Index> > m_inVertPtrI, m_inCellPtrI;
   std::vector<thrust::device_ptr<Scalar> > m_outVertPtr, m_outCellPtr;
   std::vector<thrust::device_ptr<Index> > m_outVertPtrI, m_outCellPtrI;
   typedef const Index *IndexIterator;
   //typedef thrust::device_vector<Index>::iterator IndexIterator;

   DeviceData(Scalar isoValue
              , IsoDataFunctor isoFunc
              , Index nelem
              , const Index *el
              , const unsigned char *tl
              , Index nconn
              , const Index *cl
              , Index ncoord
              , const Scalar *x
              , const Scalar *y
              , const Scalar *z)
   : m_isovalue(isoValue)
   , m_isoFunc(isoFunc)
   , m_el(el, el+nelem)
   , m_cl(cl, cl+nconn)
   , m_tl(tl, tl+nelem)
   , m_x(x, x+ncoord)
   , m_y(y, y+ncoord)
   , m_z(z, z+ncoord)
   {
      m_inVertPtr.push_back(m_x.data());
      m_inVertPtr.push_back(m_y.data());
      m_inVertPtr.push_back(m_z.data());

      for(size_t i = 0; i < m_inVertPtr.size(); i++){
         m_outVertData.push_back(new thrust::device_vector<Scalar>);
      }
      m_outVertPtr.resize(m_inVertPtr.size());
      m_numInVertData = m_inVertPtr.size();
      for(size_t i = 0; i < m_inVertPtrI.size(); i++){
         m_outVertDataI.push_back(new thrust::device_vector<Index>);
      }
      m_outVertPtrI.resize(m_inVertPtrI.size());
      m_numInVertDataI = m_inVertPtrI.size();
   }
};

template<class Data>
struct process_Cell {
   process_Cell(Data &data) : m_data(data) {
      for (int i = 0; i < m_data.m_numInVertData; i++){
         m_data.m_outVertPtr[i] = m_data.m_outVertData[i]->data();
      }
      for (int i = 0; i < m_data.m_numInVertDataI; i++){
         m_data.m_outVertPtrI[i] = m_data.m_outVertDataI[i]->data();
      }
      for (int i = 0; i < m_data.m_numInCellData; i++){
         m_data.m_outCellPtr[i] = m_data.m_outCellData[i]->data();
      }
      for (int i = 0; i < m_data.m_numInCellDataI; i++){
         m_data.m_outCellPtrI[i] = m_data.m_outCellDataI[i]->data();
      }
   }

   Data &m_data;

   __host__ __device__
   void operator()(Index ValidCellIndex) {

      const Index CellNr = m_data.m_ValidCellVector[ValidCellIndex];

      for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]/3; idx++) {
          Index outcellindex = m_data.m_LocationList[ValidCellIndex]/3+idx;
          for(int j = 0; j < m_data.m_numInCellData; j++) {
              m_data.m_outCellPtr[j][outcellindex] = m_data.m_inCellPtr[j][CellNr];
          }
          for(int j = 0; j < m_data.m_numInCellDataI; j++) {
              m_data.m_outCellPtrI[j][outcellindex] = m_data.m_inCellPtrI[j][CellNr];
          }
      }

      if (m_data.m_el) {
      const Index Cellbegin = m_data.m_el[CellNr];
      const Index Cellend = m_data.m_el[CellNr+1];
      const auto &cl = &m_data.m_cl[Cellbegin];

#define INTER(nc, triTable, edgeTable) \
    const unsigned int edge = triTable[m_data.m_caseNums[ValidCellIndex]][idx]; \
    const unsigned int v1 = edgeTable[0][edge]; \
    const unsigned int v2 = edgeTable[1][edge]; \
    const Scalar t = tinterp(m_data.m_isovalue, field[v1], field[v2]); \
    Index outvertexindex = m_data.m_LocationList[ValidCellIndex]+idx; \
    for(int j = nc; j < m_data.m_numInVertData; j++) { \
        m_data.m_outVertPtr[j][outvertexindex] = \
            lerp(m_data.m_inVertPtr[j][cl[v1]], m_data.m_inVertPtr[j][cl[v2]], t); \
    } \
    for(int j = 0; j < m_data.m_numInVertDataI; j++) { \
        m_data.m_outVertPtrI[j][outvertexindex] = \
            lerp(m_data.m_inVertPtrI[j][cl[v1]], m_data.m_inVertPtrI[j][cl[v2]], t); \
    }

      switch (m_data.m_tl[CellNr] & ~UnstructuredGrid::CONVEX_BIT) {

         case UnstructuredGrid::HEXAHEDRON: {

            Scalar field[8];
            for (int idx = 0; idx < 8; idx ++) {
               field[idx] = m_data.m_isoFunc(cl[idx]);
            }

            for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
               INTER(0, hexaTriTable, hexaEdgeTable);
            }
            break;
         }

         case UnstructuredGrid::TETRAHEDRON: {

            Scalar field[4];
            for (int idx = 0; idx < 4; idx ++) {
               field[idx] = m_data.m_isoFunc(cl[idx]);
            }

            for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
               INTER(0, tetraTriTable, tetraEdgeTable);
            }
            break;
         }

         case UnstructuredGrid::PYRAMID: {

            Scalar field[5];
            for (int idx = 0; idx < 5; idx ++) {
               field[idx] = m_data.m_isoFunc(cl[idx]);
            }

            for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
                INTER(0, pyrTriTable, pyrEdgeTable);
            }
            break;
         }

         case UnstructuredGrid::PRISM: {

            Scalar field[6];
            for (int idx = 0; idx < 6; idx ++) {
               field[idx] = m_data.m_isoFunc(cl[idx]);
            }

            for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
                INTER(0, prismTriTable, prismEdgeTable);
            }
            break;
         }

         case UnstructuredGrid::POLYHEDRON: {
            /* find all iso-points on each edge of each face,
               build a triangle for each consecutive pair and a center point,
               orient outwards towards smaller values */

            const auto &cl = m_data.m_cl;

            const Index numVert = m_data.m_numVertices[ValidCellIndex];
            Index numAvg = 0;
            Scalar middleData[MaxNumData];
            Index middleDataI[MaxNumData];
            for(int i = 0; i < MaxNumData; i++ ){
               middleData[i] = 0;
               middleDataI[i] = 0;
            };
            Scalar cd1[MaxNumData], cd2[MaxNumData];
            Index cd1I[MaxNumData], cd2I[MaxNumData];

            Index outIdx = m_data.m_LocationList[ValidCellIndex];
            for (Index i = Cellbegin; i < Cellend; i += cl[i]+1) {

               const Index nvert = cl[i];
               Index c1 = cl[i+nvert];
               bool flipped = false, haveIsect = false;
               for (Index k=i+1; k<i+nvert+1; ++k) {
                   const Index c2 = cl[k];

                   for(int i = 0; i < m_data.m_numInVertData; i++){
                       cd1[i] = m_data.m_inVertPtr[i][c1];
                       cd2[i] = m_data.m_inVertPtr[i][c2];
                   }
                   for(int i = 0; i < m_data.m_numInVertDataI; i++){
                       cd1I[i] = m_data.m_inVertPtrI[i][c1];
                       cd2I[i] = m_data.m_inVertPtrI[i][c2];
                   }

                   Scalar d1 = m_data.m_isoFunc(c1);
                   Scalar d2 = m_data.m_isoFunc(c2);

                   bool smallToBig = d1 <= m_data.m_isovalue && d2 > m_data.m_isovalue;
                   bool bigToSmall = d1 > m_data.m_isovalue && d2 <= m_data.m_isovalue;

                   if (smallToBig || bigToSmall) {
                       if (!haveIsect) {
                           flipped = bigToSmall;
                           haveIsect = true;
                       }
                       Index out = outIdx;
                       if (flipped) {
                           if (bigToSmall)
                               out += 1;
                           else
                               out -= 1;
                       }
                       Scalar t = tinterp(m_data.m_isovalue, d1, d2);
                       for(int i = 0; i < m_data.m_numInVertData; i++) {
                           Scalar v = lerp(cd1[i], cd2[i], t);
                           middleData[i] += v;
                           m_data.m_outVertPtr[i][out] = v;
                       }
                       for(int i = 0; i < m_data.m_numInVertDataI; i++){
                           Index vI = lerp(cd1I[i], cd2I[i], t);
                           middleDataI[i] += vI;
                           m_data.m_outVertPtrI[i][out] = vI;
                       }

                       ++outIdx;
                       if (bigToSmall^flipped)
                           ++outIdx;
                       ++numAvg;
                   }

                   c1 = c2;
               }
            }
            if (numAvg > 0) {
                for(int i = 0; i < m_data.m_numInVertData; i++){
                    middleData[i] /= numAvg;
                }
                for(int i = 0; i < m_data.m_numInVertDataI; i++){
                    middleDataI[i] /= numAvg;
                }
            }
            for (Index i = 2; i < numVert; i += 3) {
               const Index idx = m_data.m_LocationList[ValidCellIndex]+i;
               for(int i = 0; i < m_data.m_numInVertData; i++){
                  m_data.m_outVertPtr[i][idx] = middleData[i];
               }
               for(int i = 0; i < m_data.m_numInVertDataI; i++){
                  m_data.m_outVertPtrI[i][idx] = middleDataI[i];
               }
            };
            break;
         }
      }
      } else {

          auto cl = vistle::StructuredGridBase::cellVertices(CellNr, m_data.m_nvert);
          Scalar field[8];
          for (int idx = 0; idx < 8; idx ++) {
              field[idx] = m_data.m_isoFunc(cl[idx]);
          }

          Scalar grad[8][3];
          if (m_data.m_computeNormals) {
              const auto &H = StructuredGridBase::HexahedronIndices;
              auto n = vistle::StructuredGridBase::cellCoordinates(CellNr, m_data.m_nvert);
              for (int idx = 0; idx < 8; idx ++) {
                  Index x[3], xl[3], xu[3];
                  for (int c=0; c<3; ++c) {
                      x[c] = n[c]+H[c][idx];
                  }
                  for (int c=0; c<3; ++c) {
                      xl[c] = x[c] > 0 ? x[c]-1 : x[c];
                      xu[c] = x[c] < m_data.m_nvert[c]-1 ? x[c]+1 : x[c];
                  }
                  for (int c=0; c<3; ++c) {
                      Index xx = x[c];
                      x[c] = xl[c];
                      Index l = StructuredGridBase::vertexIndex(x, m_data.m_nvert);
                      x[c] = xu[c];
                      Index u = StructuredGridBase::vertexIndex(x, m_data.m_nvert);
                      x[c] = xx;
                      grad[idx][c] = m_data.m_isoFunc(u) - m_data.m_isoFunc(l);
                      if (m_data.m_haveCoords) {
                          grad[idx][c] /= (m_data.m_inVertPtr[3+c][u] - m_data.m_inVertPtr[3+c][l]);
                      } else {
                          grad[idx][c] /= (m_data.m_inVertPtr[3+c][xu[c]] - m_data.m_inVertPtr[3+c][xl[c]]);
                      }
                  }
              }
          }

          if (m_data.m_haveCoords) {
              for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
                  INTER(3, hexaTriTable, hexaEdgeTable);
                  if (m_data.m_computeNormals) {
                      for(int j = 0; j < 3; j++) {
                          m_data.m_outVertPtr[j][outvertexindex] = lerp(grad[v1][j], grad[v2][j], t);
                      }
                  }
              }
          } else {
              for (Index idx = 0; idx < m_data.m_numVertices[ValidCellIndex]; idx++) {
                  INTER(6, hexaTriTable, hexaEdgeTable);

                  if (m_data.m_computeNormals) {
                      for(int j = 0; j < 3; j++) {
                          m_data.m_outVertPtr[j][outvertexindex] = lerp(grad[v1][j], grad[v2][j], t);
                      }
                  }

                  auto vc1 = StructuredGridBase::vertexCoordinates(cl[v1], m_data.m_nvert);
                  auto vc2 = StructuredGridBase::vertexCoordinates(cl[v2], m_data.m_nvert);
                  for(int j = 0; j < 3; j++) {
                      m_data.m_outVertPtr[3+j][outvertexindex] =
                              lerp(m_data.m_inVertPtr[3+j][vc1[j]], m_data.m_inVertPtr[3+j][vc2[j]], t);
                  }
              }
          }
      }
   }
};

template<class Data>
struct checkcell {

   typedef float argument_type;
   typedef float result_type;
   Data &m_data;
   checkcell(Data &data) : m_data(data) {}

   // for unstructured grids
   __host__ __device__ int operator()(const thrust::tuple<Index,Index,u_char> iCell) const {

      int havelower = 0;
      int havehigher = 0;
      Index Cell = iCell.get<0>();
      Index nextCell = iCell.get<1>();
      u_char cellType = iCell.get<2>();
      if (cellType & UnstructuredGrid::GHOST_BIT)
          return 0;
      for (Index i=Cell; i<nextCell; i++) {
         float val = m_data.m_isoFunc(m_data.m_cl[i]);
         if (val>m_data.m_isovalue) {
            havelower=1;
            if (havehigher)
               return 1;
         } else {
            havehigher=1;
            if (havelower)
               return 1;
         }
      }
      return 0;
   }

   // for all types of structured grids
   __host__ __device__ int operator()(const Index Cell) const {

      auto cc = vistle::StructuredGridBase::cellCoordinates(Cell, m_data.m_nvert);
      for (int c=0; c<3; ++c) {
          if (cc[c] < m_data.m_nghost[c][0])
              return 0;
          if (cc[c]+m_data.m_nghost[c][1]+1 >= m_data.m_nvert[c])
              return 0;
      }

      auto verts = vistle::StructuredGridBase::cellVertices(Cell, m_data.m_nvert);
      int havelower = 0;
      int havehigher = 0;
      for (int i=0; i<8; ++i) {
         float val = m_data.m_isoFunc(verts[i]);
         if (val>m_data.m_isovalue) {
            havelower=1;
            if (havehigher)
               return 1;
         } else {
            havehigher=1;
            if (havelower)
               return 1;
         }
      }
      return 0;
   }
};

template<class Data>
struct classify_cell {

   classify_cell(Data &data) : m_data(data) {}

   Data &m_data;

   __host__ __device__ thrust::tuple<Index,Index> operator()(Index CellNr) {

       int tableIndex = 0;
       int numVerts = 0;
       if (m_data.m_el) {
           const auto &cl = m_data.m_cl;

           Index begin = m_data.m_el[CellNr], end = m_data.m_el[CellNr+1];
           Index nvert = end-begin;
           unsigned char CellType = m_data.m_tl[CellNr] & ~UnstructuredGrid::CONVEX_BIT;
           if (CellType != UnstructuredGrid::POLYHEDRON) {
               for (Index idx = 0; idx < nvert; idx ++) {
                   tableIndex += (((int) (m_data.m_isoFunc(m_data.m_cl[begin+idx]) > m_data.m_isovalue)) << idx);
               }
           }
           switch (CellType) {

           case UnstructuredGrid::HEXAHEDRON:
               numVerts = hexaNumVertsTable[tableIndex];
               break;

           case UnstructuredGrid::TETRAHEDRON:
               numVerts = tetraNumVertsTable[tableIndex];
               break;

           case UnstructuredGrid::PYRAMID:
               numVerts = pyrNumVertsTable[tableIndex];
               break;

           case UnstructuredGrid::PRISM:
               numVerts = prismNumVertsTable[tableIndex];
               break;

           case UnstructuredGrid::POLYHEDRON: {

               Index vertcounter = 0;
               for (Index i = begin; i < end; i += cl[i]+1) {
                   const Index N = cl[i];
                   Index prev = cl[i+N];
                   for (Index k=i+1; k<i+N+1; ++k) {
                       Index v = cl[k];

                       if (m_data.m_isoFunc(prev) <= m_data.m_isovalue && m_data.m_isoFunc(v) > m_data.m_isovalue) {
                           ++vertcounter;
                       } else if(m_data.m_isoFunc(prev) > m_data.m_isovalue && m_data.m_isoFunc(v) <= m_data.m_isovalue) {
                           ++vertcounter;
                       }

                       prev = v;
                   }
               }
               numVerts = vertcounter + vertcounter/2;
               break;
           }
           };
       } else {
           auto verts = vistle::StructuredGridBase::cellVertices(CellNr, m_data.m_nvert);
           for (int idx = 0; idx < 8; ++idx) {
               tableIndex += (((int) (m_data.m_isoFunc(verts[idx]) > m_data.m_isovalue)) << idx);
           }
           numVerts = hexaNumVertsTable[tableIndex];
       }
       return thrust::make_tuple<Index, Index> (tableIndex, numVerts);
   }
};

Leveller::Leveller(const IsoController &isocontrol, Object::const_ptr grid, const Scalar isovalue, Index processortype)
      : m_isocontrol(isocontrol)
      , m_grid(grid)
      , m_uni(UniformGrid::as(grid))
      , m_rect(RectilinearGrid::as(grid))
      , m_str(StructuredGrid::as(grid))
      , m_unstr(UnstructuredGrid::as(grid))
      , m_strbase(StructuredGridBase::as(grid))
      , m_coord(Coords::as(grid))
      , m_isoValue(isovalue)
      , m_processortype(processortype)
      , gmin(std::numeric_limits<Scalar>::max())
      , gmax(-std::numeric_limits<Scalar>::max())
      , m_objectTransform(grid->getTransform())
   {
      m_triangles = Triangles::ptr(new Triangles(Object::Initialized));
      m_triangles->setMeta(grid->meta());
   }

template<class Data, class pol>
Index Leveller::calculateSurface(Data &data) {

    typename Data::VectorIndexIterator end;
    Index nelem = m_strbase ? m_strbase->getNumElements() : m_unstr->getNumElements();
    thrust::counting_iterator<Index> first(0), last = first + nelem;;
    data.m_ValidCellVector.resize(nelem);

    if (m_strbase) {
        end = thrust::copy_if(pol(), first, last, thrust::counting_iterator<Index>(0), data.m_ValidCellVector.begin(), checkcell<Data>(data));
    } else if (m_unstr) {
        typedef thrust::tuple<typename Data::IndexIterator, typename Data::IndexIterator, typename Data::TypeIterator> Iteratortuple;
        typedef thrust::zip_iterator<Iteratortuple> ZipIterator;
        ZipIterator ElTupleVec(thrust::make_tuple(&data.m_el[0], &data.m_el[1], &data.m_tl[0]));
        end = thrust::copy_if(pol(), first, last, ElTupleVec, data.m_ValidCellVector.begin(), checkcell<Data>(data));
    }

    size_t numValidCells = end-data.m_ValidCellVector.begin();
    data.m_caseNums.resize(numValidCells);
    data.m_numVertices.resize(numValidCells);
    data.m_LocationList.resize(numValidCells);
    thrust::transform(pol(), data.m_ValidCellVector.begin(), end, thrust::make_zip_iterator(thrust::make_tuple(data.m_caseNums.begin(),data.m_numVertices.begin())), classify_cell<Data>(data));
    thrust::exclusive_scan(pol(), data.m_numVertices.begin(), data.m_numVertices.end(), data.m_LocationList.begin());
    Index totalNumVertices = 0;
    if (!data.m_numVertices.empty())
        totalNumVertices += data.m_numVertices.back();
    if (!data.m_LocationList.empty())
        totalNumVertices += data.m_LocationList.back();
    for(int i = (m_computeNormals || m_unstr ? 0 : 3); i < data.m_numInVertData; i++){
        data.m_outVertData[i]->resize(totalNumVertices);
    }
    for(int i = 0; i < data.m_numInVertDataI; i++){
        data.m_outVertDataI[i]->resize(totalNumVertices);
    }
    for (int i=0; i<data.m_numInCellData; ++i) {
        data.m_outCellData[i]->resize(totalNumVertices/3);
    }
    for (int i=0; i<data.m_numInCellDataI; ++i) {
        data.m_outCellDataI[i]->resize(totalNumVertices/3);
    }
    thrust::counting_iterator<Index> start(0), finish(numValidCells);
    thrust::for_each(pol(), start, finish, process_Cell<Data>(data));

    return totalNumVertices;
}

bool Leveller::process() {
#ifndef CUTTINGSURFACE
   Vec<Scalar>::const_ptr dataobj = Vec<Scalar>::as(m_data);
   if (!dataobj)
      return false;
#else
#endif

   switch (m_processortype) {

      case Host: {

       Index dims[3] = {0,0,0};
       if (m_strbase) {
           dims[0] = m_strbase->getNumDivisions(0);
           dims[1] = m_strbase->getNumDivisions(1);
           dims[2] = m_strbase->getNumDivisions(2);
       }
       std::vector<Scalar> unicoords[3];
       const Scalar *coords[3]{nullptr, nullptr, nullptr};
       if (m_uni) {
           for (int i=0; i<3; ++i) {
               unicoords[i].resize(dims[i]);
               coords[i] = unicoords[i].data();
               Scalar dist = 0;
               if (dims[i] > 1)
                   dist = (m_uni->max()[i]-m_uni->min()[i])/(dims[i]-1);
               Scalar val = m_uni->min()[i];
               for (Index j=0; j<dims[i]; ++j) {
                   unicoords[i][j] = val;
                   val += dist;
               }
           }
       } else if (m_rect) {
           for (int i=0; i<3; ++i)
               coords[i] = &m_rect->coords(i)[0];
       } else if (m_str) {
           for (int i=0; i<3; ++i)
               coords[i] = &m_str->x(i)[0];
       }
#ifdef CUTTINGSURFACE
       IsoDataFunctor isofunc = m_coord
               ? m_isocontrol.newFunc(m_grid->getTransform(), &m_coord->x()[0], &m_coord->y()[0], &m_coord->z()[0])
               : m_isocontrol.newFunc(m_grid->getTransform(), dims, coords[0], coords[1], coords[2]);
#else
       IsoDataFunctor isofunc = m_isocontrol.newFunc(m_grid->getTransform(), &dataobj->x()[0]);
#endif

         HostData HD = m_unstr
                 ? HostData(m_isoValue, isofunc,
                            m_unstr->el(), m_unstr->tl(), m_unstr->cl(), m_unstr->x(), m_unstr->y(), m_unstr->z())
                 : HostData(m_isoValue, isofunc,
                            dims[0], dims[1], dims[2], coords[0], coords[1], coords[2]);
         HD.setHaveCoords(m_coord ? true : false);
         if (m_strbase) {
             Index ghost[3][2];
             for (int c=0; c<3; ++c) {
                 ghost[c][0] = m_strbase->getNumGhostLayers(c, StructuredGridBase::Bottom);
                 ghost[c][1] = m_strbase->getNumGhostLayers(c, StructuredGridBase::Top);
             }
             HD.setGhostLayers(ghost);
         }
         HD.setComputeNormals(m_computeNormals);

         for (size_t i=0; i<m_vertexdata.size(); ++i) {
            if(Vec<Scalar,1>::const_ptr Scal = Vec<Scalar,1>::as(m_vertexdata[i])){
               HD.addmappeddata(Scal->x());
            }
            if(Vec<Scalar,3>::const_ptr Vect = Vec<Scalar,3>::as(m_vertexdata[i])){
               HD.addmappeddata(Vect->x());
               HD.addmappeddata(Vect->y());
               HD.addmappeddata(Vect->z());
            }
            if(Vec<Index,1>::const_ptr Idx = Vec<Index,1>::as(m_vertexdata[i])){
               HD.addmappeddata(Idx->x());
            }
         }
         for (size_t i=0; i<m_celldata.size(); ++i) {
            if(Vec<Scalar,1>::const_ptr Scal = Vec<Scalar,1>::as(m_celldata[i])){
               HD.addcelldata(Scal->x());
            }
            if(Vec<Scalar,3>::const_ptr Vect = Vec<Scalar,3>::as(m_celldata[i])){
               HD.addcelldata(Vect->x());
               HD.addcelldata(Vect->y());
               HD.addcelldata(Vect->z());
            }
            if(Vec<Index,1>::const_ptr Idx = Vec<Index,1>::as(m_celldata[i])){
               HD.addcelldata(Idx->x());
            }
         }

         Index totalNumVertices = calculateSurface<HostData, thrust::detail::host_t>(HD);

         {
             size_t idx=0;
             Normals::ptr norm;
             if (m_strbase) {
                 if (m_computeNormals) {
                     norm.reset(new Normals(Object::Initialized));
                     norm->d()->x[0] = HD.m_outVertData[idx++];
                     norm->d()->x[1] = HD.m_outVertData[idx++];
                     norm->d()->x[2] = HD.m_outVertData[idx++];
                     //FIXME: norm->setMeta();
                     norm->setMapping(DataBase::Vertex);
                 } else {
                     idx = 3;
                 }
             }
             m_triangles->d()->x[0] = HD.m_outVertData[idx++];
             m_triangles->d()->x[1] = HD.m_outVertData[idx++];
             m_triangles->d()->x[2] = HD.m_outVertData[idx++];
             m_triangles->setNormals(norm);

             size_t idxI=0;
             for (size_t i=0; i<m_vertexdata.size(); ++i) {
                 if(Vec<Scalar>::as(m_vertexdata[i])){

                     Vec<Scalar,1>::ptr out = Vec<Scalar,1>::ptr(new Vec<Scalar,1>(Object::Initialized));
                     out->d()->x[0] = HD.m_outVertData[idx++];
                     out->setMeta(m_vertexdata[i]->meta());
                     out->setMapping(DataBase::Vertex);
                     m_outvertData.push_back(out);

                 }
                 if(Vec<Scalar,3>::as(m_vertexdata[i])){

                     Vec<Scalar,3>::ptr out = Vec<Scalar,3>::ptr(new Vec<Scalar,3>(Object::Initialized));
                     out->d()->x[0] = HD.m_outVertData[idx++];
                     out->d()->x[1] = HD.m_outVertData[idx++];
                     out->d()->x[2] = HD.m_outVertData[idx++];
                     out->setMeta(m_vertexdata[i]->meta());
                     out->setMapping(DataBase::Vertex);
                     m_outvertData.push_back(out);

                 }
                 if(Vec<Index>::as(m_vertexdata[i])){

                     Vec<Index>::ptr out = Vec<Index>::ptr(new Vec<Index>(Object::Initialized));
                     out->d()->x[0] = HD.m_outVertDataI[idxI++];
                     out->setMeta(m_vertexdata[i]->meta());
                     out->setMapping(DataBase::Vertex);
                     m_outvertData.push_back(out);
                 }
             }
         }
         {
             size_t idx=0;
             size_t idxI=0;
             for (size_t i=0; i<m_celldata.size(); ++i) {
                 if(Vec<Scalar>::as(m_celldata[i])){

                     Vec<Scalar,1>::ptr out = Vec<Scalar,1>::ptr(new Vec<Scalar,1>(Object::Initialized));
                     out->d()->x[0] = HD.m_outCellData[idx++];
                     out->setMeta(m_celldata[i]->meta());
                     out->setMapping(DataBase::Element);
                     m_outcellData.push_back(out);

                 }
                 if(Vec<Scalar,3>::as(m_celldata[i])){

                     Vec<Scalar,3>::ptr out = Vec<Scalar,3>::ptr(new Vec<Scalar,3>(Object::Initialized));
                     out->d()->x[0] = HD.m_outCellData[idx++];
                     out->d()->x[1] = HD.m_outCellData[idx++];
                     out->d()->x[2] = HD.m_outCellData[idx++];
                     out->setMeta(m_celldata[i]->meta());
                     out->setMapping(DataBase::Element);
                     m_outcellData.push_back(out);

                 }
                 if(Vec<Index>::as(m_celldata[i])){

                     Vec<Index>::ptr out = Vec<Index>::ptr(new Vec<Index>(Object::Initialized));
                     out->d()->x[0] = HD.m_outCellDataI[idxI++];
                     out->setMeta(m_celldata[i]->meta());
                     out->setMapping(DataBase::Element);
                     m_outcellData.push_back(out);
                 }
             }
         }
         break;
      }

      case Device: {
         std::cerr << "untested Device code path" << std::endl;
         assert("don't use the Device code path" == 0);

         DeviceData DD(m_isoValue,
#ifndef CUTTINGSURFACE
               m_isocontrol.newFunc(m_grid->getTransform(), &dataobj->x()[0]),
#else
               m_isocontrol.newFunc(m_grid->getTransform(), &m_coord->x()[0], &m_coord->y()[0], &m_coord->z()[0]),
#endif
               m_unstr->getNumElements(), m_unstr->el(), m_unstr->tl(), m_unstr->getNumCorners(), m_unstr->cl(), m_unstr->getSize(), m_unstr->x(), m_unstr->y(), m_unstr->z());

#if 0
         Index totalNumVertices = calculateSurface<DeviceData, thrust::device>(DD);
#else
         Index totalNumVertices = 0;
#endif

         m_triangles->x().resize(totalNumVertices);
         Scalar *out_x = m_triangles->x().data();
         thrust::copy(DD.m_outVertData[0]->begin(), DD.m_outVertData[0]->end(), out_x);

         m_triangles->y().resize(totalNumVertices);
         Scalar *out_y = m_triangles->y().data();
         thrust::copy(DD.m_outVertData[1]->begin(), DD.m_outVertData[1]->end(), out_y);

         m_triangles->z().resize(totalNumVertices);
         Scalar *out_z = m_triangles->z().data();
         thrust::copy(DD.m_outVertData[2]->begin(), DD.m_outVertData[2]->end(), out_z);

         if(m_vertexdata.size()){
            if(Vec<Scalar>::as(m_vertexdata[0])){

               Vec<Scalar>::ptr out = Vec<Scalar>::ptr(new Vec<Scalar>(totalNumVertices));
               thrust::copy(DD.m_outVertData[3]->begin(), DD.m_outVertData[3]->end(), out->x().data());
               out->setMeta(m_vertexdata[0]->meta());
               m_outvertData.push_back(out);

            }
            if(Vec<Scalar,3>::as(m_vertexdata[0])){

               Vec<Scalar,3>::ptr out = Vec<Scalar,3>::ptr(new Vec<Scalar,3>(totalNumVertices));
               thrust::copy(DD.m_outVertData[3]->begin(), DD.m_outVertData[3]->end(), out->x().data());
               thrust::copy(DD.m_outVertData[4]->begin(), DD.m_outVertData[4]->end(), out->y().data());
               thrust::copy(DD.m_outVertData[5]->begin(), DD.m_outVertData[5]->end(), out->z().data());
               out->setMeta(m_vertexdata[0]->meta());
               m_outvertData.push_back(out);

            }
         }
         break;
      }
   }

   return true;
}

#ifndef CUTTINGSURFACE
void Leveller::setIsoData(Vec<Scalar>::const_ptr obj) {
   m_data = obj;
}
#endif

void Leveller::setComputeNormals(bool value) {
    m_computeNormals = value;
}

void Leveller::addMappedData(DataBase::const_ptr mapobj ){
    if (mapobj->mapping() == DataBase::Element)
        m_celldata.push_back(mapobj);
    else
        m_vertexdata.push_back(mapobj);
}

Object::ptr Leveller::result() {
      return m_triangles;
   }

DataBase::ptr Leveller::mapresult() const {
   if(m_outvertData.size())
      return m_outvertData[0];
   else if(m_outcellData.size())
      return m_outcellData[0];
   else
      return DataBase::ptr();
}

DataBase::ptr Leveller::cellresult() const {
   if(m_outcellData.size())
      return m_outcellData[0];
   else
      return DataBase::ptr();
}

std::pair<Scalar, Scalar> Leveller::range() {
   return std::make_pair(gmin, gmax);
}
