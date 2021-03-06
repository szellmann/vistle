#ifndef VISTLE_RHRCONTROLLER_H
#define VISTLE_RHRCONTROLLER_H

#include "renderer.h"
#include <rhr/rhrserver.h>
#include <boost/asio/ip/address.hpp>
#include <string>

namespace vistle {

class V_RENDEREREXPORT RhrController {

 public:
   RhrController(vistle::Module *module, int displayRank);
   bool handleParam(const vistle::Parameter *p);
   std::shared_ptr<RhrServer> server() const;
   int rootRank() const;

   unsigned short listenPort() const;
   std::string listenHost() const;

 private:
   bool initializeServer();

   vistle::Module *m_module;
   int m_displayRank;

   IntParameter *m_rhrConnectionMethod;
   StringParameter *m_rhrLocalEndpoint;
   IntParameter *m_rhrBasePort;
   unsigned short m_forwardPort; //< current port mapping

   IntParameter *m_rgbaEncoding;
   RhrServer::ColorCodec m_rgbaCodec;
   IntParameter *m_depthPrec;
   Integer m_prec;
   IntParameter *m_depthQuant;
   bool m_quant;
   IntParameter *m_depthSnappy;
   bool m_snappy;
   IntParameter *m_depthZfp;
   bool m_zfp;
   IntParameter *m_depthZfpMode;
   RhrServer::ZfpMode m_zfpMode;

   IntVectorParameter *m_sendTileSizeParam;
   IntParamVector m_sendTileSize;

   std::shared_ptr<RhrServer> m_rhr;
};

}
#endif

