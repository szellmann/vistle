#include <sstream>
#include <iomanip>

#include <core/object.h>
#include <core/lines.h>
#include <core/tubes.h>

#include "Tubes.h"

DEFINE_ENUM_WITH_STRING_CONVERSIONS(MapMode,
      (Fixed)
      (Radius)
      (CrossSection)
      (InvRadius)
      (InvCrossSection)
)

MODULE_MAIN(ToTubes)

using namespace vistle;

ToTubes::ToTubes(const std::string &shmname, const std::string &name, int moduleID)
   : Module("Tubes", shmname, name, moduleID) {

   createInputPort("grid_in");
   createInputPort("data_in");
   createOutputPort("grid_out");
   createOutputPort("data_out");

   m_radius = addFloatParameter("radius", "radius or radius scale factor of tube", 1.);
   setParameterMinimum(m_radius, (Float)0.);
   m_mapMode = addIntParameter("map_mode", "mapping of data to tube diameter", (Integer)Fixed, Parameter::Choice);
   V_ENUM_SET_CHOICES(m_mapMode, MapMode);
   m_range = addVectorParameter("range", "allowed radius range", ParamVector(0., 1.));
   setParameterMinimum(m_range, ParamVector(0., 0.));

   m_startStyle = addIntParameter("start_style", "cap style for initial segments", (Integer)Tubes::Open, Parameter::Choice);
   V_ENUM_SET_CHOICES_SCOPE(m_startStyle, CapStyle, Tubes);
   m_jointStyle = addIntParameter("connection_style", "cap style for segment connections", (Integer)Tubes::Round, Parameter::Choice);
   V_ENUM_SET_CHOICES_SCOPE(m_jointStyle, CapStyle, Tubes);
   m_endStyle = addIntParameter("end_style", "cap style for final segments", (Integer)Tubes::Open, Parameter::Choice);
   V_ENUM_SET_CHOICES_SCOPE(m_endStyle, CapStyle, Tubes);
}

ToTubes::~ToTubes() {

}

template<typename S>
S clamp(S v, S vmin, S vmax) {
   return std::min(std::max(v, vmin), vmax);
}

bool ToTubes::compute() {

   auto lines = accept<Lines>("grid_in");
   auto radius = accept<Vec<Scalar, 1>>("grid_in");
   auto radius3 = accept<Vec<Scalar, 3>>("grid_in");
   DataBase::ptr basedata;
   if (!lines && radius) {
      lines = Lines::as(radius->grid());
      basedata = radius->clone();
   }
   if (!lines && radius3) {
      lines = Lines::as(radius3->grid());
      basedata = radius3->clone();
   }
   if (!lines) {
      sendError("no Lines object");
      return true;
   }

   const MapMode mode = (MapMode)m_mapMode->getValue();
   if (mode != Fixed && !radius && !radius3) {
      sendError("data input required for varying radius");
      return true;
   }

   Tubes::ptr tubes;
   auto cl = lines->cl().data();
   // set coordinates
   if (lines->getNumCorners() == 0) {
      tubes = Tubes::clone<Vec<Scalar, 3>>(lines);
      tubes->components().resize(lines->getNumElements()+1);
   } else {
      tubes.reset(new Tubes(lines->getNumElements(), lines->getNumCorners()));
      auto lx = lines->x().data();
      auto ly = lines->y().data();
      auto lz = lines->z().data();
      auto tx = tubes->x().data();
      auto ty = tubes->y().data();
      auto tz = tubes->z().data();
      for (Index i=0; i<lines->getNumCorners(); ++i) {
         const Index l = cl[i];
         tx[i] = lx[l];
         ty[i] = ly[l];
         tz[i] = lz[l];
      }
   }

   // set radii
   auto r = tubes->r().data();
   auto radx = radius3 ? radius3->x().data() : radius ? radius->x().data() : nullptr;
   auto rady = radius3 ? radius3->y().data() : nullptr;
   auto radz = radius3 ? radius3->z().data() : nullptr;
   const Scalar scale = m_radius->getValue();
   const Scalar rmin = m_range->getValue()[0];
   const Scalar rmax = m_range->getValue()[1];
   for (Index i=0; i<lines->getNumCorners(); ++i) {
      const Index l = cl[i];
      const Scalar rad = (radx && rady && radz) ? Vector3(radx[l], rady[l], radz[l]).norm() : radx ? radx[l] : Scalar(1.);
      switch (mode) {
         case Fixed:
            r[i] = scale;
            break;
         case Radius:
            r[i] = scale * rad;
            break;
         case CrossSection:
            r[i] = scale * sqrt(rad);
            break;
         case InvRadius:
            if (fabs(rad) >= 1e-9)
               r[i] = scale / rad;
            else
               r[i] = 1e9;
            break;
         case InvCrossSection:
            if (fabs(rad) >= 1e-9)
               r[i] = scale / sqrt(rad);
            else
               r[i] = 1e9;
            break;
      }

      r[i] = clamp(r[i], rmin, rmax);
   }

   // set tube lengths
   tubes->d()->components = lines->d()->el;

   tubes->setCapStyles((Tubes::CapStyle)m_startStyle->getValue(), (Tubes::CapStyle)m_jointStyle->getValue(), (Tubes::CapStyle)m_endStyle->getValue());
   tubes->setMeta(lines->meta());
   tubes->copyAttributes(lines);

   if (basedata) {
       basedata->setGrid(tubes);
       addObject("grid_out", basedata);
   } else {
       addObject("grid_out", tubes);
   }

   auto data = accept<DataBase>("data_in");
   if (data) {
       auto ndata = data->clone();
       ndata->setGrid(tubes);
       addObject("data_out", ndata);
   }

   return true;
}