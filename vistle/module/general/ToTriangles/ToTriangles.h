#ifndef TOTRIANGLES_H
#define TOTRIANGLES_H

#include <module/module.h>

class ToTriangles: public vistle::Module {

 public:
   ToTriangles(const std::string &name, int moduleID, mpi::communicator comm);
   ~ToTriangles();

 private:
   virtual bool compute();
};

#endif
