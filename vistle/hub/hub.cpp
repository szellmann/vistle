/*
 * Visualization Testing Laboratory for Exascale Computing (VISTLE)
 */

#include <iostream>
#include <fstream>
#include <exception>
#include <cstdlib>
#include <sstream>

#include <core/assert.h>
#include <util/hostname.h>
#include <util/directory.h>
#include <util/spawnprocess.h>
#include <util/sleep.h>
#include <core/object.h>
#include <core/message.h>
#include <core/tcpmessage.h>
#include <core/messagerouter.h>
#include <core/porttracker.h>
#include <core/statetracker.h>
#include <core/shm.h>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include "uimanager.h"
#include "uiclient.h"
#include "hub.h"

#ifdef HAVE_PYTHON
#include "pythoninterpreter.h"
#endif

//#define DEBUG_DISTRIBUTED

namespace asio = boost::asio;
using std::shared_ptr;
using namespace vistle;
using message::Router;
using message::Id;
namespace dir = vistle::directory;

#define CERR std::cerr << "Hub " << m_hubId << ": " 

Hub *hub_instance = nullptr;

Hub::Hub()
: m_port(31093)
, m_masterPort(m_port)
, m_masterHost("localhost")
, m_acceptor(new boost::asio::ip::tcp::acceptor(m_ioService))
, m_stateTracker("Hub state")
, m_uiManager(*this, m_stateTracker)
, m_managerConnected(false)
, m_quitting(false)
, m_isMaster(true)
, m_slaveCount(0)
, m_hubId(Id::Invalid)
, m_moduleCount(0)
, m_traceMessages(message::INVALID)
, m_execCount(0)
, m_barrierActive(false)
, m_barrierReached(0)
{

   vassert(!hub_instance);
   hub_instance = this;

   message::DefaultSender::init(m_hubId, 0);
   m_uiManager.lockUi(true);
}

Hub::~Hub() {

   m_dataProxy.reset();
   hub_instance = nullptr;
}

Hub &Hub::the() {

   return *hub_instance;
}

const StateTracker &Hub::stateTracker() const {

   return m_stateTracker;
}

StateTracker &Hub::stateTracker() {

   return m_stateTracker;
}

unsigned short Hub::port() const {

   return m_port;
}

vistle::process_handle Hub::launchProcess(const std::vector<std::string> &argv) {

	assert(!argv.empty());
#ifdef _WIN32
	auto pid = spawn_process("spawn_vistle.bat", argv);
	std::cerr << "launched " << argv[0] << " with PID " << pid << std::endl;
#else
    std::vector<std::string> args;
    args.push_back(argv[0]);
    std::copy(argv.begin(), argv.end(), std::back_inserter(args));
    auto pid = spawn_process("spawn_vistle.sh", args);
    //std::cerr << "launched " << argv[0] << " with PID " << pid << std::endl;
#endif
	return pid;
}

bool Hub::sendMessage(shared_ptr<socket> sock, const message::Message &msg) {

   bool result = true;
   try {
      result = message::send(*sock, msg);
   } catch(const boost::system::system_error &err) {
      std::cerr << "exception: err.code()=" << err.code() << std::endl;
      result = false;
      if (err.code() == boost::system::errc::broken_pipe) {
         std::cerr << "broken pipe" << std::endl;
      } else {
         throw(err);
      }
   }
   return result;
}

bool Hub::startServer() {

   while (!m_acceptor->is_open()) {

      asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v6(), m_port);
      m_acceptor->open(endpoint.protocol());
      m_acceptor->set_option(acceptor::reuse_address(true));
      try {
         m_acceptor->bind(endpoint);
      } catch(const boost::system::system_error &err) {
         if (err.code() == boost::system::errc::address_in_use) {
            m_acceptor->close();
            ++m_port;
            continue;
         }
         throw(err);
      }
      m_acceptor->listen();
      CERR << "listening for connections on port " << m_port << std::endl;
      startAccept();
   }

   return true;
}

bool Hub::startAccept() {
   
   shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ioService));
   addSocket(sock);
   m_acceptor->async_accept(*sock, [this, sock](boost::system::error_code ec) {
         handleAccept(sock, ec);
         });
   return true;
}

void Hub::handleAccept(shared_ptr<asio::ip::tcp::socket> sock, const boost::system::error_code &error) {

   if (error) {
      removeSocket(sock);
      return;
   }

   addClient(sock);

   message::Identify ident(message::Identify::REQUEST);
   sendMessage(sock, ident);

   startAccept();
}

void Hub::addSocket(shared_ptr<asio::ip::tcp::socket> sock, message::Identify::Identity ident) {

   bool ok = m_sockets.insert(std::make_pair(sock, ident)).second;
   vassert(ok);
   (void)ok;
}

bool Hub::removeSocket(shared_ptr<asio::ip::tcp::socket> sock) {

   return m_sockets.erase(sock) > 0;
}

void Hub::addClient(shared_ptr<asio::ip::tcp::socket> sock) {

   //CERR << "new client" << std::endl;
   m_clients.insert(sock);
}

void Hub::addSlave(const std::string &name, shared_ptr<asio::ip::tcp::socket> sock) {

   vassert(m_isMaster);
   ++m_slaveCount;
   const int slaveid = Id::MasterHub - m_slaveCount;
   m_slaves[slaveid].sock = sock;
   m_slaves[slaveid].name = name;
   m_slaves[slaveid].ready = false;
   m_slaves[slaveid].id = slaveid;

   message::SetId set(slaveid);
   sendMessage(sock, set);
}

void Hub::slaveReady(Slave &slave) {

   CERR << "slave hub '" << slave.name << "' ready" << std::endl;

   vassert(m_isMaster);
   vassert(!slave.ready);

   auto state = m_stateTracker.getState();
   for (auto &m: state) {
      sendMessage(slave.sock, m);
   }
   slave.ready = true;
}

bool Hub::dispatch() {

   m_ioService.poll();

   bool ret = true;
   bool work = false;
   size_t avail = 0;
   do {
      std::shared_ptr<boost::asio::ip::tcp::socket> sock;
      avail = 0;
      for (auto &s: m_clients) {
         boost::asio::socket_base::bytes_readable command(true);
         s->io_control(command);
         if (command.get() > avail) {
            avail = command.get();
            sock = s;
         }
      }
      boost::system::error_code error;
      if (sock) {
          work = true;
          handleWrite(sock, error);
      }
   } while (avail > 0);

   if (auto pid = vistle::try_wait()) {
      work = true;
      auto it = m_processMap.find(pid);
      if (it == m_processMap.end()) {
         CERR << "unknown process with PID " << pid << " exited" << std::endl;
      } else {
         const int id = it->second;
         CERR << "process with id " << id << " (PID " << pid << ") exited" << std::endl;
         m_processMap.erase(it);
         if (id == 0) {
            // manager died
            m_dataProxy.reset();
            if (!m_quitting) {
               m_uiManager.requestQuit();
               CERR << "manager died - cannot continue" << std::endl;
               for (auto ent: m_processMap) {
                  vistle::kill_process(ent.first);
               }
               if (startCleaner()) {
                  m_quitting = true;
               } else {
                  exit(1);
               }
            }
         }
         if (id >= message::Id::ModuleBase
                 && m_stateTracker.getModuleState(id) != StateObserver::Unknown
                 && m_stateTracker.getModuleState(id) != StateObserver::Quit) {
            // synthesize ModuleExit message for crashed modules
            message::ModuleExit m;
            m.setSenderId(id);
            sendManager(m); // will be returned and forwarded to master hub
         }
      }
   }

   if (m_quitting) {
      if (m_processMap.empty()) {
         ret = false;
      } else {
#if 0
          CERR << "still " << m_processMap.size() << " processes running" << std::endl;
          for (const auto &proc: m_processMap) {
              std::cerr << "   id: " << proc.second << ", pid: " << proc.first << std::endl;
          }
#endif
      }
   }

   vistle::adaptive_wait(work);

   return ret;
}

void Hub::handleWrite(std::shared_ptr<boost::asio::ip::tcp::socket> sock, const boost::system::error_code &error) {

    message::Identify::Identity senderType = message::Identify::UNKNOWN;
    auto it = m_sockets.find(sock);
    if (it != m_sockets.end())
       senderType = it->second;
    message::Buffer msg;
    bool received = false;
    if (message::recv(*sock, msg, received) && received) {
       bool ok = true;
       if (senderType == message::Identify::UI) {
          ok = m_uiManager.handleMessage(msg, sock);
       } else if (senderType == message::Identify::LOCALBULKDATA) {
          //ok = handleLocalData(msg, sock);
          CERR << "invalid identity on socket: " << senderType << std::endl;
          ok = false;
       } else if (senderType == message::Identify::REMOTEBULKDATA) {
          //ok = handleRemoteData(msg, sock);
          CERR << "invalid identity on socket: " << senderType << std::endl;
          ok = false;
       } else {
          ok = handleMessage(msg, sock);
       }
       if (!ok) {
          m_quitting = true;
          //break;
       }
    }
}

bool Hub::sendMaster(const message::Message &msg) {

   vassert(!m_isMaster);
   if (m_isMaster) {
      return false;
   }

   int numSent = 0;
   for (auto &sock: m_sockets) {
      if (sock.second == message::Identify::HUB) {
         ++numSent;
         sendMessage(sock.first, msg);
      }
   }
   vassert(numSent == 1);
   return numSent == 1;
}

bool Hub::sendManager(const message::Message &msg, int hub) {

   if (hub == Id::LocalHub || hub == m_hubId || (hub == Id::MasterHub && m_isMaster)) {
      vassert(m_managerConnected);
      if (!m_managerConnected) {
         CERR << "sendManager: no connection, cannot send " << msg << std::endl;
         return false;
      }

      int numSent = 0;
      for (auto &sock: m_sockets) {
         if (sock.second == message::Identify::MANAGER) {
            ++numSent;
            sendMessage(sock.first, msg);
         }
      }
      vassert(numSent == 1);
      return numSent == 1;
   } else {
      sendHub(msg, hub);
   }
   return true;
}

bool Hub::sendSlaves(const message::Message &msg, bool returnToSender) {

   vassert(m_isMaster);
   if (!m_isMaster)
      return false;

   int senderHub = msg.senderId();
   if (senderHub > 0) {
      //std::cerr << "mod id " << senderHub;
      senderHub = m_stateTracker.getHub(senderHub);
      //std::cerr << " -> hub id " << senderHub << std::endl;
   }

   for (auto &slave: m_slaves) {
      if (slave.first != senderHub || returnToSender) {
         //std::cerr << "to slave id: " << sock.first << " (!= " << senderHub << ")" << std::endl;
         if (slave.second.ready)
            sendMessage(slave.second.sock, msg);
      }
   }
   return true;
}

bool Hub::sendHub(const message::Message &msg, int hub) {

   if (hub == m_hubId)
      return true;

   if (!m_isMaster && hub == Id::MasterHub) {
      sendMaster(msg);
      return true;
   }

   for (auto &slave: m_slaves) {
      if (slave.first == hub) {
         sendMessage(slave.second.sock, msg);
         return true;
      }
   }

   return false;
}

bool Hub::sendSlave(const message::Message &msg, int dest) {

   vassert(m_isMaster);
   if (!m_isMaster)
      return false;

   auto it = m_slaves.find(dest);
   if (it == m_slaves.end())
      return false;

   return sendMessage(it->second.sock, msg);
}

bool Hub::sendUi(const message::Message &msg) {

   m_uiManager.sendMessage(msg);
   return true;
}

int Hub::idToHub(int id) const {

   if (id >= Id::ModuleBase)
      return m_stateTracker.getHub(id);

   if (id == Id::LocalManager || id == Id::LocalHub)
       return m_hubId;

   return m_stateTracker.getHub(id);
}

int Hub::id() const {

    return m_hubId;
}

void Hub::hubReady() {
   vassert(m_managerConnected);
   if (m_isMaster) {
      processScript();
   } else {
      message::AddHub hub(m_hubId, m_name);
      hub.setDestId(Id::ForBroadcast);
      hub.setPort(m_port);
      hub.setDataPort(m_dataProxy->port());

      for (auto &sock: m_sockets) {
         if (sock.second == message::Identify::HUB) {
            try {
               auto addr = sock.first->local_endpoint().address();
               if (addr.is_v6()) {
                  hub.setAddress(addr.to_v6());
               } else if (addr.is_v4()) {
                  hub.setAddress(addr.to_v4());
               }
            } catch (std::bad_cast &except) {
               CERR << "AddHub: failed to convert local address to v6" << std::endl;
            }
         }
      }

      sendMaster(hub);
   }
}

bool Hub::handleMessage(const message::Message &recv, shared_ptr<asio::ip::tcp::socket> sock) {

   using namespace vistle::message;

   message::Buffer buf(recv);
   Message &msg = buf;
   message::Identify::Identity senderType = message::Identify::UNKNOWN;
   auto it = m_sockets.find(sock);
   if (sock) {
      vassert(it != m_sockets.end());
      if (it != m_sockets.end())
         senderType = it->second;
   }

   if (senderType == Identify::UI)
      msg.setSenderId(m_hubId);

   bool masterAdded = false;
   switch (msg.type()) {
      case message::IDENTIFY: {

         auto &id = static_cast<const Identify &>(msg);
         CERR << "ident msg: " << id.identity() << std::endl;
         if (id.identity() != Identify::UNKNOWN && id.identity() != Identify::REQUEST) {
            it->second = id.identity();
         }
         switch(id.identity()) {
            case Identify::REQUEST: {
               if (senderType == Identify::REMOTEBULKDATA) {
                  sendMessage(sock, Identify(Identify::REMOTEBULKDATA, m_hubId));
               } else if (senderType == Identify::LOCALBULKDATA) {
                  sendMessage(sock, Identify(Identify::LOCALBULKDATA, -1));
               } else if (m_isMaster) {
                  sendMessage(sock, Identify(Identify::HUB, m_name));
               } else {
                  sendMessage(sock, Identify(Identify::SLAVEHUB, m_name));
               }
               break;
            }
            case Identify::MANAGER: {
               vassert(!m_managerConnected);
               m_managerConnected = true;

               if (m_hubId != Id::Invalid) {
                  message::SetId set(m_hubId);
                  sendMessage(sock, set);
                  if (m_hubId <= Id::MasterHub) {
                     auto state = m_stateTracker.getState();
                     for (auto &m: state) {
                        sendMessage(sock, m);
                     }
                  }
                  hubReady();
               }
               m_uiManager.lockUi(false);
               break;
            }
            case Identify::UI: {
               m_uiManager.addClient(sock);
               break;
            }
            case Identify::HUB: {
               vassert(!m_isMaster);
               CERR << "master hub connected" << std::endl;
               break;
            }
            case Identify::SLAVEHUB: {
               vassert(m_isMaster);
               CERR << "slave hub '" << id.name() << "' connected" << std::endl;
               addSlave(id.name(), sock);
               break;
            }
            default: {
               CERR << "invalid identity: " << id.identity() << std::endl;
               break;
            }
         }
         return true;
      }
      case message::ADDHUB: {
         auto &mm = static_cast<const AddHub &>(msg);
         if (m_isMaster) {
            auto it = m_slaves.find(mm.id());
            if (it == m_slaves.end()) {
               break;
            }
            auto &slave = it->second;
            slaveReady(slave);
         } else {
            if (mm.id() == Id::MasterHub) {
               CERR << "received AddHub for master" << std::endl;
               auto m = mm;
               m.setAddress(m_masterSocket->remote_endpoint().address());
               m_stateTracker.handle(m, true);
               masterAdded = true;
            } else {
                m_stateTracker.handle(mm, true);
            }
         }
         if (mm.id() > m_hubId) {
             m_dataProxy->connectRemoteData(mm.id());
         }
         break;
      }
      case message::CONNECT: {
         //CERR << "handling connect: " << msg << std::endl;
         auto &mm = static_cast<const Connect &>(msg);
         if (m_isMaster) {
             if (mm.isNotification()) {
                 CERR << "discarding notification on master: " << msg << std::endl;
                 return true;
             }
             if (m_stateTracker.handleConnect(mm)) {
                 handlePriv(mm);
                 handleQueue();
             } else {
                 //CERR << "delaying connect: " << msg << std::endl;
                 m_queue.emplace_back(msg);
                 return true;
             }
         } else {
             if (mm.isNotification()) {
                 m_stateTracker.handle(mm);
                 sendManager(mm);
                 sendUi(mm);
             } else {
                 sendMaster(mm);
             }
             return true;
         }
         break;
      }
      case message::DISCONNECT: {
         auto &mm = static_cast<const Disconnect &>(msg);
         if (m_isMaster) {
             if (mm.isNotification()) {
                 CERR << "discarding notification on master: " << msg << std::endl;
                 return true;
             }
             if (m_stateTracker.handleDisconnect(mm)) {
                 handlePriv(mm);
                 handleQueue();
             } else {
                 m_queue.emplace_back(msg);
                 return true;
             }
         } else {
             if (mm.isNotification()) {
                 m_stateTracker.handle(mm);
                 sendManager(mm);
                 sendUi(mm);
             } else {
                 sendMaster(mm);
             }
             return true;
         }
      }
      default:
         break;
   }

   const int dest = idToHub(msg.destId());
   const int sender = idToHub(msg.senderId());

   {
       bool mgr=false, ui=false, master=false, slave=false;

       if (m_hubId == Id::MasterHub) {
           if (msg.destId() == Id::ForBroadcast)
               msg.setDestId(Id::Broadcast);
       }
       bool track = Router::the().toTracker(msg, senderType) && !masterAdded;
       m_stateTracker.handle(msg, track);

       if (Router::the().toManager(msg, senderType, sender)
               || (Id::isModule(msg.destId()) && dest == m_hubId)) {

           sendManager(msg);
           mgr = true;
       }
       if (Router::the().toUi(msg, senderType)) {
           sendUi(msg);
           ui = true;
       }
       if (Id::isModule(msg.destId()) && !Id::isHub(dest)) {
           CERR << "failed to translate module id " << msg.destId() << " to hub" << std::endl;
       } else  if (!Id::isHub(dest)) {
           if (Router::the().toMasterHub(msg, senderType, sender)) {
               sendMaster(msg);
               master = true;
           }
           if (Router::the().toSlaveHub(msg, senderType, sender)) {
               sendSlaves(msg, msg.destId()==Id::Broadcast /* return to sender */ );
               slave = true;
           }
           vassert(!(slave && master));
       } else {
           if (dest != m_hubId) {
               if (m_isMaster) {
                   sendHub(msg, dest);
                   //sendSlaves(msg);
                   slave = true;
               } else if (sender == m_hubId) {
                   sendMaster(msg);
                   master = true;
               }
           }
       }

       if (m_traceMessages == message::ANY || msg.type() == m_traceMessages
#ifdef DEBUG_DISTRIBUTED
               || msg.type() == message::ADDOBJECT
               || msg.type() == message::ADDOBJECTCOMPLETED
#endif
               ) {
           if (track) std::cerr << "t"; else std::cerr << ".";
           if (mgr) std::cerr << "m" ;else std::cerr << ".";
           if (ui) std::cerr << "u"; else std::cerr << ".";
           if (slave) { std::cerr << "s"; } else if (master) { std::cerr << "M"; } else std::cerr << ".";
           std::cerr << " " << msg << std::endl;
       }
   }

   if (Router::the().toHandler(msg, senderType)) {

      switch(msg.type()) {

         case message::SPAWN: {
            auto &spawn = static_cast<const Spawn &>(msg);
            if (m_isMaster) {
               auto notify = spawn;
               notify.setSenderId(m_hubId);
               if (spawn.spawnId() == Id::Invalid) {
                  vassert(spawn.spawnId() == Id::Invalid);
                  notify.setSpawnId(Id::ModuleBase + m_moduleCount);
                  ++m_moduleCount;
               }
               notify.setDestId(spawn.hubId());
               sendManager(notify, spawn.hubId());
               CERR << "sendManager: " << notify << std::endl;
               notify.setDestId(Id::Broadcast);
               m_stateTracker.handle(notify);
               sendManager(notify);
               sendUi(notify);
               sendSlaves(notify, true /* return to sender */);
            } else {
               if (spawn.spawnId() != Id::Invalid) {
                  m_stateTracker.handle(spawn);
                  sendManager(spawn);
                  sendUi(spawn);
               } else {
                  sendMaster(spawn);
               }
            }
            break;
         }

         case message::SPAWNPREPARED: {

#ifndef MODULE_THREAD
            auto &spawn = static_cast<const SpawnPrepared &>(msg);
            vassert(spawn.hubId() == m_hubId);

            std::string name = spawn.getName();
            AvailableModule::Key key(spawn.hubId(), name);
            auto it = m_availableModules.find(key);
            if (it == m_availableModules.end()) {
               if (spawn.hubId() == m_hubId)
                  CERR << "refusing to spawn " << name << ": not in list of available modules" << std::endl;
               return true;
            }
            std::string path = it->second.path;

            std::string executable = path;
            std::vector<std::string> argv;
            argv.push_back(executable);
            argv.push_back(Shm::instanceName(hostname(), m_port));
            argv.push_back(name);
            argv.push_back(boost::lexical_cast<std::string>(spawn.spawnId()));

			auto pid = launchProcess(argv);
			if (pid) {
				//std::cerr << "started " << executable << " with PID " << pid << std::endl;
				m_processMap[pid] = spawn.spawnId();
			} else {
				std::cerr << "program " << argv[0] << " failed to start" << std::endl;
			}
#endif
            break;
         }

         case message::SETID: {

            vassert(!m_isMaster);
            auto &set = static_cast<const SetId &>(msg);
            m_hubId = set.getId();
            message::DefaultSender::init(m_hubId, 0);
            Router::init(message::Identify::SLAVEHUB, m_hubId);
            CERR << "got hub id " << m_hubId << std::endl;
            if (m_managerConnected) {
               sendManager(set);
               for (auto &mod: m_localModules) {
                   mod.hub = m_hubId;
                   AvailableModule::Key key(mod.hub, mod.name);
                   m_availableModules.emplace(key, mod);
                   message::ModuleAvailable avail(mod.hub, mod.name, mod.path);
                   m_stateTracker.handle(avail);
                   sendMaster(avail);
               }
               m_localModules.clear();
            }
            if (m_managerConnected) {
               auto state = m_stateTracker.getState();
               for (auto &m: state) {
                  sendMessage(sock, m);
               }
               hubReady();
            }
            break;
         }

         case message::MODULEAVAILABLE: {
            auto &mm = static_cast<ModuleAvailable &>(msg);
            AvailableModule mod;
            mod.hub = mm.hub();
            mod.name = mm.name();
            mod.path = mm.path();
            if (mm.hub() == Id::Invalid && senderType == message::Identify::MANAGER) {
                mod.hub = m_hubId;
            }
            if (mod.hub == Id::Invalid && senderType == Identify::MANAGER) {
                m_localModules.emplace_back(mod);
            } else {
                AvailableModule::Key key(mod.hub, mod.name);
                m_availableModules.emplace(key, mod);
            }
            break;
         }

         case message::QUIT: {

            std::cerr << "hub: got quit: " << msg << std::endl;
            auto &quit = static_cast<const Quit &>(msg);
            if (senderType == message::Identify::MANAGER) {
               m_uiManager.requestQuit();
               if (m_isMaster)
                  sendSlaves(quit);
            } else if (senderType == message::Identify::HUB) {
               m_uiManager.requestQuit();
               sendManager(quit);
            } else if (senderType == message::Identify::UI) {
               m_uiManager.requestQuit();
               if (m_isMaster)
                  sendSlaves(quit);
               else
                  sendMaster(quit);
               sendManager(quit);
            } else {
               m_uiManager.requestQuit();
               sendSlaves(quit);
               sendManager(quit);
            }
            m_quitting = true;
            break;
         }

         case message::EXECUTE: {
            auto &exec = static_cast<const Execute &>(msg);
            handlePriv(exec);
            break;
         }

         case message::CANCELEXECUTE: {
            auto &cancel = static_cast<const CancelExecute &>(msg);
            handlePriv(cancel);
            break;
         }

         case message::BARRIER: {
            auto &barr = static_cast<const Barrier &>(msg);
            handlePriv(barr);
            break;
         }

         case message::BARRIERREACHED: {
            auto &reached = static_cast<const BarrierReached &>(msg);
            handlePriv(reached);
            break;
         }

         case message::REQUESTTUNNEL: {
            auto &tunnel = static_cast<const RequestTunnel &>(msg);
            handlePriv(tunnel);
            break;
         }

         case message::ADDPORT:
         {
             handleQueue();
             break;
         }
         default: {
            break;
         }
      }
   }

   return true;
}

bool Hub::handleQueue() {

    using namespace message;

    //CERR << "unqueuing " << m_queue.size() << " messages" << std::endl;;

    bool again = true;
    while (again) {
        again = false;
        decltype (m_queue) queue;
        for (auto &m: m_queue) {
            if (m.type() == message::CONNECT) {
                auto &mm = m.as<Connect>();
                if (m_stateTracker.handleConnect(mm)) {
                    again = true;
                    handlePriv(mm);
                } else {
                    queue.push_back(m);
                }
            } else if (m.type() == message::DISCONNECT) {
                auto &mm = m.as<Disconnect>();
                if (m_stateTracker.handleDisconnect(mm)) {
                    again = true;
                    handlePriv(mm);
                } else {
                    queue.push_back(m);
                }
            } else {
                std::cerr << "message other than Connect/Disconnect in queue: " << m << std::endl;
                queue.push_back(m);
            }
        }
        std::swap(m_queue, queue);
    }

    return true;
}

bool Hub::init(int argc, char *argv[]) {

   m_prefix = dir::prefix(argc, argv);

   m_name = hostname();

   namespace po = boost::program_options;
   po::options_description desc("usage");
   desc.add_options()
      ("help,h", "show this message")
      ("hub,c", po::value<std::string>(), "connect to hub")
      ("batch,b", "do not start user interface")
      ("gui,g", "start graphical user interface")
      ("tui,t", "start command line interface")
      ("name", "Vistle script to process or slave name")
      ;
   po::variables_map vm;
   try {
      po::positional_options_description popt;
      popt.add("name", 1);
      po::store(po::command_line_parser(argc, argv).options(desc).positional(popt).run(), vm);
      po::notify(vm);
   } catch (std::exception &e) {
      CERR << e.what() << std::endl;
      CERR << desc << std::endl;
      return false;
   }

   if (vm.count("help")) {
      CERR << desc << std::endl;
      return false;
   }

   if (vm.count("") > 0) {
      CERR << desc << std::endl;
      return false;
   }

   startServer();
   m_dataProxy.reset(new DataProxy(*this, m_port+1));

   std::string uiCmd = "vistle_gui";

   if (vm.count("hub") > 0) {
      m_isMaster = false;
      m_masterHost = vm["hub"].as<std::string>();
      auto colon = m_masterHost.find(':');
      if (colon != std::string::npos) {
         m_masterPort = boost::lexical_cast<unsigned short>(m_masterHost.substr(colon+1));
         m_masterHost = m_masterHost.substr(0, colon);
      }

      if (!connectToMaster(m_masterHost, m_masterPort)) {
         CERR << "failed to connect to master at " << m_masterHost << ":" << m_masterPort << std::endl;
         return false;
      }
   } else {
      // this is the master hub
      m_hubId = Id::MasterHub;
      message::DefaultSender::init(m_hubId, 0);
      Router::init(message::Identify::HUB, m_hubId);

      message::AddHub master(m_hubId, m_name);
      master.setPort(m_port);
      master.setDataPort(m_dataProxy->port());
      m_stateTracker.handle(master);
      m_masterPort = m_port;
   }

   // start UI
   if (const char *pbs_env = getenv("PBS_ENVIRONMENT")) {
      if (std::string("PBS_INTERACTIVE") != pbs_env) {
         if (!vm.count("batch"))
            CERR << "apparently running in PBS batch mode - defaulting to no UI" << std::endl;
         uiCmd.clear();
      }
   }
   if (vm.count("gui")) {
      uiCmd = "vistle_gui";
   } else if (vm.count("tui")) {
      uiCmd = "blower";
   }
   if (vm.count("batch")) {
      uiCmd.clear();
   }

   if (!uiCmd.empty()) {
      std::string uipath = dir::bin(m_prefix) + "/" + uiCmd;
      startUi(uipath);
   }

   if (vm.count("name") == 1) {
      if (m_isMaster)
         m_scriptPath = vm["name"].as<std::string>();
      else
         m_name = vm["name"].as<std::string>();
   }

   std::string port = boost::lexical_cast<std::string>(this->port());
   std::string dataPort = boost::lexical_cast<std::string>(m_dataProxy->port());

   // start manager on cluster
   std::string cmd = dir::bin(m_prefix) + "/vistle_manager";
   std::vector<std::string> args;
   args.push_back(cmd);
   args.push_back(hostname());
   args.push_back(port);
   args.push_back(dataPort);
   auto pid = launchProcess(args);
   if (!pid) {
      CERR << "failed to spawn Vistle manager " << std::endl;
      exit(1);
   }
   m_processMap[pid] = 0;

   return true;
}

bool Hub::startCleaner() {

#if defined(MODULE_THREAD) && defined(NO_SHMEM)
   return true;
#endif

#ifdef SHMDEBUG
   CERR << "SHMDEBUG: preserving shm for session " << Shm::instanceName(hostname(), m_port) << std::endl;
   return true;
#endif

   if (getenv("SLURM_JOB_ID")) {
      CERR << "not starting under clean_vistle - running SLURM" << std::endl;
      return false;
   }

   // run clean_vistle on cluster
   std::string cmd = dir::bin(m_prefix) + "/clean_vistle";
   std::vector<std::string> args;
   args.push_back(cmd);
   std::string shmname = Shm::instanceName(hostname(), m_port);
   args.push_back(shmname);
   auto pid = launchProcess(args);
   if (!pid) {
      CERR << "failed to spawn clean_vistle" << std::endl;
      return false;
   }
   m_processMap[pid] = -1;
   return true;
}

bool Hub::connectToMaster(const std::string &host, unsigned short port) {

   vassert(!m_isMaster);

   asio::ip::tcp::resolver resolver(m_ioService);
   asio::ip::tcp::resolver::query query(host, boost::lexical_cast<std::string>(port));
   asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
   boost::system::error_code ec;
   m_masterSocket.reset(new boost::asio::ip::tcp::socket(m_ioService));
   asio::connect(*m_masterSocket, endpoint_iterator, ec);
   if (ec) {
      CERR << "could not establish connection to " << host << ":" << port << std::endl;
      return false;
   }

#if 0
   message::Identify ident(message::Identify::SLAVEHUB, m_name);
   sendMessage(m_masterSocket, ident);
#endif
   addSocket(m_masterSocket, message::Identify::HUB);
   addClient(m_masterSocket);

   CERR << "connected to master at " << host << ":" << port << std::endl;
   return true;
}

bool Hub::startUi(const std::string &uipath) {

   std::string port = boost::lexical_cast<std::string>(this->m_masterPort);

   std::vector<std::string> args;
   args.push_back(uipath);
   args.push_back("-from-vistle");
   args.push_back(m_masterHost);
   args.push_back(port);
   auto pid = vistle::spawn_process(uipath, args);
   if (!pid) {
      CERR << "failed to spawn UI " << uipath << std::endl;
      return false;
   }

   m_processMap[pid] = -1;

   return true;
}

bool Hub::processScript() {

   vassert(m_uiManager.isLocked());
#ifdef HAVE_PYTHON
   if (!m_scriptPath.empty()) {
      PythonInterpreter inter(m_scriptPath, dir::share(m_prefix));
      while(inter.check()) {
         dispatch();
      }
   }
   return true;
#else
   return false;
#endif
}

bool Hub::handlePriv(const message::Execute &exec) {

   message::Execute toSend(exec);
   if (exec.getExecutionCount() > m_execCount)
      m_execCount = exec.getExecutionCount();
   if (exec.getExecutionCount() < 0)
      toSend.setExecutionCount(++m_execCount);

   if (Id::isModule(exec.getModule())) {
      const int hub = m_stateTracker.getHub(exec.getModule());
      toSend.setDestId(exec.getModule());
      sendManager(toSend, hub);
   } else {
      // execute all sources in dataflow graph
      for (auto &mod: m_stateTracker.runningMap) {
         int id = mod.first;
         int hub = mod.second.hub;
         auto inputs = m_stateTracker.portTracker()->getInputPorts(id);
         bool isSource = true;
         for (auto &input: inputs) {
            if (!input->connections().empty())
               isSource = false;
         }
         if (isSource) {
            toSend.setModule(id);
            toSend.setDestId(id);
            sendManager(toSend, hub);
         }
      }
   }

   return true;
}

bool Hub::handlePriv(const message::CancelExecute &cancel) {

    message::CancelExecute toSend(cancel);

    if (Id::isModule(cancel.getModule())) {
        const int hub = m_stateTracker.getHub(cancel.getModule());
        toSend.setDestId(cancel.getModule());
        sendManager(toSend, hub);
    }

    return true;
}

bool Hub::handlePriv(const message::Barrier &barrier) {

   CERR << "Barrier request: " << barrier.uuid() << " by " << barrier.senderId() << std::endl;
   vassert(!m_barrierActive);
   vassert(m_barrierReached == 0);
   m_barrierActive = true;
   m_barrierUuid = barrier.uuid();
   message::Buffer buf(barrier);
   buf.setDestId(Id::NextHop);
   if (m_isMaster)
      sendSlaves(buf, true);
   sendManager(buf);
   return true;
}

bool Hub::handlePriv(const message::BarrierReached &reached) {

   ++m_barrierReached;
   CERR << "Barrier " << reached.uuid() << " reached by " << reached.senderId() << " (now " << m_barrierReached << ")" << std::endl;
   vassert(m_barrierActive);
   vassert(m_barrierUuid == reached.uuid());
   // message must be received from local manager and each slave
   if (m_isMaster) {
      if (m_barrierReached == m_slaves.size()+1) {
         m_barrierActive = false;
         m_barrierReached = 0;
         message::BarrierReached r(reached.uuid());
         r.setDestId(Id::NextHop);
         m_stateTracker.handle(r);
         sendUi(r);
         sendSlaves(r);
         sendManager(r);
      }
   } else {
      if (reached.senderId() == Id::MasterHub) {
         m_barrierActive = false;
         m_barrierReached = 0;
         sendUi(reached);
         sendManager(reached);
      }
   }
   return true;
}

bool Hub::handlePriv(const message::RequestTunnel &tunnel) {

    return m_tunnelManager.processRequest(tunnel);
}

bool Hub::handlePriv(const message::Connect &conn) {

    message::Connect c(conn);
    c.setNotify(true);
    sendUi(c);
    sendManager(c);
    sendSlaves(c);

    return true;
}

bool Hub::handlePriv(const message::Disconnect &disc) {

    message::Disconnect d(disc);
    d.setNotify(true);
    sendUi(d);
    sendManager(d);
    sendSlaves(d);

    return true;
}

int main(int argc, char *argv[]) {

   try {
      Hub hub;
      if (!hub.init(argc, argv)) {
         return 1;
      }

      while(hub.dispatch())
         ;
   } catch (vistle::exception &e) {
      std::cerr << "Hub: fatal exception: " << e.what() << std::endl << e.where() << std::endl;
      return 1;
   } catch (std::exception &e) {
      std::cerr << "Hub: fatal exception: " << e.what() << std::endl;
      return 1;
   }

   return 0;
}
