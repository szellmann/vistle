/*
 * Visualization Testing Laboratory for Exascale Computing (VISTLE)
 */
#include <core/statetracker.h>
#include <core/messagequeue.h>
#include <core/tcpmessage.h>

#include "uimanager.h"
#include "uiclient.h"
#include "hub.h"

namespace vistle {

UiManager::UiManager(Hub &hub, StateTracker &stateTracker)
: m_hub(hub)
, m_stateTracker(stateTracker)
, m_locked(false)
, m_requestQuit(false)
, m_uiCount(0)
{
}

UiManager::~UiManager() {

   disconnect();
}

bool UiManager::handleMessage(const message::Message &msg, std::shared_ptr<boost::asio::ip::tcp::socket> sock) {

   using namespace vistle::message;

   switch(msg.type()) {
   case MODULEEXIT:
   case QUIT: {
       auto it = m_clients.find(sock);
       if (it == m_clients.end()) {
           std::cerr << "UiManager: unknown UI quit" << std::endl;
       } else {
           it->second->cancel();
           removeClient(it->second);
       }
       if (msg.type() == MODULEEXIT)
           return true;
       break;
   }
   default:
       break;
   }

   if (isLocked()) {

      m_queue.emplace_back(msg);
      return true;
   }

   return m_hub.handleMessage(msg, sock);
}

void UiManager::sendMessage(const message::Message &msg) {

   std::vector<std::shared_ptr<UiClient>> toRemove;

   for(auto ent: m_clients) {
      if (!sendMessage(ent.second, msg)) {
         toRemove.push_back(ent.second);
      }
   }

   for (auto ent: toRemove) {
      removeClient(ent);
   }
}

bool UiManager::sendMessage(std::shared_ptr<UiClient> c, const message::Message &msg) const {

   auto &ioService = c->socket()->get_io_service();
   bool ret = message::send(*c->socket(), msg);
   ioService.poll();
   return ret;
}

void UiManager::requestQuit() {

   m_requestQuit = true;
   sendMessage(message::Quit());
}

void UiManager::disconnect() {

   sendMessage(message::Quit());

   for(const auto &c: m_clients) {
      c.second->cancel();
   }

   m_clients.clear();
}

void UiManager::addClient(std::shared_ptr<boost::asio::ip::tcp::socket> sock) {

   ++m_uiCount;
   std::shared_ptr<UiClient> c(new UiClient(*this, m_uiCount, sock));

   m_clients.insert(std::make_pair(sock, c));

   std::cerr << "UiManager: new UI connected, now have " << m_uiCount << " connections" << std::endl;

   if (m_requestQuit) {

      sendMessage(c, message::Quit());
   } else {

      sendMessage(c, message::SetId(c->id()));

      sendMessage(c, message::LockUi(m_locked));

      auto state = m_stateTracker.getState();
      for (auto &m: state) {
         sendMessage(c, m);
      }
   }
}

bool UiManager::removeClient(std::shared_ptr<UiClient> c) {

   for (auto &ent: m_clients) {
      if (ent.second == c) {
         if (!c->done()) {
             sendMessage(c, message::Quit());
             c->cancel();
         }
         m_clients.erase(ent.first);
         --m_uiCount;
         return true;
      }
   }

   return false;
}

void UiManager::lockUi(bool locked) {

   if (m_locked != locked) {
      sendMessage(message::LockUi(locked));
      m_locked = locked;
   }

   if (!m_locked) {
      for (auto &m: m_queue) {
         m_hub.handleMessage(m);
      }
      m_queue.clear();
   }
}

bool UiManager::isLocked() const {

   return m_locked;
}

} // namespace vistle
