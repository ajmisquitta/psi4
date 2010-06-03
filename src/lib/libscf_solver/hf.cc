/*
 *  hf.cpp
 *  matrix
 *
 *  Created by Justin Turney on 4/9/08.
 *  Copyright 2008 by Justin M. Turney, Ph.D.. All rights reserved.
 *
 */

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <utility>

#include <psifiles.h>
#include <libciomr/libciomr.h>
#include <libpsio/psio.h>
#include <libchkpt/chkpt.hpp>
#include <libipv1/ip_lib.h>
#include <libiwl/iwl.hpp>
#include <libqt/qt.h>
#include <psifiles.h>

#include "hf.h"

#include <libmints/basisset.h>
#include <libmints/onebody.h>
#include <libmints/twobody.h>
#include <libmints/integral.h>
#include <libmints/molecule.h>

using namespace boost;
using namespace std;
using namespace psi;

namespace psi { namespace scf {

HF::HF(Options& options, shared_ptr<PSIO> psio, shared_ptr<Chkpt> chkpt)
    : Wavefunction(options, psio, chkpt),
      df_storage_(disk),
      nuclear_dipole_contribution_(3),
      nuclear_quadrupole_contribution_(6),
      print_(3),
      addExternalPotential_(false)
{
    common_init();
}

HF::~HF()
{
    if (direct_integrals_ == false && ri_integrals_ == false) {
        delete[] so2symblk_;
        delete[] so2index_;
        delete[] pk_symoffset_;
    }
    free(zvals_);
}

void HF::common_init()
{
    S_.reset(factory_.create_matrix("S"));
    Shalf_.reset(factory_.create_matrix("S^-1/2"));
    Sphalf_.reset(factory_.create_matrix("S^+1/2"));
    H_.reset(factory_.create_matrix("One-electron Hamiltonion"));
    C_.reset(factory_.create_matrix("MO coefficients"));

    Eold_    = 0.0;
    E_       = 0.0;
    maxiter_ = 40;

    // Read information from input file
    maxiter_ = options_.get_int("MAXITER");

    // Read in DOCC and SOCC from memory
    int nirreps = factory_.nirreps();
    int ndocc = 0, nsocc = 0;
    input_docc_ = false;
    if (options_["DOCC"].has_changed()) {
        input_docc_ = true;
        for (int i=0; i<nirreps; ++i) {
            doccpi_[i] = options_["DOCC"][i].to_integer();
            ndocc += 2*doccpi_[i];
        }
    } else {
        for (int i=0; i<nirreps; ++i)
            doccpi_[i] = 0;
    }
    input_socc_ = false;
    if (options_["SOCC"].has_changed()) {
        input_socc_ = true;
        for (int i=0; i<nirreps; ++i) {
            soccpi_[i] = options_["SOCC"][i].to_integer();
            nsocc += soccpi_[i];
        }
    } else {
        for (int i=0; i<nirreps; ++i)
            soccpi_[i] = 0;
    }

    // Read information from checkpoint
    nuclearrep_ = chkpt_->rd_enuc();
    natom_ = chkpt_->rd_natom();
    zvals_ = chkpt_->rd_zvals();


    // Determine the number of electrons in the system
    charge_ = options_.get_int("CHARGE");
    nElec_  = 0;
    for (int i=0; i<natom_; ++i)
        nElec_ += (int)zvals_[i];
    nElec_ -= charge_;

    // If the user told us the multiplicity, read it from the input
    if(options_["MULTP"].has_changed()){
        multiplicity_ = options_.get_int("MULTP");
    }else{
        if(nElec_%2){
            multiplicity_ = 2;
            // There are an odd number of electrons
            fprintf(outfile,"\tThere are an odd number of electrons - assuming doublet.\n"
                            "\tSpecify the multiplicity with the MULTP option in the\n"
                            "\tinput if this is incorrect\n\n");
        }else{
            multiplicity_ = 1;
            // There are an even number of electrons
            fprintf(outfile,"\tThere are an even number of electrons - assuming singlet.\n"
                            "\tSpecify the multiplicity with the MULTP option in the\n"
                            "\tinput if this is incorrect\n\n");
        }
    }
    // Make sure that the multiplicity is reasonable
    if(multiplicity_ - 1 > nElec_){
        char *str = new char[100];
        sprintf(str, "There are not enough electrons for multiplicity = %d, \n"
                     "please check your input and use the MULTP keyword", multiplicity_);
        throw SanityCheckError(str, __FILE__, __LINE__);
        delete [] str;
    }
    if(multiplicity_%2 == nElec_%2){
        char *str = new char[100];
        sprintf(str, "A multiplicity of %d with %d electrons is impossible.\n"
                     "Please check your input and use the MULTP and/or CHARGE keywords",
                     multiplicity_, nElec_);
        throw SanityCheckError(str, __FILE__, __LINE__);
        delete [] str;
    }

    nbeta_  = (nElec_ - multiplicity_ + 1)/2;
    nalpha_ = nbeta_ + multiplicity_ - 1;

//  if (ndocc != 0 && nbeta_ != ndocc && nalpha_ != (ndocc + nsocc)) {
//      char *str = "Your DOCC, SOCC, charge, and multiplicity does not make sense.\n";
//      fprintf(outfile, str);
//      throw SanityCheckError(str, __FILE__, __LINE__);
//  }

    perturb_h_ = false;
    perturb_h_ = options_.get_bool("PERTURB_H");
    perturb_ = nothing;
    lambda_ = 0.0;
    if (perturb_h_) {
        string perturb_with;

        lambda_ = options_.get_double("LAMBDA");

        if (options_["PERTURB_WITH"].has_changed()) {
            perturb_with = options_.get_str("PERTURB_WITH");
            // Do checks to see what perturb_with is.
            if (perturb_with == "DIPOLE_X")
                perturb_ = dipole_x;
            else if (perturb_with == "DIPOLE_Y")
                perturb_ = dipole_y;
            else if (perturb_with == "DIPOLE_Z")
                perturb_ = dipole_z;
            else
                fprintf(outfile, "Unknown PERTURB_WITH. Applying no perturbation.\n");
        } else {
            fprintf(outfile, "PERTURB_H is true, but PERTURB_WITH not found, applying no perturbation.\n");
        }
    }

    // Run integral direct? default no
    direct_integrals_ = false;
    direct_integrals_ = options_.get_bool("DIRECT");

    // How much stuff shall we echo to the user?
    print_ = options_.get_int("PRINT");
    //fprintf(outfile,"  Print = %d\n",print_);

    //Run density fitting? default no
    ri_integrals_ = false;

    if (options_.get_bool("RI_HF"))
    {
        ri_integrals_ = true;
        direct_integrals_ = false;  // direct 4-index ints
    }
    
    //Somewhat idiot proofed, you cant turn local K on without DF
    local_K_ = false;
    if (ri_integrals_) {
        if (options_.get_bool("L_HF")) {
            local_K_ = true;    
        }
    }

    //For HF algorithms, J and K are both required always.
    J_is_required_ = true;
    K_is_required_ = true;
    
    //Use schwarz sieve? default no
    schwarz_ = 0.0;
    if (options_["SCHWARZ_CUTOFF"].has_changed())
    {
        schwarz_ = options_.get_double("SCHWARZ_CUTOFF");
    }
    
    // Handle common diis info
    diis_enabled_ = true;
    num_diis_vectors_ = 4;

    // Allocate memory for DIISnum_diis_vectors_
    //  First, did the user request a different number of diis vectors?
    num_diis_vectors_ = options_.get_int("DIIS_VECTORS");
    diis_enabled_ = options_.get_bool("DIIS");

    // Don't perform DIIS if less than 2 vectors requested, or user requested a negative number
    if (num_diis_vectors_ < 2) {
        // disable diis
        diis_enabled_ = false;
    }

    // Initialize DIIS manager
    if (diis_enabled_)
        diis_manager_ = shared_ptr<DIISManager>(new DIISManager(num_diis_vectors_, "HF DIIS vector"));

    // Save cartesian grid? Temporary until OEPROP is fully redone
    save_grid_ = false;
    if (options_.get_bool("SAVE_CARTESIAN_GRID")) {
        save_grid_ = true;
    }
    // Alloc memory for multipoles
    Dipole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Dipole X SO-basis")));
    Dipole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Dipole Y SO-basis")));
    Dipole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Dipole Z SO-basis")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole XX")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole XY")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole XZ")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole YY")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole YZ")));
    Quadrupole_.push_back(SharedSimpleMatrix(factory_.create_simple_matrix("Quadrupole ZZ")));

    if(print_ > 1) print_header();
    if (direct_integrals_ == false && ri_integrals_ == false)
        form_indexing();
}

void HF::find_occupation(Vector & evals)
{
    std::vector<std::pair<double, int> > pairs;
    for (int h=0; h<evals.nirreps(); ++h) {
        for (int i=0; i<evals.dimpi()[h]; ++i)
            pairs.push_back(make_pair(evals.get(h, i), h));
    }
    sort(pairs.begin(),pairs.end());

    if(!input_docc_){
        memset(doccpi_, 0, sizeof(int) * evals.nirreps());
        for (int i=0; i<nbeta_; ++i)
            doccpi_[pairs[i].second]++;
    }
    if(!input_socc_){
        memset(soccpi_, 0, sizeof(int) * evals.nirreps());
        for (int i=nbeta_; i<nalpha_; ++i)
            soccpi_[pairs[i].second]++;
    }

    if(print_>5){
        fprintf(outfile, "\tDOCC: [");
        for (int h=0; h<evals.nirreps(); ++h){
            fprintf(outfile, "%3d ", doccpi_[h]);
        }
        fprintf(outfile, "]\n");
        fprintf(outfile, "\tSOCC: [");
        for (int h=0; h<evals.nirreps(); ++h){
            fprintf(outfile, "%3d ", soccpi_[h]);
        }
        fprintf(outfile, "]\n");
    }

    for (int i=0; i<evals.nirreps(); ++i) {
        nalphapi_[i] = doccpi_[i] + soccpi_[i];
        nbetapi_[i]  = doccpi_[i];
    }
}

void HF::print_header()
{
    char *temp;
    char **temp2;
    char *reference;

    ip_string(const_cast<char*>("REFERENCE"), &reference, 0);

    fprintf(outfile, " %s: by Justin Turney and Rob Parrish\n\n", reference);
#ifdef _DEBUG
    fprintf(outfile, "  Debug version.\n");
#else
    fprintf(outfile, "  Release version.\n");
#endif
    temp = chkpt_->rd_sym_label();
    fprintf(outfile, "  Running in %s symmetry.\n", temp);
    free(temp);

    temp2 = chkpt_->rd_irr_labs();
    fprintf(outfile, "  Input DOCC vector = (");
    for (int h=0; h<factory_.nirreps(); ++h) {
        fprintf(outfile, "%2d %3s ", doccpi_[h], temp2[h]);
    }
    fprintf(outfile, ")\n");
    fprintf(outfile, "  Input SOCC vector = (");
    for (int h=0; h<factory_.nirreps(); ++h) {
        fprintf(outfile, "%2d %3s ", soccpi_[h], temp2[h]);
        free(temp2[h]);
    }
    free(temp2);

    fprintf(outfile, ")\n");
    fprintf(outfile, "  Nuclear repulsion = %20.15f\n", nuclearrep_);

    fprintf(outfile, "  Energy threshold  = %3.2e\n", energy_threshold_);
    fprintf(outfile, "  Density threshold = %3.2e\n\n", density_threshold_);
    fflush(outfile);
    free(reference);
}

void HF::form_indexing()
{
    int h, i, ij, offset, pk_size;
    int nirreps = factory_.nirreps();
    int *opi = factory_.rowspi();
    int nso;

    nso = chkpt_->rd_nso();
    so2symblk_ = new int[nso];
    so2index_  = new int[nso];

    ij = 0; offset = 0; pk_size = 0; pk_pairs_ = 0;
    for (h=0; h<nirreps; ++h) {
        for (i=0; i<opi[h]; ++i) {
            so2symblk_[ij] = h;
            so2index_[ij] = ij-offset;

            if (debug_ > 3)
                fprintf(outfile, "so2symblk_[%3d] = %3d, so2index_[%3d] = %3d\n", ij, so2symblk_[ij], ij, so2index_[ij]);

            ij++;
        }
        offset += opi[h];

        // Add up possible pair combinations that yield A1 symmetry
        pk_pairs_ += ioff[opi[h]];
    }

    // Compute the number of pairs in PK
    pk_size_ = INDEX2(pk_pairs_-1, pk_pairs_-1) + 1;

    // Compute PK symmetry mapping
    pk_symoffset_ = new int[nirreps];

    // Compute an offset in the PK matrix telling where a given symmetry block starts.
    pk_symoffset_[0] = 0;
    for (h=1; h<nirreps; ++h) {
        pk_symoffset_[h] = pk_symoffset_[h-1] + ioff[opi[h-1]];
    }
}

void HF::form_H()
{
    SharedMatrix kinetic(factory_.create_matrix("Kinetic Integrals"));
    SharedMatrix potential(factory_.create_matrix("Potential Integrals"));

    // Form the multipole integrals
    form_multipole_integrals();

    // Load in kinetic and potential matrices
    int nso = chkpt_->rd_nso();
    double *integrals = init_array(ioff[nso]);

    // Kinetic
    if (!direct_integrals_&&!ri_integrals_) {
        IWL::read_one(psio_.get(), PSIF_OEI, const_cast<char*>(PSIF_SO_T), integrals, ioff[nso], 0, 0, outfile);
        kinetic->set(integrals);
        IWL::read_one(psio_.get(), PSIF_OEI, const_cast<char*>(PSIF_SO_V), integrals, ioff[nso], 0, 0, outfile);
        potential->set(integrals);
    }
    else {
        IntegralFactory integral(basisset_, basisset_, basisset_, basisset_);
        shared_ptr<OneBodyInt> T(integral.kinetic());
        shared_ptr<OneBodyInt> V(integral.potential());

        T->compute(kinetic);
        V->compute(potential);
    }

    if (debug_ > 2)
        kinetic->print(outfile);

    if (debug_ > 2)
        potential->print(outfile);

    H_->copy(kinetic);
    H_->add(potential);

    if (debug_ > 2)
        H_->print(outfile);

    free(integrals);

    // if (perturb_h_) {
    //     if (perturb_ == dipole_x) {
    //         fprintf(outfile, "  Perturbing H by %f Dmx.\n", lambda_);
    //         H_.add(lambda_ * Dipole_[0]);
    //     } else if (perturb_ == dipole_y) {
    //         fprintf(outfile, "  Perturbing H by %f Dmy.\n", lambda_);
    //         H_.add(lambda_ * Dipole_[1]);
    //     } else if (perturb_ == dipole_z) {
    //         fprintf(outfile, "  Perturbing H by %f Dmz.\n", lambda_);
    //         H_.add(lambda_ * Dipole_[2]);
    //     }
    //     H_.print(outfile, "with perturbation");
    // }
}

void HF::form_Shalf()
{
    int nso = chkpt_->rd_nso();

    // Overlap
    if (!direct_integrals_&&!ri_integrals_) {
        double *integrals = init_array(ioff[nso]);
        IWL::read_one(psio_.get(), PSIF_OEI, const_cast<char*>(PSIF_SO_S), integrals, ioff[nso], 0, 0, outfile);
        S_->set(integrals);
        free(integrals);
    }
    else {
        IntegralFactory integral(basisset_, basisset_, basisset_, basisset_);
        OneBodyInt *S = integral.overlap();
        S->compute(S_);
        delete S;
    }
    // Form S^(-1/2) matrix
    Matrix eigvec;
    Matrix eigtemp;
    Matrix eigtemp2;
    Vector eigval;
    factory_.create_matrix(eigvec, "L");
    factory_.create_matrix(eigtemp, "Temp");
    factory_.create_matrix(eigtemp2);
    factory_.create_vector(eigval);

    S_->diagonalize(eigvec, eigval);

    // Convert the eigenvales to 1/sqrt(eigenvalues)
    int *dimpi = eigval.dimpi();
    for (int h=0; h<eigval.nirreps(); ++h) {
        for (int i=0; i<dimpi[h]; ++i) {
            double scale = 1.0 / sqrt(eigval.get(h, i));
            eigval.set(h, i, scale);
        }
    }
    // Create a vector matrix from the converted eigenvalues
    eigtemp2.set(eigval);

    eigtemp.gemm(false, true, 1.0, eigtemp2, eigvec, 0.0);
    Shalf_->gemm(false, false, 1.0, eigvec, eigtemp, 0.0);

    S_->diagonalize(eigvec, eigval);
    // Convert the eigenvalues to sqrt(eigenvalues)
    for (int h=0; h<eigval.nirreps(); ++h) {
        for (int i=0; i<dimpi[h]; ++i) {
            double scale = sqrt(eigval.get(h, i));
            eigval.set(h, i, scale);
        }
    }
    // Create a vector matrix from the converted eigenvalues
    eigtemp2.set(eigval);

    // Works for diagonalize:
    eigtemp.gemm(false, true, 1.0, eigtemp2, eigvec, 0.0);
    Sphalf_->gemm(false, false, 1.0, eigvec, eigtemp, 0.0);

    if (debug_ > 3) {
        Shalf_->print(outfile);
        Sphalf_->print(outfile);
    }
}

int *HF::compute_fcpi(int nfzc, SharedVector &eigvalues)
{
    int *frzcpi = new int[eigvalues->nirreps()];
    // Print out orbital energies.
    std::vector<std::pair<double, int> > pairs;
    for (int h=0; h<eigvalues->nirreps(); ++h) {
        for (int i=0; i<eigvalues->dimpi()[h]; ++i)
            pairs.push_back(make_pair(eigvalues->get(h, i), h));
        frzcpi[h] = 0;
    }
    sort(pairs.begin(),pairs.end());

    for (int i=0; i<nfzc; ++i)
        frzcpi[pairs[i].second]++;

    return frzcpi;
}

int *HF::compute_fvpi(int nfzv, SharedVector &eigvalues)
{
    int *frzvpi = new int[eigvalues->nirreps()];
    // Print out orbital energies.
    std::vector<std::pair<double, int> > pairs;
    for (int h=0; h<eigvalues->nirreps(); ++h) {
        for (int i=0; i<eigvalues->dimpi()[h]; ++i)
            pairs.push_back(make_pair(eigvalues->get(h, i), h));
        frzvpi[h] = 0;
    }
    sort(pairs.begin(),pairs.end(), greater<std::pair<double, int> >());

    for (int i=0; i<nfzv; ++i)
        frzvpi[pairs[i].second]++;

    return frzvpi;
}

void HF::form_multipole_integrals()
{
    // Initialize an integral object
    IntegralFactory integral(basisset_, basisset_, basisset_, basisset_);

    // Get a dipole integral object
    OneBodyInt* dipole = integral.dipole();
    OneBodyInt* quadrupole= integral.quadrupole();

    // Compute the dipole integrals
    dipole->compute(Dipole_);
    quadrupole->compute(Quadrupole_);

    delete quadrupole;
    delete dipole;

    // Get the nuclear contribution to the dipole
    nuclear_dipole_contribution_ = molecule_->nuclear_dipole_contribution();
    nuclear_quadrupole_contribution_ = molecule_->nuclear_quadrupole_contribution();

    // Save the dipole integrals
    Dipole_[0]->save(psio_, PSIF_OEI);
    Dipole_[1]->save(psio_, PSIF_OEI);
    Dipole_[2]->save(psio_, PSIF_OEI);

    Quadrupole_[0]->save(psio_, PSIF_OEI);
    Quadrupole_[1]->save(psio_, PSIF_OEI);
    Quadrupole_[2]->save(psio_, PSIF_OEI);
    Quadrupole_[3]->save(psio_, PSIF_OEI);
    Quadrupole_[4]->save(psio_, PSIF_OEI);
    Quadrupole_[5]->save(psio_, PSIF_OEI);
}

bool HF::load_or_compute_initial_C()
{
    bool ret = false;
    string prefix(chkpt_->build_keyword(const_cast<char*>("MO coefficients")));
    if (chkpt_->exist(const_cast<char*>(prefix.c_str()))) {
        // Read MOs from checkpoint and set C_ to them
        double **vectors = chkpt_->rd_scf();
        C_->set(const_cast<const double**>(vectors));
        free_block(vectors);

        form_D();

        // Read SCF energy from checkpoint file.
        E_ = chkpt_->rd_escf();

        ret = true;
    } else {
        form_initial_C();
        form_D();
        // Compute an initial energy using H and D
        E_ = compute_initial_E();

        ret = false;
    }

    return ret;
}
void HF::getUHFAtomicDensity(shared_ptr<BasisSet> bas, int nelec, int nhigh, double** D)
{
    print_ = options_.get_int("PRINT");
    shared_ptr<Molecule> mol = bas->molecule();    
    
    int nbeta = (nelec-nhigh)/2;
    int nalpha = nelec-nbeta;
    int natom = mol->natom();
    int norbs = bas->nbf();
    
    if (print_>5)
        fprintf(outfile,"  nalpha = %d, nbeta = %d, norbs = %d\n",nalpha,nbeta,norbs);

    if (print_>5) {
        bas->print(outfile);
        mol->print();
    }

    if (natom != 1) {
        throw std::domain_error("SAD Atomic UHF has been given a molecule, not an atom"); 
    }

    double** Dold = block_matrix(norbs,norbs);
    double **Shalf = block_matrix(norbs, norbs);
    double** Ca = block_matrix(norbs,norbs);
    double** Cb = block_matrix(norbs,norbs);
    double** Da = block_matrix(norbs,norbs);
    double** Db = block_matrix(norbs,norbs);    
    double** Fa = block_matrix(norbs,norbs);
    double** Fb = block_matrix(norbs,norbs);    
    double** Ga = block_matrix(norbs,norbs);
    double** Gb = block_matrix(norbs,norbs);    
    double** H = block_matrix(norbs,norbs);    

    IntegralFactory integral(bas, bas, bas, bas);
    OneBodyInt *S_ints = integral.overlap();
    OneBodyInt *T_ints = integral.kinetic();
    OneBodyInt *V_ints = integral.potential();
    TwoBodyInt *TEI = integral.eri();

    //Compute Shalf;
    //Fill S
    double **S = block_matrix(norbs,norbs);
    const double* S_buffer = S_ints->buffer();
    for (int MU = 0; MU < bas->nshell(); MU++) {
        int numMU = bas->shell(MU)->nfunction();
        for (int NU = 0; NU<= MU; NU++) {
            int numNU = bas->shell(NU)->nfunction();
            S_ints->compute_shell(MU,NU);
            for (int m = 0, index = 0; m<numMU; m++) {
                int omu = bas->shell(MU)->function_index()+m;
                for (int n = 0; n<numNU; n++, index++) {
                    int onu = bas->shell(NU)->function_index()+n;
                    S[omu][onu] = S_buffer[index];
                    S[onu][omu] = S_buffer[index];
                }
            }
        }
    }

    if (print_>6) {
    fprintf(outfile,"  S:\n");
    print_mat(S,norbs,norbs,outfile);
    }
    // S^{-1/2}
    
    // First, diagonalize S
    // the C_DSYEV call replaces the original matrix J with its eigenvectors
    double* eigval = init_array(norbs);
    int lwork = norbs * 3;
    double* work = init_array(lwork);
    int stat = C_DSYEV('v','u',norbs,S[0],norbs,eigval, work,lwork);
    if (stat != 0) {
        fprintf(outfile, "C_DSYEV failed\n");
        exit(PSI_RETURN_FAILURE);
    }
    free(work);
    
    double **S_copy = block_matrix(norbs, norbs);
    C_DCOPY(norbs*norbs,S[0],1,S_copy[0],1); 

    // Now form S^{-1/2} = U(T)*s^{-1/2}*U,
    // where s^{-1/2} is the diagonal matrix of the inverse square roots
    // of the eigenvalues, and U is the matrix of eigenvectors of S
    for (int i=0; i<norbs; i++) {
        if (eigval[i] < 1.0E-10)
            eigval[i] = 0.0;
        else 
            eigval[i] = 1.0 / sqrt(eigval[i]);

        // scale one set of eigenvectors by the diagonal elements s^{-1/2}
        C_DSCAL(norbs, eigval[i], S[i], 1);
    }
    free(eigval);

    // Smhalf = S_copy(T) * S
    C_DGEMM('t','n',norbs,norbs,norbs,1.0,
            S_copy[0],norbs,S[0],norbs,0.0,Shalf[0],norbs);

    free_block(S);
    free_block(S_copy);
    
    if (print_>6) {
    fprintf(outfile,"  S^-1/2:\n");
    print_mat(Shalf,norbs,norbs,outfile);
    }

    //Compute H
    const double* T_buffer = T_ints->buffer();
    const double* V_buffer = V_ints->buffer();
    for (int MU = 0; MU < bas->nshell(); MU++) {
        int numMU = bas->shell(MU)->nfunction();
        for (int NU = 0; NU<= MU; NU++) {
            int numNU = bas->shell(NU)->nfunction();
            V_ints->compute_shell(MU,NU);
            T_ints->compute_shell(MU,NU);
            for (int m = 0, index = 0; m<numMU; m++) {
                int omu = bas->shell(MU)->function_index()+m;
                for (int n = 0; n<numNU; n++, index++) {
                    int onu = bas->shell(NU)->function_index()+n;
                    H[omu][onu] = T_buffer[index]+V_buffer[index];
                    H[onu][omu] = T_buffer[index]+V_buffer[index];
                }
            }
        }
    }

    if (print_>6) {
    fprintf(outfile,"  H:\n");
    print_mat(H,norbs,norbs,outfile);
    }

    //Compute initial Ca and Da from core guess
    atomicUHFHelperFormCandD(nalpha,norbs,Shalf,H,Ca,Da);
    //Compute initial Cb and Db from core guess
    atomicUHFHelperFormCandD(nbeta,norbs,Shalf,H,Cb,Db);
    //Compute intial D  
    C_DCOPY(norbs*norbs,Da[0],1,D[0],1);
    C_DAXPY(norbs*norbs,1.0,Db[0],1,D[0],1);   
    if (print_>6) {
    fprintf(outfile,"  Ca:\n");
    print_mat(Ca,norbs,norbs,outfile);

    fprintf(outfile,"  Cb:\n");
    print_mat(Cb,norbs,norbs,outfile);

    fprintf(outfile,"  Da:\n");
    print_mat(Da,norbs,norbs,outfile);

    fprintf(outfile,"  Db:\n");
    print_mat(Db,norbs,norbs,outfile);

    fprintf(outfile,"  D:\n");
    print_mat(D,norbs,norbs,outfile);
    }

    //Compute inital E for reference
    double E = C_DDOT(norbs*norbs,D[0],1,H[0],1); 
    E += C_DDOT(norbs*norbs,Da[0],1,Fa[0],1); 
    E += C_DDOT(norbs*norbs,Db[0],1,Fb[0],1); 
    E *= 0.5;       

    const double* buffer = TEI->buffer();

    double E_tol = 1E-5;
    double D_tol = 1E-5;
    int maxiter = 50;

    double E_old;
    int iteration = 0;

    bool converged = false; 
    if (print_>1) {
    fprintf(outfile, "\n  Initial Atomic UHF Energy:    %14.10f\n\n",E);
    fprintf(outfile, "                                         Total Energy            Delta E              Density RMS\n\n");
    fflush(outfile);
    }
    do {

        iteration++;

        //Copy the old values over for error analysis 
        E_old = E;    
        //I'm only going to use the total for now, could be expanded later
        C_DCOPY(norbs*norbs,D[0],1,Dold[0],1);    

        //Form Ga and Gb via integral direct
        memset((void*) Ga[0], '\0',norbs*norbs*sizeof(double));    
        memset((void*) Gb[0], '\0',norbs*norbs*sizeof(double));    
    
        //At the moment this is 8-fold slower than it could be, we'll see if it is signficant
        for (int MU = 0; MU < bas->nshell(); MU++) {
        int numMU = bas->shell(MU)->nfunction();
        for (int NU = 0; NU < bas->nshell(); NU++) {
        int numNU = bas->shell(NU)->nfunction();
        for (int LA = 0; LA < bas->nshell(); LA++) {
        int numLA = bas->shell(LA)->nfunction();
        for (int SI = 0; SI < bas->nshell(); SI++) {
        int numSI = bas->shell(SI)->nfunction();
        TEI->compute_shell(MU,NU,LA,SI);
        for (int m = 0, index = 0; m < numMU; m++) {
        int omu = bas->shell(MU)->function_index() + m;
        for (int n = 0; n < numNU; n++) {
        int onu = bas->shell(NU)->function_index() + n;
        for (int l = 0; l < numLA; l++) {
        int ola = bas->shell(LA)->function_index() + l;
        for (int s = 0; s < numSI; s++, index++) {
        int osi = bas->shell(SI)->function_index() + s;
             //fprintf(outfile,"  Integral (%d, %d| %d, %d) = %14.10f\n",omu,onu,ola,osi,buffer[index]);
             Ga[omu][onu] += D[ola][osi]*buffer[index];
             //Ga[ola][osi] += D[omu][onu]*buffer[index];
             Ga[omu][osi] -= Da[onu][ola]*buffer[index]; 
             Gb[omu][onu] += D[ola][osi]*buffer[index];
             //Gb[ola][osi] += D[omu][onu]*buffer[index];
             Gb[omu][osi] -= Db[onu][ola]*buffer[index]; 
        } 
        } 
        } 
        } 
        }      
        }      
        }      
        }      

        //Form Fa and Fb
        C_DCOPY(norbs*norbs,H[0],1,Fa[0],1);
        C_DAXPY(norbs*norbs,1.0,Ga[0],1,Fa[0],1);   
        C_DCOPY(norbs*norbs,H[0],1,Fb[0],1);
        C_DAXPY(norbs*norbs,1.0,Gb[0],1,Fb[0],1);   
    
        //Compute E
        E = C_DDOT(norbs*norbs,D[0],1,H[0],1); 
        E += C_DDOT(norbs*norbs,Da[0],1,Fa[0],1); 
        E += C_DDOT(norbs*norbs,Db[0],1,Fb[0],1); 
        E *= 0.5;       
 
        //Diagonalize Fa and Fb to from Ca and Cb and Da and Db
        atomicUHFHelperFormCandD(nalpha,norbs,Shalf,Fa,Ca,Da);
        atomicUHFHelperFormCandD(nbeta,norbs,Shalf,Fb,Cb,Db);

        //Form D
        C_DCOPY(norbs*norbs,Da[0],1,D[0],1);
        C_DAXPY(norbs*norbs,1.0,Db[0],1,D[0],1);  

        //Form delta D and Drms
        C_DAXPY(norbs*norbs,-1.0,D[0],1,Dold[0],1);
        double Drms = sqrt(1.0/(1.0*norbs)*C_DDOT(norbs*norbs,Dold[0],1,Dold[0],1));  

        double deltaE = fabs(E-E_old);        
    
        if (print_>6) {
        fprintf(outfile,"  Fa:\n");
        print_mat(Fa,norbs,norbs,outfile);

        fprintf(outfile,"  Fb:\n");
        print_mat(Fb,norbs,norbs,outfile);

        fprintf(outfile,"  Ga:\n");
        print_mat(Ga,norbs,norbs,outfile);

        fprintf(outfile,"  Gb:\n");
        print_mat(Gb,norbs,norbs,outfile);

        fprintf(outfile,"  Ca:\n");
        print_mat(Ca,norbs,norbs,outfile);

        fprintf(outfile,"  Cb:\n");
        print_mat(Cb,norbs,norbs,outfile);

        fprintf(outfile,"  Da:\n");
        print_mat(Da,norbs,norbs,outfile);

        fprintf(outfile,"  Db:\n");
        print_mat(Db,norbs,norbs,outfile);

        fprintf(outfile,"  D:\n");
        print_mat(D,norbs,norbs,outfile);
        }
        if (print_>1)
            fprintf(outfile, "  @Atomic UHF iteration %3d energy: %20.14f    %20.14f %20.14f\n", iteration, E, deltaE, Drms);
        if (iteration > 1 && deltaE < E_tol && Drms < D_tol)
            converged = true;

        if (iteration > maxiter) {  
            fprintf(outfile, "Atomic UHF is not converging!");
            break;
        }

        //Check convergence 
    } while (!converged);
    if (converged && print_ > 1)
        fprintf(outfile, "\n  @Atomic UHF Final Energy: %20.14f\n", E);
    
    free_block(Ca);
    free_block(Cb);
    free_block(Da);
    free_block(Db);
    free_block(Fa);
    free_block(Fb);
    free_block(Ga);
    free_block(Gb);
    free_block(H);
    free_block(Shalf);
}
void HF::atomicUHFHelperFormCandD(int nelec, int norbs,double** Shalf, double**F, double** C, double** D)
{
    //Forms C in the AO basis for SAD Guesses
    double **Temp = block_matrix(norbs,norbs);
    double **Fp = block_matrix(norbs,norbs);
    double **Cp = block_matrix(norbs,norbs);
    
    //Form F' = X'FX = XFX for symmetric orthogonalization 
    C_DGEMM('N','N',norbs,norbs,norbs,1.0,Shalf[0],norbs,F[0],norbs,0.0,Temp[0],norbs);  
    C_DGEMM('N','N',norbs,norbs,norbs,1.0,Temp[0],norbs,Shalf[0],norbs,0.0,Fp[0],norbs);  

    //Form C' = eig(F')
    double *eigvals = init_array(norbs);
    sq_rsp(norbs, norbs, Fp,  eigvals, 1, Cp, 1.0e-14);
    free(eigvals);    

    //Form C = XC'
    C_DGEMM('N','N',norbs,norbs,norbs,1.0,Shalf[0],norbs,Cp[0],norbs,0.0,C[0],norbs);

    //Form D = Cocc*Cocc'
    C_DGEMM('N','T',norbs,norbs,nelec,1.0,C[0],norbs,C[0],norbs,0.0,D[0],norbs); 

    free_block(Temp);
    free_block(Cp);
    free_block(Fp);
}

}}
