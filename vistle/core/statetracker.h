#ifndef STATETRACKER_H
#define STATETRACKER_H

#include <vector>
#include <map>
#include <set>
#include <string>

#include <mutex>
#include <condition_variable>

#include "export.h"
#include "message.h"
#include "messages.h"

namespace vistle {

class Parameter;
typedef std::set<std::shared_ptr<Parameter>> ParameterSet;
class PortTracker;

class V_COREEXPORT StateObserver {

 public:

   StateObserver(): m_modificationCount(0) {}
   virtual ~StateObserver() {}

   virtual void moduleAvailable(int hub, const std::string &moduleName, const std::string &path) = 0;

   virtual void newModule(int moduleId, const boost::uuids::uuid &spawnUuid, const std::string &moduleName) = 0;
   virtual void deleteModule(int moduleId) = 0;

   enum ModuleStateBits {
      Unknown = 0,
      Known = 1,
      Initialized = 2,
      Killed = 4,
      Quit = 8,
      Busy = 16,
   };
   virtual void moduleStateChanged(int moduleId, int stateBits) = 0;

   virtual void newParameter(int moduleId, const std::string &parameterName) = 0;
   virtual void parameterValueChanged(int moduleId, const std::string &parameterName) = 0;
   virtual void parameterChoicesChanged(int moduleId, const std::string &parameterName) = 0;
   virtual void deleteParameter(int moduleId, const std::string &parameterName) = 0;

   virtual void newPort(int moduleId, const std::string &portName) = 0;
   virtual void deletePort(int moduleId, const std::string &portName) = 0;

   virtual void newConnection(int fromId, const std::string &fromName,
         int toId, const std::string &toName) = 0;

   virtual void deleteConnection(int fromId, const std::string &fromName,
         int toId, const std::string &toName) = 0;

   virtual void info(const std::string &text, message::SendText::TextType textType, int senderId, int senderRank, message::Type refType, const message::uuid_t &refUuid) = 0;

   virtual void quitRequested();

   virtual void resetModificationCount();
   virtual void incModificationCount();
   long modificationCount() const;

private:
   long m_modificationCount;
};

struct V_COREEXPORT HubData {

    HubData(int id, const std::string &name)
        : id(id)
        , name(name)
        , port(0)
        , dataPort(0)
    {}

    int id;
    std::string name;
    unsigned short port;
    unsigned short dataPort;
    boost::asio::ip::address address;
};

class V_COREEXPORT StateTracker {
   friend class ClusterManager;
   friend class Hub;
   friend class DataProxy;
   friend class PortTracker;

 public:
   StateTracker(const std::string &name, std::shared_ptr<PortTracker> portTracker=std::shared_ptr<PortTracker>());
   ~StateTracker();

   typedef std::recursive_mutex mutex;
   typedef std::unique_lock<mutex> mutex_locker;
   mutex &getMutex();

   bool dispatch(bool &received);

   int getMasterHub() const;
   std::vector<int> getHubs() const;
   std::vector<int> getSlaveHubs() const;
   const std::string &hubName(int id) const;
   std::vector<int> getRunningList() const;
   std::vector<int> getBusyList() const;
   int getHub(int id) const;
   const HubData &getHubData(int id) const;
   std::string getModuleName(int id) const;
   int getModuleState(int id) const;

   std::vector<std::string> getParameters(int id) const;
   std::shared_ptr<Parameter> getParameter(int id, const std::string &name) const;

   ParameterSet getConnectedParameters(const Parameter &param) const;

   bool handle(const message::Message &msg, bool track=true);
   bool handleConnect(const message::Connect &conn);
   bool handleDisconnect(const message::Disconnect &disc);

   std::shared_ptr<PortTracker> portTracker() const;

   std::vector<message::Buffer> getState() const;

   const std::vector<AvailableModule> &availableModules() const;

   void registerObserver(StateObserver *observer);

   bool registerRequest(const message::uuid_t &uuid);
   std::shared_ptr<message::Buffer> waitForReply(const message::uuid_t &uuid);

   std::vector<int> waitForSlaveHubs(size_t count);
   std::vector<int> waitForSlaveHubs(const std::vector<std::string> &names);

   int graphChangeCount() const;

 protected:
   std::shared_ptr<message::Buffer> removeRequest(const message::uuid_t &uuid);
   bool registerReply(const message::uuid_t &uuid, const message::Message &msg);

   typedef std::map<std::string, std::shared_ptr<Parameter>> ParameterMap;
   typedef std::map<int, std::string> ParameterOrder;
   struct Module {
      int id;
      int hub;
      bool initialized;
      bool killed;
      bool busy;
      std::string name;
      ParameterMap parameters;
      ParameterOrder paramOrder;
      int height; //< length of shortest path to a sink

      message::ObjectReceivePolicy::Policy objectPolicy;
      message::SchedulingPolicy::Schedule schedulingPolicy;
      message::ReducePolicy::Reduce reducePolicy;

      int state() const;
      bool isSink() const { return height==0; }
      Module(int id, int hub): id(id), hub(hub), initialized(false), killed(false), busy(false), height(0),
         objectPolicy(message::ObjectReceivePolicy::Local), schedulingPolicy(message::SchedulingPolicy::Single), reducePolicy(message::ReducePolicy::Locally)
      {}
   };

   typedef std::map<int, Module> RunningMap;
   RunningMap runningMap; //< currently running modules on all connected clusters
   void computeHeights(); //< compute heights for all modules in runningMap
   RunningMap quitMap; //< history of already terminated modules - for module -> hub mapping
   typedef std::set<int> ModuleSet;
   ModuleSet busySet;
   int m_graphChangeCount = 0;

   std::vector<AvailableModule> m_availableModules;

   std::set<StateObserver *> m_observers;

   std::vector<message::Buffer> m_queue;
   void processQueue();
   void cleanQueue(int moduleId);

 private:
   bool handlePriv(const message::AddHub &slave);
   bool handlePriv(const message::Ping &ping);
   bool handlePriv(const message::Pong &pong);
   bool handlePriv(const message::Trace &trace);
   bool handlePriv(const message::Spawn &spawn);
   bool handlePriv(const message::Started &started);
   bool handlePriv(const message::Connect &connect);
   bool handlePriv(const message::Disconnect &disc);
   bool handlePriv(const message::ModuleExit &moduleExit);
   bool handlePriv(const message::Execute &execute);
   bool handlePriv(const message::ExecutionProgress &prog);
   bool handlePriv(const message::Busy &busy);
   bool handlePriv(const message::Idle &idle);
   bool handlePriv(const message::AddPort &createPort);
   bool handlePriv(const message::RemovePort &destroyPort);
   bool handlePriv(const message::AddParameter &addParam);
   bool handlePriv(const message::RemoveParameter &removeParam);
   bool handlePriv(const message::SetParameter &setParam);
   bool handlePriv(const message::SetParameterChoices &choices);
   bool handlePriv(const message::Kill &kill);
   bool handlePriv(const message::AddObject &addObj);
   bool handlePriv(const message::ObjectReceived &objRecv);
   bool handlePriv(const message::Barrier &barrier);
   bool handlePriv(const message::BarrierReached &barrierReached);
   bool handlePriv(const message::SendText &info);
   bool handlePriv(const message::ReplayFinished &reset);
   bool handlePriv(const message::Quit &quit);
   bool handlePriv(const message::ModuleAvailable &mod);
   bool handlePriv(const message::ObjectReceivePolicy &pol);
   bool handlePriv(const message::ReducePolicy &pol);
   bool handlePriv(const message::SchedulingPolicy &pol);
   bool handlePriv(const message::RequestTunnel &tunnel);

   std::shared_ptr<PortTracker> m_portTracker;

   std::set<message::uuid_t> m_alreadySeen;

   mutex m_replyMutex;
   std::condition_variable_any m_replyCondition;
   std::map<message::uuid_t, std::shared_ptr<message::Buffer>> m_outstandingReplies;

   mutex m_slaveMutex;
   std::condition_variable_any m_slaveCondition;

   message::Type m_traceType;
   int m_traceId;
   std::string m_name;
   std::vector<HubData> m_hubs;
};

} // namespace vistle

#endif
