#ifndef _psi_src_lib_libmints_electricfield_h_
#define _psi_src_lib_libmints_electricfield_h_

#include <libmints/basisset.h>
#include <libmints/gshell.h>
#include <libmints/osrecur.h>
#include <libmints/onebody.h>
#include <libmints/integral.h>

namespace psi {

/*! \ingroup MINTS
 *  \class ElectricFieldInt
 *  \brief Computes electric field integrals.
 *
 *  Use an IntegralFactory to create this object.
 */
class ElectricFieldInt : public OneBodyInt
{
    //! Obara and Saika recursion object to be used.
    ObaraSaikaTwoCenterElectricField efield_recur_;                 // Both ElectricField and VIDeriv should give the same result
//    ObaraSaikaTwoCenterVIDerivRecursion efield_recur_;

    //! Computes the electric field between two gaussian shells.
    void compute_pair(shared_ptr<GaussianShell>, shared_ptr<GaussianShell>);

public:
    //! Constructor. Do not call directly use an IntegralFactory.
    ElectricFieldInt(std::vector<SphericalTransform>&, shared_ptr<BasisSet>, shared_ptr<BasisSet>, int deriv=0);
    //! Virtual destructor
    virtual ~ElectricFieldInt();

    //! Compute dipole between two shells, result stored in buffer_.
    void compute_shell(int, int);
    //! Compute dipole derivative between two shells, result stored in buffer_.
    //void compute_shell_deriv1(int, int);

    /** Compute all dipole integrals and store them in an array of matrices.
     *  @param result Contains the dipole moment integrals. Order is [mu_x, mu_y, mu_].
     */
    void compute(std::vector<shared_ptr<SimpleMatrix> > &result);
    /** Compute all dipole derivatives and store them in an array of matrices.
     *  @param result Contains the dipole moment derivative integrals. Order is [mu_x(Aix,Aiy,Aiz...An), mu_y..., mu_z...]
     */
    //void compute_deriv1(std::vector<shared_ptr<SimpleMatrix> > &result);

    //! Does the method provide first derivatives?
    bool has_deriv1() { return false; }
};

}

#endif
    
