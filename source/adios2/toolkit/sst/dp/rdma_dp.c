#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adios2/common/ADIOSConfig.h"
#include <atl.h>
#include <evpath.h>

#include <SSTConfig.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#ifdef SST_HAVE_FI_GNI
#include <rdma/fi_ext_gni.h>
#ifdef SST_HAVE_CRAY_DRC
#include <rdmacred.h>
#endif /* SST_HAVE_CRAY_DRC */
#endif /* SST_HAVE_FI_GNI */

#include "sst_data.h"

#include "adios2/toolkit/profiling/taustubs/taustubs.h"
#include "dp_interface.h"

#define DP_AV_DEF_SIZE 512
#define REQ_LIST_GRAN 8
#define DP_DATA_RECV_SIZE 64

pthread_mutex_t fabric_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t wsr_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ts_mutex = PTHREAD_MUTEX_INITIALIZER;

struct fabric_state
{
    struct fi_context *ctx;
    // struct fi_context *lctx;
    struct fi_info *info;
    // struct fi_info *linfo;
    int local_mr_req;
    int rx_cq_data;
    size_t addr_len;
    size_t msg_prefix_size;
    // size_t lmsg_prefix_size;
    struct fid_fabric *fabric;
    struct fid_domain *domain;
    struct fid_ep *signal;
    struct fid_cq *cq_signal;
    struct fid_av *av;
    pthread_t listener;
#ifdef SST_HAVE_CRAY_DRC
    drc_info_handle_t drc_info;
    uint32_t credential;
    struct fi_gni_auth_key *auth_key;
#endif /* SST_HAVE_CRAY_DRC */
};

// static int PreloadPosted = 0;

/*
 *  Some conventions:
 *    `RS` indicates a reader-side item.
 *    `WS` indicates a writer-side item.
 *    `WSR` indicates a writer-side per-reader item.
 *
 *   We keep different "stream" structures for the reader side and for the
 *   writer side.  On the writer side, there's actually a "stream"
 *   per-connected-reader (a WSR_Stream), with the idea that some (many?)
 *   RDMA transports will require connections/pairing, so we'd need to track
 *   resources per reader.
 *
 *   Generally the 'contact information' we exchange at init time includes
 *   the address of the local 'stream' data structure.  This address isn't
 *   particularly useful to the far side, but it can be returned with
 *   requests to indicate what resource is targeted.  For example, when a
 *   remote memory read request arrives at the writer from the reader, it
 *   includes the WSR_Stream value that is the address of the writer-side
 *   per-reader data structure.  Upon message arrival, we just cast that
 *   value back into a pointer.
 *
 *   By design, neither the data plane nor the control plane reference the
 *   other's symbols directly.  The interface between the control plane and
 *   the data plane is represented by the types and structures defined in
 *   dp_interface.h and is a set of function pointers and FFS-style
 *   descriptions of the data structures to be communicated at init time.
 *   This allows for the future possibility of loading planes at run-time, etc.
 *
 *   This "Rdma" data plane uses control plane functionality to implement
 *   the ReadRemoteMemory functionality.  That is, it both the request to
 *   read memory and the response which carries the data are actually
 *   accomplished using the connections and message delivery facilities of
 *   the control plane, made available here via CP_Services.  A real data
 *   plane would replace one or both of these with RDMA functionality.
 */

static void init_fabric(struct fabric_state *fabric, struct _SstParams *Params)
{
    struct fi_info *hints, *info, *originfo, *useinfo;
    struct fi_av_attr av_attr = {0};
    struct fi_cq_attr cq_attr = {0};
    char *ifname;

    hints = fi_allocinfo();
    hints->caps = FI_MSG | FI_SEND | FI_RECV | FI_REMOTE_READ |
                  FI_REMOTE_WRITE | FI_RMA | FI_READ | FI_WRITE;
    hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_CONTEXT2 | FI_MSG_PREFIX |
                  FI_ASYNC_IOV | FI_RX_CQ_DATA;
    hints->domain_attr->mr_mode = FI_MR_BASIC;
    hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
    hints->domain_attr->data_progress = FI_PROGRESS_AUTO;
    hints->ep_attr->type = FI_EP_RDM;

    if (Params->DataInterface)
    {
        ifname = Params->DataInterface;
    }
    else
    {
        ifname = getenv("FABRIC_IFACE");
    }

    fabric->info = NULL;

    fi_getinfo(FI_VERSION(1, 5), NULL, NULL, 0, hints, &info);
    if (!info)
    {
        return;
    }
    fi_freeinfo(hints);

    originfo = info;
    useinfo = NULL;
    while (info)
    {
        char *prov_name = info->fabric_attr->prov_name;
        char *domain_name = info->domain_attr->name;

        if (ifname && strcmp(ifname, domain_name) == 0)
        {
            useinfo = info;
            break;
        }
        if ((((strcmp(prov_name, "verbs") == 0) && info->src_addr) ||
             (strcmp(prov_name, "gni") == 0) ||
             (strcmp(prov_name, "psm2") == 0)) &&
            (!useinfo || !ifname ||
             (strcmp(useinfo->domain_attr->name, ifname) != 0)))
        {
            useinfo = info;
        }
        else if (((strstr(prov_name, "verbs") && info->src_addr) ||
                  strstr(prov_name, "gni") || strstr(prov_name, "psm2")) &&
                 !useinfo)
        {
            useinfo = info;
        }
        info = info->next;
    }

    info = useinfo;

    if (!info)
    {
        return;
    }

    if (info->mode & FI_CONTEXT2)
    {
        fabric->ctx = calloc(2, sizeof(*fabric->ctx));
    }
    else if (info->mode & FI_CONTEXT)
    {
        fabric->ctx = calloc(1, sizeof(*fabric->ctx));
    }
    else
    {
        fabric->ctx = NULL;
    }

    if (info->mode & FI_LOCAL_MR)
    {
        fabric->local_mr_req = 1;
    }
    else
    {
        fabric->local_mr_req = 0;
    }

    if (info->mode & FI_MSG_PREFIX)
    {
        fabric->msg_prefix_size = info->ep_attr->msg_prefix_size;
    }
    else
    {
        fabric->msg_prefix_size = 0;
    }

    if (info->mode & FI_RX_CQ_DATA)
    {
        fabric->rx_cq_data = 1;
    }
    else
    {
        fabric->rx_cq_data = 0;
    }

    fabric->addr_len = info->src_addrlen;

    info->domain_attr->mr_mode = FI_MR_BASIC;
#ifdef SST_HAVE_CRAY_DRC
    if (strstr(info->fabric_attr->prov_name, "gni") && fabric->auth_key)
    {
        info->domain_attr->auth_key = (uint8_t *)fabric->auth_key;
        info->domain_attr->auth_key_size = sizeof(struct fi_gni_raw_auth_key);
    }
#endif /* SST_HAVE_CRAY_DRC */
    fabric->info = fi_dupinfo(info);

    fi_fabric(info->fabric_attr, &fabric->fabric, fabric->ctx);
    fi_domain(fabric->fabric, info, &fabric->domain, fabric->ctx);
    info->ep_attr->type = FI_EP_RDM;
    fi_endpoint(fabric->domain, info, &fabric->signal, fabric->ctx);

    av_attr.type = FI_AV_MAP;
    av_attr.count = DP_AV_DEF_SIZE;
    av_attr.ep_per_node = 0;
    fi_av_open(fabric->domain, &av_attr, &fabric->av, fabric->ctx);
    fi_ep_bind(fabric->signal, &fabric->av->fid, 0);

    cq_attr.size = 0;
    cq_attr.format = FI_CQ_FORMAT_DATA;
    cq_attr.wait_obj = FI_WAIT_UNSPEC;
    cq_attr.wait_cond = FI_CQ_COND_NONE;
    fi_cq_open(fabric->domain, &cq_attr, &fabric->cq_signal, fabric->ctx);
    fi_ep_bind(fabric->signal, &fabric->cq_signal->fid, FI_TRANSMIT | FI_RECV);

    fi_enable(fabric->signal);

    fi_freeinfo(originfo);
}

static void fini_fabric(struct fabric_state *fabric)
{

    int status;

    do
    {
        status = fi_close((struct fid *)fabric->cq_signal);
    } while (status == FI_EBUSY);

    fi_close((struct fid *)fabric->domain);
    fi_close((struct fid *)fabric->fabric);

    if (status)
    {
        // TODO: error handling
    }

    fi_freeinfo(fabric->info);

    if (fabric->ctx)
    {
        free(fabric->ctx);
    }

#ifdef SST_HAVE_CRAY_DRC
    if (Fabric->auth_key)
    {
        free(Fabric->auth_key);
    }
#endif /* SST_HAVE_CRAY_DRC */
}

typedef struct fabric_state *FabricState;

typedef struct _RdmaCompletionHandle
{
    struct fid_mr *LocalMR;
    void *CPStream;
    void *Buffer;
    size_t Length;
    int Rank;
    int Pending;
    double StartWTime;
    void *PreloadBuffer;
} * RdmaCompletionHandle;

typedef struct _RdmaBufferHandle
{
    uint8_t *Block;
    uint64_t Key;
} * RdmaBufferHandle;

typedef struct _RdmaBuffer
{
    struct _RdmaBufferHandle Handle;
    uint64_t BufferLen;
    uint64_t Offset;
} * RdmaBuffer;

typedef struct _RdmaReqLogEntry
{
    size_t Offset;
    size_t Length;
} * RdmaReqLogEntry;

typedef struct _RdmaRankReqLog
{
    RdmaBuffer ReqLog;
    int Entries;
    union
    {
        int MaxEntries; // Reader Side
        int Rank;       // Writer Side
    };
    union
    {
        void *Buffer;               // Reader side
        uint64_t PreloadBufferSize; // Writer side
    };
    long BufferSize;
    union
    {
        struct fid_mr *preqbmr;       // Reader side
        struct _RdmaRankReqLog *next; // Writer side
    };
    RdmaCompletionHandle *PreloadHandles;
} * RdmaRankReqLog;

typedef struct _RdmaStepLogEntry
{
    long Timestep;
    RdmaRankReqLog RankLog;
    struct _RdmaStepLogEntry *Next;
    int Entries;
    long BufferSize;
    int WRanks;
} * RdmaStepLogEntry;

typedef struct _Rdma_RS_Stream
{
    CManager cm;
    void *CP_Stream;
    int Rank;
    FabricState Fabric;

    long PreloadStep;
    int PreloadPosted;
    RdmaStepLogEntry StepLog;
    RdmaStepLogEntry PreloadStepLog;
    struct _RdmaBuffer PreloadBuffer;
    struct fid_mr *pbmr;
    uint64_t *RecvCounter;

    struct _SstParams *Params;
    struct _RdmaReaderContactInfo *ContactInfo;

    /* writer info */
    int WriterCohortSize;
    CP_PeerCohort PeerCohort;
    struct _RdmaWriterContactInfo *WriterContactInfo;

    fi_addr_t *WriterAddr;
    struct _RdmaBufferHandle *WriterRoll;

    int PendingReads;
    long EarlyReads;
    long TotalReads;

    void *RecvDataBuffer;
    struct fid_mr *rbmr;
    void *rbdesc;
} * Rdma_RS_Stream;

typedef struct _TimestepEntry
{
    long Timestep;
    struct _SstData *Data;
    struct _RdmaBufferHandle *DP_TimestepInfo;
    struct _TimestepEntry *Prev, *Next;
    struct fid_mr *mr;
    void *Desc;
    uint64_t Key;
    uint64_t OutstandingWrites;
    int BufferSlot;
} * TimestepList;

typedef struct _Rdma_WSR_Stream
{
    struct _Rdma_WS_Stream *WS_Stream;
    CP_PeerCohort PeerCohort;
    int ReaderCohortSize;
    struct _RdmaWriterContactInfo *WriterContactInfo;
    struct _RdmaBuffer *ReaderRoll;
    struct fid_mr *rrmr;
    fi_addr_t *ReaderAddr;
    int SelectLocked;
    int Preload;
    int SelectionPulled;
    RdmaRankReqLog PreloadReq;
    TimestepList LastReleased;
    int PreloadUsed[2];
} * Rdma_WSR_Stream;

typedef struct _Rdma_WS_Stream
{
    CManager cm;
    void *CP_Stream;
    int Rank;
    FabricState Fabric;
    int DefLocked;
    TimestepList Timesteps;
    int ReaderCount;
    Rdma_WSR_Stream *Readers;
} * Rdma_WS_Stream;

typedef struct _RdmaReaderContactInfo
{
    void *RS_Stream;
    size_t Length;
    void *Address;
} * RdmaReaderContactInfo;

typedef struct _RdmaWriterContactInfo
{
    void *WS_Stream;
    size_t Length;
    void *Address;
    struct _RdmaBufferHandle ReaderRollHandle;
} * RdmaWriterContactInfo;

static TimestepList GetStep(Rdma_WS_Stream Stream, long Timestep)
{
    TimestepList Step;

    //fprintf(stderr, "rank %d: %s\n", Stream->Rank, __func__);

    pthread_mutex_lock(&ts_mutex);
    //fprintf(stderr, "rank %d: got lock\n", Stream->Rank);
    Step = Stream->Timesteps;
    while (Step && Step->Timestep != Timestep)
    {
        //fprintf(stderr, "rank %d: Step = %p, Step->Next = %p\n", Stream->Rank,
        //        Step, Step->Next);
        Step = Step->Prev;
    }
    pthread_mutex_unlock(&ts_mutex);

    //fprintf(stderr, "rank %d: leaving %s\n", Stream->Rank, __func__);

    return (Step);
}

static DP_RS_Stream RdmaInitReader(CP_Services Svcs, void *CP_Stream,
                                   void **ReaderContactInfoPtr,
                                   struct _SstParams *Params,
                                   attr_list WriterContact)
{
    Rdma_RS_Stream Stream = malloc(sizeof(struct _Rdma_RS_Stream));
    CManager cm = Svcs->getCManager(CP_Stream);
    MPI_Comm comm = Svcs->getMPIComm(CP_Stream);
    RdmaReaderContactInfo ContactInfo =
        malloc(sizeof(struct _RdmaReaderContactInfo));
    FabricState Fabric;
    int rc;

    memset(Stream, 0, sizeof(*Stream));
    Stream->Fabric = calloc(1, sizeof(*Fabric));

    Fabric = Stream->Fabric;
    /*
     * save the CP_stream value of later use
     */
    Stream->CP_Stream = CP_Stream;

    MPI_Comm_rank(comm, &Stream->Rank);
    *ReaderContactInfoPtr = NULL;

    ContactInfo->RS_Stream = Stream;

    if (Params)
    {
        Stream->Params = malloc(sizeof(*Stream->Params));
        memcpy(Stream->Params, Params, sizeof(*Params));
    }

#ifdef SST_HAVE_CRAY_DRC
    long attr_cred;
    if (!get_int_attr(WriterContact, attr_atom_from_string("RDMA_DRC_CRED"),
                      &attr_cred))
    {
        Svcs->verbose(CP_Stream, "Didn't find DRC credential\n");
        return NULL;
    }
    Fabric->credential = attr_cred;

    rc = drc_access(Fabric->credential, 0, &Fabric->drc_info);
    if (rc != DRC_SUCCESS)
    {
        Svcs->verbose(CP_Stream,
                      "Could not access DRC credential. Failed with %d.\n", rc);
        return NULL;
    }

    Fabric->auth_key = malloc(sizeof(*Fabric->auth_key));
    Fabric->auth_key->type = GNIX_AKT_RAW;
    Fabric->auth_key->raw.protection_key =
        drc_get_first_cookie(Fabric->drc_info);
    Svcs->verbose(CP_Stream, "Using protection key %08x.\n",
                  Fabric->auth_key->raw.protection_key);

#endif /* SST_HAVE_CRAY_DRC */

    init_fabric(Stream->Fabric, Stream->Params);
    if (!Fabric->info)
    {
        Svcs->verbose(CP_Stream, "Could not find a valid transport fabric.\n");
        return NULL;
    }

    Svcs->verbose(CP_Stream, "Fabric Parameters:\n%s\n",
                  fi_tostr(Fabric->info, FI_TYPE_INFO));

    ContactInfo->Length = Fabric->info->src_addrlen;
    ContactInfo->Address = malloc(ContactInfo->Length);
    fi_getname((fid_t)Fabric->signal, ContactInfo->Address,
               &ContactInfo->Length);

    Stream->PreloadStep = -1;
    Stream->ContactInfo = ContactInfo;
    Stream->PreloadPosted = 0;
    Stream->PendingReads = 0;
    Stream->RecvCounter = NULL;
    Stream->EarlyReads = 0;
    Stream->TotalReads = 0;

    *ReaderContactInfoPtr = ContactInfo;

    return Stream;
}

static void RdmaReadPatternLocked(CP_Services Svcs, DP_WSR_Stream WSRStream_v,
                                  long EffectiveTimestep)
{
    Rdma_WSR_Stream WSR_Stream = (Rdma_WSR_Stream)WSRStream_v;
    Rdma_WS_Stream WS_Stream = WSR_Stream->WS_Stream;

    Svcs->verbose(WS_Stream->CP_Stream, "read pattern is locked.\n");

    WSR_Stream->SelectLocked = EffectiveTimestep;
    WSR_Stream->Preload = 1;
}

static void RdmaWritePatternLocked(CP_Services Svcs, DP_RS_Stream Stream_v,
                                   long EffectiveTimestep)
{
    Rdma_RS_Stream Stream = (Rdma_RS_Stream)Stream_v;

    Stream->PreloadStep = EffectiveTimestep;
    Svcs->verbose(Stream->CP_Stream, "write pattern is locked.\n");
}

static DP_WS_Stream RdmaInitWriter(CP_Services Svcs, void *CP_Stream,
                                   struct _SstParams *Params, attr_list DPAttrs)
{
    Rdma_WS_Stream Stream = malloc(sizeof(struct _Rdma_WS_Stream));
    CManager cm = Svcs->getCManager(CP_Stream);
    MPI_Comm comm = Svcs->getMPIComm(CP_Stream);
    FabricState Fabric;
    int rc;

    memset(Stream, 0, sizeof(struct _Rdma_WS_Stream));

    MPI_Comm_rank(comm, &Stream->Rank);

    Stream->Fabric = calloc(1, sizeof(struct fabric_state));
    Fabric = Stream->Fabric;
#ifdef SST_HAVE_CRAY_DRC
    if (Stream->Rank == 0)
    {
        rc = drc_acquire(&Fabric->credential, DRC_FLAGS_FLEX_CREDENTIAL);
        if (rc != DRC_SUCCESS)
        {
            Svcs->verbose(CP_Stream,
                          "Could not acquire DRC credential. Failed with %d.\n",
                          rc);
            goto err_out;
        }
        else
        {
            Svcs->verbose(CP_Stream, "DRC acquired credential id %d.\n",
                          Fabric->credential);
        }
    }

    MPI_Bcast(&Fabric->credential, sizeof(Fabric->credential), MPI_BYTE, 0,
              comm);
    rc = drc_access(Fabric->credential, 0, &Fabric->drc_info);
    if (rc != DRC_SUCCESS)
    {
        Svcs->verbose(CP_Stream,
                      "Could not access DRC credential. Failed with %d.\n", rc);
        goto err_out;
    }

    Fabric->auth_key = malloc(sizeof(*Fabric->auth_key));
    Fabric->auth_key->type = GNIX_AKT_RAW;
    Fabric->auth_key->raw.protection_key =
        drc_get_first_cookie(Fabric->drc_info);
    Svcs->verbose(CP_Stream, "Using protection key %08x.\n",
                  Fabric->auth_key->raw.protection_key);
    long attr_cred = Fabric->credential;
    set_long_attr(DPAttrs, attr_atom_from_string("RDMA_DRC_CRED"), attr_cred);

#endif /* SST_HAVE_CRAY_DRC */

    init_fabric(Stream->Fabric, Params);
    Fabric = Stream->Fabric;
    if (!Fabric->info)
    {
        Svcs->verbose(CP_Stream, "Could not find a valid transport fabric.\n");
        goto err_out;
    }

    Svcs->verbose(CP_Stream, "Fabric Parameters:\n%s\n",
                  fi_tostr(Fabric->info, FI_TYPE_INFO));

    /*
     * save the CP_stream value of later use
     */
    Stream->CP_Stream = CP_Stream;

    Stream->DefLocked = -1;

    return (void *)Stream;

err_out:
    if (Stream)
    {
        if (Stream->Fabric)
        {
            free(Stream->Fabric);
        }
        free(Stream);
    }
    return (NULL);
}

static DP_WSR_Stream RdmaInitWriterPerReader(CP_Services Svcs,
                                             DP_WS_Stream WS_Stream_v,
                                             int readerCohortSize,
                                             CP_PeerCohort PeerCohort,
                                             void **providedReaderInfo_v,
                                             void **WriterContactInfoPtr)
{
    Rdma_WS_Stream WS_Stream = (Rdma_WS_Stream)WS_Stream_v;
    Rdma_WSR_Stream WSR_Stream = malloc(sizeof(*WSR_Stream));
    FabricState Fabric = WS_Stream->Fabric;
    RdmaWriterContactInfo ContactInfo;
    RdmaReaderContactInfo *providedReaderInfo =
        (RdmaReaderContactInfo *)providedReaderInfo_v;
    MPI_Comm comm = Svcs->getMPIComm(WS_Stream->CP_Stream);
    RdmaBufferHandle ReaderRollHandle;
    int i;

    WSR_Stream->WS_Stream = WS_Stream; /* pointer to writer struct */
    WSR_Stream->PeerCohort = PeerCohort;

    WSR_Stream->ReaderCohortSize = readerCohortSize;

    WSR_Stream->ReaderAddr =
        calloc(readerCohortSize, sizeof(*WSR_Stream->ReaderAddr));

    for (i = 0; i < readerCohortSize; i++)
    {
        fi_av_insert(Fabric->av, providedReaderInfo[i]->Address, 1,
                     &WSR_Stream->ReaderAddr[i], 0, NULL);
        Svcs->verbose(WS_Stream->CP_Stream,
                      "Received contact info for RS_Stream %p, WSR Rank %d\n",
                      providedReaderInfo[i]->RS_Stream, i);
    }

    /*
     * add this writer-side reader-specific stream to the parent writer stream
     * structure
     */
    pthread_mutex_lock(&wsr_mutex);
    WS_Stream->Readers = realloc(
        WS_Stream->Readers, sizeof(*WSR_Stream) * (WS_Stream->ReaderCount + 1));
    WS_Stream->Readers[WS_Stream->ReaderCount] = WSR_Stream;
    WS_Stream->ReaderCount++;
    pthread_mutex_unlock(&wsr_mutex);

    ContactInfo = calloc(1, sizeof(struct _RdmaWriterContactInfo));
    memset(ContactInfo, 0, sizeof(struct _RdmaWriterContactInfo));
    ContactInfo->WS_Stream = WSR_Stream;

    ContactInfo->Length = Fabric->info->src_addrlen;
    ContactInfo->Address = malloc(ContactInfo->Length);
    fi_getname((fid_t)Fabric->signal, ContactInfo->Address,
               &ContactInfo->Length);

    ReaderRollHandle = &ContactInfo->ReaderRollHandle;
    ReaderRollHandle->Block =
        calloc(readerCohortSize, sizeof(struct _RdmaBuffer));
    fi_mr_reg(Fabric->domain, ReaderRollHandle->Block,
              readerCohortSize * sizeof(struct _RdmaBuffer), FI_REMOTE_WRITE, 0,
              0, 0, &WSR_Stream->rrmr, Fabric->ctx);
    ReaderRollHandle->Key = fi_mr_key(WSR_Stream->rrmr);

    WSR_Stream->WriterContactInfo = ContactInfo;

    WSR_Stream->ReaderRoll = malloc(sizeof(struct _RdmaBuffer));
    WSR_Stream->ReaderRoll->Handle = *ReaderRollHandle;
    WSR_Stream->ReaderRoll->BufferLen =
        readerCohortSize * sizeof(struct _RdmaBuffer);

    WSR_Stream->Preload = 0;
    WSR_Stream->SelectionPulled = 0;
    WSR_Stream->SelectLocked = -1;

    WSR_Stream->LastReleased = NULL;

    *WriterContactInfoPtr = ContactInfo;

    /* Do this for testing. Delete later! */
    RdmaReadPatternLocked(Svcs, WSR_Stream, 0); //

    return WSR_Stream;
}

static void RdmaProvideWriterDataToReader(CP_Services Svcs,
                                          DP_RS_Stream RS_Stream_v,
                                          int writerCohortSize,
                                          CP_PeerCohort PeerCohort,
                                          void **providedWriterInfo_v)
{
    Rdma_RS_Stream RS_Stream = (Rdma_RS_Stream)RS_Stream_v;
    FabricState Fabric = RS_Stream->Fabric;
    RdmaWriterContactInfo *providedWriterInfo =
        (RdmaWriterContactInfo *)providedWriterInfo_v;
    void *CP_Stream = RS_Stream->CP_Stream;
    int rc;

    RS_Stream->PeerCohort = PeerCohort;
    RS_Stream->WriterCohortSize = writerCohortSize;
    RS_Stream->WriterAddr =
        calloc(writerCohortSize, sizeof(*RS_Stream->WriterAddr));
    RS_Stream->WriterRoll =
        calloc(writerCohortSize, sizeof(*RS_Stream->WriterRoll));

    /*
     * make a copy of writer contact information (original will not be
     * preserved)
     */
    RS_Stream->WriterContactInfo =
        malloc(sizeof(struct _RdmaWriterContactInfo) * writerCohortSize);

    for (int i = 0; i < writerCohortSize; i++)
    {
        RS_Stream->WriterContactInfo[i].WS_Stream =
            providedWriterInfo[i]->WS_Stream;
        fi_av_insert(Fabric->av, providedWriterInfo[i]->Address, 1,
                     &RS_Stream->WriterAddr[i], 0, NULL);
        RS_Stream->WriterRoll[i] = providedWriterInfo[i]->ReaderRollHandle;
        Svcs->verbose(RS_Stream->CP_Stream,
                      "Received contact info for WS_stream %p, WSR Rank %d\n",
                      RS_Stream->WriterContactInfo[i].WS_Stream, i);
    }

    /* Do this for testing. Delete later! */
    RdmaWritePatternLocked(Svcs, RS_Stream, 0); //
}

static void LogRequest(CP_Services Svcs, Rdma_RS_Stream RS_Stream, int Rank,
                       long Timestep, size_t Offset, size_t Length)
{
    RdmaStepLogEntry *StepLog_p;
    RdmaStepLogEntry StepLog;
    RdmaBuffer LogEntry;
    size_t ReqLogSize;
    int LogIdx;

    StepLog_p = &(RS_Stream->StepLog);
    while (*StepLog_p && Timestep < (*StepLog_p)->Timestep)
    {
        StepLog_p = &((*StepLog_p)->Next);
    }

    if (!(*StepLog_p) || (*StepLog_p)->Timestep != Timestep)
    {
        StepLog = malloc(sizeof(*StepLog));
        StepLog->RankLog =
            calloc(RS_Stream->WriterCohortSize, sizeof(*StepLog->RankLog));
        StepLog->Timestep = Timestep;
        StepLog->Next = *StepLog_p;
        StepLog->BufferSize = 0;
        StepLog->WRanks = 0;
        *StepLog_p = StepLog;
    }
    else
    {
        StepLog = *StepLog_p;
    }

    StepLog->BufferSize += Length;
    StepLog->Entries++;
    if (!StepLog->RankLog[Rank].ReqLog)
    {
        ReqLogSize =
            (REQ_LIST_GRAN * sizeof(struct _RdmaRankReqLog)) +
            sizeof(uint64_t); // extra uint64_t for the preload buffer key
        StepLog->RankLog[Rank].ReqLog = calloc(1, ReqLogSize);
        StepLog->RankLog[Rank].MaxEntries = REQ_LIST_GRAN;
        StepLog->WRanks++;
    }
    if (StepLog->RankLog[Rank].MaxEntries == StepLog->RankLog[Rank].Entries)
    {
        StepLog->RankLog[Rank].MaxEntries *= 2;
        ReqLogSize = (StepLog->RankLog[Rank].MaxEntries *
                      sizeof(struct _RdmaRankReqLog)) +
                     sizeof(uint64_t);
        StepLog->RankLog[Rank].ReqLog =
            realloc(StepLog->RankLog[Rank].ReqLog, ReqLogSize);
    }
    StepLog->RankLog[Rank].BufferSize += Length;
    LogIdx = StepLog->RankLog[Rank].Entries++;
    LogEntry = &StepLog->RankLog[Rank].ReqLog[LogIdx];
    LogEntry->BufferLen = Length;
    LogEntry->Offset = Offset;
    LogEntry->Handle.Block = NULL;
}

static ssize_t PostRead(CP_Services Svcs, Rdma_RS_Stream RS_Stream, int Rank,
                        long Timestep, size_t Offset, size_t Length,
                        void *Buffer, RdmaBufferHandle Info,
                        RdmaCompletionHandle *ret_v)
{
    FabricState Fabric = RS_Stream->Fabric;
    fi_addr_t SrcAddress = RS_Stream->WriterAddr[Rank];
    void *LocalDesc = NULL;
    uint8_t *Addr;
    RdmaCompletionHandle ret;
    ssize_t rc;

    //fprintf(stderr, "rank %d, %s\n", RS_Stream->Rank, __func__);

    *ret_v = malloc(sizeof(struct _RdmaCompletionHandle));
    ret = *ret_v;
    ret->Pending = 1;
    ret->StartWTime = MPI_Wtime();

    if (Fabric->local_mr_req)
    {
        // register dest buffer
        fi_mr_reg(Fabric->domain, Buffer, Length, FI_READ, 0, 0, 0,
                  &ret->LocalMR, Fabric->ctx);
        LocalDesc = fi_mr_desc(ret->LocalMR);
    }
    //fprintf(stderr, "rank %d did mr_reg\n", RS_Stream->Rank);

    Addr = Info->Block + Offset;

    Svcs->verbose(
        RS_Stream->CP_Stream,
        "Remote read target is Rank %d (Offset = %zi, Length = %zi)\n", Rank,
        Offset, Length);

    do
    {
        rc = fi_read(Fabric->signal, Buffer, Length, LocalDesc, SrcAddress,
                     (uint64_t)Addr, Info->Key, ret);
    } while (rc == -EAGAIN);

    if (rc != 0)
    {
        Svcs->verbose(RS_Stream->CP_Stream, "fi_read failed with code %d.\n",
                      rc);
        return (rc);
    }
    else
    {

        Svcs->verbose(RS_Stream->CP_Stream,
                      "Posted RDMA get for Writer Rank %d for handle %p\n",
                      Rank, (void *)ret);
        RS_Stream->PendingReads++;
    }

    return (rc);
}

static RdmaBuffer GetRequest(Rdma_RS_Stream Stream, RdmaStepLogEntry StepLog,
                             int Rank, size_t Offset, size_t Length)
{

    RdmaRankReqLog RankLog = &StepLog->RankLog[Rank];
    RdmaBuffer Req;
    int i;

    //fprintf(stderr, "rank %d, %s\n", Stream->Rank, __func__);

    for (i = 0; i < RankLog->Entries; i++)
    {
        Req = &RankLog->ReqLog[i];
        if (Req->BufferLen == Length && Req->Offset == Offset)
        {
            return (Req);
        }
    }

    return (NULL);
}

static void *RdmaReadRemoteMemory(CP_Services Svcs, DP_RS_Stream Stream_v,
                                  int Rank, long Timestep, size_t Offset,
                                  size_t Length, void *Buffer,
                                  void *DP_TimestepInfo)
{
    RdmaCompletionHandle ret;
    Rdma_RS_Stream RS_Stream = (Rdma_RS_Stream)Stream_v;
    RdmaBufferHandle Info = (RdmaBufferHandle)DP_TimestepInfo;
    RdmaStepLogEntry StepLog;
    RdmaRankReqLog RankLog;
    RdmaBuffer Req;
    int BufferSlot;
    int WRidx;

    Svcs->verbose(RS_Stream->CP_Stream,
                  "Performing remote read of Writer Rank %d at step %d\n", Rank,
                  Timestep);

    if (Info)
    {
        Svcs->verbose(RS_Stream->CP_Stream,
                      "Block address is %p, with a key of %d\n", Info->Block,
                      Info->Key);
    }
    else
    {
        Svcs->verbose(RS_Stream->CP_Stream, "Timestep info is null\n");
        free(ret);
        return (NULL);
    }

   //fprintf(stderr, "%d about to lock\n", RS_Stream->Rank);
    pthread_mutex_lock(&ts_mutex);
   //fprintf(stderr, "%d locked\n", RS_Stream->Rank);
    if (RS_Stream->PreloadPosted)
    {
        RS_Stream->TotalReads++;
        Req = GetRequest(RS_Stream, RS_Stream->PreloadStepLog, Rank, Offset,
                         Length);
       //fprintf(stderr, "%d got req\n", RS_Stream->Rank);
        if (Req)
        {
            BufferSlot = Timestep & 1;
            StepLog = RS_Stream->PreloadStepLog;
            RankLog = &StepLog->RankLog[Rank];
            WRidx = Req - RankLog->ReqLog;
            ret = &((RankLog->PreloadHandles[BufferSlot])[WRidx]);
            ret->PreloadBuffer =
                    Req->Handle.Block + (BufferSlot * StepLog->BufferSize);
            ret->Pending++;
            if (ret->Pending == 0)
            {
                // the data has already been preloaded, safe to copy
                memcpy(Buffer, ret->PreloadBuffer, Length);
                RS_Stream->EarlyReads++;
            }
            else if (ret->Pending != 1)
            {
               //fprintf(stderr,
               //         "rank %d, wrank %d, entry %d, buffer slot %d, bad "
               //         "handle pending value.\n",
               //         RS_Stream->Rank, Rank, WRidx, BufferSlot);
            }
            ret->StartWTime = MPI_Wtime();
        }
        else
        {
            Svcs->verbose(RS_Stream->CP_Stream,
                          "read patterns are fixed, but new request to rank %d "
                          "(Offset = %zi, Length = %zi \n",
                          Rank, Offset, Length);
            ret->PreloadBuffer = NULL;
            if (PostRead(Svcs, RS_Stream, Rank, Timestep, Offset, Length,
                         Buffer, Info, &ret) != 0)
            {
                free(ret);
                return (NULL);
            }
        }
    }
    else
    {
        LogRequest(Svcs, RS_Stream, Rank, Timestep, Offset, Length);
       //fprintf(stderr, "%d req logged\n", RS_Stream->Rank);
        if (PostRead(Svcs, RS_Stream, Rank, Timestep, Offset, Length, Buffer,
                     Info, &ret) != 0)
        {
            free(ret);
            return (NULL);
        }
        ret->PreloadBuffer = NULL;
    }
    pthread_mutex_unlock(&ts_mutex);

    ret->CPStream = RS_Stream;
    ret->Buffer = Buffer;
    ret->Rank = Rank;
    ret->Length = Length;

   //fprintf(stderr, "Rank %d, leaving %s\n", RS_Stream->Rank, __func__);

    return (ret);
}

static void RdmaNotifyConnFailure(CP_Services Svcs, DP_RS_Stream Stream_v,
                                  int FailedPeerRank)
{
    Rdma_RS_Stream Stream = (Rdma_RS_Stream)
        Stream_v; /* DP_RS_Stream is the return from InitReader */
    CManager cm = Svcs->getCManager(Stream->CP_Stream);
    Svcs->verbose(Stream->CP_Stream,
                  "received notification that writer peer "
                  "%d has failed, failing any pending "
                  "requests\n",
                  FailedPeerRank);
    //   This is what EVPath does...
    //   FailRequestsToRank(Svcs, cm, Stream, FailedPeerRank);
}

/* We still have to handle Pull completions while waiting for push to complete
 */
static int DoPushWait(CP_Services Svcs, Rdma_RS_Stream Stream,
                      RdmaCompletionHandle Handle)
{
    FabricState Fabric = Stream->Fabric;
    RdmaStepLogEntry StepLog = Stream->PreloadStepLog;
    RdmaRankReqLog RankLog;
    RdmaBuffer Req;
    RdmaCompletionHandle Handle_t;
    struct fi_cq_data_entry CQEntry = {0};
    int WRank, WRidx;
    int BufferSlot;

   //fprintf(stderr, "rank %d: %s\n", Stream->Rank, __func__);

    while (Handle->Pending > 0)
    {
        ssize_t rc;
        rc = fi_cq_sread(Fabric->cq_signal, (void *)(&CQEntry), 1, NULL, -1);
       //fprintf(stderr, "Rank %d got completion\n", Stream->Rank);
        if (rc < 1)
        {
            Svcs->verbose(Stream->CP_Stream,
                          "failure while waiting for completions (%d).\n", rc);
            return 0;
        }
        else if (CQEntry.flags & FI_REMOTE_CQ_DATA)
        {
            BufferSlot = CQEntry.data >> 31;
            WRidx = (CQEntry.data >> 20) & 0x3FF;
            WRank = CQEntry.data & 0x0FFFFF;
           //fprintf(stderr, "rank %d, incoming push is to bin %d\n",
           //         Stream->Rank, BufferSlot);

            Svcs->verbose(Stream->CP_Stream,
                          "got completion for Rank %d, push request %d.\n",
                          WRank, WRidx);
            RankLog = &StepLog->RankLog[WRank];
            Handle_t = (RdmaCompletionHandle) &
                       ((RankLog->PreloadHandles[BufferSlot])[WRidx]);
            if (Handle_t)
            {
                pthread_mutex_lock(&ts_mutex);
                Handle_t->Pending--;
                if (Handle_t->Pending == 0)
                {
                    // already saw a ReadRemote for this data, safe to copy
                    memcpy(Handle_t->Buffer, CQEntry.buf, CQEntry.len);
                }
                else if (Handle_t->Pending != -1)
                {
                   fprintf(stderr,
                            "rank %d, wrank %d, entry %d, buffer slot %d, bad "
                            "handle pending value.\n",
                            Stream->Rank, WRank, WRidx, BufferSlot);
                } else {
                    
                }
                pthread_mutex_unlock(&ts_mutex);
            }
            else
            {
                Svcs->verbose(
                    Stream->CP_Stream,
                    "Got push completion without a known handle...\n");
            }
        }
        else
        {
            Svcs->verbose(Stream->CP_Stream,
                          "got completion for request with handle %p.\n",
                          CQEntry.op_context);
            Handle_t = (RdmaCompletionHandle)CQEntry.op_context;
            Handle_t->Pending--;
        }
    }

    if (Handle->LocalMR && Fabric->local_mr_req)
    {
        fi_close((struct fid *)Handle->LocalMR);
    }

    return (1);
}

static int DoPullWait(CP_Services Svcs, Rdma_RS_Stream Stream,
                      RdmaCompletionHandle Handle)
{
    FabricState Fabric = Stream->Fabric;
    RdmaCompletionHandle Handle_t;
    struct fi_cq_data_entry CQEntry = {0};

   //fprintf(stderr, "rank %d: %s\n", Stream->Rank, __func__);

    while (Handle->Pending > 0)
    {
        ssize_t rc;
        rc = fi_cq_sread(Fabric->cq_signal, (void *)(&CQEntry), 1, NULL, -1);
        if (rc < 1)
        {
            Svcs->verbose(Stream->CP_Stream,
                          "failure while waiting for completions (%d).\n", rc);
            return 0;
        }
        else
        {
            Svcs->verbose(
                Stream->CP_Stream,
                "got completion for request with handle %p (flags %li).\n",
                CQEntry.op_context, CQEntry.flags);
            Handle_t = (RdmaCompletionHandle)CQEntry.op_context;
            Handle_t->Pending--;
        }
    }

    // TODO: maybe reuse this memory registration
    if (Fabric->local_mr_req)
    {
        fi_close((struct fid *)Handle->LocalMR);
    }

    return (1);
}

/*
 * RdmaWaitForCompletion should return 1 if successful, but 0 if the reads
 * failed for some reason or were aborted by RdmaNotifyConnFailure()
 */
static int RdmaWaitForCompletion(CP_Services Svcs, void *Handle_v)
{
    RdmaCompletionHandle Handle = (RdmaCompletionHandle)Handle_v;
    Rdma_RS_Stream Stream = Handle->CPStream;

   //fprintf(stderr, "Rank %d, %s\n", Stream->Rank, __func__);

    if (Stream->PreloadPosted && Handle->PreloadBuffer)
    {
        return (DoPushWait(Svcs, Stream, Handle));
    }
    else
    {
        return (DoPullWait(Svcs, Stream, Handle));
    }
}

static void RdmaProvideTimestep(CP_Services Svcs, DP_WS_Stream Stream_v,
                                struct _SstData *Data,
                                struct _SstData *LocalMetadata, long Timestep,
                                void **TimestepInfoPtr)
{
    Rdma_WS_Stream Stream = (Rdma_WS_Stream)Stream_v;
    TimestepList Entry = malloc(sizeof(struct _TimestepEntry));
    RdmaBufferHandle Info = malloc(sizeof(struct _RdmaBufferHandle));
    FabricState Fabric = Stream->Fabric;

   //fprintf(stderr, "Rank %d, %s, %li\n", Stream->Rank, __func__, Timestep);

    Entry->Data = malloc(sizeof(*Data));
    memcpy(Entry->Data, Data, sizeof(*Data));
    Entry->Timestep = Timestep;
    Entry->DP_TimestepInfo = Info;
    Entry->BufferSlot = -1;
    Entry->Desc = NULL;

    fi_mr_reg(Fabric->domain, Data->block, Data->DataSize,
              FI_WRITE | FI_REMOTE_READ, 0, 0, 0, &Entry->mr, Fabric->ctx);
    Entry->Key = fi_mr_key(Entry->mr);
    if (Fabric->local_mr_req)
    {
        Entry->Desc = fi_mr_desc(Entry->mr);
    }
    pthread_mutex_lock(&ts_mutex);
    if (Stream->Timesteps)
    {
        Stream->Timesteps->Next = Entry;
    }
    Entry->Prev = Stream->Timesteps;
    Entry->Next = NULL;
    Stream->Timesteps = Entry;
    // Probably doesn't need to be in the lock
    // |
    // ---------------------------------------------------------------------------------------------------
    Info->Key = Entry->Key;
    pthread_mutex_unlock(&ts_mutex);
    Info->Block = (uint8_t *)Data->block;

    Svcs->verbose(Stream->CP_Stream,
                  "Providing timestep data with block %p and access key %d\n",
                  Info->Block, Info->Key);

    *TimestepInfoPtr = Info;
}

static void RdmaReleaseTimestep(CP_Services Svcs, DP_WS_Stream Stream_v,
                                long Timestep)
{
    Rdma_WS_Stream Stream = (Rdma_WS_Stream)Stream_v;
    Rdma_WSR_Stream WSR_Stream;
    TimestepList *List = &Stream->Timesteps;
    TimestepList ReleaseTSL;
    RdmaBufferHandle Info;
    int i;

    Svcs->verbose(Stream->CP_Stream, "Releasing timestep %ld\n", Timestep);

    pthread_mutex_lock(&ts_mutex);
    while ((*List) && (*List)->Timestep != Timestep)
    {
        List = &((*List)->Prev);
    }

    if ((*List) == NULL)
    {
        /*
         * Shouldn't ever get here because we should never release a
         * timestep that we don't have.
         */
       //fprintf(stderr, "Failed to release Timestep %ld, not found\n",
       //         Timestep);
        assert(0);
    }

    ReleaseTSL = *List;
    *List = ReleaseTSL->Prev;
    pthread_mutex_unlock(&ts_mutex);
    fi_close((struct fid *)ReleaseTSL->mr);
    if (ReleaseTSL->Data)
    {
        free(ReleaseTSL->Data);
    }
    Info = ReleaseTSL->DP_TimestepInfo;
    if (Info)
    {
        free(Info);
    }
    free(ReleaseTSL);
}

static void RdmaDestroyRankReqLog(Rdma_RS_Stream RS_Stream,
                                  RdmaRankReqLog RankReqLog)
{
    RdmaReqLogEntry tmp;
    int i;

    for (i = 0; i < RS_Stream->WriterCohortSize; i++)
    {
        if (RankReqLog[i].ReqLog)
        {
            free(RankReqLog[i].ReqLog);
        }
    }
    free(RankReqLog);
}

static void RdmaDestroyReader(CP_Services Svcs, DP_RS_Stream RS_Stream_v)
{
    Rdma_RS_Stream RS_Stream = (Rdma_RS_Stream)RS_Stream_v;
    RdmaStepLogEntry StepLog = RS_Stream->StepLog;
    RdmaStepLogEntry tStepLog;

    fprintf(stderr, "Reader Rank %d: %li early reads of %li total reads (where preload was possible.)\n", RS_Stream->Rank, RS_Stream->EarlyReads, RS_Stream->TotalReads);

    Svcs->verbose(RS_Stream->CP_Stream, "Tearing down RDMA state on reader.\n");
    if (RS_Stream->Fabric)
    {
        fini_fabric(RS_Stream->Fabric);
    }

    while (StepLog)
    {
        RdmaDestroyRankReqLog(RS_Stream, StepLog->RankLog);
        tStepLog = StepLog;
        StepLog = StepLog->Next;
        free(tStepLog);
    }

    free(RS_Stream->WriterContactInfo);
    free(RS_Stream->WriterAddr);
    free(RS_Stream->WriterRoll);
    if (RS_Stream->ContactInfo)
    {
        free(RS_Stream->ContactInfo->Address);
        free(RS_Stream->ContactInfo);
    }
    free(RS_Stream);
}

static void RdmaDestroyWriterPerReader(CP_Services Svcs,
                                       DP_WSR_Stream WSR_Stream_v)
{
    Rdma_WSR_Stream WSR_Stream = (Rdma_WSR_Stream)WSR_Stream_v;
    Rdma_WS_Stream WS_Stream = WSR_Stream->WS_Stream;
    RdmaWriterContactInfo WriterContactInfo;

    pthread_mutex_lock(&wsr_mutex);
    for (int i = 0; i < WS_Stream->ReaderCount; i++)
    {
        if (WS_Stream->Readers[i] == WSR_Stream)
        {
            WS_Stream->Readers[i] =
                WS_Stream->Readers[WS_Stream->ReaderCount - 1];
            break;
        }
    }
    fi_close((struct fid *)WSR_Stream->rrmr);
    if (WSR_Stream->ReaderAddr)
    {
        free(WSR_Stream->ReaderAddr);
    }
    WS_Stream->Readers = realloc(
        WS_Stream->Readers, sizeof(*WSR_Stream) * (WS_Stream->ReaderCount - 1));
    WS_Stream->ReaderCount--;
    pthread_mutex_unlock(&wsr_mutex);

    if (WSR_Stream->WriterContactInfo)
    {
        WriterContactInfo = WSR_Stream->WriterContactInfo;
        free(WriterContactInfo->Address);
    }
    if (WriterContactInfo->ReaderRollHandle.Block)
    {
        free(WriterContactInfo->ReaderRollHandle.Block);
    }
    free(WSR_Stream->WriterContactInfo);

    if (WSR_Stream->ReaderRoll)
    {
        free(WSR_Stream->ReaderRoll);
    }

    free(WSR_Stream);
}

static FMField RdmaReaderContactList[] = {
    {"reader_ID", "integer", sizeof(void *),
     FMOffset(RdmaReaderContactInfo, RS_Stream)},
    {"Length", "integer", sizeof(int), FMOffset(RdmaReaderContactInfo, Length)},
    {"Address", "integer[Length]", sizeof(char),
     FMOffset(RdmaReaderContactInfo, Address)},
    {NULL, NULL, 0, 0}};

static FMStructDescRec RdmaReaderContactStructs[] = {
    {"RdmaReaderContactInfo", RdmaReaderContactList,
     sizeof(struct _RdmaReaderContactInfo), NULL},
    {NULL, NULL, 0, NULL}};

static FMField RdmaBufferHandleList[] = {
    {"Block", "integer", sizeof(void *), FMOffset(RdmaBufferHandle, Block)},
    {"Key", "integer", sizeof(uint64_t), FMOffset(RdmaBufferHandle, Key)},
    {NULL, NULL, 0, 0}};

static FMStructDescRec RdmaBufferHandleStructs[] = {
    {"RdmaBufferHandle", RdmaBufferHandleList, sizeof(struct _RdmaBufferHandle),
     NULL},
    {NULL, NULL, 0, NULL}};

static void RdmaDestroyWriter(CP_Services Svcs, DP_WS_Stream WS_Stream_v)
{
    Rdma_WS_Stream WS_Stream = (Rdma_WS_Stream)WS_Stream_v;
    TimestepList List;
#ifdef SST_HAVE_CRAY_DRC
    uint32_t Credential;

    Credential = WS_Stream->Fabric->credential;
#endif /* SST_HAVE_CRAY_DRC */

    Svcs->verbose(WS_Stream->CP_Stream, "Tearing down RDMA state on writer.\n");
    if (WS_Stream->Fabric)
    {
        fini_fabric(WS_Stream->Fabric);
    }

#ifdef SST_HAVE_CRAY_DRC
    if (WS_Stream->Rank == 0)
    {
        drc_release(Credential, 0);
    }
#endif /* SST_HAVE_CRAY_DRC */

    while (WS_Stream->ReaderCount > 0)
    {
        RdmaDestroyWriterPerReader(Svcs, WS_Stream->Readers[0]);
    }

    pthread_mutex_lock(&ts_mutex);
    while (WS_Stream->Timesteps)
    {
        List = WS_Stream->Timesteps;
        RdmaReleaseTimestep(Svcs, WS_Stream, List->Timestep);
    }
    pthread_mutex_unlock(&ts_mutex);

    free(WS_Stream->Fabric);
    free(WS_Stream);
}

static FMField RdmaWriterContactList[] = {
    {"writer_ID", "integer", sizeof(void *),
     FMOffset(RdmaWriterContactInfo, WS_Stream)},
    {"Length", "integer", sizeof(int), FMOffset(RdmaWriterContactInfo, Length)},
    {"Address", "integer[Length]", sizeof(char),
     FMOffset(RdmaWriterContactInfo, Address)},
    {"ReaderRollHandle", "RdmaBufferHandle", sizeof(struct _RdmaBufferHandle),
     FMOffset(RdmaWriterContactInfo, ReaderRollHandle)},
    {NULL, NULL, 0, 0}};

static FMStructDescRec RdmaWriterContactStructs[] = {
    {"RdmaWriterContactInfo", RdmaWriterContactList,
     sizeof(struct _RdmaWriterContactInfo), NULL},
    {"RdmaBufferHandle", RdmaBufferHandleList, sizeof(struct _RdmaBufferHandle),
     NULL},
    {NULL, NULL, 0, NULL}};

static struct _CP_DP_Interface RdmaDPInterface;

/* In RdmaGetPriority, the Rdma DP should do whatever is necessary to test to
 * see if it
 * *can* run, and then return -1 if it cannot run and a value
 * greater than 1 if it can.  Eventually if we have more than one
 * possible RDMA DP, we may need some better scheme, maybe where the
 * "priority" return value represents some desirability measure
 * (like expected bandwidth or something)
 *
 * Returns 10 if a valid RDMA fabric is found, 100 if that fabric matches
 * what is set in the FABRIC_IFACE environment variable.
 *
 */
static int RdmaGetPriority(CP_Services Svcs, void *CP_Stream,
                           struct _SstParams *Params)
{
    struct fi_info *hints, *info, *originfo, *useinfo;
    char *ifname;
    int Ret = -1;

    (void)attr_atom_from_string(
        "RDMA_DRC_KEY"); // make sure this attribute is translatable

    hints = fi_allocinfo();
    hints->caps = FI_MSG | FI_SEND | FI_RECV | FI_REMOTE_READ |
                  FI_REMOTE_WRITE | FI_RMA | FI_READ | FI_WRITE;
    hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_CONTEXT2 | FI_MSG_PREFIX |
                  FI_ASYNC_IOV | FI_RX_CQ_DATA;
    hints->domain_attr->mr_mode = FI_MR_BASIC;
    hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
    hints->domain_attr->data_progress = FI_PROGRESS_AUTO;
    hints->ep_attr->type = FI_EP_RDM;

    if (Params->DataInterface)
    {
        ifname = Params->DataInterface;
    }
    else
    {
        ifname = getenv("FABRIC_IFACE");
    }

    fi_getinfo(FI_VERSION(1, 5), NULL, NULL, 0, hints, &info);
    fi_freeinfo(hints);

    if (!info)
    {
        Svcs->verbose(CP_Stream,
                      "RDMA Dataplane could not find any viable fabrics.\n");
    }

    originfo = info;

    while (info)
    {
        char *prov_name, *domain_name;

        prov_name = info->fabric_attr->prov_name;
        domain_name = info->domain_attr->name;
        if (ifname && strcmp(ifname, domain_name) == 0)
        {
            Svcs->verbose(CP_Stream,
                          "RDMA Dataplane found the requested "
                          "interface %s, provider type %s.\n",
                          ifname, prov_name);
            Ret = 100;
            break;
        }
        if ((strstr(prov_name, "verbs") && info->src_addr) ||
            strstr(prov_name, "gni") || strstr(prov_name, "psm2"))
        {

            Svcs->verbose(CP_Stream,
                          "RDMA Dataplane sees interface %s, "
                          "provider type %s, which should work.\n",
                          domain_name, prov_name);
            Ret = 10;
        }
        info = info->next;
    }

    if (Ret == -1)
    {
        Svcs->verbose(
            CP_Stream,
            "RDMA Dataplane could not find an RDMA-compatible fabric.\n");
    }

    if (originfo)
    {
        fi_freeinfo(originfo);
    }

    Svcs->verbose(
        CP_Stream,
        "RDMA Dataplane evaluating viability, returning priority %d\n", Ret);
    return Ret;
}

/* If RdmaGetPriority has allocated resources or initialized something
 *  that needs to be cleaned up, RdmaUnGetPriority should undo that
 * operation.
 */
static void RdmaUnGetPriority(CP_Services Svcs, void *CP_Stream)
{
    Svcs->verbose(CP_Stream, "RDMA Dataplane unloading\n");
}

static void PushData(CP_Services Svcs, Rdma_WSR_Stream Stream,
                     TimestepList Step, int BufferSlot)
{
    Rdma_WS_Stream WS_Stream = Stream->WS_Stream;
    FabricState Fabric = WS_Stream->Fabric;
    RdmaRankReqLog RankReq = Stream->PreloadReq;
    RdmaBuffer Req, ReaderRoll, RollBuffer;
    uint64_t RecvCounter;
    uint8_t *StepBuffer;
    int i, rc;

   //fprintf(stderr, "rank %d %s, %li to slot %d\n", WS_Stream->Rank, __func__,
   //         Step->Timestep, BufferSlot);

    StepBuffer = (uint8_t *)Step->Data->block;
    ReaderRoll = (RdmaBuffer)Stream->ReaderRoll->Handle.Block;

    Step->OutstandingWrites = 0;
    while (RankReq)
    {
        RollBuffer = &ReaderRoll[RankReq->Rank];
       //fprintf(stderr, "RankReq->Entries = %i\n", RankReq->Entries);
        for (i = 0; i < RankReq->Entries; i++)
        {
            // TODO: this can only handle 4096 requests per reader rank. Fix.
            uint64_t Data = ((uint64_t)i << 20) | WS_Stream->Rank;
            Data |= BufferSlot << 31;
            Req = &RankReq->ReqLog[i];
           //fprintf(stderr,
           //         "StepBuffer = %p, Req->Offset = %li, Req->BufferLen = %li, "
           //         "Data = %li, Req->Handle.Block = %p, RollBuffer->Offset = "
           //         "%li\n",
           //         StepBuffer, Req->Offset, Req->BufferLen, Data,
           //         Req->Handle.Block, RollBuffer->Offset);
            do
            {
                rc = fi_writedata(Fabric->signal, StepBuffer + Req->Offset,
                                  Req->BufferLen, Step->Desc, Data,
                                  Stream->ReaderAddr[RankReq->Rank],
                                  (uint64_t)Req->Handle.Block +
                                      (BufferSlot * RankReq->PreloadBufferSize),
                                  RollBuffer->Offset, (void *)(Step->Timestep));
            } while (rc == -EAGAIN);
            if (rc != 0)
            {
                Svcs->verbose(WS_Stream->CP_Stream,
                              "fi_read failed with code %d.\n", rc);
            }
        }
        Step->OutstandingWrites += RankReq->Entries;
        RankReq = RankReq->next;
    }
   //fprintf(stderr, "rank %d leaving %s\n", WS_Stream->Rank, __func__);
}

static void RdmaReaderRegisterTimestep(CP_Services Svcs,
                                       DP_WSR_Stream WSRStream_v, long Timestep,
                                       SstPreloadModeType PreloadMode)
{
    Rdma_WSR_Stream WSR_Stream = (Rdma_WSR_Stream)WSRStream_v;
    Rdma_WS_Stream WS_Stream = WSR_Stream->WS_Stream;
    TimestepList Step;

   //fprintf(stderr, "rank %d: %s, %li\n", WS_Stream->Rank, __func__, Timestep);

    if (PreloadMode != SstPreloadNone && WS_Stream->DefLocked < 0)
    {
        WS_Stream->DefLocked = Timestep;
        if (WSR_Stream->SelectLocked >= 0)
        {
            Svcs->verbose(WS_Stream->CP_Stream, "enabling preload.\n");
            WSR_Stream->Preload = 1;
        }
    }

    Step = GetStep(WS_Stream, Timestep);
    pthread_mutex_lock(&ts_mutex);
    if (WSR_Stream->SelectionPulled &&
        WSR_Stream->PreloadUsed[Step->Timestep & 1] == 0)
    {
        PushData(Svcs, WSR_Stream, Step, Step->Timestep & 1);
        WSR_Stream->PreloadUsed[Step->Timestep & 1] = 1;
        Step->BufferSlot = Step->Timestep & 1;
    }
    pthread_mutex_unlock(&ts_mutex);

   //fprintf(stderr, "rank %d, leaving %s\n", WS_Stream->Rank, __func__);
}

static void PostPreload(CP_Services Svcs, Rdma_RS_Stream Stream, long Timestep)
{
    RdmaStepLogEntry StepLog;
    FabricState Fabric = Stream->Fabric;
    RdmaBuffer PreloadBuffer = &Stream->PreloadBuffer;
    RdmaRankReqLog RankLog;
    RdmaBuffer SendBuffer;
    struct fid_mr *sbmr = NULL;
    void *sbdesc = NULL;
    size_t SBSize;
    RdmaBuffer ReqLog;
    uint64_t PreloadKey;
    uint8_t *RawPLBuffer;
    int WRidx = 0;
    uint64_t RollDest;
    struct fi_cq_data_entry CQEntry = {0};
    uint8_t *RecvBuffer;
    RdmaBuffer CQBuffer;
    size_t RBLen;
    uint64_t *BLenHolder;
    int rc;
    int i, j;

   //fprintf(stderr, "rank %d: %s\n", Stream->Rank, __func__);

    StepLog = Stream->StepLog;
    while (StepLog)
    {
        if (StepLog->Timestep == Timestep)
        {
            break;
        }
        StepLog = StepLog->Next;
    }
    if (!StepLog)
    {
        Svcs->verbose(Stream->CP_Stream, "trying to post preload data for a "
                                         "timestep with no access history.");
        return;
    }

    Stream->PreloadStepLog = StepLog;

    PreloadBuffer->BufferLen = 2 * StepLog->BufferSize;
    PreloadBuffer->Handle.Block = malloc(PreloadBuffer->BufferLen);
    fi_mr_reg(Fabric->domain, PreloadBuffer->Handle.Block,
              PreloadBuffer->BufferLen, FI_REMOTE_WRITE, 0, 0, 0, &Stream->pbmr,
              Fabric->ctx);
    PreloadKey = fi_mr_key(Stream->pbmr);
   //fprintf(stderr, "rank %d: PreloadKey = %li\n", Stream->Rank, PreloadKey);

    SBSize = sizeof(*SendBuffer) * StepLog->WRanks;
    SendBuffer = malloc(SBSize);
    if (Fabric->local_mr_req)
    {
        fi_mr_reg(Fabric->domain, SendBuffer, SBSize, FI_WRITE, 0, 0, 0, &sbmr,
                  Fabric->ctx);
        sbdesc = fi_mr_desc(sbmr);
    }

    if (Fabric->rx_cq_data)
    {
        RBLen = 2 * StepLog->Entries * DP_DATA_RECV_SIZE;
        Stream->RecvDataBuffer = malloc(RBLen);
        fi_mr_reg(Fabric->domain, Stream->RecvDataBuffer, RBLen, FI_RECV, 0, 0,
                  0, &Stream->rbmr, Fabric->ctx);
        Stream->rbdesc = fi_mr_desc(Stream->rbmr);
        RecvBuffer = (uint8_t *)Stream->RecvDataBuffer;
        for (i = 0; i < 2 * StepLog->Entries; i++)
        {
            rc = fi_recv(Fabric->signal, RecvBuffer, DP_DATA_RECV_SIZE,
                         Stream->rbdesc, FI_ADDR_UNSPEC, Fabric->ctx);
            if (rc)
            {
               //fprintf(stderr, "Rank %d, fi_recv failed.\n", Stream->Rank);
            }
            RecvBuffer += DP_DATA_RECV_SIZE;
        }
    }

    RawPLBuffer = PreloadBuffer->Handle.Block;
    for (i = 0; i < Stream->WriterCohortSize; i++)
    {
        RankLog = &StepLog->RankLog[i];
        if (RankLog->Entries > 0)
        {
            RankLog->Buffer = (void *)RawPLBuffer;
            fi_mr_reg(Fabric->domain, RankLog->ReqLog,
                      (sizeof(struct _RdmaBuffer) * RankLog->Entries) +
                          sizeof(uint64_t),
                      FI_REMOTE_READ, 0, 0, 0, &RankLog->preqbmr, Fabric->ctx);
            for (j = 0; j < RankLog->Entries; j++)
            {
                ReqLog = &RankLog->ReqLog[j];
                ReqLog->Handle.Block = RawPLBuffer;
                ReqLog->Handle.Key = PreloadKey;
                RawPLBuffer += ReqLog->BufferLen;
            }
            /* We always allocate and extra sizeof(uint64_t) in the ReqLog. We
             * use this to make the size of the Preload Buffer available to each
             * writer. */
            // RecvCounterSB = (uint64_t *)(&RankLog->ReqLog[RankLog->Entries]);
            //*RecvCounterSB = (uint64_t)RecvCounter;
            BLenHolder = (uint64_t *)(&RankLog->ReqLog[RankLog->Entries]);
            *BLenHolder = StepLog->BufferSize;

            SendBuffer[WRidx].BufferLen =
                (RankLog->Entries * sizeof(struct _RdmaBuffer)) +
                sizeof(uint64_t);
            SendBuffer[WRidx].Offset = (uint64_t)PreloadKey;
            SendBuffer[WRidx].Handle.Block = (void *)RankLog->ReqLog;
            SendBuffer[WRidx].Handle.Key = fi_mr_key(RankLog->preqbmr);
           //fprintf(stderr, "%s: posting %li bytes at %p, preload key = %li\n",
           //         __func__, SendBuffer[WRidx].BufferLen,
           //         SendBuffer[WRidx].Handle.Block, SendBuffer[WRidx].Offset);
           //fprintf(
           //     stderr,
           //     "RankLog->ReqLog[0]:\n Handle.Block = %p, Handle.Key = %li, "
           //     "Bufferlen = %li, Offset = %li\n",
           //     RankLog->ReqLog[0].Handle.Block, RankLog->ReqLog[0].Handle.Key,
           //     RankLog->ReqLog[0].BufferLen, RankLog->ReqLog[0].Offset);
            RollDest = (uint64_t)Stream->WriterRoll[i].Block +
                       (sizeof(struct _RdmaBuffer) * Stream->Rank);
           //fprintf(stderr, "Rank %d, writing to roll buffer %p\n",
           //         Stream->Rank, (void *)RollDest);
            fi_write(Fabric->signal, &SendBuffer[WRidx],
                     sizeof(struct _RdmaBuffer), sbdesc, Stream->WriterAddr[i],
                     RollDest, Stream->WriterRoll[i].Key, &SendBuffer[WRidx]);
            RankLog->PreloadHandles = malloc(sizeof(void *) * 2);
            RankLog->PreloadHandles[0] =
                calloc(sizeof(struct _RdmaCompletionHandle), RankLog->Entries);
            RankLog->PreloadHandles[1] =
                calloc(sizeof(struct _RdmaCompletionHandle), RankLog->Entries);
            WRidx++;
        }
    }

    while (WRidx > 0)
    {
        fi_cq_sread(Fabric->cq_signal, (void *)(&CQEntry), 1, NULL, -1);
        CQBuffer = CQEntry.op_context;
        if (CQBuffer >= SendBuffer && CQBuffer < (SendBuffer + StepLog->WRanks))
        {
            WRidx--;
        }
        else
        {
            Svcs->verbose(Stream->CP_Stream,
                          "got unexpected completion while posting preload "
                          "pattern. This is probably an error.\n");
        }
    }

    if (Fabric->local_mr_req)
    {
        fi_close((struct fid *)sbmr);
    }
    free(SendBuffer);

   //fprintf(stderr, "Rank %d, leaving %s\n", Stream->Rank, __func__);
}

static void RdmaReaderReleaseTimestep(CP_Services Svcs, DP_RS_Stream Stream_v,
                                      long Timestep)
{
    Rdma_RS_Stream Stream = (Rdma_RS_Stream)Stream_v;

   //fprintf(stderr, "rank %d, %s, step %li\n", Stream->Rank, __func__,
   //         Timestep);

    pthread_mutex_lock(&ts_mutex);
    if (Timestep >= Stream->PreloadStep && !Stream->PreloadPosted)
    {
        // TODO: Destroy all StepLog entries other than the one used for Preload
        PostPreload(Svcs, Stream, Timestep);
        Stream->PreloadPosted = 1;
    }
    pthread_mutex_unlock(&ts_mutex);

    // This might be be a good spot to flush the Step list if we aren't doing
    // preload (yet.)
   //fprintf(stderr, "rank %d, leaving %s\n", Stream->Rank, __func__);
}

static void PullSelection(CP_Services Svcs, Rdma_WSR_Stream Stream)
{
    Rdma_WS_Stream WS_Stream = Stream->WS_Stream;
    FabricState Fabric = WS_Stream->Fabric;
    RdmaBuffer ReaderRoll = (RdmaBuffer)Stream->ReaderRoll->Handle.Block;
    struct _RdmaBuffer ReqBuffer = {0};
    struct fi_cq_data_entry CQEntry = {0};
    struct fid_mr *rrmr = NULL;
    void *rrdesc = NULL;
    RdmaRankReqLog *RankReq_p = &Stream->PreloadReq;
    RdmaRankReqLog RankReq, CQRankReq;
    RdmaBuffer CQReqLog;
    uint8_t *ReadBuffer;
    int i;

   //fprintf(stderr, "Rank %d, %s\n", WS_Stream->Rank, __func__);

    for (i = 0; i < Stream->ReaderCohortSize; i++)
    {
        if (ReaderRoll[i].BufferLen > 0)
        {
            RankReq = malloc(sizeof(struct _RdmaRankReqLog));
            RankReq->Entries =
                (ReaderRoll[i].BufferLen - sizeof(uint64_t)) /
                sizeof(struct _RdmaBuffer); // piggyback the RecvCounter address
            RankReq->ReqLog = malloc(ReaderRoll[i].BufferLen);
            RankReq->BufferSize = ReaderRoll[i].BufferLen;
            RankReq->next = NULL;
            RankReq->Rank = i;
            *RankReq_p = RankReq;
            RankReq_p = &RankReq->next;
            ReqBuffer.BufferLen += ReaderRoll[i].BufferLen;
           //fprintf(stderr, "Preload key for rank %d is %li\n", i,
           //         ReaderRoll[i].Offset);
        }
    }

    ReqBuffer.Handle.Block = ReadBuffer = malloc(ReqBuffer.BufferLen);
    if (Fabric->local_mr_req)
    {
        fi_mr_reg(Fabric->domain, ReqBuffer.Handle.Block, ReqBuffer.BufferLen,
                  FI_READ, 0, 0, 0, &rrmr, Fabric->ctx);
        rrdesc = fi_mr_desc(rrmr);
    }

    for (RankReq = Stream->PreloadReq; RankReq; RankReq = RankReq->next)
    {
        RankReq->ReqLog = (RdmaBuffer)ReadBuffer;
       //fprintf(stderr, "%s: reading %li bytes from %p\n", __func__,
       //         RankReq->BufferSize, ReaderRoll[RankReq->Rank].Handle.Block);
        fi_read(Fabric->signal, RankReq->ReqLog, RankReq->BufferSize, rrdesc,
                Stream->ReaderAddr[RankReq->Rank],
                (uint64_t)ReaderRoll[RankReq->Rank].Handle.Block,
                ReaderRoll[RankReq->Rank].Handle.Key, RankReq);
        ReadBuffer += RankReq->BufferSize;
    }

    RankReq = Stream->PreloadReq;
    while (RankReq)
    {
       //fprintf(stderr, "About to fi_cq_sread\n");
        fi_cq_sread(Fabric->cq_signal, (void *)(&CQEntry), 1, NULL, -1);
        CQRankReq = CQEntry.op_context;
        if(CQEntry.flags & FI_READ)
        {
            CQReqLog = CQRankReq->ReqLog;
           //fprintf(stderr,
           //         "Got Completion (CQBuffer.BufferLen = %li, CQBuffer.Offset "
           //         "= %li)\n",
           //         CQReqLog->BufferLen, CQReqLog->Offset);
            CQRankReq->PreloadBufferSize =
                *((uint64_t *)&CQReqLog[CQRankReq->Entries]);
           //fprintf(stderr,
           //         "Remote reader rank %d has a preload buffer size of %li\n",
           //         CQRankReq->Rank, CQRankReq->PreloadBufferSize);
            RankReq = RankReq->next;
        }
        else
        {
            Svcs->verbose(
                WS_Stream->CP_Stream,
                "got unexpected completion while fetching preload patterns."
                " This is probably an error.\n");
        }
    }

   //fprintf(stderr, "Leaving %s\n", __func__);

    if (Fabric->local_mr_req)
    {
        fi_close((struct fid *)rrmr);
    }
}

static void CompletePush(CP_Services Svcs, Rdma_WSR_Stream Stream,
                         TimestepList Step)
{
    Rdma_WS_Stream WS_Stream = Stream->WS_Stream;
    FabricState Fabric = WS_Stream->Fabric;
    TimestepList CQStep;
    struct fi_cq_data_entry CQEntry = {0};
    long CQTimestep;

   //fprintf(stderr, "rank %d, %s\n", WS_Stream->Rank, __func__);

    while (Step->OutstandingWrites > 0)
    {
        fi_cq_sread(Fabric->cq_signal, (void *)(&CQEntry), 1, NULL, -1);
        if (CQEntry.flags & FI_WRITE)
        {
            CQTimestep = (long)CQEntry.op_context;
            if (CQTimestep == Step->Timestep)
            {
                CQStep = Step;
            }
            else
            {
                Svcs->verbose(WS_Stream->CP_Stream,
                              "while completing step %d, saw completion notice "
                              "for step %d.\n",
                              Step->Timestep, CQTimestep);

                CQStep = GetStep(WS_Stream, CQTimestep);

                if (!CQStep)
                {
                    Svcs->verbose(WS_Stream->CP_Stream,
                                  "received completion for step %d, which we "
                                  "don't know about.\n",
                                  CQTimestep);
                }
            }
            CQStep->OutstandingWrites--;
        }
        else
        {
            Svcs->verbose(WS_Stream->CP_Stream,
                          "while waiting for push to complete, saw an unknown "
                          "completion. This is probably an error.\n");
        }
    }
   //fprintf(stderr, "rank %d, leaving %s\n", WS_Stream->Rank, __func__);
}

static void RdmaReleaseTimestepPerReader(CP_Services Svcs,
                                         DP_WSR_Stream Stream_v, long Timestep)
{
    Rdma_WSR_Stream Stream = (Rdma_WSR_Stream)Stream_v;
    Rdma_WS_Stream WS_Stream = Stream->WS_Stream;
    TimestepList Step = GetStep(WS_Stream, Timestep);

   //fprintf(stderr, "rank %d, %s, %li\n", WS_Stream->Rank, __func__, Timestep);

    if (!Step)
    {
        return;
    }

    if (Stream->Preload)
    {
        if (Stream->SelectionPulled)
        {
            // Make sure all writes for this timestep have completed
            CompletePush(Svcs, Stream, Step);
            // If data have been provided for the next timestep, push it
            pthread_mutex_lock(&ts_mutex);
            Stream->PreloadUsed[Step->Timestep & 1] = 0;
            for (Step = Step->Next; Step; Step = Step->Next)
            {
                if (Step->BufferSlot == -1)
                {
                    if (Stream->PreloadUsed[Step->Timestep & 1] == 1)
                    {
                       //fprintf(stderr,
                       //         "rank %d, RX preload buffers full, deferring "
                       //         "preload of step %li.\n",
                       //         WS_Stream->Rank, Step->Timestep);
                    }
                    else
                    {
                        PushData(Svcs, Stream, Step, Step->Timestep & 1);
                        Stream->PreloadUsed[Step->Timestep & 1] = 1;
                        Step->BufferSlot = Step->Timestep & 1;
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&ts_mutex);
        }
        else
        {
            PullSelection(Svcs, Stream);
            Stream->PreloadUsed[0] = Stream->PreloadUsed[1] = 0;
            Stream->SelectionPulled = 1;

            pthread_mutex_lock(&ts_mutex);
            Step = Step->Next;
            while (Step && Stream->PreloadUsed[Step->Timestep & 1] == 0)
            {
                PushData(Svcs, Stream, Step, Step->Timestep & 1);
                Stream->PreloadUsed[Step->Timestep & 1] = 1;
                Step->BufferSlot = Step->Timestep & 1;
                Step = Step->Next;
            }
            pthread_mutex_unlock(&ts_mutex);
        }
    }

   //fprintf(stderr, "rank %d, leaving %s\n", WS_Stream->Rank, __func__);
}

extern CP_DP_Interface LoadRdmaDP()
{
    memset(&RdmaDPInterface, 0, sizeof(RdmaDPInterface));
    RdmaDPInterface.ReaderContactFormats = RdmaReaderContactStructs;
    RdmaDPInterface.WriterContactFormats = RdmaWriterContactStructs;
    RdmaDPInterface.TimestepInfoFormats = RdmaBufferHandleStructs;
    RdmaDPInterface.initReader = RdmaInitReader;
    RdmaDPInterface.initWriter = RdmaInitWriter;
    RdmaDPInterface.initWriterPerReader = RdmaInitWriterPerReader;
    RdmaDPInterface.provideWriterDataToReader = RdmaProvideWriterDataToReader;
    RdmaDPInterface.readRemoteMemory = RdmaReadRemoteMemory;
    RdmaDPInterface.waitForCompletion = RdmaWaitForCompletion;
    RdmaDPInterface.notifyConnFailure = RdmaNotifyConnFailure;
    RdmaDPInterface.provideTimestep = RdmaProvideTimestep;
    RdmaDPInterface.readerRegisterTimestep = RdmaReaderRegisterTimestep;
    RdmaDPInterface.releaseTimestep = RdmaReleaseTimestep;
    RdmaDPInterface.readerReleaseTimestep = RdmaReleaseTimestepPerReader;
    RdmaDPInterface.WSRreadPatternLocked = RdmaReadPatternLocked;
    RdmaDPInterface.RSreadPatternLocked = RdmaWritePatternLocked;
    RdmaDPInterface.RSReleaseTimestep = RdmaReaderReleaseTimestep;
    RdmaDPInterface.destroyReader = RdmaDestroyReader;
    RdmaDPInterface.destroyWriter = RdmaDestroyWriter;
    RdmaDPInterface.destroyWriterPerReader = RdmaDestroyWriterPerReader;
    RdmaDPInterface.getPriority = RdmaGetPriority;
    RdmaDPInterface.unGetPriority = RdmaUnGetPriority;
    return &RdmaDPInterface;
}
