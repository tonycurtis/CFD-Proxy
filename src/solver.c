/*
 * This file is part of a small exa2ct benchmark kernel
 * The kernel aims at a dataflow implementation for 
 * hybrid solvers which make use of unstructured meshes.
 *
 * Contact point for exa2ct: 
 *                 https://projects.imec.be/exa2ct
 *
 * Contact point for this kernel: 
 *                 christian.simmendinger@t-systems.com
 */


#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#ifdef USE_GASPI
#include <GASPI.h>
#endif
#include "solver.h"
#include "util.h"

#include "gradients.h"
#include "rangelist.h"
#include "exchange_data_mpi.h"
#include "exchange_data_mpidma.h"
#ifdef USE_GASPI
#include "exchange_data_gaspi.h"
#endif

#define N_MEDIAN 100
#define N_SOLVER 10

void test_solver(comm_data *cd, solver_data *sd)
{
  int k;
  double time, median[N_SOLVER][N_MEDIAN];

  for (k = 0; k < N_MEDIAN; ++k)
    { 
      /* comm free */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_comm_free(sd);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[0][k] = time;

      /* MPI bulk sync */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_mpi_bulk_sync(cd, sd);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[1][k] = time;


      /* MPI bulk sync, early recv */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
      exchange_dbl_mpi_post_recv(cd, NGRAD * 3);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    int final = (i == sd->niter-1) ? 1 : 0;
	    compute_gradients_gg_mpi_early_recv(cd, sd, final);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[2][k] = time;

      /* MPI async */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
      exchange_dbl_mpi_post_recv(cd, NGRAD * 3);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    int final = (i == sd->niter-1) ? 1 : 0;
	    compute_gradients_gg_mpi_async(cd, sd, final);
	  }  
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[3][k] = time;

#ifdef USE_GASPI
      /* GASPI bulk sync */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_gaspi_bulk_sync(cd, sd);  
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[4][k] = time;

      /* GASPI async */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_gaspi_async(cd, sd);
	  }  
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[5][k] = time;

#endif

      /* MPI put/fence bulk sync */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_mpifence_bulk_sync(cd, sd);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[6][k] = time;

      /* MPI put/fence async */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
      mpidma_async_win_fence(MPI_MODE_NOPRECEDE);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_mpifence_async(cd, sd);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[7][k] = time;

      /* MPI PSCW bulk sync */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    compute_gradients_gg_mpipscw_bulk_sync(cd, sd);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[8][k] = time;

      /* MPI PSCW async */
      time = -now();
      MPI_Barrier(MPI_COMM_WORLD);
      mpidma_async_post_start();
#pragma omp parallel default (none) shared(cd, sd, stdout)
      {
	int i;
	for (i = 0; i < sd->niter; ++i)
	  {
	    int final = (i == sd->niter-1) ? 1 : 0;
	    compute_gradients_gg_mpipscw_async(cd, sd, final);
	  }
      }
      MPI_Barrier(MPI_COMM_WORLD);
      time += now();
      median[9][k] = time;

    }

  if (cd->iProc == 0)
    {
      for (k = 0; k < N_SOLVER; ++k)
	{ 
	  sort_median(&median[k][0], &median[k][N_MEDIAN-1]);
	}

      printf("                             comm_free: %10.6f\n",median[0][N_MEDIAN/2]);
      printf("            exchange_dbl_mpi_bulk_sync: %10.6f\n",median[1][N_MEDIAN/2]);
      printf("           exchange_dbl_mpi_early_recv: %10.6f\n",median[2][N_MEDIAN/2]);

#ifdef USE_MPI_MULTI_THREADED
      printf("          exchange_dbl_mpi_async_multi: %10.6f\n",median[3][N_MEDIAN/2]);
#else
      printf("     exchange_dbl_mpi_async_serialized: %10.6f\n",median[3][N_MEDIAN/2]);
#endif

#ifdef USE_GASPI
      printf("          exchange_dbl_gaspi_bulk_sync: %10.6f\n",median[4][N_MEDIAN/2]);
      printf("              exchange_dbl_gaspi_async: %10.6f\n",median[5][N_MEDIAN/2]);
#endif

      printf("       exchange_dbl_mpifence_bulk_sync: %10.6f\n",median[6][N_MEDIAN/2]);

#ifdef USE_MPI_MULTI_THREADED
      printf("     exchange_dbl_mpifence_async_multi: %10.6f\n",median[7][N_MEDIAN/2]);
#else
      printf("exchange_dbl_mpifence_async_serialized: %10.6f\n",median[7][N_MEDIAN/2]);
#endif

      printf("        exchange_dbl_mpipscw_bulk_sync: %10.6f\n",median[8][N_MEDIAN/2]);

#ifdef USE_MPI_MULTI_THREADED
      printf("      exchange_dbl_mpipscw_async_multi: %10.6f\n",median[9][N_MEDIAN/2]);
#else
      printf(" exchange_dbl_mpipscw_async_serialized: %10.6f\n",median[9][N_MEDIAN/2]);
#endif

    }
}
