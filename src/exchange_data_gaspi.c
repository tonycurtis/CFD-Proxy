#ifdef USE_GASPI
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
#include <string.h>
#include <GASPI.h>

#include "exchange_data_gaspi.h"
#include "solver_data.h"
#include "comm_data.h"
#include "rangelist.h"
#include "threads.h"
#include "waitsome.h"
#include "queue.h"
#include "util.h"

#include "error_handling.h"

void init_gaspi_segments(comm_data *cd
			 , int dim2
			 )
{

  int i, j;  
  const int max_elem_sz = NGRAD * 3;
  const size_t szd = sizeof(double);
  ASSERT(dim2 == max_elem_sz);  

  gaspi_number_t snum = 0; 
  SUCCESS_OR_DIE(gaspi_segment_num(&snum));
  ASSERT(snum == 0);  

  gaspi_size_t rsz = 0, ssz = 0;
  for(i = 0; i < cd->ncommdomains; i++)
    {
      int k = cd->commpartner[i];
      ssz +=  cd->sendcount[k] * max_elem_sz * szd;
      rsz +=  cd->recvcount[k] * max_elem_sz * szd;
    }  
  
  for(j = 0; j < 2; j++)
    {      
      /* create gaspi send segments for gradients */ 
      SUCCESS_OR_DIE(gaspi_segment_create(j
			   , ssz
			   , GASPI_GROUP_ALL
			   , GASPI_BLOCK
			   , GASPI_ALLOC_DEFAULT
					  ));
      
      /* create gaspi recv segments for gradients */ 
      SUCCESS_OR_DIE(gaspi_segment_create(2+j
			   , rsz
			   , GASPI_GROUP_ALL
			   , GASPI_BLOCK
			   , GASPI_ALLOC_DEFAULT
					  ));
    }

}



void exchange_dbl_gaspi_write(comm_data *cd
			      , double *data
			      , int dim2
			      , int buffer_id
			      , int i)
{
  int *commpartner  = cd->commpartner;
  int *sendcount    = cd->sendcount;
  int **sendindex   = cd->sendindex;

  gaspi_queue_id_t queue_id = 0;
  gaspi_offset_t *remote_recv_offset    = cd->remote_recv_offset;
  gaspi_offset_t *local_send_offset     = cd->local_send_offset;
  gaspi_notification_id_t *notification = cd->notification;

  int j, count;
  size_t szd = sizeof(double);

  int k = commpartner[i];
  count = sendcount[k];
 
  if(count > 0)
    {
      gaspi_pointer_t ptr;
      SUCCESS_OR_DIE(gaspi_segment_ptr(buffer_id, &ptr));

      double *sbuf = (double *) (ptr + local_send_offset[k]);
      for(j = 0; j < count; j++)
	{
	  int n1 = dim2 * j;
	  int n2 = dim2 * sendindex[k][j];
	  memcpy(&sbuf[n1], &data[n2], dim2 * sizeof(double));
	}

      gaspi_size_t size = count * dim2 * szd;

      // issue write
      wait_for_queue_max_half (&queue_id);
      SUCCESS_OR_DIE ( gaspi_write_notify
		       ( buffer_id
			 , local_send_offset[k]
			 , k 
			 , 2+buffer_id
			 , remote_recv_offset[k]
			 , size
			 , notification[k]
			 , 1
			 , queue_id
			 , GASPI_BLOCK
			 ));

    }

}


static void exchange_dbl_gaspi_copy_out(int recvcount
					, int *recvindex
					, gaspi_offset_t local_recv_offset
					, double *data
					, int dim2
					, int buffer_id)
{

  int j;

  /* copy the data from the recvbuffer into out data field */
  if(recvcount > 0)
    {
      gaspi_pointer_t ptr;
      SUCCESS_OR_DIE(gaspi_segment_ptr(2+buffer_id, &ptr));
      
      double *rbuf = (double *) (ptr + local_recv_offset);
      for(j = 0; j < recvcount; j++)
	{
	  int n1 = dim2 * j;
	  int n2 = dim2 * recvindex[j];
	  memcpy(&data[n2], &rbuf[n1], dim2 * sizeof(double));
	}
    }

}


void exchange_dbl_gaspi_bulk_sync(comm_data *cd
				  , double *data
				  , int dim2
				  )
{

  int ncommdomains  = cd->ncommdomains;
  int *commpartner  = cd->commpartner;
  int *recvcount    = cd->recvcount;
  int **recvindex   = cd->recvindex;

  gaspi_offset_t *remote_recv_offset    = cd->remote_recv_offset;
  gaspi_offset_t *local_recv_offset     = cd->local_recv_offset;

  int i;

  ASSERT(dim2 > 0);
  ASSERT(ncommdomains != 0);

  ASSERT(recvcount != NULL);
  ASSERT(recvindex != NULL);

  ASSERT(remote_recv_offset != NULL);
  ASSERT(local_recv_offset != NULL);

  gaspi_number_t snum = 0; 
  SUCCESS_OR_DIE(gaspi_segment_num(&snum));
  ASSERT(snum == 4);

  /* wait for completed computation before send */
  if (this_is_the_last_thread())
    {
      int buffer_id;
      /* send buffer_id */
      buffer_id = cd->send_stage % 2;
      for(i = 0; i < ncommdomains; i++)
	{
	  exchange_dbl_gaspi_write(cd, data, dim2, buffer_id, i);
	}

      /* recv buffer_id */
      buffer_id = cd->recv_stage % 2;

      /* wait for notify */
      for(i = 0; i < ncommdomains; i++)
	{ 
	  int k = commpartner[i];
	  if(recvcount[k] > 0)
	    {
	      // wait for data notification
	      gaspi_notification_id_t id, test = i;
	      gaspi_notification_t value;
	      SUCCESS_OR_DIE(gaspi_notify_waitsome (2+buffer_id
						    , test
						    , 1
						    , &id
						    , GASPI_BLOCK
						    ));
	      ASSERT (id == test);	  
	      SUCCESS_OR_DIE (gaspi_notify_reset (2+buffer_id
						  , id
						  , &value
						  ));
	      ASSERT (value == 1);
	    }
	}
      /* copy the data from the recvbuffer into out data field */
      for(i = 0; i < ncommdomains; i++)
	{
	  int k = commpartner[i];
	  exchange_dbl_gaspi_copy_out(recvcount[k]
				      , recvindex[k]
				      , local_recv_offset[k]
				      , data
				      , dim2
				      , buffer_id
				      );
	}
  
      // inc stage counter
      cd->send_stage++;
      cd->recv_stage++;

    }

}


void exchange_dbl_gaspi_async(comm_data *cd
			      , double *data
			      , int dim2
			      )
{

  int ncommdomains  = cd->ncommdomains;
  int *commpartner  = cd->commpartner;
  int *recvcount    = cd->recvcount;
  int **recvindex   = cd->recvindex;

  gaspi_offset_t *local_recv_offset     = cd->local_recv_offset;

  ASSERT(dim2 > 0);
  ASSERT(ncommdomains != 0);

  ASSERT(recvcount != NULL);
  ASSERT(recvindex != NULL);

  ASSERT(local_recv_offset != NULL);

  gaspi_number_t snum = 0; 
  SUCCESS_OR_DIE(gaspi_segment_num(&snum));
  ASSERT(snum == 4);


  if (this_is_the_first_thread())
    {
      int i;
      /* buffer_id */
      int buffer_id = cd->recv_stage % 2;
      for (i = 0; i < ncommdomains; ++i)
	{
	  gaspi_notification_id_t id;
	  gaspi_notification_t value = 0;	  	  
	  /* test .. */
	  SUCCESS_OR_DIE(gaspi_notify_waitsome (2+buffer_id
						, 0
						, ncommdomains
						, &id
						, GASPI_BLOCK
						));
	  /* .. and reset */
	  SUCCESS_OR_DIE (gaspi_notify_reset (2+buffer_id
					      , id
					      , &value
					      ));
	  ASSERT(value != 0);
	  int k = commpartner[id];	  
	  ASSERT(recvcount[k] > 0);	  
	  /* copy the data from the recvbuffer into out data field */
	  exchange_dbl_gaspi_copy_out( recvcount[k]
				      , recvindex[k]
				      , local_recv_offset[k]
				      , data
				      , dim2
				      , buffer_id
				      );
	}
    }
  
  // inc stage counter
  if (this_is_the_last_thread())
    {
      cd->send_stage++;
      cd->recv_stage++;
    }

}

#endif











