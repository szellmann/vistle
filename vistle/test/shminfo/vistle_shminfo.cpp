/*
 * Visualization Testing Laboratory for Exascale Computing (VISTLE)
 */


#include <iostream>
#include <fstream>
#include <cstdlib>

#include <mpi.h>

#include <core/shm.h>
#include <core/object.h>
#include <core/shmvector.h>

using namespace vistle;

int main(int argc, char ** argv) {

    vistle::registerTypes();

    MPI_Init(&argc, &argv);
    int rank = -1, size = -1;
    int forceRank = -1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    std::string shmid;

    if (argc > 2) {
        shmid = argv[2];
    } else {
        std::fstream shmlist;
        shmlist.open(Shm::shmIdFilename().c_str(), std::ios::in);

        while (!shmlist.eof() && !shmlist.fail()) {
            std::string id;
            shmlist >> id;
            if (!id.empty() && id.find("_recv_") == std::string::npos && id.find("_send_") == std::string::npos) {
                shmid = id;
            }
        }

        if (shmid.empty()) {
            std::cerr << "vistle session id is required" << std::endl;
            exit(1);
        } else {
            std::cerr << "defaulting to vistle session id " << shmid << std::endl;
        }
    }

    if (argc > 1) {
        forceRank = atoi(argv[1]);
        size = 1;
    }

    try {
        Shm::attach(shmid, -1, forceRank >= 0 ? forceRank : rank);
    } catch (std::exception &ex) {
        std::cerr << "failed to attach to " << shmid << ": " << ex.what() << std::endl;
        MPI_Finalize();
        exit(1);
    }

#ifndef SHMDEBUG
   std::cerr << "recompile with -DSHMDEBUG" << std::endl;
#endif

    for (int r=0; r<size; ++r) {

        if (forceRank == -1) {
            MPI_Barrier(MPI_COMM_WORLD);
            if (r != rank)
                continue;
        } else {
            if (rank != 0)
                continue;
        }

#ifdef SHMDEBUG
       for (size_t i=0; i<Shm::s_shmdebug->size(); ++i) {
           const ShmDebugInfo &info = (*Shm::s_shmdebug)[i];
           std::cout << (info.type ? info.type : '0') << " " << (info.deleted ? "del" : "ref") << " " << info.name;
           if (info.deleted) {
               std::cout << std::endl;
           } else {
               switch(info.type) {
               case 'O':
                   if (info.handle) {
                       std::shared_ptr<const Object> obj;
                       try {
                          obj = Shm::the().getObjectFromHandle(info.handle);
                       } catch (vistle::exception e) {
                          std::cout << " ERR " << e.what() << std::endl;
                          continue;
                       }
                       if (obj) {
                           std::cout << " type " << obj->getType();
                           std::cout << " ref " << obj->refcount() << std::endl;
                       } else {
                           std::cout << " ERR" << std::endl;
                       }
                   } else {
                       std::cout << " no handle" << std::endl;
                   }
                   break;
               case 'V':
                   if (info.handle) {
#ifdef NO_SHMEM
                       const void *p = info.handle;
#else
                       const void *p = Shm::the().shm().get_address_from_handle(info.handle);
#endif
                       const ShmVector<int> *v = static_cast<const ShmVector<int> *>(p);
                       std::cout << " ref " << (*v)->refcount() << std::endl;
                   } else {
                       std::cout << " no handle" << std::endl;
                   }
                   break;
               case '\0':
                   std::cout << std::endl;
                   break;
               default:
                   std::cout << " unknown type" << std::endl;
                   break;
               }
           }
       }
#endif
   }

   MPI_Finalize();

   return 0;
}
