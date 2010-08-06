#ifndef _psi_src_lib_libmints_potential_h_
#define _psi_src_lib_libmints_potential_h_

#include <vector>
#include <boost/shared_ptr.hpp>

namespace psi {

    class BasisSet;
    class GaussianShell;
    class ObaraSaikaTwoCenterVIRecursion;
    class ObaraSaikaTwoCenterVIDerivRecursion;
    class OneBodyInt;
    class IntegralFactory;
    class SphericalTransform;

/*! \ingroup MINTS
 *  \class PotentialInt
 *  \brief Computes potential integrals.
 * Use an IntegralFactory to create this object.
 */
class PotentialInt : public OneBodyInt
{
    
    /// Computes integrals between two shell objects.
    void compute_pair(boost::shared_ptr<GaussianShell>, boost::shared_ptr<GaussianShell>);
    /// Computes integrals between two shell objects.
    void compute_pair_deriv1(boost::shared_ptr<GaussianShell>, boost::shared_ptr<GaussianShell>);
    
protected:
    /// Recursion object that does the heavy lifting.
    ObaraSaikaTwoCenterVIRecursion potential_recur_;
    /// Recursion object that does the heavy lifting.
    ObaraSaikaTwoCenterVIDerivRecursion potential_deriv_recur_;

public:
    /// Constructor
    PotentialInt(std::vector<SphericalTransform>&, boost::shared_ptr<BasisSet>, boost::shared_ptr<BasisSet>, int deriv=0);
    ~PotentialInt();
    
    /// Computes integrals between two shells.
    void compute_shell(int, int);
    /// Computes integrals between two shells.
    void compute_shell_deriv1(int, int);
    
    /// Does the method provide first derivatives?
    bool has_deriv1() { return true; }
};

}

#endif
