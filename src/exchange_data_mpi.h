#ifndef EXCHANGE_DATA_H
#define EXCHANGE_DATA_H

#include "comm_data.h"

void init_mpi_requests(comm_data *cd
		       , int dim2
		       );

double exchange_dbl_mpi_bulk_sync(comm_data *cd
				  , double *data
				  , int dim2
				  );


double exchange_dbl_mpi_early_recv(comm_data *cd
				   , double *data
				   , int dim2
				   , int final
				   );

double exchange_dbl_mpi_async(comm_data *cd
			      , double *data
			      , int dim2
			      , int final
			      );

void exchange_dbl_mpi_send(comm_data *cd
			   , double *data
			   , int dim2
			   , int i
			   );

void exchange_dbl_mpi_post_recv(comm_data *cd
				, int dim2
				);

#endif

