/**************************************************************************
 * Base class for all solvers. Specifies required interface functions
 *
 * Changelog:
 *
 * 2009-08 Ben Dudson, Sean Farley
 *    * Major overhaul, and changed API. Trying to make consistent
 *      interface to PETSc and SUNDIALS solvers
 *
 * 2013-08 Ben Dudson
 *    * Added OO-style API, to allow multiple physics models to coexist
 *      For now both APIs are supported
 *
 **************************************************************************
 * Copyright 2010 B.D.Dudson, S.Farley, M.V.Umansky, X.Q.Xu
 *
 * Contact: Ben Dudson, bd512@york.ac.uk
 *
 * This file is part of BOUT++.
 *
 * BOUT++ is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * BOUT++ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with BOUT++.  If not, see <http://www.gnu.org/licenses/>.
 *
 **************************************************************************/

#ifndef __SOLVER_H__
#define __SOLVER_H__

#include "bout_types.hxx"
#include "boutexception.hxx"
#include "datafile.hxx"
#include "options.hxx"
#include "unused.hxx"
#include "bout/monitor.hxx"

///////////////////////////////////////////////////////////////////
// C function pointer types

class Solver;

/// RHS function pointer
using rhsfunc = int (*)(BoutReal);

/// User-supplied preconditioner function
using PhysicsPrecon = int (*)(BoutReal t, BoutReal gamma, BoutReal delta);

/// User-supplied Jacobian function
using Jacobian = int (*)(BoutReal t);

/// Solution monitor, called each timestep
using TimestepMonitorFunc = int (*)(Solver* solver, BoutReal simtime, BoutReal lastdt);

//#include "globals.hxx"
#include "field2d.hxx"
#include "field3d.hxx"
#include "vector2d.hxx"
#include "vector3d.hxx"

#define BOUT_NO_USING_NAMESPACE_BOUTGLOBALS
#include "physicsmodel.hxx"
#undef BOUT_NO_USING_NAMESPACE_BOUTGLOBALS

#include <list>
#include <string>

using SolverType = std::string;
constexpr auto SOLVERCVODE = "cvode";
constexpr auto SOLVERPVODE = "pvode";
constexpr auto SOLVERIDA = "ida";
constexpr auto SOLVERPETSC = "petsc";
constexpr auto SOLVERSLEPC = "slepc";
constexpr auto SOLVERKARNIADAKIS = "karniadakis";
constexpr auto SOLVERRK4 = "rk4";
constexpr auto SOLVEREULER = "euler";
constexpr auto SOLVERRK3SSP = "rk3ssp";
constexpr auto SOLVERPOWER = "power";
constexpr auto SOLVERARKODE = "arkode";
constexpr auto SOLVERIMEXBDF2 = "imexbdf2";
constexpr auto SOLVERSNES = "snes";
constexpr auto SOLVERRKGENERIC = "rkgeneric";

enum SOLVER_VAR_OP {LOAD_VARS, LOAD_DERIVS, SET_ID, SAVE_VARS, SAVE_DERIVS};

///////////////////////////////////////////////////////////////////

/*!
 * Interface to integrators, mainly for time integration
 *
 *
 * Creation
 * --------
 *
 * Solver is a base class and can't be created directly:
 *
 *     Solver *solver = Solver(); // Error
 *
 * Instead, use the create() static function:
 *
 *     Solver *solver = Solver::create(); // ok
 *
 * By default this will use the options in the "solver" section
 * of the options, equivalent to:
 *
 *     Options *opts = Options::getRoot()->getSection("solver");
 *     Solver *solver = Solver::create(opts);
 *
 * To use a different set of options, for example if there are
 * multiple solvers, use a different option section:
 *
 *     Options *opts = Options::getRoot()->getSection("anothersolver");
 *     Solver *anothersolver = Solver::create(opts);
 *
 * Problem specification
 * ---------------------
 *
 * The equations to be solved are specified in a PhysicsModel object
 *
 *     class MyProblem : public PhysicsModel {
 *       protected:
 *         // This function called once at beginning
 *         int init(bool restarting) {
 *           SOLVE_FOR(f);   // Specify variables to solve
 *           // Set f to initial value if needed
 *           // otherwise read from input options
 *           return 0;
 *         }
 *
 *         // This function called to evaluate time derivatives
 *         int rhs(BoutReal t) {
 *            ddt(f) = 1.0; // Calculate time derivatives
 *            return 0;
 *         }
 *       private:
 *         Field3D f; // A variable to evolve
 *     }
 *
 * The init() and rhs() functions must be defined, but there
 * are other functions which can be defined. See PhysicsModel
 * documentation for details.
 *
 * Create an object, then add to the solver:
 *
 *     MyProblem *prob = MyProblem();
 *     solver->setModel(prob);
 *
 * Running simulation
 * ------------------
 *
 * To run a calculation
 *
 *     solver->solve();
 *
 * This will use NOUT and TIMESTEP in the solver options
 * (specified during creation). If these are not present
 * then the global options will be used.
 *
 * To specify NOUT and TIMESTEP, pass the values to solve:
 *
 *     solver->solve(NOUT, TIMESTEP);
 */
class Solver {
public:
  Solver(Options* opts = nullptr);
  virtual ~Solver();

  /////////////////////////////////////////////
  // New API

  /// Specify physics model to solve. Currently only one model can be
  /// evolved by a Solver.
  virtual void setModel(PhysicsModel* model);

  /////////////////////////////////////////////
  // Old API

  /// Set the RHS function
  virtual void setRHS(rhsfunc f) { phys_run = f; }
  /// Specify a preconditioner (optional)
  void setPrecon(PhysicsPrecon f) { prefunc = f; }
  /// Specify a Jacobian (optional)
  virtual void setJacobian(Jacobian UNUSED(j)) {}
  /// Split operator solves
  virtual void setSplitOperator(rhsfunc fC, rhsfunc fD);

  /////////////////////////////////////////////
  // Monitors

  /// A type to set where in the list monitors are added
  enum MonitorPosition { BACK, FRONT };

  /// Add a monitor to be called every output
  void addMonitor(Monitor* f, MonitorPosition pos = FRONT);
  /// Remove a monitor function previously added
  void removeMonitor(Monitor* f);

  /// Add a monitor function to be called every timestep
  void addTimestepMonitor(TimestepMonitorFunc f);
  /// Remove a previously added timestep monitor
  void removeTimestepMonitor(TimestepMonitorFunc f);

  /////////////////////////////////////////////
  // Routines to add variables. Solvers can just call these
  // (or leave them as-is)

  /// Add a variable to be solved. This must be done in the
  /// initialisation stage, before the simulation starts.
  virtual void add(Field2D& v, const std::string name);
  virtual void add(Field3D& v, const std::string name);
  virtual void add(Vector2D& v, const std::string name);
  virtual void add(Vector3D& v, const std::string name);

  /// Returns true if constraints available
  virtual bool constraints() { return has_constraints; }

  /// Add constraint functions (optional). These link a variable v to
  /// a control parameter C_v such that v is adjusted to keep C_v = 0.
  virtual void constraint(Field2D& v, Field2D& C_v, const std::string name);
  virtual void constraint(Field3D& v, Field3D& C_v, const std::string name);
  virtual void constraint(Vector2D& v, Vector2D& C_v, const std::string name);
  virtual void constraint(Vector3D& v, Vector3D& C_v, const std::string name);

  /// Set a maximum internal timestep (only for explicit schemes)
  virtual void setMaxTimestep(BoutReal dt) { max_dt = dt; }
  /// Return the current internal timestep
  virtual BoutReal getCurrentTimestep() { return 0.0; }

  /// Start the solver. By default solve() uses options
  /// to determine the number of steps and the output timestep.
  /// If nout and dt are specified here then the options are not used
  ///
  /// @param[in] nout   Number of output timesteps
  /// @param[in] dt     The time between outputs
  int solve(int nout = -1, BoutReal dt = 0.0);

  /// Initialise the solver
  /// NOTE: nout and tstep should be passed to run, not init.
  ///       Needed because of how the PETSc TS code works
  virtual int init(int nout, BoutReal tstep);

  /// Run the solver, calling monitors nout times, at intervals of
  /// tstep. This function is called by solve(), and is specific to
  /// each solver type
  ///
  /// This should probably be protected, since it shouldn't be called
  /// by users.
  virtual int run() = 0;

  /// Should wipe out internal field vector and reset from current field object data
  virtual void resetInternalFields() {
    throw BoutException("resetInternalFields not supported by this Solver");
  }

  // Solver status. Optional functions used to query the solver
  /// Number of 2D variables. Vectors count as 3
  virtual int n2Dvars() const { return f2d.size(); }
  /// Number of 3D variables. Vectors count as 3
  virtual int n3Dvars() const { return f3d.size(); }

  /// Get and reset the number of calls to the RHS function
  int resetRHSCounter();
  /// Same but for explicit timestep counter - for IMEX
  int resetRHSCounter_e();
  /// Same but fur implicit timestep counter - for IMEX
  int resetRHSCounter_i();

  /// Test if this solver supports split operators (e.g. implicit/explicit)
  bool splitOperator() { return split_operator; }

  bool canReset{false};

  /// Add evolving variables to output (dump) file or restart file
  ///
  /// @param[inout] outputfile   The file to add variable to
  /// @param[in] save_repeat    If true, add variables with time dimension
  virtual void outputVars(Datafile& outputfile, bool save_repeat = true);

  /// Create a Solver object. This uses the "type" option in the given
  /// Option section to determine which solver type to create.
  static Solver* create(Options* opts = nullptr);

  /// Create a Solver object, specifying the type
  static Solver* create(const SolverType& type, Options* opts = nullptr);

  /// Pass the command-line arguments. This static function is
  /// called by BoutInitialise, and puts references
  /// into protected variables. These may then be used by Solvers
  /// to control behavior
  static void setArgs(int& c, char**& v) {
    pargc = &c;
    pargv = &v;
  }

protected:
  /// Number of command-line arguments
  static int* pargc;
  /// Command-line arguments
  static char*** pargv;

  /// Settings to use during initialisation (set by constructor)
  Options* options{nullptr};

  /// Number of processors
  int NPES{1};
  /// This processor's index
  int MYPE{0};

  /// Calculate the number of evolving variables on this processor
  int getLocalN();

  /// A structure to hold an evolving variable
  template <class T>
  struct VarStr {
    bool constraint{false};          /// Does F_var represent a constraint?
    T* var{nullptr};                 /// The evolving variable
    T* F_var{nullptr};               /// The time derivative or constraint on var
    T* MMS_err{nullptr};             /// Error for MMS
    CELL_LOC location{CELL_DEFAULT}; /// For fields and vector components
    bool covariant{false};           /// For vectors
    bool evolve_bndry{false};        /// Are the boundary regions being evolved?
    std::string name;                /// Name of the variable
  };

  /// Does \p var represent field \p name?
  template<class T>
  friend bool operator==(const VarStr<T>& var, const std::string& name) {
    return var.name == name;
  }

  /// Does \p vars contain a field with \p name?
  template<class T>
  bool contains(const std::vector<VarStr<T>>& vars, const std::string& name) {
    const auto in_vars = std::find(begin(vars), end(vars), name);
    return in_vars != end(vars);
  }

  /// Helper function for getLocalN: return the number of points to
  /// evolve in \p f, plus the accumulator \p value
  ///
  /// If f.evolve_bndry, includes the boundary (NB: not guard!) points
  ///
  /// FIXME: This could be a lambda local to getLocalN with an `auto`
  /// argument in C++14
  template <class T>
  friend int local_N_sum(int value, const VarStr<T>& f);

  /// Vectors of variables to evolve
  std::vector<VarStr<Field2D>> f2d;
  std::vector<VarStr<Field3D>> f3d;
  std::vector<VarStr<Vector2D>> v2d;
  std::vector<VarStr<Vector3D>> v3d;

  /// Can this solver handle constraints? Set to true if so.
  bool has_constraints{false};
  /// Has init been called yet?
  bool initialised{false};

  /// Current simulation time
  BoutReal simtime{0.0};
  /// Current iteration (output time-step) number
  int iteration{0};

  /// Run the user's RHS function
  int run_rhs(BoutReal t);
  /// Calculate only the convective parts
  int run_convective(BoutReal t);
  /// Calculate only the diffusive parts
  int run_diffusive(BoutReal t, bool linear = true);

  /// Calls all monitor functions
  int call_monitors(BoutReal simtime, int iter, int NOUT);

  /// Should timesteps be monitored?
  bool monitor_timestep{false};
  int call_timestep_monitors(BoutReal simtime, BoutReal lastdt);

  /// Do we have a user preconditioner?
  bool have_user_precon();
  int run_precon(BoutReal t, BoutReal gamma, BoutReal delta);

  // Loading data from BOUT++ to/from solver
  void load_vars(BoutReal* udata);
  void load_derivs(BoutReal* udata);
  void save_vars(BoutReal* udata);
  void save_derivs(BoutReal* dudata);
  void set_id(BoutReal* udata);

  /// Returns a Field3D containing the global indices
  Field3D globalIndex(int localStart);

  /// Maximum internal timestep
  BoutReal max_dt{-1.0};

  /// Get the list of monitors
  auto getMonitors() const -> const std::list<Monitor*>& { return monitors; }
  /// Get the list of timestep monitors
  auto getTimestepMonitors() const -> const std::list<TimestepMonitorFunc>& {
    return timestep_monitors;
  }

private:
  /// Number of calls to the RHS function
  int rhs_ncalls{0};
  /// Number of calls to the explicit (convective) RHS function
  int rhs_ncalls_e{0};
  /// Number of calls to the implicit (diffusive) RHS function
  int rhs_ncalls_i{0};
  /// Has the init function of the solver been called?
  bool initCalled{false};
  /// Default sampling rate at which to call monitors - same as output to screen
  int freqDefault{1};
  /// timestep - shouldn't be changed after init is called.
  BoutReal timestep{-1};
  /// Physics model being evolved
  PhysicsModel* model{nullptr};

  /// The user's RHS function
  rhsfunc phys_run{nullptr};
  /// The user's preconditioner function
  PhysicsPrecon prefunc{nullptr};
  /// Is the physics model using separate convective (explicit) and
  /// diffusive (implicit) RHS functions?
  bool split_operator{false};
  /// Convective part (if split operator)
  rhsfunc phys_conv{nullptr};
  /// Diffusive part (if split operator)
  rhsfunc phys_diff{nullptr};

  /// Enable sources and solutions for Method of Manufactured Solutions
  bool mms{false};
  /// Initialise variables to the manufactured solution
  bool mms_initialise{false};

  void add_mms_sources(BoutReal t);
  void calculate_mms_error(BoutReal t);

  /// List of monitor functions
  std::list<Monitor*> monitors;
  /// List of timestep monitor functions
  std::list<TimestepMonitorFunc> timestep_monitors;

  /// Should be run before user RHS is called
  void pre_rhs(BoutReal t);
  /// Should be run after user RHS is called
  void post_rhs(BoutReal t);

  /// Loading data from BOUT++ to/from solver
  void loop_vars_op(Ind2D i2d, BoutReal* udata, int& p, SOLVER_VAR_OP op, bool bndry);
  void loop_vars(BoutReal* udata, SOLVER_VAR_OP op);

  /// Check if a variable has already been added
  bool varAdded(const std::string& name);
};

#endif // __SOLVER_H__
