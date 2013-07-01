#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>

#include <core/message.h>
#include <core/tcpmessage.h>
#include <core/parameter.h>
#include <core/port.h>
#include "userinterface.h"

namespace asio = boost::asio;

namespace vistle {

UserInterface::UserInterface(const std::string &host, const unsigned short port)
: m_stateTracker(&m_portTracker)
{
   message::DefaultSender::init(0, 0);

   const int HOSTNAMESIZE = 64;
   char hostname[HOSTNAMESIZE];
   gethostname(hostname, HOSTNAMESIZE - 1);

   std::cerr << "  userinterface ["  << id() << "] started as " << hostname << ":"
#ifndef _WIN32
             << getpid() << std::endl;
#else
             << std::endl;
#endif

   m_hostname = hostname;

   std::stringstream portstr;
   portstr << port;

   asio::ip::tcp::resolver resolver(m_ioService);
   asio::ip::tcp::resolver::query query(host, portstr.str());
   asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
   m_socket = new asio::ip::tcp::socket(m_ioService);
   asio::connect(socket(), endpoint_iterator);
}

int UserInterface::id() const {

   return m_id;
}

std::string UserInterface::host() const {

   return m_hostname;
}

boost::asio::ip::tcp::socket &UserInterface::socket() const {

   return *m_socket;
}

StateTracker &UserInterface::state() {

   return m_stateTracker;
}

bool UserInterface::dispatch() {

   char msgRecvBuf[message::Message::MESSAGE_SIZE];
   vistle::message::Message *message = (vistle::message::Message *) msgRecvBuf;

   bool received = true;
   while (received) {

      received = false;
      if (!message::recv(socket(), *message, received)) {
         return false;
      }
      
      if (!received)
         break;

      if (!handleMessage(message))
         return false;

   }

   return true;
}


bool UserInterface::sendMessage(const message::Message &message) const {

   return message::send(socket(), message);
}


bool UserInterface::handleMessage(const vistle::message::Message *message) {

   bool ret = m_stateTracker.handleMessage(*message);

   MessageMap::iterator it = m_messageMap.find(message->uuid());
   if (it != m_messageMap.end()) {
      it->second.buf.resize(message->size());
      memcpy(it->second.buf.data(), message, message->size());
      it->second.received = true;
      it->second.cond.notify_all();
   }

   switch (message->type()) {

      case message::Message::SETID: {
         const message::SetId *id = static_cast<const message::SetId *>(message);
         m_id = id->getId();
         message::DefaultSender::init(0, 0);
         std::cerr << "received new UI id: " << m_id << std::endl;
         break;
      }

      case message::Message::QUIT: {
         const message::Quit *quit = static_cast<const message::Quit *>(message);
         (void)quit;
         return false;
         break;
      }

      default:
         break;
   }

   return ret;
}

boost::mutex &UserInterface::mutexForMessage(const message::Message::uuid_t &uuid) {

   return m_messageMap[uuid].mutex;
}

boost::condition_variable &UserInterface::conditionForMessage(const message::Message::uuid_t &uuid) {

   return m_messageMap[uuid].cond;
}

bool UserInterface::getMessage(const message::Message::uuid_t &uuid, message::Message &msg) {

   MessageMap::iterator it = m_messageMap.find(uuid);
   if (it == m_messageMap.end()) {
      return false;
   }
   if (!it->second.received) {
      return false;
   }

   memcpy(&msg, &*it->second.buf.data(), it->second.buf.size());
   m_messageMap.erase(it);
   return true;
}

void UserInterface::registerObserver(StateObserver *observer) {

   m_stateTracker.registerObserver(observer);
}

UserInterface::~UserInterface() {

   vistle::message::ModuleExit m;
   sendMessage(m);

   std::cerr << "  userinterface [" << host() << "] [" << id()
             << "] quit" << std::endl;
}

} // namespace vistle
