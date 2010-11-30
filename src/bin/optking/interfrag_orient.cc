/*! \file interfrag_orient.cc
    \ingroup OPTKING
    \brief function moves the geometry of fragment B so that the interfragment coordinates
      have the given values

   ndA = # of ref pts on A to worry about
   ndB = # of ref pts on B to worry about

   Value at least
    ndA   ndB
   ------------
     1     1     R_AB
     2     1     + theta_A
     1     2     + theta_B
     2     2     + theta_A + theta_B + fix tau
     3     2     + phi_A
     2     3     + phi_A + phi_B
   ------------
   
   returns true if successful, false if not
*/

#include "frag.h"
#include "interfrag.h"
#include "print.h"
#include "v3d.h"

#define EXTERN
#include "globals.h"

namespace opt {

using namespace v3d;

void zmat_point(double *A, double *B, double *C, double R_CD, double theta_BCD,
  double phi_ABCD, double *D);

void rotate_vecs(double *w, double phi, double **v, int num_v);

// arguments specify the bond length and angle in radians desired for interfragment coordinates
bool INTERFRAG::orient_fragment(double *q_target) {

  int pts, i, xyz;
  double tval, norm, B_angle, R_B1B2, R_B2B3, e12[3], e12b[3], e12c[3], e12d[3], erot[3];
  double **ref_A, **ref_B, **ref_B_final;
  double sign, cross1[3], cross2[3], cross3[3], phi2, phi3;

  // fill-in unused values with defaults to make code below work
  double R_AB, theta_A, theta_B, tau, phi_A, phi_B;
  R_AB  = 1.0;
  theta_A = theta_B = tau = phi_A = phi_B = _pi/2;

  int cnt = 0;
  if (D_on[0]) R_AB     = q_target[cnt++];
  if (D_on[1]) theta_A = q_target[cnt++]; 
  if (D_on[2]) theta_B = q_target[cnt++];
  if (D_on[3]) tau   =  q_target[cnt++];
  if (D_on[4]) phi_A =  q_target[cnt++];
  if (D_on[5]) phi_B =  q_target[cnt++];

  fprintf(outfile,"\n\tInterfragment coordinates to be obtained:\n");
  if (D_on[0]) fprintf(outfile,"\t  R_AB:%10.5f",    R_AB);
  if (D_on[1]) fprintf(outfile,"\t  theta_A:%10.5f", theta_A);
  if (D_on[2]) fprintf(outfile,"\t  theta_B:%10.5f", theta_B);
  if (D_on[3]) fprintf(outfile,"\t  tau:%10.5f",     tau);
  if (D_on[4]) fprintf(outfile,"\t  phi_A:%10.5f",   phi_A);
  if (D_on[5]) fprintf(outfile,"\t  phi_B:%10.5f", phi_B);
  fprintf(outfile,"\n");

  ref_A = init_matrix(3,3);
  ref_B = init_matrix(ndB,3);
  ref_B_final = init_matrix(ndB,3);

  // compute current location of reference points on A and B
  for (pts=0; pts<ndA; ++pts)
    for (i=0; i<A->natom;++i)
      for (xyz=0; xyz<3; ++xyz)
        ref_A[pts][xyz] += weightA[pts][i] * A->geom[i][xyz];

  // stick SOMETHING in for non-specified reference atoms for zmat_point() function
  if (ndA < 3)
    for (xyz=0; xyz<3; ++xyz)
      ref_A[2][xyz] = (xyz+1);

  if (ndA < 2)
    for (xyz=0; xyz<3; ++xyz)
      ref_A[1][xyz] = (xyz+2);

  for (pts=0; pts<ndB; ++pts)
    for (i=0; i<B->natom;++i)
      for (xyz=0; xyz<3; ++xyz)
        ref_B[pts][xyz] += weightB[pts][i] * B->geom[i][xyz];

  // compute B1-B2 distance, B2-B3 distance, and B1-B2-B3 angle
  if (ndB>1)
    R_B1B2 = v3d_dist(ref_B[1], ref_B[0]);

  if (ndB>2) {
    R_B2B3 = v3d_dist(ref_B[2], ref_B[1]);
    v3d_angle(ref_B[0], ref_B[1], ref_B[2], B_angle);
  }

  // determine target location of reference pts for B in coordinate system of A
  zmat_point(ref_A[2], ref_A[1], ref_A[0], R_AB, theta_A, phi_A, ref_B_final[0]);
  if (ndB>1)
    zmat_point(ref_A[1], ref_A[0], ref_B_final[0], R_B1B2, theta_B, tau, ref_B_final[1]);
  if (ndB>2)
    zmat_point(ref_A[0], ref_B_final[0], ref_B_final[1], R_B2B3, B_angle, phi_B, ref_B_final[2]);

  // translate B->geom (and reference points) to place B1 in correct location
  for (xyz=0; xyz<3; ++xyz) {
    tval = ref_B_final[0][xyz] - ref_B[0][xyz];
    for (i=0; i<B->natom; ++i)
      B->geom[i][xyz] += tval;
    for (pts=0; pts<ndB; ++pts)
      ref_B[pts][xyz] += tval;
  }

  if (ndB>1) { /* move fragment B to place reference point B2 in correct location */
    /* Determine rotational angle and axis */
    v3d_eAB(ref_B[1],       ref_B[0], e12);  /* v B1->B2 */
    v3d_eAB(ref_B_final[1], ref_B[0], e12b); /* v B1->B2_final */
    B_angle = acos(v3d_dot(e12b,e12));

    if (fabs(B_angle) > 1.0e-7) {
      v3d_cross_product(e12,e12b,erot);

      /* Move B to put B1 at origin */
      for (xyz=0; xyz<3; ++xyz)
        for (i=0; i<B->natom;++i)
          B->geom[i][xyz] -= ref_B[0][xyz];

      /* Rotate B */
      rotate_vecs(erot, B_angle, B->geom, B->natom);

      /* Move B back to coordinate system of A */
      for (xyz=0; xyz<3; ++xyz)
        for (i=0; i<B->natom;++i)
          B->geom[i][xyz] += ref_B[0][xyz];

      // recompute current B reference points
      for (pts=0; pts<ndB; ++pts) 
        for (xyz=0; xyz<3; ++xyz) {
          ref_B[pts][xyz] = 0.0;
          for (i=0; i<B->natom;++i)
            ref_B[pts][xyz] += weightB[pts][i] * B->geom[i][xyz];
        }
    }
  }
  if (ndB==3) { // move fragment B to place reference point B3 in correct location
    // Determine rotational angle and axis
    v3d_eAB(ref_B[1], ref_B[0], erot);  /* B1 -> B2 is rotation axis */

    /* Calculate B3-B1-B2-B3' torsion angle */
    v3d_eAB(ref_B[2], ref_B[0], e12);  /* v B1->B3 */
    v3d_eAB(ref_B[1], ref_B[0], e12b); /* v B1->B2 */
    phi2 = acos(v3d_dot(e12,e12b));
    v3d_eAB(ref_B[0], ref_B[1], e12c);  /* v B2->B1 */
    v3d_eAB(ref_B_final[2], ref_B[1], e12d); /* v B2->B3' */
    phi3 = acos(v3d_dot(e12c,e12d));

    v3d_cross_product(e12 , e12b, cross1) ; /* B3->B1 x B1->B2 */
    v3d_cross_product(e12c, e12d, cross2) ; /* B1->B2 x B2->B3 */
    tval = v3d_dot(cross1, cross2) ;

    if ((sin(phi2) > 1e-7) && (sin(phi3) > 1e-7)) {
      tval /= sin(phi2) ;
      tval /= sin(phi3) ;
    }
    else tval = 2.0;

    if (tval > 0.9999999999) B_angle = 0.0;
    else if (tval < -0.9999999999) B_angle = _pi;
    else B_angle = acos(tval) ;

    sign = 1.0; /* check sign */
    v3d_cross_product(cross1, cross2, cross3);
    norm = sqrt(v3d_dot(cross3, cross3));
    if (fabs(norm) > 1e-14) {
      for (xyz=0; xyz<3; ++xyz)
        cross3[xyz] *= 1.0/norm;
      tval = v3d_dot(cross3, e12b);
      if (tval < 0.0) sign = -1.0;
    }
    B_angle *= sign;
    if (fabs(B_angle) > 1.0e-10) {

      // Move B to put B2 at origin
      for (xyz=0; xyz<3; ++xyz)
        for (i=0; i<B->natom;++i)
          B->geom[i][xyz] -= ref_B[1][xyz];

      rotate_vecs(erot, B_angle, B->geom, B->natom);

      // Translate B1 back to coordinate system of A
      for (xyz=0; xyz<3; ++xyz)
        for (i=0; i<B->natom;++i)
          B->geom[i][xyz] += ref_B[1][xyz];

      // update B reference points
      for (pts=0; pts<ndB; ++pts)
        for (xyz=0; xyz<3; ++xyz) {
          ref_B[pts][xyz] = 0.0;
          for (i=0; i<B->natom;++i)
            ref_B[pts][xyz] += weightB[pts][i] * B->geom[i][xyz];
      }
    }
  }
   // check to see if desired reference points were obtained
   tval = 0.0;
   for (i=0; i<ndB; ++i)
     for (xyz=0; xyz<3; ++xyz)
       tval += (ref_B[i][xyz] - ref_B_final[i][xyz])*(ref_B[i][xyz] - ref_B_final[i][xyz]);
   tval = sqrt(tval);

   free_matrix(ref_A);
   free_matrix(ref_B);
   free_matrix(ref_B_final);

   fprintf(outfile,"\tDifference from target, |x_target - x_achieved| = %.2e\n",tval);

   if (tval > 1.0e-8) {
     fprintf(outfile,"Unsuccessful at orienting fragments.\n");
     return false;
   }
   else {
     fprintf(outfile,"Successfully oriented fragments.\n");
     return true;
   }
}

/*
void unit_vec(double *B, double *A, double *AB) {
  double norm = 0.0;
  int i;

  for (i=0; i<3; i++)
    norm += (A[i]-B[i])*(A[i]-B[i]);
  norm = sqrt(norm);
  for (i=0; i<3; i++)
    AB[i] = (B[i] - A[i]) / norm;
  return;
}
*/

/* Given the xyz coordinates for three points and R, theta, and phi, returns the
coordinates of a fourth point; angles in radians */

void zmat_point(double *A, double *B, double *C, double R_CD, double theta_BCD,
  double phi_ABCD, double *D) {

  double eAB[3],eBC[3],eX[3],eY[3], cosABC, sinABC;

  v3d_eAB(B,A,eAB); /* vector B->A */
  v3d_eAB(C,B,eBC); /* vector C->B */
  cosABC = -v3d_dot(eBC,eAB);

  sinABC = sqrt(1 - (cosABC * cosABC) );
  if ( (sinABC - 1.0e-14) < 0.0 ) {
    printf("Reference points cannot be colinear.");
    exit(PSI_RETURN_FAILURE);
  }

  v3d_cross_product(eAB,eBC,eY);
  for(int xyz=0;xyz<3;xyz++)
    eY[xyz] /= sinABC;
  v3d_cross_product(eY,eBC,eX);
  for (int xyz=0;xyz<3;xyz++)
    D[xyz] = C[xyz] + R_CD * ( - eBC[xyz] * cos(theta_BCD) +
                                 eX[xyz] * sin(theta_BCD) * cos(phi_ABCD) +
                                 eY[xyz] * sin(theta_BCD) * sin(phi_ABCD) );
  return;
}

/*!
** rotate_vecs(): Rotate a set of vectors around an arbitrary axis
**
** \brief Rotate a set of vectors around an arbitrary axis
** Vectors are rows of input matrix
**
** \param  w     double *  : axis to rotate around (wx, wy, wz) - gets normalized here
** \param  phi   double    : magnitude of rotation
** \param  v   double ** : points to rotate - column dim is 3; overwritten on exit
** \param  num_v  int       :
**
** Returns: none
**
** Rollin King, Feb. 2008
** \ingroup OPT
*/

void rotate_vecs(double *w, double phi, double **v, int num_v) {
  int i, j;
  double **R, **v_new, wx, wy, wz, cp, norm;

  norm = sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);

  w[0] /= norm; w[1] /= norm; w[2] /= norm;

  wx = w[0]; wy = w[1]; wz = w[2];
  cp = 1.0 - cos(phi);

  R = init_matrix(3,3);

  R[0][0] =     cos(phi) + wx*wx*cp;
  R[0][1] = -wz*sin(phi) + wx*wy*cp;
  R[0][2] =  wy*sin(phi) + wx*wz*cp;
  R[1][0] =  wz*sin(phi) + wx*wy*cp;
  R[1][1] =     cos(phi) + wy*wy*cp;
  R[1][2] = -wx*sin(phi) + wy*wz*cp;
  R[2][0] = -wy*sin(phi) + wx*wz*cp;
  R[2][1] =  wx*sin(phi) + wy*wz*cp;
  R[2][2] =     cos(phi) + wz*wz*cp;

  v_new = init_matrix(num_v,3);
  //mmult(R, 0, v, 1, v_new, 1, 3, 3, num_v, 0);
  opt_matrix_mult(R, 0, v, 1, v_new, 1, 3, 3, num_v, 0);

  for (i=0; i<num_v; ++i)
    for (j=0; j<3; ++j)
      v[i][j] = v_new[i][j];

  free_matrix(v_new);
  free_matrix(R);
}

} // opt
