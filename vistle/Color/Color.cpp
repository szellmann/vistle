#include <sstream>
#include <iomanip>
#include <values.h>
#include "vector.h"
#include "object.h"

#include "Color.h"

#define lerp(a, b, t) ( a + t * (b - a) )

MODULE_MAIN(Color)

ColorMap::ColorMap(std::map<float, vistle::Vector> & pins,
                   const size_t w): width(w) {

   data = new unsigned char[width * 4];

   std::map<float, vistle::Vector>::iterator current = pins.begin();
   std::map<float, vistle::Vector>::iterator next = ++pins.begin();

   for (size_t index = 0; index < width; index ++) {

      if (((float) index) / width > next->first) {
         if (next != pins.end()) {
            current ++;
            next ++;
         }
      }

      if (next == pins.end()) {
         data[index * 4] = current->second.x;
         data[index * 4 + 1] = current->second.y;
         data[index * 4 + 2] = current->second.z;
         data[index * 4 + 3] = 1.0;
      } else {

         float a = current->first;
         float b = next->first;

         float t = ((index / (float) width) - a) / (b - a);

         data[index * 4] =
            (lerp(current->second.x, next->second.x, t) * 255.0);
         data[index * 4 + 1] =
            (lerp(current->second.y, next->second.y, t) * 255.0);
         data[index * 4 + 2] =
            (lerp(current->second.z, next->second.z, t) * 255.0);
         data[index * 4 + 3] = 1.0;
      }
   }
}

ColorMap::~ColorMap() {

   delete[] data;
}

Color::Color(int rank, int size, int moduleID)
   : Module("Color", rank, size, moduleID) {

   createInputPort("data_in");
   createOutputPort("data_out");
}

Color::~Color() {

}

void Color::getMinMax(const vistle::Object * object,
                      float & min, float & max) {

   if (object) {
      switch (object->getType()) {

         case vistle::Object::SET: {

            const vistle::Set *set = static_cast<const vistle::Set*>(object);
            for (size_t index = 0; index < set->getNumElements(); index ++)
               getMinMax(set->getElement(index), min, max);

            break;
         }

         case vistle::Object::VECFLOAT: {

            const vistle::Vec<float> *data =
               static_cast<const vistle::Vec<float> *>(object);

            const float *x = &((*data->x)[0]);
            int numElements = data->getSize();
            for (int index = 0; index < numElements; index ++) {
               if (x[index] < min)
                  min = x[index];
               if (x[index] > max)
                  max = x[index];
            }
            break;
         }

         default:
            break;
      }
   }
}

vistle::Object * Color::addTexture(vistle::Object * object,
                                   const float min, const float max,
                                   const ColorMap & cmap) {

   if (object) {
      switch (object->getType()) {

         case vistle::Object::SET: {

            vistle::Set *set = static_cast<vistle::Set*>(object);
            vistle::Set *out = vistle::Set::create();
            out->setBlock(object->getBlock());
            out->setTimestep(object->getTimestep());

            for (size_t index = 0; index < set->getNumElements(); index ++)
               out->elements->push_back(addTexture(set->getElement(index),
                                                   min, max, cmap));
            return out;
            break;
         }

         case vistle::Object::VECFLOAT: {

            vistle::Vec<float> * f = static_cast<vistle::Vec<float> *>(object);
            const size_t numElem = f->getSize();
            float *x = &((*f->x)[0]);

            vistle::Texture1D *tex = vistle::Texture1D::create(cmap.width,
                                                               min, max);
            tex->setBlock(object->getBlock());
            tex->setTimestep(object->getTimestep());

            unsigned char *pix = &(*tex->pixels)[0];
            for (size_t index = 0; index < cmap.width * 4; index ++)
               pix[index] = cmap.data[index];

            for (size_t index = 0; index < numElem; index ++)
               tex->coords->push_back((x[index] - min) / (max - min));

            return tex;
            break;
         }

         default:
            break;
      }
   }
   return NULL;
}

bool Color::compute() {

   std::map<float, vistle::Vector> pins;
   pins.insert(std::make_pair(0.0, vistle::Vector(0.0, 0.0, 1.0)));
   pins.insert(std::make_pair(0.5, vistle::Vector(1.0, 0.0, 0.0)));
   pins.insert(std::make_pair(1.0, vistle::Vector(1.0, 1.0, 0.0)));

   ColorMap cmap(pins, 32);
   std::list<vistle::Object *> objects = getObjects("data_in");

   while (objects.size() > 0) {

      float min = FLT_MAX;
      float max = -FLT_MAX;

      getMinMax(objects.front(), min, max);

      vistle::Object *out = addTexture(objects.front(), min, max, cmap);
      if (out)
         addObject("data_out", out);

      removeObject("data_in", objects.front());

      objects = getObjects("data_in");
   }

   return true;
}
