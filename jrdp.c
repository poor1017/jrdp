
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include "jrdp.h"


int jreq_count = 0;
int jpkt_count = 0;
int jrdp_srvsock = -1;
int jrdp_prvsock = -1;
int jrdp_priority = 0;
int jrdp_sock = -1;
static char* myhname = NULL;
static long myhaddr = 0L;
struct sockaddr_in jrdp_client_address_port;

PJREQ jrdp_activeQ = NULL;
PJREQ jrdp_pendingQ = NULL;
PJREQ jrdp_partialQ = NULL;
PJREQ jrdp_runQ = NULL;
PJREQ jrdp_doneQ = NULL;
int jrdp_activeQ_len = 0;
int jrdp_pendingQ_len = 0;
int jrdp_partialQ_len = 0;
int jrdp_partialQ_max_len = 20;

const struct timeval zerotime = { 0, 0 };
const struct timeval infinitetime = { -1, -1 };
const struct timeval bogustime = { -2, -2 };


PJPACKET
jrdp_pktalloc()
{
    PJPACKET    pkt;
	pkt = (PJPACKET)malloc( sizeof(JPACKET) );
	if ( !pkt )
        return NULL;
    ++jpkt_count;

    pkt->seq = 0;
    pkt->length = 0;
    pkt->start = pkt->data + JRDP_PKT_HDR;
    pkt->text = pkt->start;
    pkt->ioptr = pkt->start;
    pkt->prev = NULL;
    pkt->next = NULL;
    return pkt;
}

PJREQ
jrdp_reqalloc(void)
{
    PJREQ       req;
	req = (PJREQ)malloc( sizeof(JREQ) );
	if ( !req )
        return NULL;
    ++jreq_count;

    req->status = JRDP_STATUS_NOSTART;
#ifdef ARDP_MY_WINDOW_SZ
    req->flags = ARDP_FLAG_SEND_MY_WINDOW_SIZE; /*  used by clients. */
#else
    req->flags = 0;
#endif
    req->cid = 0;
    req->outpkt = NULL;
    req->rcvd_tot = 0;
    req->rcvd_thru = 0;
    req->rcvd = NULL;
    req->trns_tot = 0;
    req->trns_thru = 0;
    req->trns = NULL;
    req->prcvd_thru = 0;
    req->window_sz = 16;
    req->pwindow_sz = 16;
    memset( &(req->peer), '\000', sizeof(struct sockaddr_in) );
    req->peer_hostname = NULL;

    req->rcvd_time.tv_sec = 0;
    req->rcvd_time.tv_usec = 0;
    req->svc_start_time.tv_sec = 0;
    req->svc_start_time.tv_usec = 0;
    req->svc_comp_time.tv_sec = 0;
    req->svc_comp_time.tv_usec = 0;
    req->timeout.tv_sec = 4;
    req->timeout.tv_usec = 0;
    req->timeout_adj.tv_sec = 4;
    req->timeout_adj.tv_usec = 0;
    req->wait_till.tv_sec = 0;
    req->wait_till.tv_usec = 0;
    req->retries = 3;
    req->retries_rem = 3;
    req->svc_rwait = 0;
    req->svc_rwait_seq = 0;
    req->inf_queue_pos = 0;
    req->inf_sys_time = 0;

    req->prev = NULL;
    req->next = NULL;

    return req;
}

void
jrdp_pktfree( PJPACKET pkt )
{
    free(pkt);

    return;
}

void
jrdp_reqfree( PJREQ req )
{
    return;
}

int
jrdp_pack( PJREQ req, int flags, const char* buf, int buflen )
{
    int         remaining;      /* Space remaining in pkt  */
    int         written;        /* # Octets written        */
    char*       newline;        /* last nl in string       */
    int         deflen;         /* Length of deferred text */
    PJPACKET    pkt = NULL;     /* Current packet          */
    PJPACKET    ppkt;           /* Previous packet         */

    /* Note: New text gets written after pkt->ioptr.  If   */
    /* there is text waiting for a newline, then ->ioptr   */
    /* may be beyond ->start+->length, which will be the   */
    /* text up to the final newline seen so far            */

    /* XXX For now if NOSPLITL, must also specify NOSPLITB */
    if( ( flags & JRDP_PACK_NOSPLITL ) && !( flags & JRDP_PACK_NOSPLITB ) )
       return 1;

    if ( buf && !buflen )
        buflen = strlen(buf);
    if ( !buf )
        buf = "";

    while ( 1 )
    {
        /* Find ot allocate last packet in ->outpkt */
        if ( req->outpkt )
            pkt = req->outpkt->prev;
        if ( !pkt )
        {
            if ( ( pkt = jrdp_pktalloc() ) == NULL )
            {
                printf( "Create packet failed.\n" );
                exit(-1);
            }
            APPEND_ITEM( pkt, req->outpkt );
        }

        deflen = pkt->ioptr - ( pkt->start + pkt->length );

        if ( ( flags & JRDP_PACK_NOSPLITB ) && ( buflen > JRDP_PKT_LEN_S ) )
        {
            printf( "Too long.\n" );
            exit(-1);
        }

        remaining = JRDP_PKT_LEN_S - ( pkt->ioptr - pkt->start );

        if ( remaining == 0 )
        {
            ppkt = pkt;
            if ( ( pkt = jrdp_pktalloc()) == NULL )
            {
                printf( "Create packet failed.\n" );
                exit(-1);
            }
            if ( ppkt->start + ppkt->length != ppkt->ioptr )
            {
                /* Must move deferred text to new packet */
                memcpy( pkt->start, ppkt->start + ppkt->length, deflen );
                ppkt->ioptr = ppkt->start + ppkt->length;
                *(ppkt->ioptr) = '\0';
                pkt->ioptr += deflen;
            }
            APPEND_ITEM( pkt, req->outpkt );
            remaining = JRDP_PKT_LEN_S;
        }

        written = min( remaining, buflen );
        memcpy( pkt->ioptr, buf, written );
        pkt->ioptr += written;
        pkt->length = pkt->ioptr - pkt->start;
        /* Always need to stick on trailing NULL for sake of jrdp_respond(), which expects it. */
        *pkt->ioptr = '\0';

        if ( written != buflen )
        {
            buf += written;
            buflen -= written;
        }
        else
            break;
    }

    if ( flags & JRDP_PACK_COMPLETE )
    {
        deflen = pkt->ioptr - ( pkt->start + pkt->length );
        /* If deferred, write a newline */
        if ( deflen )
        {
            *(pkt->ioptr++) = '\n';
            pkt->length = pkt->ioptr - pkt->start;
            /* Always need to stick on trailing NULL for sake of jrdp_respond(), which expects it. */
            *pkt->ioptr = '\0';
        }
    }

    return 0;
}


int
jrdp_hostname2name_addr( const char* hostname_arg, char** official_hnameGSP, struct sockaddr_in* hostaddr_arg )
{
    struct hostent* hp;		/* Remote host we're connecting to. */
    char* hostname = (char*)malloc( sizeof(char) * ( strlen(hostname_arg) + 1 ) );
    strcpy( hostname, hostname_arg ); // working copy
    struct sockaddr_in* hostaddr = hostaddr_arg; // working copy

    char* openparen;		/* for parsing hostname string. */
    int req_udp_port = 0;		/* port requested, HOST byte order */

    if ( !hostname )
    {
        printf( "No host name to parse.\n" );
        exit(-1);
    }

    if ( !hostaddr_arg )
    {
        printf( "No host address to write to.\n" );
        exit(-1);
    }
	
    /* If a port is included, save it away */
    if ( ( openparen = strchr( hostname, '(' ) ) || ( openparen = strchr( hostname, ':' ) ) )
    {
        sscanf( openparen + 1, "%d", &req_udp_port );
        *openparen = '\0';
    }
    /* hostname is now the host name without the port number. */

    /* Here, we do a regular gethostbyname() lookup, ignoring the cache. */
    hp = gethostbyname( (char*)hostname ); /* cast to char * since
                                              GETHOSTBYNAME has a bad
                                              prototype on some systems -- we
                                              know it's const char *,
                                              though. */  

    /* We may have the hostent structure we need. */
    if ( hp == NULL )
    {
        printf( "Here, there is NO hostname match.\n" );
        exit(-1);
    }

    /* Here we have a HOSTENT. Put the new data into the cache. */
    memset( (char*)hostaddr, 0, sizeof(struct sockaddr_in) );
    memcpy( (char*)&hostaddr->sin_addr, hp->h_addr, hp->h_length );
    hostaddr->sin_family = hp->h_addrtype;

    if ( official_hnameGSP )
    {
        *official_hnameGSP = (char*)malloc( sizeof(char) * ( strlen(hp->h_name) + 1 ) );
        strcpy( *official_hnameGSP, hp->h_name );
    }

    free(hostname);
    hostaddr->sin_port = htons(req_udp_port);
    return 0;
}

static void
set_haddr(void)
{
    /* See above for explanation.  EWWWWWWW */
    static int weird_Solaris_2_5_1_gethostname_bug_demonstrated = 0;

    /* This means bugs that haven't appeared yet, but might. */
    static int even_weirder_gethostname_bug_demonstrated = 0;

    /* We use a temporary here; bug-catching. */
    int gethostname_retval;

    char* s = NULL;
    struct sockaddr_in sin;

    assert(!myhname);
    myhname = (char*)malloc( sizeof(char) * MAXHOSTNAMELEN );
    gethostname_retval = gethostname( myhname, MAXHOSTNAMELEN );
    if ( gethostname_retval == -1 )
    {
        printf( "JRDP: Negative Error retrieving host name" );
        exit(-1);
    }

    if ( gethostname_retval )
    {
        printf( "JRDP: Positive Error retrieving host name" );
    }

    jrdp_hostname2name_addr( myhname, &s, &sin );
    free(myhname);
    myhname = s;
    myhaddr = sin.sin_addr.s_addr;
}

u_int32_t
myaddress(void)
{
    if ( !myhaddr )
        set_haddr();
    return myhaddr;
}

const char *
myhostname(void)
{
    if ( !myhaddr )
        set_haddr();
    return myhname;
}

void
jrdp_initialize(void)
{
    printf( "\nEntering jrdp_initialize()\n" );

    return;
}

int
jrdp_bind_port( const char* portname )
{
    printf( "\nEntering jrdp_bind_port()\n" );
    struct sockaddr_in s_in = {AF_INET};
    struct servent* sp;

    int             on = 1;
    int             port_no = 0;

    if ( !portname || !*portname )
    {
        printf( "jrdp_bind_port() did not get a valid port name as an argument; using the default server port: %d\n", DEFAULT_PORT );
        s_in.sin_port = htons( (u_int16_t) DEFAULT_PORT );
    }
    else if ( *portname == '#' )
    {
        sscanf( portname + 1, "%d", &port_no );
        if ( port_no == 0 )
        {
            printf( "jrdp_bind_port: cannot bind: \"%s\" is an invalid port specifier; port number must follow #\n", portname );
            return -1;
        }
        s_in.sin_port = htons( (u_int16_t) port_no );
    }
    else if ( ( sp = getservbyname( portname, "udp" ) ) != NULL )
    {
        s_in.sin_port = sp->s_port;
    }
    else if ( strcmp( portname, DEFAULT_PEER ) == 0 )
    {
        printf( "jrdp_bind_port: udp/%s unknown service - using %d\n", DEFAULT_PEER, DEFAULT_PORT );
        s_in.sin_port = htons( (u_int16_t) DEFAULT_PORT );
    }
    else
    {
	    printf( "jrdp_bind_port: udp/%s unknown service\n", portname );
	    return -1;
    }

    if ( ( jrdp_srvsock = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0)
    {
        printf( "jrdp_bind_port: Can't open socket\n" );
        return -1;
    }

    if ( setsockopt( jrdp_srvsock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0 )
        printf( "dirsrv: setsockopt (SO_REUSEADDR): error; continuing as best we can.\n" );
    
    s_in.sin_addr.s_addr = (u_int32_t) myaddress();

    if ( bind( jrdp_srvsock, (struct sockaddr *)&s_in, sizeof(struct sockaddr_in) ) < 0 )
    {
        printf( "jrdp_bind_port(): Can not bind socket\n" );
        return -1;
    }

    return ( ntohs(s_in.sin_port) );
}

PJREQ
jrdp_get_nxt_blocking(void)
{
    PJREQ   nxtreq = NULL;
    fd_set  readfds;
    int     tmp;

    while ( 1 )
    {
        if ( ( nxtreq = jrdp_get_nxt_nonblocking() ) )
            break;

        FD_ZERO(&readfds);
        if ( jrdp_srvsock != -1 )
            FD_SET( jrdp_srvsock, &readfds );
        if ( jrdp_prvsock != -1)
            FD_SET( jrdp_prvsock, &readfds );
        tmp = select( max( jrdp_srvsock, jrdp_prvsock ) + 1, &readfds, (fd_set*)0, (fd_set*)0, NULL );
    }

    return nxtreq;
}

PJREQ
jrdp_get_nxt_nonblocking(void)
{
    if ( jrdp_receive( 0, 0 ) )
    {
        printf( "jrdp: receive request failed.\n" );
        exit(-1);
    }

    if ( jrdp_pendingQ )
    {
        //EXTERN_MUTEXED_LOCK(jrdp_pendingQ);
        if ( jrdp_pendingQ )
        {
            PJREQ nxtreq = jrdp_pendingQ;

            EXTRACT_ITEM( nxtreq, jrdp_pendingQ );
            --jrdp_pendingQ_len;
            //if ( nxtreq->priority > 0 ) jrdp_priorityQ_len--;
            //EXTERN_MUTEXED_UNLOCK(jrdp_pendingQ);

            //nxtreq->svc_start_time = jrdp__gettimeofday();
            //EXTERN_MUTEXED_LOCK(jrdp_runQ);
            APPEND_ITEM( nxtreq, jrdp_runQ );
            //EXTERN_MUTEXED_UNLOCK(jrdp_runQ);

            return nxtreq;
        }
        //EXTERN_MUTEXED_UNLOCK(jrdp_pendingQ);
    }

    return NULL;
}

int
jrdp_headers( PJREQ req )
{
    PJPACKET ptmp;

    for ( ptmp = req->trns; ptmp; ptmp = ptmp->next )
    {
        int old_hlength;    /* Old header length */
        int new_hlength;    /* New header length, whatever it may be.  */
        u_int16_t ftmp;	/* Temporary for values of fields     */
        //int stamp_window_size = ( req->flags & ARDP_FLAG_SEND_MY_WINDOW_SIZE ) && ( ptmp->seq == 1 );
        //int stamp_priority = ( req->priority || jrdp_priority ) && ( ptmp->seq == 1 );
        //int request_queue_status = jrdp_config.client_request_queue_status && ( ptmp->seq == 1 );
        int stamp_message_npkts = (ptmp->seq == 1);

        /* if it is the last packet, set ACK bit.  */
        /* If it is the last packet of the next window to be sent, set ACK. */
        /* Otherwise unset ACK bit. */
        int set_ack_bit = !ptmp->next || ( req->pwindow_sz && ( ptmp->seq == req->pwindow_sz + req->prcvd_thru ) );

        old_hlength = ptmp->text - ptmp->start;

        /* XXX Should do further tests to make sure all packets present */
        if ( ptmp->seq == 0 )
        {
            printf( "jrdp: sequence number not set in packet.\n" );
            exit(-1);
        }

        if ( /*request_queue_status || */stamp_message_npkts || 
                /*stamp_priority || stamp_window_size || */req->rcvd_thru )
            new_hlength = 11;
        else
            new_hlength = 9;

        //if ( stamp_priority )
            //new_hlength += 2;
        //if ( stamp_window_size )
            //new_hlength += 2;
        if ( stamp_message_npkts )
            new_hlength += 2;

        /* Allocate space for the new header */
        ptmp->start = ptmp->text - new_hlength;
        ptmp->length += new_hlength - old_hlength;

        /* Fill out the header */
        ptmp->start[0] = (char)129;
        //ptmp->start[1] = ptmp->context_flags; /* fix this when doing security context */
        ptmp->start[2] = 0; /* flags1 */
        if ( set_ack_bit ) 
            ptmp->start[2] |= 0x01;
        if ( stamp_message_npkts )
            ptmp->start[2] |= 0x04;
        //if ( stamp_priority )
            //ptmp->start[2] |= 0x08;
        //if ( stamp_window_size )
            //ptmp->start[2] |= 0x20;

        /* Octet 3: one option */
        //if ( request_queue_status)
            //ptmp->start[3] = (unsigned char) 253; /* no arguments */
        //else
            ptmp->start[3] = 0;

        ptmp->start[4] = (char)new_hlength;
        /* Connection ID (octets 5 & 6) */
        memcpy( ptmp->start + 5, &(req->cid), sizeof(char)*2 );
        /* Sequence number (octets 7 & 8) */
        ftmp = htons(ptmp->seq);
        memcpy( ptmp->start + 7, &ftmp, sizeof(char)*2 );

        if ( new_hlength > 9 )
        {
            char* optiondata = ptmp->start + 11; /* where options go */
            /* Received through (octets 9 & 10) */
            ftmp = htons( req->rcvd_thru );
            memcpy( ptmp->start + 9, &ftmp, sizeof(char)*2 );
            if ( stamp_message_npkts )
            {
                ftmp = htons(req->trns_tot);
                memcpy( optiondata, &ftmp,sizeof(char)*2 );
                optiondata += 2;
            }
            /*
            if ( stamp_priority )
            {
                if ( req->priority )
                    ftmp = htons(req->priority);
                else
                    ftmp = htons(jrdp_priority);
                memcpy( optiondata, &ftmp,sizeof(char)*2 );
                optiondata += 2;
            }
            if ( stamp_window_size )
            {
                ftmp = htons(req->window_sz);
                memcpy( optiondata, &ftmp, sizeof(char)*2 );
                optiondata += 2;
            }
            */
            assert( optiondata == ptmp->start + new_hlength );
        }
    }

    return 0;
}

int
jrdp_send( PJREQ req, const char* dname, struct sockaddr_in* dest, int ttwait )
{
    PJPACKET ptmp;

    if ( dname )
    {
        int dname_sz = strlen(dname);

        if ( req->peer_hostname == NULL || strlen(req->peer_hostname) < dname_sz )
        {
            free(req->peer_hostname);
            req->peer_hostname = (char*)malloc( sizeof(char)*dname_sz + 1 );
        }
        strcpy( req->peer_hostname, dname );
    }

    if ( jrdp_sock < 0 )
    {
        if ( jrdp_init() )
        {
            printf( "jrdp_send init failed.\n" );
            exit(-1);
        }
    }

    if ( req->status == JRDP_STATUS_FREE )
    {
        printf( "Attempt to send free request.\n" );
        exit(-1);
    }

    while ( req->outpkt )
    {
        req->outpkt->seq = ++(req->trns_tot);
        ptmp = req->outpkt;
        EXTRACT_ITEM( ptmp, req->outpkt );
        APPEND_ITEM( ptmp, req->trns );
    }

    /* Assign connection ID */
    req->cid = 13148; // jrdp_next_cid();

    if ( !dest || ( dest->sin_addr.s_addr == 0 ) )
    {
        if ( dname == NULL || *dname == '\0' )
        {
            printf( "No peer for sender.\n" );
            exit(-1);
        }

        if ( jrdp_hostname2name_addr( dname, NULL, &(req->peer) ) )
        {
            printf( "Bad host name.\n" );
            exit(-1);
        }
    }
    else
        memcpy( &(req->peer), dest, sizeof(struct sockaddr_in) );

    if ( req->peer.sin_port == 0 )
    {
        printf( "No port to use.\n" );
        exit(-1);
    }

    if ( dest && dest->sin_addr.s_addr == 0 )
        memcpy( dest, &(req->peer), sizeof(struct sockaddr_in) );

    if ( jrdp_headers(req) )
    {
        printf( "Add packet head failed.\n" );
        exit(-1);
    }
    req->status = JRDP_STATUS_ACTIVE;

    //EXTERN_MUTEXED_LOCK(jrdp_activeQ);
    APPEND_ITEM( req, jrdp_activeQ );
    ++jrdp_activeQ_len;
    req->wait_till = jrdp__addtimes( jrdp__gettimeofday(), req->timeout_adj );
    if ( jrdp_xmit( req, req->pwindow_sz ) )
    {
        printf( "Xmit packets failed.\n" );
        exit(-1);
    }
    //EXTERN_MUTEXED_UNLOCK(jrdp_activeQ);

    if ( ttwait )
        return 1;//jrdp_retrieve(req,ttwait);
    else 
        return 999; // This request is still pending.
}

int
jrdp_receive( int timeout_sec, int timeout_usec )
{
    struct sockaddr_in  from;
    int                 addr_struct_len;
    int                 n = 0;
    PJPACKET            pkt;
    unsigned char       flags1;
    unsigned char       flags2;
    u_int16_t           pid;    /* protocol ID for higher-level protocol. This is currently ignored after being set. */
    int                 qpos; /* Position of new req in queue       */
    int                 dpos; /* Position of dupe in queue          */
    PJREQ               creq; /* Current request                    */
    PJREQ               treq; /* Temporary request pointer          */
    PJREQ               nreq; /* New request pointer                */
    PJREQ               areq = NULL; /* Request needing ack        */
    PJREQ               match_in_runQ = NULL; /* if match found in runq for completed request. */
    int                 ack_bit_set; /* ack bit set on packet we're processing? */
    char*               ctlptr;
    u_int16_t           stmp;
    int                 tmp;
    int                 check_for_ack = 1;
    fd_set              readfds; /* used for select */
    struct timeval      now = bogustime; /* Time - used for retries  */
    struct timeval      rr_time = zerotime; /* Time last retrans from done queue */
    struct timeval      time_out = bogustime;


 check_for_more:

    time_out.tv_sec = timeout_sec;
    time_out.tv_usec = timeout_usec;
    now = jrdp__gettimeofday();

    FD_ZERO( &readfds );
    FD_SET( jrdp_srvsock, &readfds );
    if ( jrdp_prvsock != -1 )
        FD_SET( jrdp_prvsock, &readfds );

    tmp = select( max( jrdp_srvsock, jrdp_prvsock ) + 1, &readfds, (fd_set*)0, (fd_set*)0, &time_out );

    if ( tmp == 0 )
    {
        if ( areq )
        {
            jrdp_acknowledge(areq);
            areq = NULL;
        }
        return 0;
    }

    if ( tmp < 0 )
    {
        printf( "jrdp: receive listening failed.\n" );
        exit(-1);
    }

    creq = jrdp_reqalloc();
    pkt = jrdp_pktalloc();
    addr_struct_len = sizeof(struct sockaddr_in);

    if ( ( jrdp_prvsock >= 0 ) && FD_ISSET( jrdp_prvsock, &readfds ) )
        n = recvfrom( jrdp_prvsock, pkt->data, JRDP_PKT_LEN_R, 0, (struct sockaddr*)&from, &addr_struct_len );
    else
    {
        assert( FD_ISSET( jrdp_srvsock, &readfds ) );
        n = recvfrom( jrdp_srvsock, pkt->data, JRDP_PKT_LEN_R, 0, (struct sockaddr*)&from, &addr_struct_len );
    }

    if ( n <= 0 )
    {
        printf( "Bad recvfrom n = %d.\n", n );
        jrdp_reqfree(creq);
        jrdp_pktfree(pkt);
    }

    memcpy( &(creq->peer), &from, addr_struct_len );
    creq->cid = 0;
    creq->rcvd_time = now;
    creq->prcvd_thru = 0;

    pkt->start = pkt->data;
    pkt->length = n;
    *( pkt->start + pkt->length ) = '\0';
    pkt->seq = 1;


    /* Start of block of code handling JRDP headers. */
    assert( pkt->start[0] == (char)129 );

    int	hdr_len;
    flags1 = pkt->start[2];
    /* Was the ack bit set on the packet we just received? */
    ack_bit_set = flags1 & 0x01;
    flags2 = pkt->start[3];
    hdr_len = (unsigned) pkt->start[4]; /* header length */
    if ( hdr_len >= 7 ) // connection ID
    {
        memcpy( &stmp, pkt->start + 5, sizeof(char)*2 );
        if ( stmp )
            creq->cid = ntohs(stmp);
    }
    if ( hdr_len >= 9 ) // Packet sequence number
    {
        memcpy( &stmp, pkt->start + 7, sizeof(char)*2 );
        pkt->seq = ntohs(stmp);
    }
    else // No packet number specified, so this is the only one
    {
        pkt->seq = 1;
        creq->rcvd_tot = 1;
    }
    if ( hdr_len >= 11 ) // Received through
    {
        memcpy( &stmp, pkt->start + 9, sizeof(char)*2 ); // 0 means don't know
        if ( stmp )
            creq->prcvd_thru = ntohs(stmp);
    }

    printf( "Received packet (cid = %d; seq = %d of %d%s)", creq->cid, pkt->seq, creq->rcvd_tot, ack_bit_set ? "; ACK bit set" : "" );

    ctlptr = pkt->start + 11;
    /* **** Handle the octet 2 flags (flags1) : *** */
    /* Bit 0: ack bit: handled above */
    /* Bit 2: Total packet count specified in a 2-octet argument that follows. */
    if ( ( flags1 & 0x4 ) && ( ctlptr < pkt->start + hdr_len ) )
    {
        memcpy( &stmp, ctlptr, sizeof(char)*2 );
        creq->rcvd_tot = ntohs(stmp);
        ctlptr += 2;
    }
    /* Bit 3: Priority specified in a 2-octet argument  */
    /*
    if ( ( flags1 & 0x8 ) && ( ctlptr < pkt->start + hdr_len ) )
    {
        memcpy( &stmp, ctlptr, sizeof(char)*2 );
        creq->priority = ntohs(stmp);
        ctlptr += 2;
    }
    */
    /* Bit 4: protocol id specified in a 2-octet argument */
    if ( ( flags1 & 0x10 ) && ( ctlptr < pkt->start + hdr_len ) )
    {
        memcpy( &stmp, ctlptr, sizeof(char)*2 );
        pid = ntohs(stmp);
        ctlptr += 2;
    }
    if ( ctlptr < pkt->start + hdr_len ) // if still control info
    {
        /* Window size follows (2 octets of information). */
        if ( flags1 & 0x20 ) // Bit 5
        {
            memcpy( &stmp, ctlptr, sizeof(char)*2 );
            creq->pwindow_sz = ntohs(stmp);
            ctlptr += 2;
            if ( creq->pwindow_sz == 0 )
            {
                creq->pwindow_sz = 16;
                printf( "Client explicity reset window size to server default (%d)", 16 );
            }
            else
            {
                if ( creq->pwindow_sz > 256 )
                {
                    printf( "Client set window size to %d; server will limit it to %d", creq->pwindow_sz, 256 );
                    creq->pwindow_sz = 256;
                }
                else
                {
                    printf( "Client set window size to %d", creq->pwindow_sz );
                }
            }
        }
    }
    /* Bit 6: Wait time specified in a 2-octet argument.  The server ignores this flag; it is only sent by the server to the client. */
    /* Bit 7: OFLAGS, which means that octet 2 should be interpreted as an additional set of flags. No flags are currently defined; this is ignored. */
    /* **** Handle the octet 3 flags (flags2) : *** */
    if ( flags2 == 1 ) // cancel request
    {
        printf( "It's cancel flag.\n" );
        exit(-1);
    }
    /*
    if ( ( creq->priority < 0 ) && ( creq->priority != -42 ) )
    {
        printf( "Priority %d requested - ignored", creq->priority, 0 );
        creq->priority = 0;
    }
    */
    if ( pkt->seq == 1 )
        creq->rcvd_thru = max( creq->rcvd_thru, 1 );

    pkt->length -= hdr_len;
    pkt->start += hdr_len;
    pkt->text = pkt->start;
    /* End of block of code handling JRDP headers. */


    /* Done queue */
    //EXTERN_MUTEXED_LOCK(jrdp_doneQ);
    for ( treq = jrdp_doneQ; treq; treq = treq->next )
    {
	    if ( ( treq->cid == creq->cid ) && ( memcmp( (char*)&(treq->peer), (char *)&from, sizeof(from) ) == 0 ) )
        {
            /* Request is already on doneQ */
            /* flags2 lets us reset the received-through count. */
            if ( ( flags2 == 2 ) || creq->prcvd_thru > treq->prcvd_thru )
            {
                treq->prcvd_thru = creq->prcvd_thru;
                rr_time = zerotime; /* made progress, don't supress retransmission */
	        }
            else
            {
                /* We did not make progress; let's cut back on the */
                /* number of packets so we don't flood the */
                /* client.  This is identical to the client's */
                /* behavior; rationale documented in ardp_pr_actv.c */
                if ( treq->pwindow_sz > 4 )
                    treq->pwindow_sz /= 2;
            }
            nreq = treq;

            /* Retransmit reply if not completely received and if we didn't retransmit it this second  */
            if ( ( nreq->prcvd_thru != nreq->trns_tot ) && ( !jrdp__eqtimeval( rr_time, now ) || ( nreq != jrdp_doneQ ) ) )
            {
                PJPACKET tpkt; // Temporary pkt pointer
                printf( "Transmitting reply window from %d to %d (prcvd_thru = %d of %d total response size (trns_tot))", nreq->prcvd_thru + 1, min( nreq->prcvd_thru + nreq->pwindow_sz, nreq->trns_tot ), nreq->prcvd_thru, nreq->trns_tot, 0 );
                /* Transmit a window's worth of outstanding packets */
                for ( tpkt = nreq->trns; tpkt; tpkt = tpkt->next )
                {
                    if ( ( tpkt->seq <= ( nreq->prcvd_thru + nreq->pwindow_sz ) ) && ( ( tpkt->seq == 0 ) || ( tpkt->seq > nreq->prcvd_thru ) ) )
                    {
                        int ack_bit;
                        //jrdp_header_ack_rwait( tpkt, nreq, ack_bit = ( tpkt->seq == ( nreq->prcvd_thru + nreq->pwindow_sz ) || ( pkt->seq == nreq->trns_tot && nreq->svc_rwait_seq > nreq->prcvd_thru ) ), ( nreq->svc_rwait_seq > nreq->prcvd_thru ) );
                        if ( ack_bit )
                            printf( "Pkt %d will request an ACK", tpkt->seq );
                        jrdp_snd_pkt( tpkt, nreq );
                    }
                }
                rr_time = now; /* Remember time of retransmission */
            }
            /* Move matched request to front of queue */
            /* nreq is definitely in jrdp_doneQ right now. */
            /* This replaces much icky special-case code that used to be here
               -- swa, Feb 9, 1994 */
            EXTRACT_ITEM( nreq, jrdp_doneQ );
            PREPEND_ITEM( nreq, jrdp_doneQ );
            assert( !creq->rcvd );
            jrdp_reqfree(creq);
            jrdp_pktfree(pkt);
            //EXTERN_MUTEXED_UNLOCK(jrdp_doneQ);
            goto check_for_more;
        }

        /* While we're checking the done queue also see if any
         * replies require follow up requests for acknowledgments
         * This is currently used when the server has requested that the client
         * wait for a long time, and then has rescinded that wait request
         * (because the message is done).  Such a rescinding of the wait
         * request should be received by the client, and the server is
         * expecting an ACK. */
        if ( check_for_ack && ( treq->svc_rwait_seq > treq->prcvd_thru ) && ( treq->retries_rem > 0 ) && jrdp__timeislater( now, treq->wait_till ) )
        {
            printf( "Requested ack not received - pinging client (%d of %d/%d ack)", treq->prcvd_thru, treq->svc_rwait_seq, treq->trns_tot, 0 );
            /* Resend the final packet only - to wake the client  */
            if ( treq->trns )
                jrdp_snd_pkt( treq->trns->prev, treq );
            jrdp__adjust_backoff( &(treq->timeout_adj) );
            treq->wait_till = jrdp__addtimes( treq->timeout_adj, now );
            treq->retries_rem--;
        }
    }
    //EXTERN_MUTEXED_UNLOCK(jrdp_doneQ);
    check_for_ack = 0; /* Only check once per call to jrdp_receive */


    /* If unsequenced control packet free it and check for more */
    if ( pkt->seq == 0 )
    {
        jrdp_reqfree(creq);
        jrdp_pktfree(pkt);
        goto check_for_more;
    }

    assert( !creq->rcvd );
    /* Check if incomplete and match up with other incomplete requests */
    if ( creq->rcvd_tot != 1 )
    {
        /* Look for rest of request */
        for ( treq = jrdp_partialQ; treq; treq = treq->next)
        {
            if ( ( treq->cid != 0 ) && ( treq->cid == creq->cid ) && ( memcmp( (char*)&(treq->peer), (char*)&from, addr_struct_len ) == 0 ) )
            {
                PJPACKET tpkt;
                jrdp_update_cfields( treq, creq );
                if ( creq->rcvd_tot )
                    treq->rcvd_tot = creq->rcvd_tot;
                /* We now have no further use for CREQ, since we need no more information from it. */
                jrdp_reqfree(creq);
                creq = NULL;
                tpkt = treq->rcvd;
                while ( tpkt )
                {
                    if ( tpkt->seq == treq->rcvd_thru + 1 )
                        treq->rcvd_thru++;
                    if ( tpkt->seq == pkt->seq )
                    {
                        /* Duplicate - discard */
                        printf( "Multi-packet duplicate received (pkt %d of %d)", pkt->seq, treq->rcvd_tot );
                        if ( ack_bit_set && areq != treq )
                        {
                            if ( areq )
                                jrdp_acknowledge(areq);
                            areq = treq;
                        }
                        jrdp_pktfree(pkt);
                        goto check_for_more;
                    }
                    if ( tpkt->seq > pkt->seq )
                    {
                        /* Insert new packet in rcvd */
                        INSERT_NEW_ITEM1_AFTER_ITEM2_IN_LIST(pkt, tpkt, treq->rcvd);
                        if ( pkt->seq == treq->rcvd_thru + 1 )
                            treq->rcvd_thru++;
                        while ( tpkt && ( tpkt->seq == treq->rcvd_thru + 1 ) )
                        {
                            treq->rcvd_thru++;
                            tpkt = tpkt->next;
                        }
                        pkt = NULL;
                        break;
                    }
                    tpkt = tpkt->next;
                }

                if ( pkt )
                {
                    /* Append at end of rcvd */
                    APPEND_ITEM( pkt, treq->rcvd );
                    if ( pkt->seq == treq->rcvd_thru + 1 )
                        treq->rcvd_thru++;
                    pkt = NULL;
                }

                if ( treq->rcvd_tot && ( treq->rcvd_thru == treq->rcvd_tot ) )
                {
                    if ( areq == treq )
                        areq = NULL;
                    creq = treq;
                    EXTRACT_ITEM( creq, jrdp_partialQ );
                    --jrdp_partialQ_len;
                    printf( "Multi-packet request complete" );
                    goto req_complete;
                }
                else
                {
                    if ( ack_bit_set && areq != treq )
                    {
                        if ( areq )
                            jrdp_acknowledge(areq);
                        areq = treq;
                    }
                    printf( "Multi-packet request continued (rcvd %d of %d)", treq->rcvd_thru, treq->rcvd_tot );
                    goto check_for_more;
                }
                internal_error( "Should never get here." );
            }
        }

        /* This is the first packet we received in the request */
        /* log it and queue it and check for more              */
        /* Acknowledge it if an ack requested (i.e., tiny windows). */
        if ( ack_bit_set && areq != creq )
        {
            if ( areq )
                jrdp_acknowledge(areq);
            areq = creq;
            /* XXX note that this code will blow up if the incomplete request
            pointed to by AREQ is dropped before the ACK is sent.  This
            should be fixed, but won't be right now. */
        }
        printf( "Multi-packet request received (pkt %d of %d)", pkt->seq, creq->rcvd_tot );
        APPEND_ITEM( pkt, creq->rcvd );
        APPEND_ITEM( creq, jrdp_partialQ );
        if ( ++jrdp_partialQ_len > jrdp_partialQ_max_len )
        {
            treq = jrdp_partialQ;
            EXTRACT_ITEM( treq, jrdp_partialQ );
            jrdp_partialQ_len--;
            printf( "Too many incomplete requests - dropped (rthru %d of %d)", treq->rcvd_thru, treq->rcvd_tot );
            jrdp_reqfree(treq);
        }
	    goto check_for_more;
    }

    printf( "Received JRDP packet" );

 req_complete:

    /* A complete multi-packet request has been received or a single-packet
       request (always complete) has been received. */
    /* At this point, creq refers to an RREQ structure that has either just
       been removed from the jrdp_partialQ or was never put on a queue. */

    qpos = 0;
    dpos = 1;

    /* Insert this message at end of pending but make sure the request is not already in pending */
    nreq = creq;
    creq = NULL;

    if ( pkt )
    {
        /* Receive a single-packet request. */
        nreq->rcvd_tot = 1;
        APPEND_ITEM( pkt, nreq->rcvd );
        pkt = NULL;
    }

    /*
     * nreq now refers to a freestanding JREQ structure (a message) that 
     * is complete but on no queue.  We search for a match.
     */

    /* First search for a match in the runQ (a match that is being run) */
    //EXTERN_MUTEXED_LOCK(jrdp_runQ);
    for ( match_in_runQ = jrdp_runQ; match_in_runQ; match_in_runQ = match_in_runQ->next )
    {
        if ( ( match_in_runQ->cid == nreq->cid ) && ( memcmp( (char*)&(match_in_runQ->peer), (char*)&(nreq->peer), addr_struct_len ) == 0 ) )
        {
            /* Request is running right now */
            jrdp_update_cfields( match_in_runQ, nreq );
            printf( "Duplicate discarded (presently executing)" );
            jrdp_reqfree(nreq);
            nreq = match_in_runQ;
            break; /* leave match_in_runQ set to nreq  */
        }
    }
    //EXTERN_MUTEXED_UNLOCK(jrdp_runQ);

    /* XXX this code could be simplified and improved -- STeve & Katia, 9/26/96 */
    if ( match_in_runQ )
    {
        /* do nothing; handled above. */
    }
    else
    {
        //EXTERN_MUTEXED_LOCK(jrdp_pendingQ);
        if ( jrdp_pendingQ )
        {
            int keep_looking = 1; /* Keep looking for dup. Initially say to keep on looking. */
            for ( treq = jrdp_pendingQ; treq; )
            {
                if ( ( treq->cid == nreq->cid ) && ( memcmp( (char*)&(treq->peer), (char*)&(nreq->peer), addr_struct_len ) == 0 ) )
                {
                    /* Request is already on queue */
                    jrdp_update_cfields( treq, nreq );
                    printf( "Duplicate discarded (%d of %d)", dpos, jrdp_pendingQ_len );
                    jrdp_reqfree(nreq);
                    nreq = treq;
                    keep_looking = 0; /* We found the duplicate */
                    break;
                }
                /*
                if ( ( jrdp_pri_override && jrdp_pri_func && ( jrdp_pri_func( nreq, treq ) < 0 ) ) || ( !jrdp_pri_override && ( ( nreq->priority < treq->priority ) || ( ( treq->priority == nreq->priority ) && jrdp_pri_func && ( jrdp_pri_func( nreq, treq ) < 0 ) ) ) ) )
                {
                    INSERT_NEW_ITEM1_BEFORE_ITEM2_IN_LIST( nreq, treq, jrdp_pendingQ );
                    jrdp_pendingQ_len++;
                    if ( nreq->priority > 0 )
                        jrdp_priorityQ_len++;
                    LOG_PACKET(nreq, dpos);
                    qpos = dpos;
                    keep_looking = 1;  // There may still be a duplicate
                    break;
                }
                */
                if ( !treq->next )
                {
                    APPEND_ITEM( nreq, jrdp_pendingQ );
                    jrdp_pendingQ_len++;
                    //if ( nreq->priority > 0 )
                        //jrdp_priorityQ_len++;
                    //LOG_PACKET( nreq, dpos + 1 );
                    qpos = dpos + 1;
                    keep_looking = 0; // Nothing more to check
                    break;
                }
                treq = treq->next;
                dpos++;
            }
            /* Save queue position to send to client if appropriate */
            qpos = dpos;
            /* If not added at end of queue, check behind packet for dup */
            if ( keep_looking )
            {
                /* This block is related to priority. */
                while ( treq )
                {
                    if ( ( treq->cid == nreq->cid ) && ( memcmp( (char*)&(treq->peer), (char*)&(nreq->peer), addr_struct_len ) == 0 ) )
                    {
                        /* Found a dup */
                        printf( "Duplicate replaced (removed at %d)", dpos );
                        jrdp_pendingQ_len--;
                        //if ( treq->priority > 0 )
                            //jrdp_priorityQ_len--;
                        EXTRACT_ITEM( treq, jrdp_pendingQ );
                        jrdp_reqfree(treq);
                        break;
                    }
                    treq = treq->next;
                    dpos++;
                }
            }
        }
        else
        {
            nreq->next = NULL;
            jrdp_pendingQ = nreq;
            jrdp_pendingQ->prev = nreq;
            jrdp_pendingQ_len++;
            //if ( nreq->priority > 0 )
                //jrdp_priorityQ_len++;
            //LOG_PACKET(nreq, dpos);
            qpos = dpos;
        }
        //EXTERN_MUTEXED_UNLOCK(jrdp_pendingQ);
    }

    /* Fill in queue position and system time */
    nreq->inf_queue_pos = qpos;

    /* Here we can perform additional processing on a newly-received ARDP
       request.  (This includes additional processing on a newly-received
       request that was a duplicate of a previous request.)
       At this point, 'nreq' is a variable referring to the request that we've
       just received.  The Prospero directory service takes advantage of this
       to request an additional wait for heavily-loaded databases. */
    //if ( ardp_newly_received_additional )
        //(*ardp_newly_received_additional)(nreq);
    /* Each time we receive a  completed request, 
       including a duplicate of a request already on one of the queues, we tell
       the client about our backoff.  This is how the archie servers have
       historically done it. */ 
    //if ( ardp_config.wait_time )
        //ardp_rwait( nreq, ardp_config.wait_time, nreq->inf_queue_pos, nreq->inf_sys_time );

    goto check_for_more;

    return 0;
}

int
jrdp_xmit( PJREQ req, int window )
{
    PJPACKET        ptmp;
    u_int16_t       stmp;
    int             ns;	// Number of bytes actually sent.
    PJPACKET        ack = NULL; // Only an ack to be sent.

    if ( window < 0 || req->prcvd_thru >= req->trns_tot )
    {
        /* All our packets got through, send acks only */
        if ( req->prcvd_thru > req->trns_thru )
            printf( "Our peer appears to be confused: it thinks we sent it %d packets; we only have sent through %d. Proceeding as well as we can.\n", req->prcvd_thru, req->trns_tot );

        ack = jrdp_pktalloc();

        /* add header */
        ack->start -= 11;
        ack->length += 11;
        ack->start[0] = 129; /* version # */
        ack->start[1] = '\0'; /* context bits; protected bits -- unused  */
        ack->start[2] = ack->start[3] = '\0'; /* flags & options; all unset */
        ack->start[4] = 11; /* header length */
        /* Connection ID */
        memcpy( ack->start + 5, &(req->cid), sizeof(char)*2 );
        ack->start[7] = ack->start[8] = 0; /* Sequence # is 0 (unsequenced control pkt) */
        /* Received Through */
        stmp = htons(req->rcvd_thru);
        memcpy( ack->start + 9 , &stmp, sizeof(char)*2 );

        ptmp = ack;
    }
    else
    {
        /* don't send acks yet; more to deliver */
        ptmp = req->trns;
    }

    /* Note that we don't want to get rid of packts before the */
    /* peer received through since the peer might later tell   */
    /* us it forgot them and ask us to send them again         */
    /* XXX whether this is allowable should be an application  */
    /* specific configration option.                           */
    while ( ptmp )
    {
        if ( (window > 0 ) && ( ptmp->seq > req->prcvd_thru + window ) )
            break;

        if ( ( ptmp->seq == 0 ) || ( ptmp->seq > req->prcvd_thru ) )
        {
            printf( "Sending message%s (cid=%d) (seq=%d)", (ptmp == ack) ? " (ACK only)" : "", ntohs(req->cid), ntohs(ptmp->seq) );
            if ( req->peer.sin_family == AF_INET )
            {
                printf( " to");
                if ( req->peer_hostname && *req->peer_hostname )
                    printf( " %s", req->peer_hostname);

                printf( " [%s(%d)]", inet_ntoa(req->peer.sin_addr), ntohs(req->peer.sin_port) );
            }
            printf( "...");

            ns = sendto( jrdp_sock,(char*)(ptmp->start), ptmp->length, 0, (struct sockaddr*)&(req->peer), sizeof(struct sockaddr_in) );

            if ( ns != ptmp->length )
            {
                printf( "jrdp: error in sendto(): sent sent only %d bytes of the %d byte message.\n", ns, ptmp->length );
                if ( ack )
                    jrdp_pktfree(ack);
                return 1;
            }

            printf( "...Sent.\n" );

            if ( req->trns_thru < ptmp->seq )
                req->trns_thru = ptmp->seq;
        }

        ptmp = ptmp->next;
    }

    if (ack)
       jrdp_pktfree(ack);

    return 0;
}

int
jrdp_init(void)
{
    int tmp; // For stepping through ports; also temp dummy value.

    if ( jrdp_sock != -1 )
    {
        close(jrdp_sock);
        jrdp_sock = -1;
        //printf( "jrdp_init(): closing port # %d; opening new one...", ntohs(jrdp_client_address_port.sin_port) );
        memset( &jrdp_client_address_port, '\000', sizeof(struct sockaddr_in) );
    }

    //ardp__set_def_port_no();

    /* Open the local socket from which packets will be sent */
    if ( (jrdp_sock = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
    {
        printf( "jrdp: Can't open client's sending socket.\n" );
        exit(-1);
    }

    /* Now bind the port. */
    memset( &jrdp_client_address_port, '\000', sizeof(struct sockaddr_in) );
    jrdp_client_address_port.sin_family = AF_INET;
    jrdp_client_address_port.sin_addr.s_addr = myaddress();
    if ( bind( jrdp_sock, (struct sockaddr*)&jrdp_client_address_port, sizeof(struct sockaddr_in) ) )
    {
        printf( "JRDP: bind() completed with error: client address(port) are: %s(%d)", inet_ntoa(jrdp_client_address_port.sin_addr), ntohs(jrdp_client_address_port.sin_port) );
        close(jrdp_sock);
        jrdp_sock = -1;
        memset( &jrdp_client_address_port, '\000', sizeof(struct sockaddr_in));
        exit(-1);
    }

    /* OK, we now have successfully bound, either to a prived or non-priv'd port. */
    memset( &jrdp_client_address_port, 0, sizeof(struct sockaddr_in));
    tmp = sizeof(struct sockaddr_in);
    /* Returns 0 on success, -1 on failure. */
    if ( getsockname( jrdp_sock, (struct sockaddr*)&jrdp_client_address_port, &tmp) )
    {
        printf( "JRDP: getsockname() completed with error: client address(port) are: %s(%d)", inet_ntoa(jrdp_client_address_port.sin_addr), ntohs(jrdp_client_address_port.sin_port) );
        close(jrdp_sock);
        jrdp_sock = -1;
        memset( &jrdp_client_address_port, '\000', sizeof(struct sockaddr_in) );
        exit(-1);
    }

    return 0;
}

struct timeval
jrdp__gettimeofday(void)
{
    struct timeval now;
    int tmp = gettimeofday( &now, NULL );
    if ( tmp )
    {
        printf( "gettimeofday() returned error code; can't happen!\n" );
        exit(-1);
    }
    return now;
}

int
jrdp__eqtimeval( const struct timeval t1, const struct timeval t2 )
{
    return t1.tv_sec == t2.tv_sec && t1.tv_usec == t2.tv_usec;
}

int 
jrdp__timeislater( struct timeval t1, struct timeval t2 )
{
    if ( jrdp__eqtimeval( t1, bogustime ) || jrdp__eqtimeval( t2, bogustime ) )
    {
        printf( "Time compare error.\n" );
        exit(-1);
    }

    if ( jrdp__eqtimeval( t2, infinitetime ) )
        return jrdp__eqtimeval( t1, infinitetime );
    if ( jrdp__eqtimeval( t1, infinitetime ) )
        return 1;
    return ( ( t1.tv_sec > t2.tv_sec ) || ( ( t1.tv_sec == t2.tv_sec ) && ( t1.tv_usec >= t2.tv_usec ) ) );
}

struct timeval
jrdp__addtimes( struct timeval t1, struct timeval t2 )
{
    struct timeval retval;

    if ( jrdp__eqtimeval( t1, bogustime) || jrdp__eqtimeval( t2, bogustime ) )
        return bogustime;
    retval.tv_sec = t1.tv_sec + t2.tv_sec;
    retval.tv_usec = t1.tv_usec + t2.tv_usec;
    if ( retval.tv_usec >= UFACTOR )
    {
        retval.tv_usec -= UFACTOR;
        ++(retval.tv_sec);
    }
    return retval;
}

void
jrdp__adjust_backoff( struct timeval* tv )
{
    tv->tv_sec = JRDP_BACKOFF(tv->tv_sec);
    tv->tv_usec = JRDP_BACKOFF(tv->tv_usec);
    while ( tv->tv_usec >= UFACTOR )
    {
        tv->tv_usec -= UFACTOR;
        ++(tv->tv_sec);
    }
}

void
jrdp_update_cfields( PJREQ existing, PJREQ new )
{
    if ( new->prcvd_thru > existing->prcvd_thru )
        existing->prcvd_thru = new->prcvd_thru;
}

int
jrdp_acknowledge( PJREQ req )
{
    return 0;
}

int
jrdp_snd_pkt( PJPACKET pkt, PJREQ req )
{
    int n;
    printf( "Sending pkt %d.", pkt->seq );
    n = sendto( ( ( jrdp_prvsock != -1 ) ? jrdp_prvsock : jrdp_srvsock ), pkt->start, pkt->length, 0, (struct sockaddr*)&(req->peer), addr_struct_len );
    if ( n == pkt->length )
        return 0;
    printf( "Attempt to send message failed; sent %d bytes of %d", n, pkt->length );
    return 1;
}

void
jrdp_header_ack_rwait( PJPACKET pkt, PJREQ req, int is_ack_needed, int is_rwait_needed )
{
    int new_hlength;
    int old_hlength;
    u_int16_t stmp;
    old_hlength = tpkt->text - tpkt->start;

}
