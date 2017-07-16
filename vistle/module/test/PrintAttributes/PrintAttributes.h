#ifndef PRINTATTRIBUTES_H
#define PRINTATTRIBUTES_H

#include <module/module.h>

class PrintAttributes: public vistle::Module {

 public:
   PrintAttributes(const std::string &name, int moduleID, mpi::communicator comm);
   ~PrintAttributes();

 private:
   virtual bool compute();
};

#endif
