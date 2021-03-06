//-------------------------------------------------------------------------
// RECTILINEAR GRID CLASS H
// *
// * Rectilinear Grid Container Object
//-------------------------------------------------------------------------

#include "rectilineargrid.h"
#include "cellalgorithm.h"
#include "unstr.h"
#include "assert.h"

namespace vistle {

// CONSTRUCTOR
//-------------------------------------------------------------------------
RectilinearGrid::RectilinearGrid(const Index numDivX, const Index numDivY, const Index numDivZ, const Meta &meta)
    : RectilinearGrid::Base(RectilinearGrid::Data::create(numDivX, numDivY, numDivZ, meta))
{
    refreshImpl();
}

// REFRESH IMPL
//-------------------------------------------------------------------------
void RectilinearGrid::refreshImpl() const {
    const Data *d = static_cast<Data *>(m_data);
    for (int c=0; c<3; ++c) {
        if (d && d->coords[c].valid()) {
            m_numDivisions[c] = d->coords[c]->size();
            m_coords[c] = d->coords[c]->data();

            m_ghostLayers[c][0] = (*d->ghostLayers[c])[0];
            m_ghostLayers[c][1] = (*d->ghostLayers[c])[1];
        } else {
            m_numDivisions[c] = 0;
            m_coords[c] = nullptr;

            m_ghostLayers[c][0] = 0;
            m_ghostLayers[c][1] = 0;
        }
    }
}

// CHECK IMPL
//-------------------------------------------------------------------------
bool RectilinearGrid::checkImpl() const {

    for (int c=0; c<3; ++c) {
        V_CHECK(d()->coords[c]->check());
        V_CHECK(d()->ghostLayers[c]->check());
        V_CHECK(d()->ghostLayers[c]->size() == 2);
    }


   return true;
}

// IS EMPTY
//-------------------------------------------------------------------------
bool RectilinearGrid::isEmpty() const {

   return (getNumDivisions(0) == 0 || getNumDivisions(1) == 0 || getNumDivisions(2) == 0);
}

// GET FUNCTION - GHOST CELL LAYER
//-------------------------------------------------------------------------
Index RectilinearGrid::getNumGhostLayers(unsigned dim, GhostLayerPosition pos) {
    unsigned layerPosition = (pos == Bottom) ? 0 : 1;

    return (*d()->ghostLayers[dim])[layerPosition];
}

// GET FUNCTION - GHOST CELL LAYER CONST
//-------------------------------------------------------------------------
Index RectilinearGrid::getNumGhostLayers(unsigned dim, GhostLayerPosition pos) const {
    unsigned layerPosition = (pos == Bottom) ? 0 : 1;

    return m_ghostLayers[dim][layerPosition];
}

// SET FUNCTION - GHOST CELL LAYER
//-------------------------------------------------------------------------
void RectilinearGrid::setNumGhostLayers(unsigned dim, GhostLayerPosition pos, unsigned value) {
    unsigned layerPosition = (pos == Bottom) ? 0 : 1;

    (*d()->ghostLayers[dim])[layerPosition] = value;
    m_ghostLayers[dim][layerPosition] = value;

    return;
}

Index RectilinearGrid::getNumVertices() const {
    return getNumDivisions(0)*getNumDivisions(1)*getNumDivisions(2);
}

// GET FUNCTION - BOUNDS
//-------------------------------------------------------------------------
std::pair<Vector, Vector> RectilinearGrid::getBounds() const {

    return std::make_pair(Vector(m_coords[0][0], m_coords[1][0], m_coords[2][0]),
            Vector(m_coords[0][m_numDivisions[0]-1], m_coords[1][m_numDivisions[1]-1], m_coords[2][m_numDivisions[2]-1]));
}

// CELL BOUNDS
//-------------------------------------------------------------------------
std::pair<Vector, Vector> RectilinearGrid::cellBounds(Index elem) const {
    auto n = cellCoordinates(elem, m_numDivisions);
    Vector min(m_coords[0][n[0]], m_coords[1][n[1]], m_coords[2][n[2]]);
    Vector max(m_coords[0][n[0]+1], m_coords[1][n[1]+1], m_coords[2][n[2]+1]);
    return std::make_pair(min, max);
}

// FIND CELL
//-------------------------------------------------------------------------
Index RectilinearGrid::findCell(const Vector &point, Index hint, int flags) const {

    const bool acceptGhost = flags&AcceptGhost;

    for (int c=0; c<3; ++c) {
        if (point[c] < m_coords[c][0])
            return InvalidIndex;
        if (point[c] > m_coords[c][m_numDivisions[c]-1])
            return InvalidIndex;
    }

    Index n[3] = {0, 0, 0};
    for (int c=0; c<3; ++c) {
        for (Index i=0; i<m_numDivisions[c]; ++i) {
            if (m_coords[c][i] >= point[c])
                break;
            n[c] = i;
        }
    }

    Index el = cellIndex(n, m_numDivisions);
    if (acceptGhost || !isGhostCell(el))
        return el;
    return InvalidIndex;

}

// INSIDE CHECK
//-------------------------------------------------------------------------
bool RectilinearGrid::inside(Index elem, const Vector &point) const {

    if (elem == InvalidIndex)
        return false;

    std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
    assert(n[0] < m_numDivisions[0]-1);
    assert(n[1] < m_numDivisions[1]-1);
    assert(n[2] < m_numDivisions[2]-1);
    for (int c=0; c<3; ++c) {
        Scalar x0 = m_coords[c][n[c]], x1 = m_coords[c][n[c]+1];
        if (point[c] < x0)
            return false;
        if (point[c] > x1)
            return false;
    }

    return true;
}

Scalar RectilinearGrid::exitDistance(Index elem, const Vector &point, const Vector &dir) const {

    if (elem == InvalidIndex)
        return false;

    Vector raydir(dir.normalized());

    std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
    assert(n[0] < m_numDivisions[0]-1);
    assert(n[1] < m_numDivisions[1]-1);
    assert(n[2] < m_numDivisions[2]-1);
    Scalar exitDist = -1;
    Scalar t0[3], t1[3];
    for (int c=0; c<3; ++c) {
        Scalar x0 = m_coords[c][n[c]], x1 = m_coords[c][n[c]+1];
        t0[c] = (x0-point[c])/raydir[c];
        t1[c] = (x1-point[c])/raydir[c];
    }

    for (int c=0; c<3; ++c) {
        if (t0[c] > 0) {
           if (exitDist<0 || exitDist>t0[c])
              exitDist = t0[c];
        }
        if (t1[c] > 0) {
           if (exitDist<0 || exitDist>t1[c])
              exitDist = t1[c];
        }
    }

    return exitDist;
}

Vector RectilinearGrid::getVertex(Index v) const {
    auto n = vertexCoordinates(v, m_numDivisions);
    return Vector(m_coords[0][n[0]], m_coords[1][n[1]], m_coords[2][n[2]]);
}


// GET INTERPOLATOR
//-------------------------------------------------------------------------
GridInterface::Interpolator RectilinearGrid::getInterpolator(Index elem, const Vector &point, DataBase::Mapping mapping, GridInterface::InterpolationMode mode) const {

   vassert(inside(elem, point));

#ifdef INTERPOL_DEBUG
   if (!inside(elem, point)) {
      return Interpolator();
   }
#endif

   if (mapping == DataBase::Element) {
       std::vector<Scalar> weights(1, 1.);
       std::vector<Index> indices(1, elem);

       return Interpolator(weights, indices);
   }

   std::array<Index,3> n = cellCoordinates(elem, m_numDivisions);
   std::array<Index,8> cl = cellVertices(elem, m_numDivisions);

   Vector corner0(m_coords[0][n[0]], m_coords[1][n[1]], m_coords[2][n[2]]);
   Vector corner1(m_coords[0][n[0]+1], m_coords[1][n[1]+1], m_coords[2][n[2]+1]);
   const Vector diff = point-corner0;
   const Vector size = corner1-corner0;

   const Index nvert = 8;
   std::vector<Index> indices((mode==Linear || mode==Mean) ? nvert : 1);
   std::vector<Scalar> weights((mode==Linear || mode==Mean) ? nvert : 1);

   if (mode == Mean) {
       const Scalar w = Scalar(1)/nvert;
       for (Index i=0; i<nvert; ++i) {
           indices[i] = cl[i];
           weights[i] = w;
       }
   } else if (mode == Linear) {
       vassert(nvert == 8);
       for (Index i=0; i<nvert; ++i) {
           indices[i] = cl[i];
       }
       Vector ss = diff;
       for (int c=0; c<3; ++c) {
           ss[c] /= size[c];
       }
       weights[0] = (1-ss[0])*(1-ss[1])*(1-ss[2]);
       weights[1] = ss[0]*(1-ss[1])*(1-ss[2]);
       weights[2] = ss[0]*ss[1]*(1-ss[2]);
       weights[3] = (1-ss[0])*ss[1]*(1-ss[2]);
       weights[4] = (1-ss[0])*(1-ss[1])*ss[2];
       weights[5] = ss[0]*(1-ss[1])*ss[2];
       weights[6] = ss[0]*ss[1]*ss[2];
       weights[7] = (1-ss[0])*ss[1]*ss[2];
   } else {
      weights[0] = 1;

      if (mode == First) {
         indices[0] = cl[0];
      } else if(mode == Nearest) {
          Vector ss = diff;
          int nearest=0;
          for (int c=0; c<3; ++c) {
              nearest <<= 1;
              ss[c] /= size[c];
              if (ss[c] < 0.5)
                  nearest |= 1;
          }
          indices[0] = cl[nearest];
      }
   }

   return Interpolator(weights, indices);

}

void RectilinearGrid::Data::initData() {

    for (int c=0; c<3; ++c) {
        ghostLayers[c].construct(2);
        (*ghostLayers[c])[0] = (*ghostLayers[c])[1] = 0;
    }
}

// DATA OBJECT - CONSTRUCTOR FROM NAME & META
//-------------------------------------------------------------------------
RectilinearGrid::Data::Data(const Index numDivX, const Index numDivY, const Index numDivZ, const std::string & name, const Meta &meta)
    : RectilinearGrid::Base::Data(Object::RECTILINEARGRID, name, meta) {
    initData();
    coords[0].construct(numDivX);
    coords[1].construct(numDivY);
    coords[2].construct(numDivZ);
}

// DATA OBJECT - CONSTRUCTOR FROM DATA OBJECT AND NAME
//-------------------------------------------------------------------------
RectilinearGrid::Data::Data(const RectilinearGrid::Data &o, const std::string &n)
    : RectilinearGrid::Base::Data(o, n) {
    initData();
    for (int c=0; c<3; ++c) {
        coords[c] = o.coords[c];
        for (int i=0; i<2; ++i)
            (*ghostLayers[c])[i] = (*o.ghostLayers[c])[i];
    }
}

// DATA OBJECT - DESTRUCTOR
//-------------------------------------------------------------------------
RectilinearGrid::Data::~Data() {

}

// DATA OBJECT - CREATE FUNCTION
//-------------------------------------------------------------------------
RectilinearGrid::Data * RectilinearGrid::Data::create(const Index numDivX, const Index numDivY, const Index numDivZ, const Meta &meta) {

    const std::string name = Shm::the().createObjectId();
    Data *p = shm<Data>::construct(name)(numDivX, numDivY, numDivZ, name, meta);
    publish(p);

    return p;
}

// MACROS
//-------------------------------------------------------------------------
V_OBJECT_TYPE(RectilinearGrid, Object::RECTILINEARGRID)
V_OBJECT_CTOR(RectilinearGrid)

} // namespace vistle
