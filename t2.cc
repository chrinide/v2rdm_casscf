/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * BP-v2RDM: a boundary-point semidefinite solver for variational 2-RDM
 *           computations.
 *
 * Copyright (c) 2014, The Florida State University. All rights reserved.
 * 
 *@END LICENSE
 *
 * This code performs a semidefinite optimization of the electronic
 * energy according to the boundary-point algorithm described in
 * PRL 106, 083001 (2011).
 *
 */
#include <psi4-dec.h>
#include <libparallel/parallel.h>
#include <liboptions/liboptions.h>
#include <libqt/qt.h>

#include<libtrans/integraltransform.h>
#include<libtrans/mospace.h>

#include<libmints/wavefunction.h>
#include<libmints/mints.h>
#include<libmints/vector.h>
#include<libmints/matrix.h>
#include<../bin/fnocc/blas.h>
#include<time.h>

#include"v2rdm_solver.h"

#ifdef _OPENMP
    #include<omp.h>
#else
    #define omp_get_wtime() ( (double)clock() / CLOCKS_PER_SEC )
    #define omp_get_max_threads() 1
#endif

using namespace boost;
using namespace psi;
using namespace fnocc;

namespace psi{ namespace v2rdm_casscf{

// T2 portion of A.u 
void v2RDMSolver::T2_constraints_Au(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    int saveoff = offset;

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ij = 0; ij < gems_aa[h]; ij++) {
            int i = bas_aa_sym[h][ij][0];
            int j = bas_aa_sym[h][ij][1];
            for (int lm = 0; lm < gems_aa[h]; lm++) {
                int l = bas_aa_sym[h][lm][0];
                int m = bas_aa_sym[h][lm][1];
                for (int k = 0; k < amo_; k++) {
                    int h2 = SymmetryPair(h,symmetry[k]);
                    int myoffset = saveoff;
                    for (int myh = 0; myh < h2; myh++) {
                        myoffset += trip_aab[myh]*trip_aab[myh];
                    }
                    int ijk = ibas_aab_sym[h2][i][j][k];
                    int lmn = ibas_aab_sym[h2][l][m][k];
                    A_p[myoffset + ijk*trip_aab[h2]+lmn] += u_p[d2aaoff[h] + ij*gems_aa[h]+lm]; // + D2(ij,lm) dkn
                }
            }
        }

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {
            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];


            for (int m = 0; m < amo_; m++) {
                if ( i >= m ) continue;
                int l = i;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][m][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hmk][j][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] -= u_p[d2aboff[hmk] + jn*gems_ab[hmk]+mk]; // - D2(nj,km) dil
                }
            }

            for (int m = 0; m < amo_; m++) {
                if ( j >= m ) continue;
                int l = j;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][m][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hmk][i][n];
                    int lmn  = ibas_aab_sym[h][j][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d2aboff[hmk] + in*gems_ab[hmk]+mk]; // D2(ni,km) djl
                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = i;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][l][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hlk][j][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d2aboff[hlk] + jn*gems_ab[hlk]+lk]; // D2(nj,kl) dim

                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = j;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][l][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hlk][i][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] -= u_p[d2aboff[hlk] + in*gems_ab[hlk]+lk]; // -D2(ni,kl) djm

                }
            }

            for (int n = 0; n < amo_; n++) {
                int m = j;
                int l = i;
                if ( l >= m ) continue;

                int hmn  = SymmetryPair(symmetry[m],symmetry[n]);
                int hlmn = SymmetryPair(hmn,symmetry[l]);
                if ( hlmn != h ) continue;

                int lmn  = ibas_aab_sym[h][l][m][n];

                int h2 = symmetry[k];
                int kk = k - pitzer_offset[h2];
                int nn = n - pitzer_offset[h2];

                A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
            }
        }
        F_DAXPY(trip_aab[h] * trip_aab[h], -1.0, &u_p[t2aaboff[h]],1,&A_p[offset],1);


        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2aaboff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( j == l && i == m ) { // this one never gets used ...
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int km = ibas_ab_sym[hnj][m][k];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }

                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int km = ibas_ab_sym[hni][m][k];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }

                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int kl = ibas_ab_sym[hnj][l][k];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + ijk*trip_aab[h]+lmn] += dum;

            }
        }*/
        offset += trip_aab[h]*trip_aab[h];
    }

    saveoff = offset;

    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ij = 0; ij < gems_aa[h]; ij++) {
            int i = bas_aa_sym[h][ij][0];
            int j = bas_aa_sym[h][ij][1];
            for (int lm = 0; lm < gems_aa[h]; lm++) {
                int l = bas_aa_sym[h][lm][0];
                int m = bas_aa_sym[h][lm][1];
                for (int k = 0; k < amo_; k++) {
                    int h2 = SymmetryPair(h,symmetry[k]);
                    int myoffset = saveoff;
                    for (int myh = 0; myh < h2; myh++) {
                        myoffset += trip_aab[myh]*trip_aab[myh];
                    }
                    int ijk = ibas_aab_sym[h2][i][j][k];
                    int lmn = ibas_aab_sym[h2][l][m][k];
                    A_p[myoffset + ijk*trip_aab[h2]+lmn] += u_p[d2bboff[h] + ij*gems_aa[h]+lm]; // + D2(ij,lm) dkn
                }
            }
        }

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {
            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];


            for (int m = 0; m < amo_; m++) {
                if ( i >= m ) continue;
                int l = i;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][k][m];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hmk][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] -= u_p[d2aboff[hmk] + jn*gems_ab[hmk]+mk]; // - D2(nj,km) dil
                }
            }

            for (int m = 0; m < amo_; m++) {
                if ( j >= m ) continue;
                int l = j;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][k][m];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hmk][n][i];
                    int lmn  = ibas_aab_sym[h][j][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d2aboff[hmk] + in*gems_ab[hmk]+mk]; // D2(ni,km) djl
                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = i;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][k][l];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hlk][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d2aboff[hlk] + jn*gems_ab[hlk]+lk]; // D2(nj,kl) dim

                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = j;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][k][l];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hlk][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[offset + ijk*trip_aab[h]+lmn] -= u_p[d2aboff[hlk] + in*gems_ab[hlk]+lk]; // -D2(ni,kl) djm

                }
            }

            for (int n = 0; n < amo_; n++) {
                int m = j;
                int l = i;
                if ( l >= m ) continue;

                int hmn  = SymmetryPair(symmetry[m],symmetry[n]);
                int hlmn = SymmetryPair(hmn,symmetry[l]);
                if ( hlmn != h ) continue;

                int lmn  = ibas_aab_sym[h][l][m][n];

                int h2 = symmetry[k];
                int kk = k - pitzer_offset[h2];
                int nn = n - pitzer_offset[h2];

                A_p[offset + ijk*trip_aab[h]+lmn] += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
            }
        }
        F_DAXPY(trip_aab[h] * trip_aab[h], -1.0, &u_p[t2bbaoff[h]],1,&A_p[offset],1);

        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int km = ibas_ab_sym[hnj][k][m];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int km = ibas_ab_sym[hni][k][m];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int kl = ibas_ab_sym[hnj][k][l];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }*/
        offset += trip_aab[h]*trip_aab[h];
    }

    // big block 1: T2aaa + T2abb
#if 1
    for (int h = 0; h < nirrep_; h++) {

        // T2aaa/aaa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];
            int hij = SymmetryPair(symmetry[i],symmetry[j]);
            int ij = ibas_aa_sym[hij][i][j];
            int ijk_id = offset + ijk*(trip_aab[h]+trip_aba[h]);

            for (int l = 0; l < amo_; l++) {
                int n = k;
                int hln  = SymmetryPair(symmetry[n],symmetry[l]);
                int hm  = SymmetryPair(h,hln);
                for (int m = ( l + 1 > pitzer_offset[hm] ? l : pitzer_offset[hm] ); m < pitzer_offset[hm]+amopi_[hm]; m++) {
                    int lm = ibas_aa_sym[hij][l][m];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[ijk_id + lmn] += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }
            }

            for (int m = i+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkm][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] -= s2 * u_p[d2aaoff[hkm] + nj*gems_aa[hkm]+km]; // - D2(nj,km) dil
                }
            }
            for (int m = j+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkm][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[ijk_id + lmn] += s2 * u_p[d2aaoff[hkm] + ni*gems_aa[hkm]+km]; // D2(ni,km) djl
                }
            }
            for (int l = 0; l < i; l++) {
                if ( k == l ) continue;
                int m = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkl][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] += s2 * u_p[d2aaoff[hkl] + nj*gems_aa[hkl]+kl]; // D2(nj,kl) dim
                }
            }
            for (int l = 0; l < j; l++) {
                if ( k == l ) continue;
                int m = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkl][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] -= s2 * u_p[d2aaoff[hkl] + ni*gems_aa[hkl]+kl]; // -D2(ni,kl) djm
                }
            }
            int hk = symmetry[k];
            int kk = k - pitzer_offset[hk];
            int m = j;
            int l = i;
            for (int n = pitzer_offset[hk]; n < pitzer_offset[hk] + amopi_[hk]; n++) {
                int nn = n - pitzer_offset[hk];
                int lmn  = ibas_aab_sym[h][l][m][n];
                A_p[ijk_id + lmn] += u_p[d1aoff[hk] + nn*amopi_[hk]+kk]; // + D1(n,k) djm dil
            }
        }

        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = -u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                // this one is never entered
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                A_p[offset + id] += dum;

            }
        }*/
        // T2aaa/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            int ijk_id = offset + ijk*(trip_aab[h]+trip_aba[h]);

            for (int m = 0; m < amo_; m++) {
                int l = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_ab_sym[hkm][k][m];
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hkm][j][n];
                    int lmn  = ibas_aba_sym[h][l][m][n];
                    A_p[ijk_id + (trip_aab[h] + lmn)] += u_p[d2aboff[hkm]+jn*gems_ab[hkm]+km]; // D2(jn,km) dil
                    A_p[offset + (trip_aab[h] + lmn)*(trip_aab[h]+trip_aba[h]) + ijk] += u_p[d2aboff[hkm]+jn*gems_ab[hkm]+km]; // D2(jn,km) dil
                }
            }
            for (int m = 0; m < amo_; m++) {
                int l = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_ab_sym[hkm][k][m];
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hkm][i][n];
                    int lmn  = ibas_aba_sym[h][l][m][n];
                    A_p[ijk_id + (trip_aab[h] + lmn)] -= u_p[d2aboff[hkm]+in*gems_ab[hkm]+km]; // -D2(in,km) djl
                    A_p[offset + (trip_aab[h] + lmn)*(trip_aab[h]+trip_aba[h]) + ijk] -= u_p[d2aboff[hkm]+in*gems_ab[hkm]+km]; // -D2(in,km) djl
                }
            }
        }

        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][i][n];
                    int km = ibas_ab_sym[hin][k][m];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                A_p[offset + id] += dum;

            }
        }*/

        // T2abb/aaa
        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int lk = ibas_ab_sym[hnj][l][k];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                A_p[offset + id] += dum;

            }
        }*/

        // T2abb/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            int ijk_id = offset + (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h]);
            int hij = SymmetryPair(symmetry[i],symmetry[j]);
            int ij = ibas_ab_sym[hij][i][j];
            for (int lm = 0; lm < gems_ab[hij]; lm++) {
                int l = bas_ab_sym[hij][lm][0];
                int m = bas_ab_sym[hij][lm][1];
                int n = k;
                int lmn = ibas_aba_sym[h][l][m][n];
                A_p[ijk_id + (trip_aab[h] + lmn)] += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
            }

            for (int l = 0; l < amo_; l++) {
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][l][k];
                int m = j;
                int hlm = SymmetryPair(symmetry[l],symmetry[m]);
                int hn = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int ni = ibas_ab_sym[hlk][i][n];
                    int lmn = ibas_aba_sym[h][l][m][n];
                    A_p[ijk_id + (trip_aab[h] + lmn)] -= u_p[d2aboff[hlk] + ni*gems_ab[hlk]+lk]; // -D2(ni,kl) djm
                }
            }
            for (int m = 0; m < amo_; m++) {
                if ( k == m ) continue;
                int hkm = SymmetryPair(symmetry[k],symmetry[m]);
                int km = ibas_aa_sym[hkm][k][m];
                int l = i;
                int hlm = SymmetryPair(symmetry[l],symmetry[m]);
                int hn = SymmetryPair(h,hlm);
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( n == j ) continue;
                    int nj = ibas_aa_sym[hkm][n][j];
                    int lmn = ibas_aba_sym[h][l][m][n];
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;
                    A_p[ijk_id + (trip_aab[h] + lmn)] -= s2 * u_p[d2bboff[hkm] + nj*gems_aa[hkm]+km]; // - D2(nj,km) dil
                }
            }
            int m = j;
            int l = i;
            int hk = symmetry[k];
            int kk = k - pitzer_offset[hk];
            for (int n = pitzer_offset[hk]; n < pitzer_offset[hk]+amopi_[hk]; n++) {
                int nn = n - pitzer_offset[hk];
                int lmn = ibas_aba_sym[h][l][m][n];
                A_p[ijk_id + (trip_aab[h] + lmn)] += u_p[d1boff[hk] + nn*amopi_[hk]+kk]; // + D1(n,k) djm dil
            }

        }
        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + id] += dum;

            }
        }*/
        F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2aaaoff[h]],1,&A_p[offset],1);

        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }

    // big block 2: T2bbb + T2baa
    for (int h = 0; h < nirrep_; h++) {

        // T2bbb/bbb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];
            int hij = SymmetryPair(symmetry[i],symmetry[j]);
            int ij = ibas_aa_sym[hij][i][j];
            int ijk_id = offset + ijk*(trip_aab[h]+trip_aba[h]);

            for (int l = 0; l < amo_; l++) {
                int n = k;
                int hln  = SymmetryPair(symmetry[n],symmetry[l]);
                int hm  = SymmetryPair(h,hln);
                for (int m = ( l + 1 > pitzer_offset[hm] ? l : pitzer_offset[hm] ); m < pitzer_offset[hm]+amopi_[hm]; m++) {
                    int lm = ibas_aa_sym[hij][l][m];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[ijk_id + lmn] += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }
            }

            for (int m = i+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkm][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] -= s2 * u_p[d2bboff[hkm] + nj*gems_aa[hkm]+km]; // - D2(nj,km) dil
                }
            }
            for (int m = j+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkm][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[ijk_id + lmn] += s2 * u_p[d2bboff[hkm] + ni*gems_aa[hkm]+km]; // D2(ni,km) djl
                }
            }
            for (int l = 0; l < i; l++) {
                if ( k == l ) continue;
                int m = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkl][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] += s2 * u_p[d2bboff[hkl] + nj*gems_aa[hkl]+kl]; // D2(nj,kl) dim
                }
            }
            for (int l = 0; l < j; l++) {
                if ( k == l ) continue;
                int m = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkl][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[ijk_id + lmn] -= s2 * u_p[d2bboff[hkl] + ni*gems_aa[hkl]+kl]; // -D2(ni,kl) djm
                }
            }
            int hk = symmetry[k];
            int kk = k - pitzer_offset[hk];
            int m = j;
            int l = i;
            for (int n = pitzer_offset[hk]; n < pitzer_offset[hk] + amopi_[hk]; n++) {
                int nn = n - pitzer_offset[hk];
                int lmn  = ibas_aab_sym[h][l][m][n];
                A_p[ijk_id + lmn] += u_p[d1boff[hk] + nn*amopi_[hk]+kk]; // + D1(n,k) djm dil
            }
        }

        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2bboff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2bboff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                A_p[offset + id] = dum;

            }
        }*/

        // T2bbb/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            int ijk_id = offset + ijk*(trip_aab[h]+trip_aba[h]);

            for (int m = 0; m < amo_; m++) {
                int l = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_ab_sym[hkm][m][k];
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hkm][n][j];
                    int lmn  = ibas_aba_sym[h][l][m][n];
                    A_p[ijk_id + (trip_aab[h] + lmn)] += u_p[d2aboff[hkm]+jn*gems_ab[hkm]+km]; // D2(jn,km) dil
                    A_p[offset + (trip_aab[h] + lmn)*(trip_aab[h]+trip_aba[h]) + ijk] += u_p[d2aboff[hkm]+jn*gems_ab[hkm]+km]; // D2(jn,km) dil
                }
            }
            for (int m = 0; m < amo_; m++) {
                int l = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_ab_sym[hkm][m][k];
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hkm][n][i];
                    int lmn  = ibas_aba_sym[h][l][m][n];
                    A_p[ijk_id + (trip_aab[h] + lmn)] -= u_p[d2aboff[hkm]+in*gems_ab[hkm]+km]; // -D2(in,km) djl
                    A_p[offset + (trip_aab[h] + lmn)*(trip_aab[h]+trip_aba[h]) + ijk] -= u_p[d2aboff[hkm]+in*gems_ab[hkm]+km]; // -D2(in,km) djl
                }
            }
        }
        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][n][i];
                    int km = ibas_ab_sym[hin][m][k];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }*/

        // T2baa/bbb
        /*#pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int lk = ibas_ab_sym[hnj][k][l];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }*/
        // T2baa/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + id] = dum;

            }
        }
        F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2bbboff[h]],1,&A_p[offset],1);

        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }
#endif

}
// T2 portion of A.u (slow version!)
void v2RDMSolver::T2_constraints_Au_slow(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2aaboff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int km = ibas_ab_sym[hnj][m][k];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int km = ibas_ab_sym[hni][m][k];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int kl = ibas_ab_sym[hnj][l][k];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }

    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int km = ibas_ab_sym[hnj][k][m];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int km = ibas_ab_sym[hni][k][m];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int kl = ibas_ab_sym[hnj][k][l];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }

    // big block 1: T2aaa + T2abb
#if 1
    for (int h = 0; h < nirrep_; h++) {

        // T2aaa/aaa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = -u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                A_p[offset + id] = dum;

            }
        }
        // T2aaa/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = -u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][i][n];
                    int km = ibas_ab_sym[hin][k][m];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }

        // T2abb/aaa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = -u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int lk = ibas_ab_sym[hnj][l][k];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }

        // T2abb/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = -u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + id] = dum;

            }
        }
        //F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2aaaoff[h]],1,&A_p[offset],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }

    // big block 2: T2bbb + T2baa
    for (int h = 0; h < nirrep_; h++) {

        // T2bbb/bbb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = -u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2bboff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2bboff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                A_p[offset + id] = dum;

            }
        }
        // T2bbb/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = -u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][n][i];
                    int km = ibas_ab_sym[hin][m][k];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }

        // T2baa/bbb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = -u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int lk = ibas_ab_sym[hnj][k][l];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                A_p[offset + id] = dum;

            }
        }
        // T2baa/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = -u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                A_p[offset + id] = dum;

            }
        }
        //F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2bbboff[h]],1,&A_p[offset],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }
#endif

}
// T2 guess
void v2RDMSolver::T2_constraints_guess(SharedVector u){

    double * u_p = u->pointer();

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = 0.0;//-u_p[t2aaboff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int km = ibas_ab_sym[hnj][m][k];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int km = ibas_ab_sym[hni][m][k];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int kl = ibas_ab_sym[hnj][l][k];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                u_p[t2aaboff[h] + ijk*trip_aab[h]+lmn] = dum; // - T2(ijk,lmn)

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }

    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = 0.0;//-u_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int km = ibas_ab_sym[hnj][k][m];
                    dum -= u_p[d2aboff[hnj] + nj*gems_ab[hnj]+km]; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int km = ibas_ab_sym[hni][k][m];
                    dum += u_p[d2aboff[hni] + ni*gems_ab[hni]+km]; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int kl = ibas_ab_sym[hnj][k][l];
                    dum += u_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl]; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                u_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn] = dum; // - T2(ijk,lmn)

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }

    // big block 1: T2aaa + T2abb
#if 1
    for (int h = 0; h < nirrep_; h++) {

        // T2aaa/aaa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2aaoff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                u_p[t2aaaoff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }
        // T2aaa/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][i][n];
                    int km = ibas_ab_sym[hin][k][m];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                u_p[t2aaaoff[h] + id] = 0.0; // - T2(ijk,lmn)

            }
        }

        // T2abb/aaa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int lk = ibas_ab_sym[hnj][l][k];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                u_p[t2aaaoff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }

        // T2abb/abb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2aaaoff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                u_p[t2aaaoff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }
        //F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2aaaoff[h]],1,&A_p[offset],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }

    // big block 2: T2bbb + T2baa
    for (int h = 0; h < nirrep_; h++) {

        // T2bbb/bbb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum -= u_p[d1boff[h2] + nn*amopi_[h2]+kk]; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        dum += s * u_p[d2bboff[hni] + ni*gems_aa[hni]+km]; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        dum += s * u_p[d2bboff[hnj] + nj*gems_aa[hnj]+kl]; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        dum -= s * u_p[d2bboff[hni] + ni*gems_aa[hni]+kl]; // -D2(ni,kl) djm
                    }
                }
    
                u_p[t2bbboff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }
        // T2bbb/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][n][i];
                    int km = ibas_ab_sym[hin][m][k];
                    dum -= u_p[d2aboff[hin]+in*gems_ab[hin]+km]; // -D2(in,km) djl
                }

                u_p[t2bbboff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }

        // T2baa/bbb
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    dum += u_p[d2aboff[hjn]+jn*gems_ab[hjn]+km]; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int lk = ibas_ab_sym[hnj][k][l];
                    dum -= u_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk]; // -D2(in,km) djl
                }

                u_p[t2bbboff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }
        // T2baa/baa
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = 0.0;//-u_p[t2bbboff[h] + id]; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij]+lm]; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    dum += u_p[d1aoff[h2] + nn*amopi_[h2]+kk]; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        dum -= s * u_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km]; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    dum -= u_p[d2aboff[hni] + ni*gems_ab[hni]+kl]; // -D2(ni,kl) djm
                }
    
                u_p[t2bbboff[h] + id] = dum; // - T2(ijk,lmn)

            }
        }
        //F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[t2bbboff[h]],1,&A_p[offset],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }
#endif

}

// T2 portion of A^T.y 
void v2RDMSolver::T2_constraints_ATu(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    int saveoff = offset;

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        for (int ij = 0; ij < gems_aa[h]; ij++) {
            int i = bas_aa_sym[h][ij][0];
            int j = bas_aa_sym[h][ij][1];
            for (int lm = 0; lm < gems_aa[h]; lm++) {
                int l = bas_aa_sym[h][lm][0];
                int m = bas_aa_sym[h][lm][1];
                for (int k = 0; k < amo_; k++) {
                    int h2 = SymmetryPair(h,symmetry[k]);
                    int myoffset = saveoff;
                    for (int myh = 0; myh < h2; myh++) {
                        myoffset += trip_aab[myh]*trip_aab[myh];
                    }
                    int ijk = ibas_aab_sym[h2][i][j][k];
                    int lmn = ibas_aab_sym[h2][l][m][k];
                    A_p[d2aaoff[h] + ij*gems_aa[h]+lm] += u_p[myoffset + ijk*trip_aab[h2]+lmn]; // + D2(ij,lm) dkn
                }
            }
        }

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {
            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];


            for (int m = 0; m < amo_; m++) {
                if ( i >= m ) continue;
                int l = i;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][m][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hmk][j][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hmk] + jn*gems_ab[hmk]+mk] -= u_p[offset + ijk*trip_aab[h]+lmn]; // - D2(nj,km) dil
                }
            }

            for (int m = 0; m < amo_; m++) {
                if ( j >= m ) continue;
                int l = j;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][m][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hmk][i][n];
                    int lmn  = ibas_aab_sym[h][j][m][n];

                    A_p[d2aboff[hmk] + in*gems_ab[hmk]+mk] += u_p[offset + ijk*trip_aab[h]+lmn]; // D2(ni,km) djl
                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = i;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][l][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hlk][j][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hlk] + jn*gems_ab[hlk]+lk] += u_p[offset + ijk*trip_aab[h]+lmn]; // D2(nj,kl) dim

                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = j;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][l][k];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hlk][i][n];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hlk] + in*gems_ab[hlk]+lk] -= u_p[offset + ijk*trip_aab[h]+lmn]; // -D2(ni,kl) djm

                }
            }

            for (int n = 0; n < amo_; n++) {
                int m = j;
                int l = i;
                if ( l >= m ) continue;

                int hmn  = SymmetryPair(symmetry[m],symmetry[n]);
                int hlmn = SymmetryPair(hmn,symmetry[l]);
                if ( hlmn != h ) continue;

                int lmn  = ibas_aab_sym[h][l][m][n];

                int h2 = symmetry[k];
                int kk = k - pitzer_offset[h2];
                int nn = n - pitzer_offset[h2];

                A_p[d1boff[h2] + nn*amopi_[h2]+kk] += u_p[offset + ijk*trip_aab[h]+lmn]; // + D1(n,k) djm dil
            }
        }
        F_DAXPY(trip_aab[h] * trip_aab[h], -1.0, &u_p[offset],1,&A_p[t2aaboff[h]],1);


        /*for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2aaboff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int km = ibas_ab_sym[hnj][m][k];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+km] -= dum; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int km = ibas_ab_sym[hni][m][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+km] += dum; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int kl = ibas_ab_sym[hnj][l][k];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl] += dum; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }*/
        offset += trip_aab[h]*trip_aab[h];
    }

    saveoff = offset;

    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        for (int ij = 0; ij < gems_aa[h]; ij++) {
            int i = bas_aa_sym[h][ij][0];
            int j = bas_aa_sym[h][ij][1];
            for (int lm = 0; lm < gems_aa[h]; lm++) {
                int l = bas_aa_sym[h][lm][0];
                int m = bas_aa_sym[h][lm][1];
                for (int k = 0; k < amo_; k++) {
                    int h2 = SymmetryPair(h,symmetry[k]);
                    int myoffset = saveoff;
                    for (int myh = 0; myh < h2; myh++) {
                        myoffset += trip_aab[myh]*trip_aab[myh];
                    }
                    int ijk = ibas_aab_sym[h2][i][j][k];
                    int lmn = ibas_aab_sym[h2][l][m][k];
                    A_p[d2bboff[h] + ij*gems_aa[h]+lm] += u_p[myoffset + ijk*trip_aab[h2]+lmn]; // + D2(ij,lm) dkn
                }
            }
        }

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {
            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];


            for (int m = 0; m < amo_; m++) {
                if ( i >= m ) continue;
                int l = i;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][k][m];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hmk][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hmk] + jn*gems_ab[hmk]+mk] -= u_p[offset + ijk*trip_aab[h]+lmn]; // - D2(nj,km) dil
                }
            }

            for (int m = 0; m < amo_; m++) {
                if ( j >= m ) continue;
                int l = j;
                int hmk = SymmetryPair(symmetry[k],symmetry[m]);
                int mk = ibas_ab_sym[hmk][k][m];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hmk][n][i];
                    int lmn  = ibas_aab_sym[h][j][m][n];

                    A_p[d2aboff[hmk] + in*gems_ab[hmk]+mk] += u_p[offset + ijk*trip_aab[h]+lmn]; // D2(ni,km) djl
                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = i;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][k][l];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int jn = ibas_ab_sym[hlk][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hlk] + jn*gems_ab[hlk]+lk] += u_p[offset + ijk*trip_aab[h]+lmn]; // D2(nj,kl) dim

                }
            }

            for (int l = 0; l < amo_; l++) {
                int m = j;
                if ( l >= m ) continue;
                int hlk = SymmetryPair(symmetry[k],symmetry[l]);
                int lk = ibas_ab_sym[hlk][k][l];
                int hlm  = SymmetryPair(symmetry[m],symmetry[l]);
                int hn  = SymmetryPair(h,hlm);
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    int in = ibas_ab_sym[hlk][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aboff[hlk] + in*gems_ab[hlk]+lk] -= u_p[offset + ijk*trip_aab[h]+lmn]; // -D2(ni,kl) djm

                }
            }

            for (int n = 0; n < amo_; n++) {
                int m = j;
                int l = i;
                if ( l >= m ) continue;

                int hmn  = SymmetryPair(symmetry[m],symmetry[n]);
                int hlmn = SymmetryPair(hmn,symmetry[l]);
                if ( hlmn != h ) continue;

                int lmn  = ibas_aab_sym[h][l][m][n];

                int h2 = symmetry[k];
                int kk = k - pitzer_offset[h2];
                int nn = n - pitzer_offset[h2];

                A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += u_p[offset + ijk*trip_aab[h]+lmn]; // + D1(n,k) djm dil
            }
        }
        F_DAXPY(trip_aab[h] * trip_aab[h], -1.0, &u_p[offset],1,&A_p[t2bbaoff[h]],1);

        /*for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int km = ibas_ab_sym[hnj][k][m];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+km] -= dum; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int km = ibas_ab_sym[hni][k][m];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+km] += dum; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int kl = ibas_ab_sym[hnj][k][l];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl] += dum; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }*/
        offset += trip_aab[h]*trip_aab[h];
    }

    // big block 1: T2aaa + T2abb
#if 1
    for (int h = 0; h < nirrep_; h++) {

        // T2aaa/aaa
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];
            int hij = SymmetryPair(symmetry[i],symmetry[j]);
            int ij = ibas_aa_sym[hij][i][j];
            int ijk_id = offset + ijk*(trip_aab[h]+trip_aba[h]);

            for (int l = 0; l < amo_; l++) {
                int n = k;
                int hln  = SymmetryPair(symmetry[n],symmetry[l]);
                int hm  = SymmetryPair(h,hln);
                for (int m = ( l + 1 > pitzer_offset[hm] ? l : pitzer_offset[hm] ); m < pitzer_offset[hm]+amopi_[hm]; m++) {
                    int lm = ibas_aa_sym[hij][l][m];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aaoff[hij] + ij*gems_aa[hij]+lm] += u_p[ijk_id + lmn]; // + D2(ij,lm) dkn
                }
            }

            for (int m = i+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkm][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[d2aaoff[hkm] + nj*gems_aa[hkm]+km] -= s2 * u_p[ijk_id + lmn]; // - D2(nj,km) dil
                }
            }
            for (int m = j+1; m < amo_; m++) {
                if ( k == m ) continue;
                int l = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkm = SymmetryPair(symmetry[m],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int km = ibas_aa_sym[hkm][k][m];
                int s1 = 1;
                if ( k > m ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkm][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];

                    A_p[d2aaoff[hkm] + ni*gems_aa[hkm]+km] += s2 * u_p[ijk_id + lmn]; // D2(ni,km) djl
                }
            }
            for (int l = 0; l < i; l++) {
                if ( k == l ) continue;
                int m = i;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( j == n ) continue;
                    int s2 = s1;
                    if ( n > j ) s2 = -s2;

                    int nj   = ibas_aa_sym[hkl][n][j];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[d2aaoff[hkl] + nj*gems_aa[hkl]+kl] += s2 * u_p[ijk_id + lmn]; // D2(nj,kl) dim
                }
            }
            for (int l = 0; l < j; l++) {
                if ( k == l ) continue;
                int m = j;
                int hml = SymmetryPair(symmetry[m],symmetry[l]);
                int hkl = SymmetryPair(symmetry[l],symmetry[k]);
                int hn  = SymmetryPair(h,hml);
                int kl = ibas_aa_sym[hkl][k][l];
                int s1 = 1;
                if ( k > l ) s1 = -s1;
                for (int n = pitzer_offset[hn]; n < pitzer_offset[hn]+amopi_[hn]; n++) {
                    if ( i == n ) continue;
                    int s2 = s1;
                    if ( n > i ) s2 = -s2;

                    int ni   = ibas_aa_sym[hkl][n][i];
                    int lmn  = ibas_aab_sym[h][l][m][n];
                    A_p[d2aaoff[hkl] + ni*gems_aa[hkl]+kl] -= s2 * u_p[ijk_id + lmn]; // -D2(ni,kl) djm
                }
            }
            int hk = symmetry[k];
            int kk = k - pitzer_offset[hk];
            int m = j;
            int l = i;
            for (int n = pitzer_offset[hk]; n < pitzer_offset[hk] + amopi_[hk]; n++) {
                int nn = n - pitzer_offset[hk];
                int lmn  = ibas_aab_sym[h][l][m][n];
                A_p[d1aoff[hk] + nn*amopi_[hk]+kk] += u_p[ijk_id + lmn]; // + D1(n,k) djm dil
            }
        }

        /*for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = u_p[offset + id];

                A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hni] + ni*gems_aa[hni]+km] += s * dum; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+kl] += s * dum; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2aaoff[hni] + ni*gems_aa[hni]+kl] -= s * dum; // -D2(ni,kl) djm
                    }
                }
            }
        }*/
        // T2aaa/abb
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);
                double dum = u_p[offset + id];

                int id2 = (lmn+trip_aab[h])*(trip_aab[h]+trip_aba[h])+ijk;
                double dum2 = u_p[offset + id2];

                //A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum + dum2; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][i][n];
                    int km = ibas_ab_sym[hin][k][m];
                    A_p[d2aboff[hin]+in*gems_ab[hin]+km] -= dum + dum2; // -D2(in,km) djl
                }
            }
        }

        // T2abb/aaa
        /*for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = u_p[offset + id];

                //A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int lk = ibas_ab_sym[hnj][l][k];
                    A_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk] -= dum; // -D2(in,km) djl
                }
            }
        }*/

        // T2abb/abb
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                //A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    A_p[d2aboff[hij] + ij*gems_ab[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[offset],1,&A_p[t2aaaoff[h]],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }

    // big block 2: T2bbb + T2baa
    for (int h = 0; h < nirrep_; h++) {

        // T2bbb/bbb
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = u_p[offset + id];

                //A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hni] + ni*gems_aa[hni]+km] += s * dum; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+kl] += s * dum; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2bboff[hni] + ni*gems_aa[hni]+kl] -= s * dum; // -D2(ni,kl) djm
                    }
                }
            }
        }
        // T2bbb/baa
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);
                double dum = u_p[offset + id];

                int id2 = (lmn+trip_aab[h])*(trip_aab[h]+trip_aba[h])+ijk;
                double dum2 = u_p[offset + id2];

                //A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum + dum2; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][n][i];
                    int km = ibas_ab_sym[hin][m][k];
                    A_p[d2aboff[hin]+in*gems_ab[hin]+km] -= dum + dum2; // -D2(in,km) djl
                }
            }
        }

        // T2baa/bbb
        /*for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = u_p[offset + id];

                //A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int lk = ibas_ab_sym[hnj][k][l];
                    A_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk] -= dum; // -D2(in,km) djl
                }
            }
        }*/

        // T2baa/baa
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                ///A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    A_p[d2aboff[hij] + ij*gems_ab[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        F_DAXPY((trip_aba[h] + trip_aab[h]) * (trip_aba[h] + trip_aab[h]), -1.0, &u_p[offset],1,&A_p[t2bbboff[h]],1);
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }
#endif

}
// T2 portion of A^T.y (slow version!)
void v2RDMSolver::T2_constraints_ATu_slow(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    int saveoff = offset;

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2aaboff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int km = ibas_ab_sym[hnj][m][k];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+km] -= dum; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int km = ibas_ab_sym[hni][m][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+km] += dum; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int kl = ibas_ab_sym[hnj][l][k];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl] += dum; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }

    saveoff = offset;

    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int km = ibas_ab_sym[hnj][k][m];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+km] -= dum; // - D2(nj,km) dil
                }
                if ( j == l ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int km = ibas_ab_sym[hni][k][m];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+km] += dum; // D2(ni,km) djl
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int kl = ibas_ab_sym[hnj][k][l];
                    A_p[d2aboff[hnj] + nj*gems_ab[hnj]+kl] += dum; // D2(nj,kl) dim
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
#if 1
    // big block 1: T2aaa + T2abb
    for (int h = 0; h < nirrep_; h++) {

        // T2aaa/aaa
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = u_p[offset + id];

                A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hni] + ni*gems_aa[hni]+km] += s * dum; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+kl] += s * dum; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2aaoff[hni] + ni*gems_aa[hni]+kl] -= s * dum; // -D2(ni,kl) djm
                    }
                }
            }
        }
        // T2aaa/abb
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][i][n];
                    int km = ibas_ab_sym[hin][k][m];
                    A_p[d2aboff[hin]+in*gems_ab[hin]+km] -= dum; // -D2(in,km) djl
                }
            }
        }

        // T2abb/aaa
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = u_p[offset + id];

                A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][n][j];
                    int lk = ibas_ab_sym[hnj][l][k];
                    A_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk] -= dum; // -D2(in,km) djl
                }
            }
        }

        // T2abb/abb
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                A_p[t2aaaoff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    A_p[d2aboff[hij] + ij*gems_ab[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][i][n];
                    int kl = ibas_ab_sym[hni][l][k];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }

    // big block 2: T2bbb + T2baa
    for (int h = 0; h < nirrep_; h++) {

        // T2bbb/bbb
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+lmn;
                //int id = ijk*trip_aab[h]+lmn;

                double dum = u_p[offset + id];

                A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }
                if ( j == l && i == m ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1boff[h2] + nn*amopi_[h2]+kk] -= dum; // - D1(n,k) djl dim
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == l ) {
                    if ( n != i && k != m ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int km = ibas_aa_sym[hni][k][m];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2bboff[hni] + ni*gems_aa[hni]+km] += s * dum; // D2(ni,km) djl
                    }
                }
                if ( i == m ) {
                    if ( n != j && k != l ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int kl = ibas_aa_sym[hnj][k][l];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2bboff[hnj] + nj*gems_aa[hnj]+kl] += s * dum; // D2(nj,kl) dim
                    }
                }
                if ( j == m ) {
                    if ( n != i && k != l ) {
                        int hni = SymmetryPair(symmetry[n],symmetry[i]);
                        int ni = ibas_aa_sym[hni][n][i];
                        int kl = ibas_aa_sym[hni][k][l];

                        int s = 1;
                        if ( n > i ) s = -s;
                        if ( k > l ) s = -s;

                        A_p[d2bboff[hni] + ni*gems_aa[hni]+kl] -= s * dum; // -D2(ni,kl) djm
                    }
                }
            }
        }
        // T2bbb/baa
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = ijk*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][n][j];
                    int km = ibas_ab_sym[hjn][m][k];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( j == l ) {
                    int hin = SymmetryPair(symmetry[i],symmetry[n]);
                    int in = ibas_ab_sym[hin][n][i];
                    int km = ibas_ab_sym[hin][m][k];
                    A_p[d2aboff[hin]+in*gems_ab[hin]+km] -= dum; // -D2(in,km) djl
                }
            }
        }

        // T2baa/bbb
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+lmn;

                double dum = u_p[offset + id];

                A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( i == l ) {
                    int hjn = SymmetryPair(symmetry[j],symmetry[n]);
                    int jn = ibas_ab_sym[hjn][j][n];
                    int km = ibas_ab_sym[hjn][k][m];
                    A_p[d2aboff[hjn]+jn*gems_ab[hjn]+km] += dum; // D2(jn,km) dil
                }
                if ( i == m ) {
                    int hnj = SymmetryPair(symmetry[j],symmetry[n]);
                    int nj = ibas_ab_sym[hnj][j][n];
                    int lk = ibas_ab_sym[hnj][k][l];
                    A_p[d2aboff[hnj]+nj*gems_ab[hnj]+lk] -= dum; // -D2(in,km) djl
                }
            }
        }

        // T2baa/baa
        for (int ijk = 0; ijk < trip_aba[h]; ijk++) {

            int i = bas_aba_sym[h][ijk][0];
            int j = bas_aba_sym[h][ijk][1];
            int k = bas_aba_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aba[h]; lmn++) {

                int l = bas_aba_sym[h][lmn][0];
                int m = bas_aba_sym[h][lmn][1];
                int n = bas_aba_sym[h][lmn][2];

                int id = (ijk+trip_aab[h])*(trip_aab[h]+trip_aba[h])+(lmn+trip_aab[h]);

                double dum = u_p[offset + id];

                A_p[t2bbboff[h] + id] -= dum; // - T2(ijk,lmn)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    A_p[d2aboff[hij] + ij*gems_ab[hij]+lm] += dum; // + D2(ij,lm) dkn
                }

                if ( j == m && i == l ) {
                    int h2 = symmetry[k];
                    int kk = k - pitzer_offset[h2];
                    int nn = n - pitzer_offset[h2];
                    A_p[d1aoff[h2] + nn*amopi_[h2]+kk] += dum; // + D1(n,k) djm dil
                }

                if ( i == l ) {
                    if ( n != j && k != m ) {
                        int hnj = SymmetryPair(symmetry[n],symmetry[j]);
                        int nj = ibas_aa_sym[hnj][n][j];
                        int km = ibas_aa_sym[hnj][k][m];

                        int s = 1;
                        if ( n > j ) s = -s;
                        if ( k > m ) s = -s;

                        A_p[d2aaoff[hnj] + nj*gems_aa[hnj]+km] -= s * dum; // - D2(nj,km) dil
                    }
                }
                if ( j == m ) {
                    int hni = SymmetryPair(symmetry[n],symmetry[i]);
                    int ni = ibas_ab_sym[hni][n][i];
                    int kl = ibas_ab_sym[hni][k][l];
                    A_p[d2aboff[hni] + ni*gems_ab[hni]+kl] -= dum; // -D2(ni,kl) djm
                }
            }
        }
        offset += (trip_aba[h]+trip_aab[h])*(trip_aba[h]+trip_aab[h]);
        //offset += trip_aab[h]*trip_aab[h];
    }
#endif

}

// T2 tilde portion of A.u (actually what Mazziotti calls T2)
void v2RDMSolver::T2_tilde_constraints_Au(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2aaboff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                int ijn = ibas_aab_sym[h2][i][j][n];
                int lmk = ibas_aab_sym[h2][l][m][k];

                dum -= u_p[t1aaboff[h2] + ijn*trip_aab[h2] + lmk]; // - T1(ijn,lmk)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2aaoff[hij] + lm*gems_aa[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2aba
    for (int h = 0; h < nirrep_; h++) {
        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int k = bas_aab_sym[h][ijk][1];
            int j = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int n = bas_aab_sym[h][lmn][1];
                int m = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2abaoff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( i != n && l != k ) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aab_sym[h2][i][n][j];
                    int lmk = ibas_aab_sym[h2][l][k][m];

                    int s = 1;
                    if ( i > n ) s = -s;
                    if ( l > k ) s = -s;

                    dum -= u_p[t1aaboff[h2] + ijn*trip_aab[h2] + lmk] * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2aboff[hij] + lm*gems_ab[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                int ijn = ibas_aab_sym[h2][i][j][n];
                int lmk = ibas_aab_sym[h2][l][m][k];

                dum -= u_p[t1bbaoff[h2] + ijn*trip_aab[h2] + lmk]; // - T1(ijn,lmk)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2bboff[hij] + lm*gems_aa[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2bab
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int k = bas_aab_sym[h][ijk][1];
            int j = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int n = bas_aab_sym[h][lmn][1];
                int m = bas_aab_sym[h][lmn][2];

                double dum = -u_p[t2baboff[h] + ijk*trip_aab[h]+lmn]; // - T2(ijk,lmn)

                if ( i != n && l != k ) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aab_sym[h2][i][n][j];
                    int lmk = ibas_aab_sym[h2][l][k][m];

                    int s = 1;
                    if ( i > n ) s = -s;
                    if ( l > k ) s = -s;

                    dum -= u_p[t1bbaoff[h2] + ijn*trip_aab[h2] + lmk] * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    dum += u_p[d2aboff[hij] + ij*gems_ab[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2aboff[hij] + lm*gems_ab[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aab[h]+lmn] = dum;

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2aaa
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aaa[h]; ijk++) {

            int i = bas_aaa_sym[h][ijk][0];
            int j = bas_aaa_sym[h][ijk][1];
            int k = bas_aaa_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aaa[h]; lmn++) {

                int l = bas_aaa_sym[h][lmn][0];
                int m = bas_aaa_sym[h][lmn][1];
                int n = bas_aaa_sym[h][lmn][2];

                double dum = -u_p[t2aaaoff[h] + ijk*trip_aaa[h]+lmn]; // - T2(ijk,lmn)

                if ( i != n && j != n && l != k && m != k) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aaa_sym[h2][i][j][n];
                    int lmk = ibas_aaa_sym[h2][l][m][k];

                    int s = 1;
                    if ( n < j ) s = -s;
                    if ( n < i ) s = -s;
                    if ( k < l ) s = -s;
                    if ( k < m ) s = -s;

                    dum -= u_p[t1aaaoff[h2] + ijn*trip_aaa[h2] + lmk] * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2aaoff[hij] + ij*gems_aa[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2aaoff[hij] + lm*gems_aa[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aaa[h]+lmn] = dum;

            }
        }
        offset += trip_aaa[h]*trip_aaa[h];
    }
    // T2bbb
    for (int h = 0; h < nirrep_; h++) {

        #pragma omp parallel for schedule (static)
        for (int ijk = 0; ijk < trip_aaa[h]; ijk++) {

            int i = bas_aaa_sym[h][ijk][0];
            int j = bas_aaa_sym[h][ijk][1];
            int k = bas_aaa_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aaa[h]; lmn++) {

                int l = bas_aaa_sym[h][lmn][0];
                int m = bas_aaa_sym[h][lmn][1];
                int n = bas_aaa_sym[h][lmn][2];

                double dum = -u_p[t2bbboff[h] + ijk*trip_aaa[h]+lmn]; // - T2(ijk,lmn)

                if ( i != n && j != n && l != k && m != k) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aaa_sym[h2][i][j][n];
                    int lmk = ibas_aaa_sym[h2][l][m][k];

                    int s = 1;
                    if ( n < j ) s = -s;
                    if ( n < i ) s = -s;
                    if ( k < l ) s = -s;
                    if ( k < m ) s = -s;

                    dum -= u_p[t1bbboff[h2] + ijn*trip_aaa[h2] + lmk] * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    dum += u_p[d2bboff[hij] + ij*gems_aa[hij] + lm];  // D2(ij,lm) dkn
                    dum += u_p[q2bboff[hij] + lm*gems_aa[hij] + ij];  // Q2(lm,ij) dkn
                }

                A_p[offset + ijk*trip_aaa[h]+lmn] = dum;

            }
        }
        offset += trip_aaa[h]*trip_aaa[h];
    }
}

// T2 tilde portion of A^T.y (actually what Mazziotti calls T2)
void v2RDMSolver::T2_tilde_constraints_ATu(SharedVector A,SharedVector u){

    double * A_p = A->pointer();
    double * u_p = u->pointer();

    // T2aab
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2aaboff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                int ijn = ibas_aab_sym[h2][i][j][n];
                int lmk = ibas_aab_sym[h2][l][m][k];

                A_p[t1aaboff[h2] + ijn*trip_aab[h2] + lmk] -= dum; // - T1(ijn,lmk)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2aaoff[hij] + lm*gems_aa[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2aba
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int k = bas_aab_sym[h][ijk][1];
            int j = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int n = bas_aab_sym[h][lmn][1];
                int m = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2abaoff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( i != n && l != k ) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aab_sym[h2][i][n][j];
                    int lmk = ibas_aab_sym[h2][l][k][m];

                    int s = 1;
                    if ( i > n ) s = -s;
                    if ( l > k ) s = -s;

                    A_p[t1aaboff[h2] + ijn*trip_aab[h2] + lmk] -= dum * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][i][j];
                    int lm = ibas_ab_sym[hij][l][m];
                    A_p[d2aboff[hij] + ij*gems_ab[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2aboff[hij] + lm*gems_ab[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2bba
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int j = bas_aab_sym[h][ijk][1];
            int k = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int m = bas_aab_sym[h][lmn][1];
                int n = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2bbaoff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                int ijn = ibas_aab_sym[h2][i][j][n];
                int lmk = ibas_aab_sym[h2][l][m][k];

                A_p[t1bbaoff[h2] + ijn*trip_aab[h2] + lmk] -= dum; // - T1(ijn,lmk)

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2bboff[hij] + lm*gems_aa[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }

            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2bab
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aab[h]; ijk++) {

            int i = bas_aab_sym[h][ijk][0];
            int k = bas_aab_sym[h][ijk][1];
            int j = bas_aab_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aab[h]; lmn++) {

                int l = bas_aab_sym[h][lmn][0];
                int n = bas_aab_sym[h][lmn][1];
                int m = bas_aab_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aab[h]+lmn];

                A_p[t2baboff[h] + ijk*trip_aab[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( i != n && l != k ) {
                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aab_sym[h2][i][n][j];
                    int lmk = ibas_aab_sym[h2][l][k][m];

                    int s = 1;
                    if ( i > n ) s = -s;
                    if ( l > k ) s = -s;

                    A_p[t1bbaoff[h2] + ijn*trip_aab[h2] + lmk] -= dum * s; // - T1(ijn,lmk)
                }

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_ab_sym[hij][j][i];
                    int lm = ibas_ab_sym[hij][m][l];
                    A_p[d2aboff[hij] + ij*gems_ab[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2aboff[hij] + lm*gems_ab[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }
            }
        }
        offset += trip_aab[h]*trip_aab[h];
    }
    // T2aaa
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aaa[h]; ijk++) {

            int i = bas_aaa_sym[h][ijk][0];
            int j = bas_aaa_sym[h][ijk][1];
            int k = bas_aaa_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aaa[h]; lmn++) {

                int l = bas_aaa_sym[h][lmn][0];
                int m = bas_aaa_sym[h][lmn][1];
                int n = bas_aaa_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aaa[h]+lmn];

                A_p[t2aaaoff[h] + ijk*trip_aaa[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( i != n && j != n && l != k && m != k) {

                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aaa_sym[h2][i][j][n];
                    int lmk = ibas_aaa_sym[h2][l][m][k];

                    int s = 1;
                    if ( n < j ) s = -s;
                    if ( n < i ) s = -s;
                    if ( k < l ) s = -s;
                    if ( k < m ) s = -s;

                    A_p[t1aaaoff[h2] + ijn*trip_aaa[h2] + lmk] -= s * dum; // - T1(ijn,lmk)
                }   

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2aaoff[hij] + ij*gems_aa[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2aaoff[hij] + lm*gems_aa[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }

            }
        }
        offset += trip_aaa[h]*trip_aaa[h];
    }
    // T2bbb
    for (int h = 0; h < nirrep_; h++) {

        for (int ijk = 0; ijk < trip_aaa[h]; ijk++) {

            int i = bas_aaa_sym[h][ijk][0];
            int j = bas_aaa_sym[h][ijk][1];
            int k = bas_aaa_sym[h][ijk][2];

            for (int lmn = 0; lmn < trip_aaa[h]; lmn++) {

                int l = bas_aaa_sym[h][lmn][0];
                int m = bas_aaa_sym[h][lmn][1];
                int n = bas_aaa_sym[h][lmn][2];

                double dum = u_p[offset + ijk*trip_aaa[h]+lmn];

                A_p[t2bbboff[h] + ijk*trip_aaa[h]+lmn] -= dum; // - T2(ijk,lmn)

                if ( i != n && j != n && l != k && m != k) {

                    int h2 = SymmetryPair(symmetry[i],SymmetryPair(symmetry[j],symmetry[n]));
                    int ijn = ibas_aaa_sym[h2][i][j][n];
                    int lmk = ibas_aaa_sym[h2][l][m][k];

                    int s = 1;
                    if ( n < j ) s = -s;
                    if ( n < i ) s = -s;
                    if ( k < l ) s = -s;
                    if ( k < m ) s = -s;

                    A_p[t1bbboff[h2] + ijn*trip_aaa[h2] + lmk] -= s * dum; // - T1(ijn,lmk)
                }   

                if ( k == n ) {
                    int hij = SymmetryPair(symmetry[i],symmetry[j]);
                    int ij = ibas_aa_sym[hij][i][j];
                    int lm = ibas_aa_sym[hij][l][m];
                    A_p[d2bboff[hij] + ij*gems_aa[hij] + lm] += dum;  // D2(ij,lm) dkn
                    A_p[q2bboff[hij] + lm*gems_aa[hij] + ij] += dum;  // Q2(lm,ij) dkn
                }

            }
        }
        offset += trip_aaa[h]*trip_aaa[h];
    }
}

}} // end namespaces
