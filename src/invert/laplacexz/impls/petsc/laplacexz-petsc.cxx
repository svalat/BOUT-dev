/*
 * LaplaceXZ implementation using PETSc
 *
 * Ben Dudson, October 2015
 *
 * Based on:
 * PETSc example of reusing preconditioner matrix (in Fortran):
 * http://www.mcs.anl.gov/petsc/petsc-current/src/ksp/ksp/examples/tutorials/ex6f.F.html
 */

#include "laplacexz-petsc.hxx"

#ifdef BOUT_HAS_PETSC  // Requires PETSc

#include <bout/assert.hxx>
#include <bout/sys/timer.hxx>

#include <msg_stack.hxx>
#include <output.hxx>

LaplaceXZpetsc::LaplaceXZpetsc(Mesh *m, Options *opt)
  : LaplaceXZ(m, opt), mesh(m), coefs_set(false) {
  /* Constructor: LaplaceXZpetsc
   * Purpose:     - Setting inversion solver options
   *              - Setting the solver method
   *              - Setting the preconditioner
   *              - Allocating memory to the matrix A (not expanded in memory)
   *              - Allocating memory to the vectors x and b in Ax=b
   */
  /* A note about the boundary condition:
   * ====================================
   * NOTE: All boundaries are currently implemented half between grid points
   *
   * We set the inner boundary first in the x-array in Ax=b, and the outer
   * boundary last in the x-array (z is periodic)
   *
   * The problem at the inner boundaries will be set by
   *
   * A_{0,j}x_{0,j} + A_{1,j}x_{1,j} = b_{0,j}
   *
   * Where the indices denote the indicies in the physical grid, and NOT in
   * the matrix. I.e. the first index refers to the x-index in the grid, and
   * the second index refers to the z-index in the grid.
   * The problem at the outer boundaries will be set by
   *
   * A_{xend,j}x_{xend,j} + A_{xend+1,j}x_{xend+1,j} = b_{xend_1,j}
   *
   * Determining the boundary conditions:
   * ====================================
   * Using x_BC at the value at the boundary, and k as the x index
   *
   * 2nd order dirichlet:
   * --------------------
   * 0.5*x_{k,j} + 0.5*x_{k+1,j} = x_BC
   * x_BC will be calculated from the grid points of the input "in" (given to
   * LaplaceXZpetsc::solve) so
   * 0.5*x_{k,j} + 0.5*x_{k+1,j} = 0.5*in_{k,j} + 0.5*in_{k+1,j}
   * which we will write as
   *
   * x_{k,j} + x_{k+1,j} = in_{k,j} + in_{k+1,j}
   *
   * 2nd order neumann:
   * --------------------
   * -(1/dx)*(x_{k,j}) + (1/dx)*x_{k+1,j} = x_BC
   * x_BC will be calculated from the grid points of the input "in" (given to
   * LaplaceXZpetsc::solve) so
   *   -(1/dx)*(x_{k,j}) + (1/dx)*x_{k+1,j}
   * = -(1/dx)*(in_{k,j}) + (1/dx)*in_{k+1,j}
   * which we will write as
   *
   * x_{0,j} - x_{1,j} = in_{0,j} - in_{1,j}
   * - x_{end-1,j} + x_{end,j} = - in_{end-1,j} + in_{end,j}
   *
   * Notice the change of sign for the inner boundary, which ensures that the
   * boundary part of main diagonal in the matrix A remains positive
   *
   * Specification through ghost point:
   * ----------------------------------
   * If options are set to use the ghost points of the input, we simply use
   * x_{k,j} = in_{k,j}
   *
   * Defualt behaviour:
   * =================-
   * If the boundary flags are not:
   * - xin will be set to 2nd order neumann as described above
   * - xout will be set to 2nd order dirichlet as described above
   *
   * Current implementations:
   * ========================
   * INVERT_AC_GRAD - As "2nd order neumann", but with 0 at the RHS
   * INVERT_SET     - As "Specifiaction through ghost points", where "in" is
   *                  given by x0
   * INVERT_RHS     - As "Specifiaction through ghost points", where "in" is
   *                  given by b
   */

  TRACE("LaplaceXZpetsc::LaplaceXZpetsc");

  if(opt == NULL) {
    // If no options supplied, use default
    opt = Options::getRoot()->getSection("laplacexz");
  }

  // Getting the boundary flags
  OPTION(opt, inner_boundary_flags, 0);
  OPTION(opt, outer_boundary_flags, 0);
  #ifdef CHECK
    // Checking flags are set to something which is not implemented
    // This is done binary (which is possible as each flag is a power of 2)
    if ( inner_boundary_flags & ~implemented_boundary_flags ) {
      throw BoutException("Attempted to set LaplaceXZ inversion boundary flag that is not implemented in petsc_laplace.cxx");
    }
    if ( outer_boundary_flags & ~implemented_boundary_flags ) {
      throw BoutException("Attempted to set LaplaceXZ inversion boundary flag that is not implemented in petsc_laplace.cxx");
    }
    if(mesh->periodicX) {
      throw BoutException("LaplacePetsc does not work with periodicity in the x direction (mesh->PeriodicX == true). Change boundary conditions or use serial-tri or cyclic solver instead");
      }
  #endif

  OPTION(opt, reuse_limit, 100);
  reuse_count = reuse_limit + 1; // So re-calculates first time

  // Convergence Parameters. Solution is considered converged if |r_k| < max( rtol * |b| , atol )
  // where r_k = b - Ax_k. The solution is considered diverged if |r_k| > dtol * |b|.
  BoutReal rtol, atol, dtol;
  int maxits; ///< Maximum iterations
  OPTION(opt, rtol, 1e-5);     // Relative tolerance
  OPTION(opt, atol, 1e-10);    // Absolute tolerance
  OPTION(opt, dtol, 1e3);      // Diverged threshold
  OPTION(opt, maxits, 100000); // Maximum iterations

  // Get KSP Solver Type
  string ksptype;
  opt->get("ksptype", ksptype, "gmres");

  // Get PC type
  string pctype;
  opt->get("pctype", pctype, "lu", true);

  string factor_package;
  opt->get("factor_package", factor_package, "petsc", true);

  // Get MPI communicator
  MPI_Comm comm = mesh->getXcomm();

  // Local size
  int localN = (mesh->xend - mesh->xstart + 1) * (mesh->ngz-1);
  if(mesh->firstX()) {
    localN += mesh->ngz-1;
  }
  if(mesh->lastX()) {
    localN += mesh->ngz-1;
  }

  // Create Vectors
  VecCreate( comm, &xs );
  VecSetSizes( xs, localN, PETSC_DETERMINE );
  VecSetFromOptions( xs );
  VecDuplicate( xs , &bs );

  for(int y = mesh->ystart; y <= mesh->yend; y++) {
    YSlice data;

    data.yindex = y;

    // Set size of Matrix on each processor to localN x localN
    MatCreate( comm, &data.MatA );
    MatSetSizes( data.MatA, localN, localN, PETSC_DETERMINE, PETSC_DETERMINE );
    MatSetFromOptions(data.MatA);

    //////////////////////////////////////////////////
    // Pre-allocate space for matrix elements

    PetscInt *d_nnz, *o_nnz;
    PetscMalloc( (localN)*sizeof(PetscInt), &d_nnz );
    PetscMalloc( (localN)*sizeof(PetscInt), &o_nnz );

    for(int i=0;i<localN;i++) {
      // Non-zero elements on this processor
      d_nnz[i] = 5; // Star pattern in 2D
      // Non-zero elements on neighboring processor
      o_nnz[i] = 0;
    }

    // X boundaries
    if(mesh->firstX()) {
      for(int z=0;z<mesh->ngz-1;z++) {
        d_nnz[z] = 2;
      }
    }else {
      // One point on another processor
      for(int z=0;z<mesh->ngz-1;z++) {
        d_nnz[z] -= 1;
        o_nnz[z] += 1;
      }
    }

    if(mesh->lastX()) {
      for(int z=0;z<mesh->ngz-1;z++) {
        int ind = localN - (mesh->ngz-1) + z;
        d_nnz[ind] = 2;
      }
    }else {
      // One point on another processor
      for(int z=0;z<mesh->ngz-1;z++) {
        int ind = localN - (mesh->ngz-1) + z;
        d_nnz[ind] -= 1;
        o_nnz[ind] += 1;
      }
    }

    MatMPIAIJSetPreallocation( data.MatA, 0, d_nnz, 0, o_nnz );
    MatSetUp(data.MatA);
    PetscFree( d_nnz );
    PetscFree( o_nnz );

    //////////////////////////////////////////////////
    // Declare KSP Context
    KSPCreate( comm, &data.ksp );

    // Set KSP type
    KSPSetType( data.ksp, ksptype.c_str() );
    // Set KSP tolerances
    KSPSetTolerances( data.ksp, rtol, atol, dtol, maxits );

    // Set KSP preconditioner
    PC pc;
    KSPGetPC(data.ksp,&pc);
    PCSetType(pc, pctype.c_str());
    PCFactorSetMatSolverPackage(pc,factor_package.c_str());

    KSPSetFromOptions( data.ksp );

    /// Add to slice vector
    slice.push_back(data);
  }

}

LaplaceXZpetsc::~LaplaceXZpetsc() {

  TRACE("LaplaceXZpetsc::~LaplaceXZpetsc");

  for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {
    MatDestroy(&it->MatA);
    MatDestroy(&it->MatP);

    KSPDestroy(&it->ksp);
  }

  VecDestroy(&bs);
  VecDestroy(&xs);
}

void LaplaceXZpetsc::setCoefs(const Field3D &Ain, const Field3D &Bin) {
  /* Function: LaplaceXZpetsc::setCoefs
   * Purpose:  - Set the matrix coefficients in the matrix MatA (member data)
   *             in Ax=b
   *             NOTE: Do not confuse the matrix A with the input coefficient A
   *                   referring to A in div(A grad_perp(B)) + Bf = b
   *
   * Input
   * Ain       - The A coefficient in div(A grad_perp(B)) + Bf = b
   * Bin       - The B coefficient in div(A grad_perp(B)) + Bf = b
   */

  TRACE("LaplaceXZpetsc::setCoefs");

  #ifdef CHECK
    // Checking flags are set to something which is not implemented
    // This is done binary (which is possible as each flag is a power of 2)
    if ( inner_boundary_flags & ~implemented_boundary_flags ) {
      throw BoutException("Attempted to set LaplaceXZ inversion boundary flag that is not implemented in petsc_laplace.cxx");
    }
    if ( outer_boundary_flags & ~implemented_boundary_flags ) {
      throw BoutException("Attempted to set LaplaceXZ inversion boundary flag that is not implemented in petsc_laplace.cxx");
    }
  #endif
  Timer timer("invert");
  // Set coefficients

  Field3D A = Ain;
  Field3D B = Bin;
  // Shift coefficients into orthogonal X-Z coordinates
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT
    A = Ain.shiftZ(true); // Shift into real space
    B = Bin.shiftZ(true);
  }

  // Each Y slice is handled as a separate set of matrices and KSP context
  for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {
    // Get Y index
    int y = it->yindex;

    int Istart, Iend;
    MatGetOwnershipRange( it->MatA, &Istart, &Iend );

    ////////////////////////////////////////////////
    // Inner X boundary (see note about BC in LaplaceXZ constructor)
    int row = Istart;
    if(mesh->firstX()) {
      if (inner_boundary_flags & INVERT_AC_GRAD){
        // Neumann 0
        /* NOTE: Sign of the elements are opposite of what one might expect,
         *       see note about BC in LaplaceXZ constructor for more details
         */
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row + (mesh->ngz-1); // +1 in X
          val = -1.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else if(inner_boundary_flags & INVERT_SET){
        // Setting BC from x0
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row + (mesh->ngz-1); // +1 in X
          val = 0.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else if(inner_boundary_flags & INVERT_RHS){
        // Setting BC from b
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row + (mesh->ngz-1); // +1 in X
          val = 0.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else{
        // Default: Neumann on inner x boundary
        /* NOTE: Sign of the elements are opposite of what one might expect,
         *       see note about BC in LaplaceXZ constructor for more details
         */
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row + (mesh->ngz-1); // +1 in X
          val = -1.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
    }

    ////////////////////////////////////////////////
    // Set matrix elements
    //
    // (1/J) d/dx ( A * J * g11 d/dx ) + (1/J) d/dz ( A * J * g33 d/dz ) + B

    for(int x=mesh->xstart; x <= mesh->xend; x++) {
      for(int z=0; z < mesh->ngz-1; z++) {
        // stencil entries
        PetscScalar c, xm, xp, zm, zp;

        // XX component

        // Metrics on x+1/2 boundary
        BoutReal J = 0.5*(mesh->J(x,y) + mesh->J(x+1,y));
        BoutReal g11 = 0.5*(mesh->g11(x,y) + mesh->g11(x+1,y));
        BoutReal dx = 0.5*(mesh->dx(x,y) + mesh->dx(x+1,y));
        BoutReal Acoef = 0.5*(A(x,y,z) + A(x+1,y,z));

        BoutReal val = Acoef * J * g11 / (mesh->J(x,y) * dx * mesh->dx(x,y));
        xp = val;
        c  = -val;

        // Metrics on x-1/2 boundary
        J = 0.5*(mesh->J(x,y) + mesh->J(x-1,y));
        g11 = 0.5*(mesh->g11(x,y) + mesh->g11(x-1,y));
        dx = 0.5*(mesh->dx(x,y) + mesh->dx(x-1,y));
        Acoef = 0.5*(A(x,y,z) + A(x-1,y,z));

        val = Acoef * J * g11 / (mesh->J(x,y) * dx * mesh->dx(x,y));
        xm = val;
        c  -= val;

        // ZZ component
        // Note that because metrics are constant in Z many terms cancel

        // Wrap around z-1 and z+1 indices
        int zminus = (z - 1 + (mesh->ngz-1)) % (mesh->ngz-1);
        int zplus = (z + 1) % (mesh->ngz-1);

        // Metrics on z+1/2 boundary
        Acoef = 0.5*(A(x,y,z) + A(x,y,zplus));

        val = Acoef * mesh->g33(x,y) / (mesh->dz*mesh->dz);
        zp = val;
        c -= val;

        // Metrics on z-1/2 boundary
        Acoef = 0.5*(A(x,y,z) + A(x,y,zminus));

        val = Acoef * mesh->g33(x,y) / (mesh->dz*mesh->dz);
        zm = val;
        c -= val;

        // B term
        c += B(x,y,z);

        /////////////////////////////////////////////////
        // Now have a 5-point stencil for the Laplacian

        // Set the centre (diagonal)
        MatSetValues(it->MatA,1,&row,1,&row,&c,INSERT_VALUES);

        // X + 1
        int col = row + (mesh->ngz-1);
        MatSetValues(it->MatA,1,&row,1,&col,&xp,INSERT_VALUES);

        // X - 1
        col = row - (mesh->ngz-1);
        MatSetValues(it->MatA,1,&row,1,&col,&xm,INSERT_VALUES);

        // Z + 1
        col = row + 1;
        if(z == mesh->ngz-2) {
          col -= mesh->ngz-1;  // Wrap around
        }
        MatSetValues(it->MatA,1,&row,1,&col,&zp,INSERT_VALUES);

        // Z - 1
        col = row - 1;
        if(z == 0) {
          col += mesh->ngz-1;  // Wrap around
        }
        MatSetValues(it->MatA,1,&row,1,&col,&zm,INSERT_VALUES);

        row++;
      }
    }

    ////////////////////////////////////////////////
    // Outer X boundary (see note about BC in LaplaceXZ constructor)
    if(mesh->lastX()) {
      if (outer_boundary_flags & INVERT_AC_GRAD){
        // Neumann 0
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row - (mesh->ngz-1); // -1 in X
          val = -1.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else if (outer_boundary_flags & INVERT_SET){
        // Setting BC from x0
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row - (mesh->ngz-1); // -1 in X
          val = 0.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else if (outer_boundary_flags & INVERT_RHS){
        // Setting BC from b
        for(int z=0; z < mesh->ngz-1; z++) {
          PetscScalar val = 1.0;
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row - (mesh->ngz-1); // -1 in X
          val = 0.0;
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
      else{
        //Default: Dirichlet on outer X boundary
        PetscScalar val = 0.5;

        for(int z=0; z < mesh->ngz-1; z++) {
          MatSetValues(it->MatA,1,&row,1,&row,&val,INSERT_VALUES);

          int col = row - (mesh->ngz-1); // -1 in X
          MatSetValues(it->MatA,1,&row,1,&col,&val,INSERT_VALUES);

          row++;
        }
      }
    }

    ASSERT1(row == Iend); // Check that row is currently on the last row

    // Assemble Matrix
    MatAssemblyBegin( it->MatA, MAT_FINAL_ASSEMBLY );
    MatAssemblyEnd( it->MatA, MAT_FINAL_ASSEMBLY );
  }

  // Increase reuse count
  reuse_count++;
  if(reuse_count > reuse_limit) {
    // Reuse limit exceeded. Reset count
    reuse_count = 0;

    // Modifying preconditioner matrix
    for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {
      // Copy matrix into preconditioner
      if(coefs_set) {
        // Preconditioner already set
        MatDestroy(&it->MatP);
      }
      MatConvert(it->MatA,MATSAME,MAT_INITIAL_MATRIX,&it->MatP);

      // Don't re-use preconditioner
      //KSPSetReusePreconditioner(it->ksp, PETSC_FALSE); // PETSc >= 3.5
    }

    // Set operators
    for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {

      //KSPSetOperators(it->ksp, it->MatA, it->MatP, SAME_PRECONDITIONER); // PETSc <= 3.4
      // Note: This is a hack to force update of the preconditioner matrix
      KSPSetOperators(it->ksp, it->MatA, it->MatP, SAME_NONZERO_PATTERN);

      //KSPSetOperators(it->ksp, it->MatA, it->MatP); // PETSc >= 3.5
    }
  }else {
    for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {
      /// Reuse the preconditioner, even if the operator changes

      KSPSetOperators(it->ksp, it->MatA, it->MatP, SAME_PRECONDITIONER); // PETSc <= 3.4
      //KSPSetReusePreconditioner(it->ksp, PETSC_TRUE);  // PETSc >= 3.5
    }
  }

  coefs_set = true;
}

Field3D LaplaceXZpetsc::solve(const Field3D &bin, const Field3D &x0in) {
  /* Function: LaplaceXZpetsc::solve
   * Purpose:  - Set the values of b in  Ax=b
   *           - Set the initial guess x0, and use this for x in  Ax=b
   *           - Solve Ax=b for x
   *           - Recast x to a Field3D
   *
   * Input
   * bin       - The b to be used in Ax=b
   * x0in      - The initial guess x0 to be used in Ax=b
   *
   * Output
   * result    - The solved x (returned as a Field3D) in the matrix problem Ax=b
   */

  TRACE("LaplaceXZpetsc::solve");

  if(!coefs_set) {
    throw BoutException("LaplaceXZpetsc: solve called before setCoefs");
  }

  Timer timer("invert");

  Field3D b = bin;
  Field3D x0 = x0in;
  // Shift x0 and b into orthogonal X-Z coordinates
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT
    b = bin.shiftZ(true); // Shift into real space
    x0 = x0in.shiftZ(true); // Shift into real space
  }

  Field3D result;
  result.allocate();

  for(vector<YSlice>::iterator it = slice.begin(); it != slice.end(); it++) {
    /// Get y index
    int y = it->yindex;

    /// Specify non-zero starting guess for solution (from input x0)
    KSPSetInitialGuessNonzero(it->ksp, PETSC_TRUE);

    //////////////////////////
    // Load initial guess x0 into xs and rhs into bs
    int Istart, Iend;
    MatGetOwnershipRange( it->MatA, &Istart, &Iend );

    // Starting index
    int ind = Istart;

    // Inner X boundary (see note about BC in LaplaceXZ constructor)
    if(mesh->firstX()) {
      if (inner_boundary_flags & INVERT_AC_GRAD){
        // Neumann 0
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xstart-1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = 0.0;
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );
          ind++;
        }
      }
      else if (inner_boundary_flags & INVERT_SET){
        // Setting BC from x0
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xstart-1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = x0(mesh->xstart,y,z);
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );
          ind++;
        }
      }
      else if (inner_boundary_flags & INVERT_RHS){
        // Setting BC from b
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xstart-1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = b(mesh->xstart,y,z);
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );
          ind++;
        }
      }
      else{
        // Default: Neumann on inner x boundary
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xstart-1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = x0(mesh->xstart-1,y,z) - x0(mesh->xstart,y,z);
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );
          ind++;
        }
      }
    }

    // Set the inner points
    for(int x=mesh->xstart;x<= mesh->xend;x++) {
      for(int z=0; z < mesh->ngz-1; z++) {
        PetscScalar val = x0(x,y,z);
        VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

        val = b(x,y,z);
        VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );
        ind++;
      }
    }

    // Outer X boundary (see note about BC in LaplaceXZ constructor)
    if(mesh->lastX()) {
      if (outer_boundary_flags & INVERT_AC_GRAD){
        // Neumann 0
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xend+1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = 0.0;
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );

          ind++;
        }
      }
      else if (outer_boundary_flags & INVERT_SET){
        // Setting BC from x0
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xend+1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = x0(mesh->xend+1,y,z);
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );

          ind++;
        }
      }
      else if (outer_boundary_flags & INVERT_RHS){
        // Setting BC from b
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xend+1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = b(mesh->xend+1,y,z);
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );

          ind++;
        }
      }
      else{
        //Default: Dirichlet on outer X boundary
        for(int z=0; z < mesh->ngz-1; z++) {
          // Setting the initial guess x0
          PetscScalar val = x0(mesh->xend+1,y,z);
          VecSetValues( xs, 1, &ind, &val, INSERT_VALUES );

          // Setting the solution b
          val = 0.5*(x0(mesh->xend,y,z) + x0(mesh->xend+1,y,z));
          VecSetValues( bs, 1, &ind, &val, INSERT_VALUES );

          ind++;
        }
      }
    }

    ASSERT1(ind == Iend); // Reached end of range

    // Assemble RHS Vector
    VecAssemblyBegin(bs);
    VecAssemblyEnd(bs);

    // Assemble Trial Solution Vector
    VecAssemblyBegin(xs);
    VecAssemblyEnd(xs);

    //////////////////////////
    // Solve the system

    KSPSolve( it->ksp, bs, xs );

    // Check if the solve converged
    KSPConvergedReason reason;
    KSPGetConvergedReason( it->ksp, &reason );

    if(reason <= 0) {
      throw BoutException("LaplaceXZ failed to converge. Reason %d", reason);
    }

    //////////////////////////
    // Copy data into result

    ind = Istart;
    // Inner X boundary
    if(mesh->firstX()) {
      for(int z=0; z < mesh->ngz-1; z++) {
        PetscScalar val;
        VecGetValues(xs, 1, &ind, &val );
        result(mesh->xstart-1,y,z) = val;
        ind++;
      }
    }

    for(int x=mesh->xstart;x<= mesh->xend;x++) {
      for(int z=0; z < mesh->ngz-1; z++) {
        PetscScalar val;
        VecGetValues(xs, 1, &ind, &val );
        result(x,y,z) = val;
        ind++;
      }
    }

    // Outer X boundary
    if(mesh->lastX()) {
      for(int z=0; z < mesh->ngz-1; z++) {
        PetscScalar val;
        VecGetValues(xs, 1, &ind, &val );
        result(mesh->xend+1,y,z) = val;
        ind++;
      }
    }
    ASSERT1(ind == Iend); // Reached end of range
  }

  // Shift result from orthogonal X-Z coordinates
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT
    result = result.shiftZ(false);
  }
  return result;
}

#endif // BOUT_HAS_PETSC
