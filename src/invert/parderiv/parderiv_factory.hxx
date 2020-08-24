class ParDerivFactory;

#ifndef __PARDERIV_FACTORY_H__
#define __PARDERIV_FACTORY_H__

#include <invert_parderiv.hxx>
#include <options.hxx>

class ParDerivFactory {
 public:
  /// Return a pointer to the only instance
  static ParDerivFactory* getInstance();
  
  InvertPar* createInvertPar(CELL_LOC location = CELL_CENTRE,
                             Mesh* mesh_in = bout::globals::mesh);
  InvertPar *createInvertPar(const char *type, Options *opt = nullptr,
                             CELL_LOC location = CELL_CENTRE,
                             Mesh* mesh_in = bout::globals::mesh);
  InvertPar* createInvertPar(Options *opts, CELL_LOC location = CELL_CENTRE,
                             Mesh* mesh_in = bout::globals::mesh);
 private:
  ParDerivFactory() {} // Prevent instantiation of this class
  static ParDerivFactory* instance; ///< The only instance of this class (Singleton)
};

#endif // __PARDERIV_FACTORY_H__
