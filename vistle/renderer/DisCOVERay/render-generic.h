#ifndef RENDER_GENERIC_H
#define RENDER_GENERIC_H

static const int MaxPacketSize = 16;
static const RTCSceneFlags sceneFlags = RTC_SCENE_COHERENT;

static const unsigned int RayEnabled = 0xffffffff;
static const unsigned int RayDisabled = 0;

struct Triangle { unsigned int v0, v1, v2; };

struct RenderObjectData {

   int t; //< timestep number
   RTCScene scene; //< embree scene id for *this* object
   int geomId; //< embree geometry id of this object's scene
   unsigned int instId; //< embree instance id of this object's scene

   Triangle *indexBuffer; //< triangle list

   unsigned int hasSolidColor;
   float solidColor[4];

   unsigned int texWidth; //< size of 1D texture (color table size)
   unsigned int8 *texData; //< 1D RGBA texture data (color table)
   float *texCoords; //< 1D texture coordinates (mapped data)
};

#endif
