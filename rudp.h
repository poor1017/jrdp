#ifndef JRDP_H_INCLUDED
#define JRDP_H_INCLUDED


#include <netdb.h>
#include <sys/types.h>
#include <sys/time.h>

#if (defined(__BIT_TYPES_DEFINED__) || defined(_MACHTYPES_H_))
  /* already got them */
#else

#ifndef _U_INT32_T_
#define _U_INT32_T_
#ifdef _WINDOWS			/* MS Windows 3.1 (ewww) */
typedef unsigned long		u_int32_t;
#else
typedef unsigned int		u_int32_t;
#endif
#endif /* _U_INT32_T_ */

#ifndef _U_INT16_T_
#define _U_INT16_T_
typedef unsigned short		u_int16_t;
#endif /* _U_INT16_T_ */

#ifndef _INT32_T_
#define _INT32_T_
#ifdef _WINDOWS
typedef long int32_t;
#else
typedef int int32_t;
#endif
#endif /* _INT32_T_ */

#ifndef _INT16_T_
#define _INT16_T_
typedef  short int16_t;
#endif /* _INT16_T_ */

#endif /* !(defined(__BIT_TYPES_DEFINED__) || defined(_MACHTYPES_H_)) */

#define max(x,y) (x > y ? x : y)
#define min(x,y) (x < y ? x : y)

#define DEFAULT_PEER    (char*) NULL
#define DEFAULT_PORT    4008

/* Queuing priorities for requests */
#define MAX_PRI    32765  /* Maximum user proiority          */
#define MAX_SPRI   32767  /* Maximum priority for system use */
#define MIN_PRI    -32765  /* Maximum user proiority          */
#define MIN_SPRI   -32768  /* Maximum priority for system use */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 257
#endif

#define JRDP_SUCCESS        0
#define JRDP_ERROR          1
#define JRDP_SELECT_FAILED  5
#define JRDP_TIMEOUT        9
#define JRDP_REFUSED        10
#define JRDP_PENDING        -1

#define JRDP_PKT_HDR    64      /* (CS)Max offset for start, default 64              */
#define JRDP_PKT_LEN_S  21      /* (CS)Max length for sent data, default 1250        */
#define JRDP_PKT_LEN_R  1405    /* (CS)Max length for received data, default 1405    */
#define JRDP_PKT_DSZ    JRDP_PKT_LEN_R + 2*JRDP_PKT_HDR

#define JRDP_PACK_SPLIT      0x00       /* OK to split across packets       */
#define JRDP_PACK_NOSPLITB   0x01       /* Don't split across packets       */
#define JRDP_PACK_NOSPLITL   0x02       /* Don't split lines across packets */
#define JRDP_PACK_NOSPLITBL  0x03       /* NOSPLITB|NOSPLITL                */
#define JRDP_PACK_TAGLENGTH  0x04       /* Include length tag for buffer    */
#define JRDP_PACK_COMPLETE   0x08       /* This is the final packet to add  */

#define JRDP_R_INCOMPLETE    0x00       /* More messages will follow        */
#define JRDP_R_NOSEND        0x02       /* Add to req->trns but don't send  */
#define JRDP_R_COMPLETE      0x08       /* This is the final packet to send */

#define JRDP_BACKOFF(x)     (2 * x)     /* (C)Backoff algorithm             */
#define UFACTOR             1000000     /* convert seconds to microseconds  */

#define CONCATENATE_LISTS( list1, list2 ) do    \
{                                               \
    if ( !(list1) )                             \
        (list1) = (list2);                      \
    else if ( !(list2) )                        \
        ;                                       \
    else                                        \
    {                                           \
        (list2)->prev->next = (list1)->prev;    \
        (list1)->prev->next = (list2);          \
        (list1)->prev = (list2)->prev;          \
        (list2)->prev = (list1)->prev->next;    \
        (list1)->prev->next = NULL;             \
    }                                           \
} while (0)

#define APPEND_ITEM( new, head ) do \
{                                   \
    if ( (head) )                   \
    {                               \
        (new)->prev = (head)->prev; \
        (head)->prev = (new);       \
        (new)->next = NULL;         \
        (new)->prev->next = (new);  \
    }                               \
    else                            \
    {                               \
        (head) = (new);             \
        (new)->prev = (new);        \
        (new)->next = NULL;         \
    }                               \
} while(0)

#define PREPEND_ITEM( new, head ) do    \
{                                       \
    assert(new);                        \
    (new)->next = NULL;                 \
    (new)->prev = (new);                \
    CONCATENATE_LISTS( (new), (head) ); \
    (head) = (new);                     \
} while(0)

#define INSERT_ITEM1_BEFORE_ITEM2(item1, item2) do  \
{								                    \
     assert(item1);                                 \
     assert(item2);                                 \
     assert((item2)->prev->next);                   \
     (item2)->prev->next = (item1);                 \
     (item1)->prev = (item2)->prev;                 \
     (item1)->next = (item2);                       \
     (item2)->prev = (item1);                       \
} while(0)

#define INSERT_NEW_ITEM1_AFTER_ITEM2_IN_LIST( item1, item2, list ) do   \
{                                                                       \
    assert(item1);                                                      \
    assert(item2);	                                                    \
    assert(list);                                                       \
    (item1)->next = (item2)->next;                                      \
    (item1)->prev = (item2);                                            \
    if ( (item2)->next )                                                \
        (item2)->next->prev = (item1);                                  \
    else                                                                \
        (list)->prev = (item1);                                         \
    (item2)->next = (item1);                                            \
} while(0)

#define EXTRACT_ITEM( item, head ) do           \
{                                               \
    if ( (head) == (item) )                     \
    {                                           \
        (head) = (item)->next;                  \
        if ( (head) )                           \
            (head)->prev = (item)->prev;        \
    }                                           \
    else                                        \
    {                                           \
        (item)->prev->next = (item)->next;      \
        if ( (item)->next )                     \
            (item)->next->prev = (item)->prev;  \
        else                                    \
            (head)->prev = (item)->prev;        \
    }                                           \
} while(0)

#define if_have_option_argument(nbytes)                 \
    if ( ctlptr + nbytes > pkt->start + hdr_len ) {}    \
    else

struct jpacket
{
    u_int16_t           seq;
    int                 length;
    char*               start;
    char*               text;
    char*               ioptr;
    char                data[JRDP_PKT_DSZ];
    struct jpacket*     prev;
    struct jpacket*     next;
};
typedef struct jpacket* PJPACKET;
typedef struct jpacket  JPACKET;

enum jrdp_status
{
    JRDP_STATUS_NOSTART = -1, /* not started or inactive */
    JRDP_STATUS_INACTIVE = -1,
    JRDP_STATUS_COMPLETE = -2, /* done */
    JRDP_STATUS_ACTIVE = -3, /* running */
    JRDP_STATUS_ACKPEND = -4,
    JRDP_STATUS_GAPS = -5,
    JRDP_STATUS_ABORTED = -6,
    JRDP_STATUS_FREE = -7, /* used for consistency checking; indicates this request is free. */
    JRDP_STATUS_FAILED = 255
};

struct jreq
{
    enum jrdp_status    status;
    int                 flags;
    u_int16_t           cid;

    struct jpacket*     outpkt;
    struct jpacket*     inpkt;
    struct jpacket*     comp_thru;

    u_int16_t           rcvd_tot;
    u_int16_t           rcvd_thru;
    struct jpacket*     rcvd;

    u_int16_t           trns_tot;
    u_int16_t           trns_thru;
    struct jpacket*     trns;

    u_int16_t           prcvd_thru;

    u_int16_t           window_sz;
    u_int16_t           pwindow_sz;

    struct sockaddr_in  peer;
    char*               peer_hostname;

    struct timeval      rcvd_time;	  /* (S)Time request was received      */
    struct timeval      svc_start_time;	  /* (S)Time service began on request  */
    struct timeval      svc_comp_time;	  /* (S)Time service was completed     */
    struct timeval      timeout;	  /* (C)Initial time to wait for resp  */
    struct timeval      timeout_adj;      /* (C)Adjusted time to wait for resp */
    struct timeval      wait_till;	  /* (C)Time at which timeout expires  */
    u_int16_t           retries;	  /* (C)Number of times to retry       */
    u_int16_t           retries_rem;	  /* (C)Number of retries remaining    */
    u_int16_t           svc_rwait;	  /* (C)Svc suggested tm2wait b4 retry */
    u_int16_t           svc_rwait_seq;	  /* (C)Seq # when scv_rwait changed   */
    /* inf_ in the following two items means 'informational' */
    u_int16_t           inf_queue_pos;    /* (C)Service queue pos if available */
    u_int32_t           inf_sys_time;     /* (C)Expected time til svc complete */

    struct jreq*        prev;
    struct jreq*        next;
};
typedef struct jreq*    PJREQ;
typedef struct jreq     JREQ;

struct jlistener
{
    int     sock;
    int     prvsock;
    int     port;

    int     req_count;
    int     pkt_count;

    int     activeQ_len;
    int     pendingQ_len;
    int     partialQ_len;
    int     partialQ_max_len;
    int     replyQ_len;
    int     replyQ_max_len;

    PJREQ   pendingQ;
    PJREQ   partialQ;
    PJREQ   runQ;
    PJREQ   doneQ;

    struct jlistener* prev;
    struct jlistener* next;
};
typedef struct jlistener*   PJLISTENER;
typedef struct jlistener    JLISTENER;

struct jsender
{
    int                 sock;
    struct sockaddr_in  peer_addr;
    int                 req_count;
    int                 pkt_count;

    PJREQ               activeQ;
    PJREQ               completeQ;

    struct jsender*     prev;
    struct jsender*     next;
};
typedef struct jsender*     PJSENDER;
typedef struct jsender      JSENDER;

PJLISTENER      rudp_lstneralloc(void);
PJSENDER        rudp_snderalloc(void);
PJPACKET        jrdp_pktalloc(void);
PJREQ           jrdp_reqalloc(void);
void            rudp_lstnerfree( PJLISTENER lstner );
void            rudp_snderfree( PJSENDER snder );
void            jrdp_pktfree( PJPACKET pkt );
void            jrdp_reqfree( PJREQ req );
int             jrdp_pack( PJREQ req, int flags, const char* buf, int buflen );
int             jrdp_hostname2name_addr( const char* hostname_arg, char** official_hnamegsp, struct sockaddr_in* hostaddr_arg );
void            set_haddr(void);
u_int32_t       myaddress(void);
const char*     myhostname(void);
void            jrdp_initialize(void);
int             jrdp_init(void);
//int             jrdp_bind_port( const char* portname );

PJREQ           jrdp_get_nxt_blocking( int sock );
PJREQ           jrdp_get_nxt_nonblocking( int sock );
int             jrdp_send( PJREQ req, const char* dname, struct sockaddr_in* dest, int ttwait );
int             jrdp_accept( PJLISTENER lstner, int timeout_sec, int timeout_usec );
int             jrdp_headers( PJREQ req );
int             jrdp_xmit( PJSENDER snder, PJREQ req, int window );
void            jrdp_header_ack_rwait( PJPACKET pkt, PJREQ req, int is_ack_needed, int is_rwait_needed );
int             jrdp_acknowledge( PJLISTENER lstner, PJREQ req );
int             jrdp_snd_pkt( PJLISTENER lstner, PJPACKET pkt, PJREQ req );
int             jrdp_retrieve( PJSENDER snder, PJREQ req, int ttwait_arg );
int             jrdp_retransmit_unacked_packets( PJSENDER snder, PJREQ req );
int             jrdp_reply( int sock, PJREQ req, int flags, const char* message, int len );
int             jrdp_respond( PJLISTENER lstner, PJREQ req, int flags );
void            jrdp_update_cfields( PJREQ existing, PJREQ newing );

int             rudp_open_listen( const char* portname );
int             rudp_connect( const char* dname, struct sockaddr_in* dest );
int             rudp_send( int sock, int flags, const char* buf, int buflen, int ttwait );
int             rudp_disconnect( int sock );
int             rudp_close_listen( int sock );
PJLISTENER      rudp_find_matched_lstner( int sock );
PJSENDER        rudp_find_matched_snder( int sock );

struct timeval  jrdp__gettimeofday(void);
int             jrdp__eqtime( const struct timeval t1, const struct timeval t2 );
int             jrdp__timeislater( struct timeval t1, struct timeval t2 );
struct timeval  jrdp__addtime( struct timeval t1, struct timeval t2 );
struct timeval  jrdp__subtime( const struct timeval minuend, const struct timeval subtrahend );
struct timeval  jrdp__mintime( const struct timeval t1, const struct timeval t2 );
void            jrdp__adjust_backoff( struct timeval* tv );
struct timeval  jrdp__next_activeQ_timeout( const struct timeval now );

extern int      jrdp_priority;

#endif /* not JRDP_H_INCLUDED */
