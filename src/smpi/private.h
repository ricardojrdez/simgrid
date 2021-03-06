/* Copyright (c) 2007, 2009, 2010. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
  * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SMPI_PRIVATE_H
#define SMPI_PRIVATE_H

#include "xbt.h"
#include "xbt/xbt_os_time.h"
#include "simgrid/simix.h"
#include "smpi/smpi_interface.h"
#include "smpi/smpi.h"
#include "smpi/smpif.h"
#include "smpi/smpi_cocci.h"
#include "instr/instr_private.h"

struct s_smpi_process_data;
typedef struct s_smpi_process_data *smpi_process_data_t;

#define PERSISTENT     0x1
#define NON_PERSISTENT 0x2
#define SEND           0x4
#define RECV           0x8
#define RECV_DELETE    0x10
#define ISEND          0x20
#define SSEND          0x40
#define PREPARED       0x80
// this struct is here to handle the problem of non-contignous data
// for each such structure these function should be implemented (vector
// index hvector hindex struct)
typedef struct s_smpi_subtype{
  void (*serialize)(const void * input, void *output, size_t count, void* subtype);
  void (*unserialize)(const void * input, void *output, size_t count, void* subtype);
  void (*subtype_free)(MPI_Datatype* type);
} s_smpi_subtype_t;

typedef struct s_smpi_mpi_datatype{
  size_t size;
  /* this let us know if a serialization is required*/
  size_t has_subtype;
  MPI_Aint lb;
  MPI_Aint ub;
  int flags;
  /* this let us know how to serialize and unserialize*/
  void *substruct;
  int in_use;
} s_smpi_mpi_datatype_t;


#define COLL_TAG_REDUCE -112
#define COLL_TAG_SCATTER -223
#define COLL_TAG_SCATTERV -334
#define COLL_TAG_GATHER -445
#define COLL_TAG_ALLGATHER -556
#define COLL_TAG_ALLGATHERV -667
#define COLL_TAG_BARRIER -778
#define COLL_TAG_REDUCE_SCATTER -889
#define COLL_TAG_ALLTOALLV -1000
#define COLL_TAG_ALLTOALL -1112
#define COLL_TAG_GATHERV -2223
#define COLL_TAG_BCAST -3334
#define COLL_TAG_ALLREDUCE -4445
//*****************************************************************************************

typedef struct s_smpi_mpi_request {
  void *buf;
  /* in the case of non-contignous memory the user address shoud be keep
   * to unserialize the data inside the user memory*/
  void *old_buf;
  /* this let us know how tounserialize at the end of
   * the communication*/
  MPI_Datatype old_type;
  size_t size;
  int src;
  int dst;
  int tag;
  //to handle cases where we have an unknown sender
  //We can't override src, tag, and size, because the request may be reused later
  int real_src;
  int real_tag;
  int truncated;
  size_t real_size;
  MPI_Comm comm;
  smx_action_t action;
  unsigned flags;
  int detached;
  MPI_Request detached_sender;
  int refcount;
#ifdef HAVE_TRACING
  int send;
  int recv;
#endif
} s_smpi_mpi_request_t;

void smpi_process_init(int *argc, char ***argv);
void smpi_process_destroy(void);
void smpi_process_finalize(void);
int smpi_process_finalized(void);

smpi_process_data_t smpi_process_data(void);
smpi_process_data_t smpi_process_remote_data(int index);
void smpi_process_set_user_data(void *);
void* smpi_process_get_user_data(void);
int smpi_process_count(void);
smx_rdv_t smpi_process_mailbox(void);
smx_rdv_t smpi_process_remote_mailbox(int index);
smx_rdv_t smpi_process_mailbox_small(void);
smx_rdv_t smpi_process_remote_mailbox_small(int index);
xbt_os_timer_t smpi_process_timer(void);
void smpi_process_simulated_start(void);
double smpi_process_simulated_elapsed(void);

void print_request(const char *message, MPI_Request request);

void smpi_global_init(void);
void smpi_global_destroy(void);

size_t smpi_datatype_size(MPI_Datatype datatype);
MPI_Aint smpi_datatype_lb(MPI_Datatype datatype);
MPI_Aint smpi_datatype_ub(MPI_Datatype datatype);
int smpi_datatype_extent(MPI_Datatype datatype, MPI_Aint * lb,
                         MPI_Aint * extent);
MPI_Aint smpi_datatype_get_extent(MPI_Datatype datatype);
int smpi_datatype_copy(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                       void *recvbuf, int recvcount,
                       MPI_Datatype recvtype);
void smpi_datatype_use(MPI_Datatype type);
void smpi_datatype_unuse(MPI_Datatype type);

int smpi_datatype_contiguous(int count, MPI_Datatype old_type,
                       MPI_Datatype* new_type, MPI_Aint lb);
int smpi_datatype_vector(int count, int blocklen, int stride,
                      MPI_Datatype old_type, MPI_Datatype* new_type);

int smpi_datatype_hvector(int count, int blocklen, MPI_Aint stride,
                      MPI_Datatype old_type, MPI_Datatype* new_type);
int smpi_datatype_indexed(int count, int* blocklens, int* indices,
                     MPI_Datatype old_type, MPI_Datatype* new_type);
int smpi_datatype_hindexed(int count, int* blocklens, MPI_Aint* indices,
                     MPI_Datatype old_type, MPI_Datatype* new_type);
int smpi_datatype_struct(int count, int* blocklens, MPI_Aint* indices,
                    MPI_Datatype* old_types, MPI_Datatype* new_type);

void smpi_datatype_create(MPI_Datatype* new_type, int size,int lb, int ub, int has_subtype, void *struct_type, int flags);


void smpi_datatype_free(MPI_Datatype* type);
void smpi_datatype_commit(MPI_Datatype* datatype);

void smpi_empty_status(MPI_Status * status);
MPI_Op smpi_op_new(MPI_User_function * function, int commute);
int smpi_op_is_commute(MPI_Op op);
void smpi_op_destroy(MPI_Op op);
void smpi_op_apply(MPI_Op op, void *invec, void *inoutvec, int *len,
                   MPI_Datatype * datatype);

MPI_Group smpi_group_new(int size);
void smpi_group_destroy(MPI_Group group);
void smpi_group_set_mapping(MPI_Group group, int index, int rank);
int smpi_group_index(MPI_Group group, int rank);
int smpi_group_rank(MPI_Group group, int index);
int smpi_group_use(MPI_Group group);
int smpi_group_unuse(MPI_Group group);
int smpi_group_size(MPI_Group group);
int smpi_group_compare(MPI_Group group1, MPI_Group group2);

MPI_Comm smpi_comm_new(MPI_Group group);
void smpi_comm_destroy(MPI_Comm comm);
MPI_Group smpi_comm_group(MPI_Comm comm);
int smpi_comm_size(MPI_Comm comm);
void smpi_comm_get_name(MPI_Comm comm, char* name, int* len);
int smpi_comm_rank(MPI_Comm comm);
MPI_Comm smpi_comm_split(MPI_Comm comm, int color, int key);
void smpi_comm_use(MPI_Comm comm);
void smpi_comm_unuse(MPI_Comm comm);

MPI_Request smpi_mpi_send_init(void *buf, int count, MPI_Datatype datatype,
                               int dst, int tag, MPI_Comm comm);
MPI_Request smpi_mpi_recv_init(void *buf, int count, MPI_Datatype datatype,
                               int src, int tag, MPI_Comm comm);
MPI_Request smpi_mpi_ssend_init(void *buf, int count, MPI_Datatype datatype,
                               int dst, int tag, MPI_Comm comm);
void smpi_mpi_start(MPI_Request request);
void smpi_mpi_startall(int count, MPI_Request * requests);
void smpi_mpi_request_free(MPI_Request * request);
MPI_Request smpi_isend_init(void *buf, int count, MPI_Datatype datatype,
                            int dst, int tag, MPI_Comm comm);
MPI_Request smpi_mpi_isend(void *buf, int count, MPI_Datatype datatype,
                           int dst, int tag, MPI_Comm comm);
MPI_Request smpi_mpi_issend(void *buf, int count, MPI_Datatype datatype,
                           int dst, int tag, MPI_Comm comm);
MPI_Request smpi_irecv_init(void *buf, int count, MPI_Datatype datatype,
                            int src, int tag, MPI_Comm comm);
MPI_Request smpi_mpi_irecv(void *buf, int count, MPI_Datatype datatype,
                           int src, int tag, MPI_Comm comm);
void smpi_mpi_recv(void *buf, int count, MPI_Datatype datatype, int src,
                   int tag, MPI_Comm comm, MPI_Status * status);
void smpi_mpi_send(void *buf, int count, MPI_Datatype datatype, int dst,
                   int tag, MPI_Comm comm);
void smpi_mpi_ssend(void *buf, int count, MPI_Datatype datatype, int dst,
                   int tag, MPI_Comm comm);
void smpi_mpi_sendrecv(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                       int dst, int sendtag, void *recvbuf, int recvcount,
                       MPI_Datatype recvtype, int src, int recvtag,
                       MPI_Comm comm, MPI_Status * status);
int smpi_mpi_test(MPI_Request * request, MPI_Status * status);
int smpi_mpi_testany(int count, MPI_Request requests[], int *index,
                     MPI_Status * status);
int smpi_mpi_testall(int count, MPI_Request requests[],
                     MPI_Status status[]);
void smpi_mpi_probe(int source, int tag, MPI_Comm comm, MPI_Status* status);
void smpi_mpi_iprobe(int source, int tag, MPI_Comm comm, int* flag,
                    MPI_Status* status);
int smpi_mpi_get_count(MPI_Status * status, MPI_Datatype datatype);
void smpi_mpi_wait(MPI_Request * request, MPI_Status * status);
int smpi_mpi_waitany(int count, MPI_Request requests[],
                     MPI_Status * status);
int smpi_mpi_waitall(int count, MPI_Request requests[],
                      MPI_Status status[]);
int smpi_mpi_waitsome(int incount, MPI_Request requests[], int *indices,
                      MPI_Status status[]);
int smpi_mpi_testsome(int incount, MPI_Request requests[], int *indices,
                      MPI_Status status[]);
void smpi_mpi_bcast(void *buf, int count, MPI_Datatype datatype, int root,
                    MPI_Comm comm);
void smpi_mpi_barrier(MPI_Comm comm);
void smpi_mpi_gather(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                     void *recvbuf, int recvcount, MPI_Datatype recvtype,
                     int root, MPI_Comm comm);
void smpi_mpi_reduce_scatter(void *sendbuf, void *recvbuf, int *recvcounts,
                       MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
void smpi_mpi_gatherv(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                      void *recvbuf, int *recvcounts, int *displs,
                      MPI_Datatype recvtype, int root, MPI_Comm comm);
void smpi_mpi_allgather(void *sendbuf, int sendcount,
                        MPI_Datatype sendtype, void *recvbuf,
                        int recvcount, MPI_Datatype recvtype,
                        MPI_Comm comm);
void smpi_mpi_allgatherv(void *sendbuf, int sendcount,
                         MPI_Datatype sendtype, void *recvbuf,
                         int *recvcounts, int *displs,
                         MPI_Datatype recvtype, MPI_Comm comm);
void smpi_mpi_scatter(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                      void *recvbuf, int recvcount, MPI_Datatype recvtype,
                      int root, MPI_Comm comm);
void smpi_mpi_scatterv(void *sendbuf, int *sendcounts, int *displs,
                       MPI_Datatype sendtype, void *recvbuf, int recvcount,
                       MPI_Datatype recvtype, int root, MPI_Comm comm);
void smpi_mpi_reduce(void *sendbuf, void *recvbuf, int count,
                     MPI_Datatype datatype, MPI_Op op, int root,
                     MPI_Comm comm);
void smpi_mpi_allreduce(void *sendbuf, void *recvbuf, int count,
                        MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
void smpi_mpi_scan(void *sendbuf, void *recvbuf, int count,
                   MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
void smpi_mpi_exscan(void *sendbuf, void *recvbuf, int count,
                   MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);

void nary_tree_bcast(void *buf, int count, MPI_Datatype datatype, int root,
                     MPI_Comm comm, int arity);
void nary_tree_barrier(MPI_Comm comm, int arity);

int smpi_coll_tuned_alltoall_ompi2(void *sendbuf, int sendcount,
                                      MPI_Datatype sendtype, void *recvbuf,
                                      int recvcount, MPI_Datatype recvtype,
                                      MPI_Comm comm);
int smpi_coll_tuned_alltoall_bruck(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm);
int smpi_coll_tuned_alltoall_basic_linear(void *sendbuf, int sendcount,
                                          MPI_Datatype sendtype,
                                          void *recvbuf, int recvcount,
                                          MPI_Datatype recvtype,
                                          MPI_Comm comm);
int smpi_coll_basic_alltoallv(void *sendbuf, int *sendcounts,
                              int *senddisps, MPI_Datatype sendtype,
                              void *recvbuf, int *recvcounts,
                              int *recvdisps, MPI_Datatype recvtype,
                              MPI_Comm comm);

// utilities
void smpi_bench_destroy(void);
void smpi_bench_begin(void);
void smpi_bench_end(void);
void smpi_execute_flops(double flops);

// f77 wrappers
void mpi_init_(int*);
void mpi_finalize_(int*);
void mpi_abort_(int* comm, int* errorcode, int* ierr);
void mpi_comm_rank_(int* comm, int* rank, int* ierr);
void mpi_comm_size_(int* comm, int* size, int* ierr);
double mpi_wtime_(void);
double mpi_wtick_(void);
void mpi_initialized_(int* flag, int* ierr);

void mpi_comm_dup_(int* comm, int* newcomm, int* ierr);
void mpi_comm_create_(int* comm, int* group, int* newcomm, int* ierr);
void mpi_comm_free_(int* comm, int* ierr);
void mpi_comm_split_(int* comm, int* color, int* key, int* comm_out, int* ierr);
void mpi_group_incl_(int* group, int* n, int* key, int* group_out, int* ierr) ;
void mpi_comm_group_(int* comm, int* group_out,  int* ierr);
void mpi_send_init_(void *buf, int* count, int* datatype, int* dst, int* tag,
                     int* comm, int* request, int* ierr);
void mpi_isend_(void *buf, int* count, int* datatype, int* dst,
                 int* tag, int* comm, int* request, int* ierr);
void mpi_irsend_(void *buf, int* count, int* datatype, int* dst,
                 int* tag, int* comm, int* request, int* ierr);
void mpi_send_(void* buf, int* count, int* datatype, int* dst,
                int* tag, int* comm, int* ierr);
void mpi_rsend_(void* buf, int* count, int* datatype, int* dst,
                int* tag, int* comm, int* ierr);
void mpi_recv_init_(void *buf, int* count, int* datatype, int* src, int* tag,
                     int* comm, int* request, int* ierr);
void mpi_irecv_(void *buf, int* count, int* datatype, int* src, int* tag,
                 int* comm, int* request, int* ierr);
void mpi_recv_(void* buf, int* count, int* datatype, int* src,
                int* tag, int* comm, MPI_Status* status, int* ierr);
void mpi_start_(int* request, int* ierr);
void mpi_startall_(int* count, int* requests, int* ierr);
void mpi_wait_(int* request, MPI_Status* status, int* ierr);
void mpi_waitany_(int* count, int* requests, int* index, MPI_Status* status, int* ierr);
void mpi_waitall_(int* count, int* requests, MPI_Status* status, int* ierr);

void mpi_barrier_(int* comm, int* ierr);
void mpi_bcast_(void* buf, int* count, int* datatype, int* root, int* comm, int* ierr);
void mpi_reduce_(void* sendbuf, void* recvbuf, int* count,
                  int* datatype, int* op, int* root, int* comm, int* ierr);
void mpi_allreduce_(void* sendbuf, void* recvbuf, int* count, int* datatype,
                     int* op, int* comm, int* ierr);
void mpi_reduce_scatter_(void* sendbuf, void* recvbuf, int* recvcounts, int* datatype,
                     int* op, int* comm, int* ierr) ;
void mpi_scatter_(void* sendbuf, int* sendcount, int* sendtype,
                   void* recvbuf, int* recvcount, int* recvtype,
                   int* root, int* comm, int* ierr);
void mpi_scatterv_(void* sendbuf, int* sendcounts, int* displs, int* sendtype,
                   void* recvbuf, int* recvcount, int* recvtype,
                   int* root, int* comm, int* ierr);
void mpi_gather_(void* sendbuf, int* sendcount, int* sendtype,
                  void* recvbuf, int* recvcount, int* recvtype,
                  int* root, int* comm, int* ierr);
void mpi_gatherv_(void* sendbuf, int* sendcount, int* sendtype,
                  void* recvbuf, int* recvcounts, int* displs, int* recvtype,
                  int* root, int* comm, int* ierr);
void mpi_allgather_(void* sendbuf, int* sendcount, int* sendtype,
                     void* recvbuf, int* recvcount, int* recvtype,
                     int* comm, int* ierr);
void mpi_allgatherv_(void* sendbuf, int* sendcount, int* sendtype,
                     void* recvbuf, int* recvcount,int* displs, int* recvtype,
                     int* comm, int* ierr) ;
void mpi_type_size_(int* datatype, int *size, int* ierr);

void mpi_scan_(void* sendbuf, void* recvbuf, int* count, int* datatype,
                int* op, int* comm, int* ierr);
void mpi_alltoall_(void* sendbuf, int* sendcount, int* sendtype,
                    void* recvbuf, int* recvcount, int* recvtype, int* comm, int* ierr);
void mpi_alltoallv_(void* sendbuf, int* sendcounts, int* senddisps, int* sendtype,
                    void* recvbuf, int* recvcounts, int* recvdisps, int* recvtype, int* comm, int* ierr);
void mpi_get_processor_name_(char *name, int *resultlen, int* ierr);
void mpi_test_ (int * request, int *flag, MPI_Status * status, int* ierr);
void mpi_testall_ (int* count, int * requests,  int *flag, MPI_Status * statuses, int* ierr);
void mpi_get_count_(MPI_Status * status, int* datatype, int *count, int* ierr);
void mpi_type_extent_(int* datatype, MPI_Aint * extent, int* ierr);
void mpi_attr_get_(int* comm, int* keyval, void* attr_value, int* flag, int* ierr );
void mpi_type_commit_(int* datatype,  int* ierr);
void mpi_type_vector_(int* count, int* blocklen, int* stride, int* old_type, int* newtype,  int* ierr);
void mpi_type_create_vector_(int* count, int* blocklen, int* stride, int* old_type, int* newtype,  int* ierr);
void mpi_type_hvector_(int* count, int* blocklen, MPI_Aint* stride, int* old_type, int* newtype,  int* ierr);
void mpi_type_create_hvector_(int* count, int* blocklen, MPI_Aint* stride, int* old_type, int* newtype,  int* ierr);
void mpi_type_free_(int* datatype, int* ierr);
void mpi_type_lb_(int* datatype, MPI_Aint * extent, int* ierr);
void mpi_type_ub_(int* datatype, MPI_Aint * extent, int* ierr);
void mpi_win_fence_( int* assert,  int* win, int* ierr);
void mpi_win_free_( int* win, int* ierr);
void mpi_win_create_( int *base, MPI_Aint* size, int* disp_unit, int* info, int* comm, int *win, int* ierr);
void mpi_info_create_( int *info, int* ierr);
void mpi_info_set_( int *info, char *key, char *value, int* ierr);
void mpi_info_free_(int* info, int* ierr);
void mpi_get_( int *origin_addr, int* origin_count, int* origin_datatype, int* target_rank,
    MPI_Aint* target_disp, int* target_count, int* target_datatype, int* win, int* ierr);
void mpi_error_string_(int* errorcode, char* string, int* resultlen, int* ierr);
void mpi_sendrecv_(void* sendbuf, int* sendcount, int* sendtype, int* dst,
                int* sendtag, void *recvbuf, int* recvcount,
                int* recvtype, int* src, int* recvtag,
                int* comm, MPI_Status* status, int* ierr);

/********** Tracing **********/
/* from smpi_instr.c */
void TRACE_internal_smpi_set_category (const char *category);
const char *TRACE_internal_smpi_get_category (void);
void TRACE_smpi_collective_in(int rank, int root, const char *operation);
void TRACE_smpi_collective_out(int rank, int root, const char *operation);
void TRACE_smpi_computing_init(int rank);
void TRACE_smpi_computing_out(int rank);
void TRACE_smpi_computing_in(int rank);

#endif
