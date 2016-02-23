
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "rudp.h"

//u_int16_t global_cid = 13148;
int rudp_jlstner_count = 0;
int rudp_jsnder_count = 0;
int jreq_count = 0;
int jpkt_count = 0;
//int jrdp_srvsock = -1;
//int jrdp_prvsock = -1;
int jrdp_priority = 0;
int jrdp_sock = -1;
static char* myhname = NULL;
static long myhaddr = 0L;
struct sockaddr_in jrdp_client_address_port;
//struct sockaddr_in peer_addr;

PJLISTENER  rudp_lstnerQ = NULL;
PJSENDER    rudp_snderQ =NULL;
//PJREQ jrdp_activeQ = NULL;
//PJREQ jrdp_completeQ = NULL;
//PJREQ jrdp_pendingQ = NULL;
//PJREQ jrdp_partialQ = NULL;
//PJREQ jrdp_runQ = NULL;
//PJREQ jrdp_doneQ = NULL;

//int jrdp_activeQ_len = 0;
//int jrdp_pendingQ_len = 0;
//int jrdp_partialQ_len = 0;
//int jrdp_partialQ_max_len = 20;
//int jrdp_replyQ_len = 0;
//int jrdp_replyQ_max_len = 20;

const struct timeval zerotime = { 0, 0 };
const struct timeval infinitetime = { -1, -1 };
const struct timeval bogustime = { -2, -2 };

int (*jrdp__accept)( PJLISTENER, int, int ) = NULL;

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
#ifdef JRDP_MY_WINDOW_SZ
    req->flags = JRDP_FLAG_SEND_MY_WINDOW_SIZE; /*  used by clients. */
#else
    req->flags = 0;
#endif
    req->cid = 0;
    req->outpkt = NULL;
    req->inpkt = NULL;
    req->comp_thru = NULL;
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
    //char*       newline;        /* last nl in string       */
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
    struct hostent* hp; /* Remote host we're connecting to. */
    char* hostname = (char*)malloc( sizeof(char) * ( strlen(hostname_arg) + 1 ) );
    strcpy( hostname, hostname_arg ); // working copy
    struct sockaddr_in* hostaddr = hostaddr_arg; // working copy

    char* openparen; /* for parsing hostname string. */
    int req_udp_port = 0; /* port requested, HOST byte order */

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

void
set_haddr(void)
{
    /* See above for explanation.  EWWWWWWW */
    //static int weird_Solaris_2_5_1_gethostname_bug_demonstrated = 0;

    /* This means bugs that haven't appeared yet, but might. */
    //static int even_weirder_gethostname_bug_demonstrated = 0;

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

/*
int
jrdp_bind_port( const char* portname )
{
    printf( "\nEntering jrdp_bind_port()\n" );
    struct sockaddr_in s_in = {AF_INET};
    struct servent* sp;

    int             on = 1;
    int             port_no = 0;

    jrdp__accept = jrdp_accept;

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
*/

PJREQ
jrdp_get_nxt_blocking( int sock )
{
    PJREQ   nxtreq = NULL;
    fd_set  readfds;
    int     tmp;

    PJLISTENER lstner = rudp_find_matched_lstner(sock);

    while ( 1 )
    {
        if ( ( nxtreq = jrdp_get_nxt_nonblocking(sock) ) )
            break;

        FD_ZERO(&readfds);
        if ( lstner->sock != -1 )
            FD_SET( lstner->sock, &readfds );
        if ( lstner->prvsock != -1)
            FD_SET( lstner->prvsock, &readfds );
        tmp = select( max( lstner->sock, lstner->prvsock ) + 1, &readfds, (fd_set*)0, (fd_set*)0, NULL );

        if ( tmp < 0 )
        {
            printf( "jrdp: receive listening failed.\n" );
            exit(-1);
        }
    }

    return nxtreq;
}

PJREQ
jrdp_get_nxt_nonblocking( int sock )
{
    PJLISTENER lstner = rudp_find_matched_lstner(sock);
    if ( jrdp_accept( lstner, 0, 0 ) )
    {
        printf( "jrdp: receive request failed.\n" );
        exit(-1);
    }

    if ( lstner->pendingQ )
    {
        //EXTERN_MUTEXED_LOCK(lstner->pendingQ);
        if ( lstner->pendingQ )
        {
            PJREQ nxtreq = lstner->pendingQ;

            EXTRACT_ITEM( nxtreq, lstner->pendingQ );
            --lstner->pendingQ_len;
            //if ( nxtreq->priority > 0 ) jrdp_priorityQ_len--;
            //EXTERN_MUTEXED_UNLOCK(lstner->pendingQ);

            //nxtreq->svc_start_time = jrdp__gettimeofday();
            //EXTERN_MUTEXED_LOCK(lstner->runQ);
            APPEND_ITEM( nxtreq, lstner->runQ );
            //EXTERN_MUTEXED_UNLOCK(lstner->runQ);

            return nxtreq;
        }
        //EXTERN_MUTEXED_UNLOCK(lstner->pendingQ);
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
        u_int16_t ftmp; /* Temporary for values of fields     */
        //int stamp_window_size = ( req->flags & JRDP_FLAG_SEND_MY_WINDOW_SIZE ) && ( ptmp->seq == 1 );
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
jrdp_accept( PJLISTENER lstner, int timeout_sec, int timeout_usec )
{
    struct sockaddr_in  from;
    int                 addr_struct_len;
    int                 n = 0;
    PJPACKET            pkt;
    unsigned char       flags1;
    unsigned char       flags2;
    //u_int16_t           pid;  /* protocol ID for higher-level protocol. This is currently ignored after being set. */
    int                 qpos; /* Position of new req in queue */
    int                 dpos; /* Position of dupe in queue    */
    PJREQ               creq; /* Current request              */
    PJREQ               treq; /* Temporary request pointer    */
    PJREQ               nreq; /* New request pointer          */
    PJREQ               areq = NULL; /* Request needing ack   */
    PJREQ               match_in_runQ = NULL; /* if match found in runQ for completed request. */
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
    FD_SET( lstner->sock, &readfds );
    if ( lstner->prvsock != -1 )
        FD_SET( lstner->prvsock, &readfds );

    tmp = select( max( lstner->sock, lstner->prvsock ) + 1, &readfds, (fd_set*)0, (fd_set*)0, &time_out );

    if ( tmp == 0 )
    {
        if ( areq )
        {
            jrdp_acknowledge( lstner, areq );
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

    if ( ( lstner->prvsock >= 0 ) && FD_ISSET( lstner->prvsock, &readfds ) )
        n = recvfrom( lstner->prvsock, pkt->data, JRDP_PKT_LEN_R, 0, (struct sockaddr*)&from, (socklen_t*)&addr_struct_len );
    else
    {
        assert( FD_ISSET( lstner->sock, &readfds ) );
        n = recvfrom( lstner->sock, pkt->data, JRDP_PKT_LEN_R, 0, (struct sockaddr*)&from, (socklen_t*)&addr_struct_len );
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

    int hdr_len;
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
    /*
    if ( ( flags1 & 0x10 ) && ( ctlptr < pkt->start + hdr_len ) )
    {
        memcpy( &stmp, ctlptr, sizeof(char)*2 );
        pid = ntohs(stmp);
        ctlptr += 2;
    }
    */
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


    //EXTERN_MUTEXED_LOCK(lstner->doneQ);
    for ( treq = lstner->doneQ; treq; treq = treq->next )
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
                /*
                 * We did not make progress; let's cut back on the number of packets
                 * so we don't flood the client. This is identical to the client's behavior;
                 */
                if ( treq->pwindow_sz > 4 )
                    treq->pwindow_sz /= 2;
            }
            nreq = treq;

            /* Retransmit reply if not completely received and if we didn't retransmit it this second  */
            if ( ( nreq->prcvd_thru != nreq->trns_tot ) && ( !jrdp__eqtime( rr_time, now ) || ( nreq != lstner->doneQ ) ) )
            {
                PJPACKET tpkt; // Temporary pkt pointer
                printf( "Transmitting reply window from %d to %d (prcvd_thru = %d of %d total response size (trns_tot))", nreq->prcvd_thru + 1, min( nreq->prcvd_thru + nreq->pwindow_sz, nreq->trns_tot ), nreq->prcvd_thru, nreq->trns_tot );
                /* Transmit a window's worth of outstanding packets */
                for ( tpkt = nreq->trns; tpkt; tpkt = tpkt->next )
                {
                    if ( ( tpkt->seq <= ( nreq->prcvd_thru + nreq->pwindow_sz ) ) && ( ( tpkt->seq == 0 ) || ( tpkt->seq > nreq->prcvd_thru ) ) )
                    {
                        int ack_bit;
                        jrdp_header_ack_rwait( tpkt, nreq, ack_bit = ( tpkt->seq == ( nreq->prcvd_thru + nreq->pwindow_sz ) || ( pkt->seq == nreq->trns_tot && nreq->svc_rwait_seq > nreq->prcvd_thru ) ), ( nreq->svc_rwait_seq > nreq->prcvd_thru ) );
                        if ( ack_bit )
                            printf( "Pkt %d will request an ACK", tpkt->seq );
                        jrdp_snd_pkt( lstner, tpkt, nreq );
                    }
                }
                rr_time = now; /* Remember time of retransmission */
            }

            EXTRACT_ITEM( nreq, lstner->doneQ );
            PREPEND_ITEM( nreq, lstner->doneQ );
            assert( !creq->rcvd );
            jrdp_reqfree(creq);
            jrdp_pktfree(pkt);
            //EXTERN_MUTEXED_UNLOCK(lstner->doneQ);
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
            printf( "Requested ack not received - pinging client (%d of %d/%d ack)", treq->prcvd_thru, treq->svc_rwait_seq, treq->trns_tot );
            /* Resend the final packet only - to wake the client  */
            if ( treq->trns )
                jrdp_snd_pkt( lstner, treq->trns->prev, treq );
            jrdp__adjust_backoff( &(treq->timeout_adj) );
            treq->wait_till = jrdp__addtime( treq->timeout_adj, now );
            treq->retries_rem--;
        }
    }
    //EXTERN_MUTEXED_UNLOCK(lstner->doneQ);
    check_for_ack = 0; /* Only check once per call to jrdp_accept */


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
        for ( treq = lstner->partialQ; treq; treq = treq->next)
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
                                jrdp_acknowledge( lstner, areq );
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
                    EXTRACT_ITEM( creq, lstner->partialQ );
                    --lstner->partialQ_len;
                    printf( "Multi-packet request complete" );
                    goto req_complete;
                }
                else
                {
                    if ( ack_bit_set && areq != treq )
                    {
                        if ( areq )
                            jrdp_acknowledge( lstner, areq );
                        areq = treq;
                    }
                    printf( "Multi-packet request continued (rcvd %d of %d)", treq->rcvd_thru, treq->rcvd_tot );
                    goto check_for_more;
                }
                printf( "Should never get here." );
                exit(-1);
            }
        }

        /* This is the first packet we received in the request */
        /* log it and queue it and check for more              */
        /* Acknowledge it if an ack requested (i.e., tiny windows). */
        if ( ack_bit_set && areq != creq )
        {
            if ( areq )
                jrdp_acknowledge( lstner, areq );
            areq = creq;
            /* XXX note that this code will blow up if the incomplete request
            pointed to by AREQ is dropped before the ACK is sent. This
            should be fixed, but won't be right now. */
        }
        printf( "Multi-packet request received (pkt %d of %d)", pkt->seq, creq->rcvd_tot );
        APPEND_ITEM( pkt, creq->rcvd );
        APPEND_ITEM( creq, lstner->partialQ );
        if ( ++lstner->partialQ_len > lstner->partialQ_max_len )
        {
            treq = lstner->partialQ;
            EXTRACT_ITEM( treq, lstner->partialQ );
            lstner->partialQ_len--;
            printf( "Too many incomplete requests - dropped (rthru %d of %d)", treq->rcvd_thru, treq->rcvd_tot );
            jrdp_reqfree(treq);
        }
        goto check_for_more;
    }

    printf( "Received JRDP packet" );

 req_complete:

    /* A complete multi-packet request has been received or a single-packet
       request (always complete) has been received. */
    /* At this point, creq refers to an JREQ structure that has either just
       been removed from the lstner->partialQ or was never put on a queue. */

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
    //EXTERN_MUTEXED_LOCK(lstner->runQ);
    for ( match_in_runQ = lstner->runQ; match_in_runQ; match_in_runQ = match_in_runQ->next )
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
    //EXTERN_MUTEXED_UNLOCK(lstner->runQ);

    /* XXX this code could be simplified and improved -- STeve & Katia, 9/26/96 */
    if ( match_in_runQ )
    {
        /* do nothing; handled above. */
    }
    else
    {
        //EXTERN_MUTEXED_LOCK(lstner->pendingQ);
        if ( lstner->pendingQ )
        {
            int keep_looking = 1; /* Keep looking for dup. Initially say to keep on looking. */
            for ( treq = lstner->pendingQ; treq; )
            {
                if ( ( treq->cid == nreq->cid ) && ( memcmp( (char*)&(treq->peer), (char*)&(nreq->peer), addr_struct_len ) == 0 ) )
                {
                    /* Request is already on queue */
                    jrdp_update_cfields( treq, nreq );
                    printf( "Duplicate discarded (%d of %d)", dpos, lstner->pendingQ_len );
                    jrdp_reqfree(nreq);
                    nreq = treq;
                    keep_looking = 0; /* We found the duplicate */
                    break;
                }
                /*
                if ( ( jrdp_pri_override && jrdp_pri_func && ( jrdp_pri_func( nreq, treq ) < 0 ) ) || ( !jrdp_pri_override && ( ( nreq->priority < treq->priority ) || ( ( treq->priority == nreq->priority ) && jrdp_pri_func && ( jrdp_pri_func( nreq, treq ) < 0 ) ) ) ) )
                {
                    INSERT_NEW_ITEM1_BEFORE_ITEM2_IN_LIST( nreq, treq, lstner->pendingQ );
                    lstner->pendingQ_len++;
                    if ( nreq->priority > 0 )
                        lstner->priorityQ_len++;
                    LOG_PACKET(nreq, dpos);
                    qpos = dpos;
                    keep_looking = 1;  // There may still be a duplicate
                    break;
                }
                */
                if ( !treq->next )
                {
                    APPEND_ITEM( nreq, lstner->pendingQ );
                    lstner->pendingQ_len++;
                    //if ( nreq->priority > 0 )
                        //lstner->priorityQ_len++;
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
                        lstner->pendingQ_len--;
                        //if ( treq->priority > 0 )
                            //lstner->priorityQ_len--;
                        EXTRACT_ITEM( treq, lstner->pendingQ );
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
            lstner->pendingQ = nreq;
            lstner->pendingQ->prev = nreq;
            lstner->pendingQ_len++;
            //if ( nreq->priority > 0 )
                //lstner->priorityQ_len++;
            //LOG_PACKET(nreq, dpos);
            qpos = dpos;
        }
        //EXTERN_MUTEXED_UNLOCK(lstner->pendingQ);
    }

    /* Fill in queue position and system time */
    nreq->inf_queue_pos = qpos;

    /*
     * Each time we receive a completed request, including a duplicate of a request
     * already on one of the queues, we tell the client about our backoff.
     */
    //if ( jrdp_config.wait_time )
        //jrdp_rwait( nreq, jrdp_config.wait_time, nreq->inf_queue_pos, nreq->inf_sys_time );

    goto check_for_more;

    return 0;
}

int
jrdp_xmit( PJSENDER snder, PJREQ req, int window )
{
    PJPACKET        ptmp;
    u_int16_t       stmp;
    int             ns; // Number of bytes actually sent.
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

            //ns = sendto( jrdp_sock,(char*)(ptmp->start), ptmp->length, 0, (struct sockaddr*)&(req->peer), sizeof(struct sockaddr_in) );
            ns = send( snder->sock, (char*)(ptmp->start), ptmp->length, MSG_NOSIGNAL );

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

    //jrdp__set_def_port_no();

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
    if ( getsockname( jrdp_sock, (struct sockaddr*)&jrdp_client_address_port, (socklen_t*)&tmp) )
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
jrdp__eqtime( const struct timeval t1, const struct timeval t2 )
{
    return t1.tv_sec == t2.tv_sec && t1.tv_usec == t2.tv_usec;
}

int 
jrdp__timeislater( struct timeval t1, struct timeval t2 )
{
    if ( jrdp__eqtime( t1, bogustime ) || jrdp__eqtime( t2, bogustime ) )
    {
        printf( "Time compare error.\n" );
        exit(-1);
    }

    if ( jrdp__eqtime( t2, infinitetime ) )
        return jrdp__eqtime( t1, infinitetime );
    if ( jrdp__eqtime( t1, infinitetime ) )
        return 1;
    return ( ( t1.tv_sec > t2.tv_sec ) || ( ( t1.tv_sec == t2.tv_sec ) && ( t1.tv_usec >= t2.tv_usec ) ) );
}

struct timeval
jrdp__addtime( struct timeval t1, struct timeval t2 )
{
    struct timeval retval;

    if ( jrdp__eqtime( t1, bogustime) || jrdp__eqtime( t2, bogustime ) )
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

struct timeval
jrdp__subtime( const struct timeval minuend, const struct timeval subtrahend )
{
    register int32_t tmp;
    struct timeval difference;
    int borrow = 0;
    if ( jrdp__eqtime( subtrahend, infinitetime ) )
    {
        printf( "jrdp__subtime(): subtrahend can't be infinite." );
        exit(-1);
    }
    if ( jrdp__eqtime( subtrahend, bogustime ) || jrdp__eqtime( minuend, bogustime ) )
    {
        printf( "jrdp__subtime(): bad arguments.");
        exit(-1);
    }
    if ( jrdp__eqtime( minuend, infinitetime ) )
        return infinitetime;
    tmp = minuend.tv_usec - subtrahend.tv_usec;
    if ( tmp < 0 )
    {
        ++borrow;
        tmp = tmp + UFACTOR;
    }
    difference.tv_usec = tmp;
    tmp = minuend.tv_sec - borrow - subtrahend.tv_sec;
    if ( tmp < 0 )
        return zerotime;
    difference.tv_sec = tmp;
    return difference;
}

struct timeval
jrdp__mintime( const struct timeval t1, const struct timeval t2 )
{
    struct timeval difference;

    if ( jrdp__eqtime( t1, bogustime ) || jrdp__eqtime( t2, bogustime ) )
    {
        printf( "jrdp__mintime(): bad arguments." );
        exit(-1);
    }
    if ( jrdp__eqtime( t2, infinitetime ) )
        return t1;
    difference = jrdp__subtime( t1, t2 );
    if ( jrdp__eqtime( difference, zerotime ) )
        return t1;
    else
        return t2;
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

struct timeval
jrdp__next_activeQ_timeout( PJSENDER snder, const struct timeval now )
{
    PJREQ req;
    struct timeval soonest;

    soonest = infinitetime;
    //EXTERN_MUTEXED_LOCK(snder->activeQ);
    for ( req = snder->activeQ; req; req = req->next )
        soonest = jrdp__mintime( soonest, req->wait_till );
    //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
    return jrdp__subtime( soonest, now );
}

void
jrdp_update_cfields( PJREQ existing, PJREQ newing )
{
    if ( newing->prcvd_thru > existing->prcvd_thru )
        existing->prcvd_thru = newing->prcvd_thru;
}

int
jrdp_acknowledge( PJLISTENER lstner, PJREQ req )
{
    PJPACKET    pkt = jrdp_pktalloc();
    short       cid = htons(req->cid);
    short       rcvd = htons(req->rcvd_thru);
    short       zero = 0;
    int         ret;

    pkt->length = 11;
    pkt->start[0] = (char) 129; /* version # */
    pkt->start[1] = 0;    /* no contexts here (yet) */
    pkt->start[2] = 0x01; /* flags: please-ack bit on. */
    pkt->start[3] = 0;    /* no options */
    pkt->start[4] = 11;   /* header length */
    memcpy( pkt->start + 5, &cid, sizeof(char)*2 );      /* Connection ID */
    memcpy( pkt->start + 7, &zero, sizeof(char)*2 );     /* Packet sequence number */
    memcpy( pkt->start + 9, &rcvd, sizeof(char)*2 );     /* Received through */

    ret = jrdp_snd_pkt( lstner, pkt, req );
    jrdp_pktfree(pkt);
    return ret;
}

int
jrdp_snd_pkt( PJLISTENER lstner, PJPACKET pkt, PJREQ req )
{
    int n;
    printf( "Sending pkt %d.", pkt->seq );
    n = sendto( ( ( lstner->prvsock != -1 ) ? lstner->prvsock : lstner->sock ), pkt->start, pkt->length, 0, (struct sockaddr*)&(req->peer), sizeof(struct sockaddr_in) );
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
    old_hlength = pkt->text - pkt->start;

    /* add total # of packets to last packet (save space). */
    int stamp_message_npkts = (pkt->seq == req->trns_tot);
    char* ctlptr = pkt->start + 11;

    if ( is_rwait_needed )
        new_hlength = 13;   /* RWAIT flag takes a two-byte argument. */
    else
        new_hlength = 11;   /* include header fields through received-through */
    if ( stamp_message_npkts )
        new_hlength += 2;
    /* Allocate space for new header. */
    pkt->start = pkt->text - new_hlength;
    pkt->length += new_hlength - old_hlength;

    pkt->start[0] = (char) 129; /* version # */
    pkt->start[1] = 0;     /* no contexts here (yet) */
    pkt->start[2] = 0;     /* flags */
    if ( is_ack_needed )
        pkt->start[2] |= 0x01; /* flags: please-ack bit on. */
    if ( stamp_message_npkts )
        pkt->start[2] |= 0x04; /* 2 octet arg too. */
    if ( is_rwait_needed )
        pkt->start[2] |= 0100; /* bit 6: clear wait (2 octet arg. too) */
    pkt->start[3] = 0;     /* no options */
    memcpy( pkt->start + 5, &req->cid, sizeof(char)*2 ); /* Connection ID */
    stmp = htons(pkt->seq);
    memcpy( pkt->start + 7, &stmp, sizeof(char)*2 ); /* Packet sequence number */
    stmp = htons(req->rcvd_thru);
    memcpy( pkt->start + 9, &stmp, sizeof(char)*2 ); /* Received Through */
    /* Write options */
    ctlptr = pkt->start + 11;
    if ( stamp_message_npkts )
    {
        stmp = req->trns_tot;
        memcpy( ctlptr, &stmp, sizeof(char)*2 );
        ctlptr += 2;
    }
    if ( is_rwait_needed )
    {
        stmp = htons(req->svc_rwait);
        memcpy( ctlptr, &stmp, sizeof(char)*2 );
        ctlptr += 2;
    }
}

int
jrdp_process_active( PJSENDER snder )
{
    fd_set              readfds;
    struct sockaddr_in  from; /* Reply received from */
    unsigned int        from_sz;
    unsigned int        hdr_len; /* Header Length */
    char*               ctlptr = NULL; /* Pointer to control field. */
    u_int16_t           cid = 0;
    //unsigned char       rdflag11;
    //unsigned char       rdflag12;
    unsigned char       tchar; /* For decoding extra fields */
    u_int16_t           stmp;
    u_int32_t           ltmp;
    int                 select_retval;
    int                 nr;
    PJREQ               req = NULL;
    PJPACKET            pkt = NULL;
    PJPACKET            ptmp = NULL;
    int                 retransmit_unacked_packets = 0;
    struct timeval      select_zerotimearg;
    struct timeval      now = zerotime;

check_for_pending:

    FD_ZERO(&readfds);
    if ( snder->sock != -1 )
        FD_SET( snder->sock, &readfds );

    /*
    if ( jrdp_srvsock != -1 )
        FD_SET( jrdp_srvsock, &readfds );
    if ( jrdp_prvsock != -1 )
        FD_SET( jrdp_prvsock, &readfds );
    */

    select_zerotimearg = zerotime;
    //select_retval = select( max( jrdp_sock, max( jrdp_srvsock, jrdp_prvsock )) + 1, &readfds, (fd_set*)0, (fd_set*)0, &select_zerotimearg );
    select_retval = select( snder->sock + 1, &readfds, (fd_set*)0, (fd_set*)0, &select_zerotimearg );

    /* If either of the server ports are ready for reading, read them first */
    /*
    if ( ( select_retval != -1 ) && ( ( ( jrdp_srvsock != -1 ) && FD_ISSET( jrdp_srvsock, &readfds ) ) || ( ( jrdp_prvsock != -1 ) && FD_ISSET( jrdp_prvsock, &readfds ) ) ) )
    {
        (*jrdp__accept)( lstner, 0, 0 );
        goto check_for_pending;
    }
    */

    /* If select_retval is zero, then nothing to process, check for timeouts */
    if ( select_retval == 0 )
    {
        //EXTERN_MUTEXED_LOCK(snder->activeQ);
        req = snder->activeQ;
        while ( req )
        {
            if ( req->status == JRDP_STATUS_ACKPEND )
            {
                jrdp_xmit( snder, req, -1 );
                req->status = JRDP_STATUS_ACTIVE;
            }
            if ( jrdp__eqtime( now, zerotime ) )
                now = jrdp__gettimeofday();
            if ( req->wait_till.tv_sec && jrdp__timeislater( now, req->wait_till ) )
            {
                if ( req->retries_rem-- > 0 )
                {
                    jrdp__adjust_backoff( &(req->timeout_adj) );
                    if ( req->pwindow_sz > 4 )
                        req->pwindow_sz /= 2;
                    jrdp_headers(req);
                    printf( "Connection %d timed out - Setting timeout to %d.%06ld seconds; transmission window to %d packets.\n", (int)ntohs(req->cid), (int)req->timeout_adj.tv_sec, (long)req->timeout_adj.tv_usec, (int)req->pwindow_sz );
                    if ( jrdp__eqtime( now, zerotime ) )
                        now = jrdp__gettimeofday();
                    req->wait_till = jrdp__addtime( req->timeout_adj, now );
                    jrdp_xmit( snder, req, req->pwindow_sz );
                }
                else
                {
                    printf( "Connection %d timed out - Retry count exceeded.\n", ntohs(req->cid) );
                    req->status = JRDP_TIMEOUT;
                    EXTRACT_ITEM( req, snder->activeQ );
                    --snder->activeQ_len;
                    //EXTERN_MUTEXED_LOCK(snder->completeQ);
                    APPEND_ITEM( req, snder->completeQ );
                    //EXTERN_MUTEXED_UNLOCK(snder->completeQ);
                }
            }
            req = req->next;
        }
        //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
        return JRDP_SUCCESS;
    }

    /* If negative, then return an error */
    if ( ( select_retval < 0) || !FD_ISSET( snder->sock, &readfds ) )
    {
        printf( "select failed : returned %d port=%d\n", select_retval, snder->sock );
        exit(-1);
    }

    /* Process incoming packets */
    pkt = jrdp_pktalloc();
    pkt->start = pkt->data;
    from_sz = sizeof(struct sockaddr_in);

    if ( (nr = recvfrom( snder->sock, pkt->start, JRDP_PKT_DSZ, 0, (struct sockaddr*)&from, &from_sz ) ) < 0 )
    {
        printf( "recvfrom fails.\n" );
        jrdp_pktfree(pkt);
        exit(-1);
    }

    pkt->length = nr;
    *(pkt->start + pkt->length) = '\0';

    printf( "Received packet from %s", inet_ntoa(from.sin_addr) );

    if (pkt->length == 0)
    {
        printf( "Client received empty packet; discarding.\n" );
        jrdp_pktfree(pkt);
        goto check_for_pending;
    }

    hdr_len = ((unsigned char*)pkt->start)[4];
    if ( pkt->length < 5 || hdr_len < 5 )
    {
        jrdp_pktfree(pkt);
        printf( "Client received malformed V1 packet with header < 5 octets; discarding" );
        goto check_for_pending;
    }
    if ( hdr_len >= 7 )
        memcpy( &cid, pkt->start + 5, sizeof(char)*2 );
    else
    {
        cid = 0;
        printf( "Why cid is 0?\n" );
        exit(-1);
    }

    /* Match up response with request */
    //EXTERN_MUTEXED_LOCK(snder->activeQ);
    for ( req = snder->activeQ; req; req = req->next )
    {
        if ( cid == req->cid )
            break;
    }

    if ( !req )
    {
        printf( "Packet received for inactive request (cid %d)\n", ntohs(cid) );
        jrdp_pktfree(pkt);
        //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
        goto check_for_pending;
    }

    ctlptr = pkt->start + 11;

    /* Octet 2: flags */
    if ( pkt->start[2] & 0x01 )
    {
        /* bit 0: ack */
        req->status = JRDP_STATUS_ACKPEND;
    }
    if ( pkt->start[2] & 0x02 )
    {
        /* bit 1: sequenced control packet */
        printf( "Sequenced control packet\n" );
        pkt->length = -1;
    }
    if ( pkt->start[2] & 0x04 )
    {
        /* bit 2: total packet count */
        if_have_option_argument(2)
        {
            memcpy( &stmp, ctlptr, sizeof(char)*2 );
            req->rcvd_tot = ntohs(stmp);
            ctlptr += 2;
        }
    }
    /*
    if ( pkt->start[2] & 0x08 )
    {
        // bit 3: priority in 2-octet argument
        // The client doesn't currently use this information but let's copy it in any case.
        if_have_option_argument(2)
        {
            memcpy( &stmp, ctlptr, sizeof(char)*2 );
            req->priority = ntohs(stmp);
            ctlptr += 2;
        }
    }
    */
    /* bit 4: protocol ID in two-octet arg. Unused in this implementation. */
    if ( pkt->start[2] & 0x20 )
    {
        /* bit 5: max window size */
        if_have_option_argument(2)
        {
            memcpy( &stmp, ctlptr, sizeof(char)*2 );
            req->pwindow_sz = ntohs(stmp);
            ctlptr += 2;
        }
    }
    if ( pkt->start[2] & 0x40 )
    {
        /* bit 6: wait time. */
        if_have_option_argument(2)
        {
            memcpy( &stmp, ctlptr, sizeof(char)*2 );
            if ( stmp || ( req->svc_rwait != ntohs(stmp) ) )
            {
                /* Make sure now is current */
                retransmit_unacked_packets = 0;
                /* New or non-zero requested wait value */
                req->svc_rwait = ntohs(stmp);
                printf( " [Service asked us to wait %d seconds]", req->svc_rwait );

                if ( req->svc_rwait >= req->timeout.tv_sec )
                {
                    req->timeout_adj.tv_sec = req->svc_rwait;
                    req->timeout_adj.tv_usec = 0;
                }
                else
                {
                    req->timeout_adj = req->timeout;
                }
                if ( jrdp__eqtime( now, zerotime ) )
                    now = jrdp__gettimeofday();
                req->wait_till = jrdp__addtime( req->timeout_adj, now );
                /* Reset the retry count */
                req->retries_rem = req->retries;
            }
            ctlptr += 2;
        }
    }
    if ( pkt->start[2] & 0x80) /* indicates octet 3 is OFLAGS. For now, we use this to just disable octet 3. */
        pkt->start[3] = 0;
    /* Octet 3: options */
    switch ( ( (unsigned char*)pkt->start)[3] )
    {
        case 0: /* no option specified */
            break;
        case 1: /* JRDP connection refused */
            printf( "  ***JRDP connection refused by server***\n" );
            req->status = JRDP_REFUSED;
            EXTRACT_ITEM( req, snder->activeQ );
            --snder->activeQ_len;
            //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
            //EXTERN_MUTEXED_LOCK(snder->completeQ);
            APPEND_ITEM( req, snder->completeQ );
            //EXTERN_MUTEXED_UNLOCK(snder->completeQ);
            goto check_for_pending;
            break;
        case 2: /* reset peer's received-through count */
            break;
        case 3: /* packets received beyond received-through */
            break;
        case 4: /* redirect */
        case 5: /* redirect and notify.  -- we treat it as case 4 */
            if_have_option_argument(6)
            {
                /* Server Redirect (case 4): */
                memcpy( &(req->peer.sin_addr), ctlptr, sizeof(char)*4 );
                ctlptr += 4;
                memcpy( &(req->peer.sin_port), ctlptr, sizeof(char)*2 );
                ctlptr += 2;
                printf( "  ***JRDP redirect to %s(%d)***", inet_ntoa(req->peer.sin_addr), ntohs(req->peer.sin_port) );
                jrdp_xmit( snder, req, req->pwindow_sz );
            }
            break;
        /* options 6 & 7 (forwarded) are for servers only */
        case 8: /* bad-version; invalidate this message */
        case 9: /* Server to client: Context Failed; Connection refused */
            goto check_for_pending;
            break;
        case 254: /* queue status information */
            if_have_option_argument(1)
            {
                tchar = *ctlptr;
                ctlptr++;
                if ( tchar & 0x1 )
                {
                    /* Queue position */
                    if_have_option_argument(2)
                    {
                        memcpy( &stmp, ctlptr, sizeof(char)*2 );
                        req->inf_queue_pos = ntohs(stmp);
                        printf( " (Current queue position on server is %d)", req->inf_queue_pos );
                        ctlptr += 2;
                    }
                }
                if ( tchar & 0x2 )
                {
                    /* Expected system time */
                    if_have_option_argument(4)
                    {
                        memcpy( &ltmp, ctlptr, sizeof(char)*4 );
                        req->inf_sys_time = ntohl(ltmp);
                        printf( " (Expected system time is %d seconds)", req->inf_sys_time );
                        ctlptr += 4;
                    }
                }
            }
            break;
        default:
            printf( "Option %d not recognized; ignored.", pkt->start[3] );
    }
    /* Octet 4: header length; already processed */
    /* Octets 5 -- 6: Connection ID, also already processed */
    /* Octet 7 -- 8: Packet Sequence Number */
    if ( hdr_len >= 9 )
    {
        memcpy( &stmp, pkt->start + 7, sizeof(char)*2 );
        pkt->seq = ntohs(stmp);
    }
    /* Octet 9 -- 10: received-through */
    if ( hdr_len >= 11 )
    {
        memcpy( &stmp, pkt->start + 9, sizeof(char)*2 );
        stmp = ntohs(stmp);
        req->prcvd_thru = max( stmp, req->prcvd_thru );
        printf( " (this rcvd_thru = %d, prcvd_thru = %d)", stmp, req->prcvd_thru );
        ctlptr += 2;
        if ( req->prcvd_thru < req->trns_tot )
        {
            if ( req->pwindow_sz )
                printf( " [planning to retransmit up to %d unACKed packets]", req->pwindow_sz );
            else
                printf( " [planning to retransmit all unACKed packets]" );
            retransmit_unacked_packets = 1;
        }
        if ( jrdp__eqtime( now, zerotime ) )
            now = jrdp__gettimeofday();
        req->wait_till = jrdp__addtime( now, req->timeout_adj );
    }
    /* Octets 11 and onward contain arguments to flags and options. */

    /* The code above has done all of the control information processing, so
       there is no need for this packet. */

    if ( pkt->seq == 0 )
    {
        printf( "Receive unsequenced control packet.\n" );
        if ( retransmit_unacked_packets )
        {
            jrdp_retransmit_unacked_packets( snder, req );
            retransmit_unacked_packets = 0;
        }
        //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
        jrdp_pktfree(pkt);
        goto check_for_pending;
    }
    if ( pkt->length >= 0 )
        pkt->length -= hdr_len;
    pkt->start += hdr_len;
    pkt->text = pkt->start;
    pkt->ioptr = pkt->start;

    printf( "Receive packet %d of %d (cid=%d).\n", pkt->seq, req->rcvd_tot, ntohs(req->cid) );

    if ( req->rcvd == NULL)
    {
        APPEND_ITEM( pkt, req->rcvd );
        memcpy( &(req->peer), &from, from_sz );
        if ( pkt->seq == 1 )
        {
            req->comp_thru = pkt;
            req->rcvd_thru = 1;
            if ( req->rcvd_tot == 1 )
                goto req_complete;
        }
        goto ins_done;
    }

    if ( req->comp_thru && ( pkt->seq <= req->rcvd_thru ) )
        jrdp_pktfree(pkt);
    else if ( pkt->seq < req->rcvd->seq )
    {
        /* First (sequentially) */
        PREPEND_ITEM( pkt, req->rcvd );
        if ( req->rcvd->seq == 1 )
        {
            req->comp_thru = req->rcvd;
            req->rcvd_thru = 1;
        }
    }
    else
    {
        /* Insert later in the packet list unless duplicate */
        ptmp = ( req->comp_thru ? req->comp_thru : req->rcvd );
        while ( pkt->seq > ptmp->seq )
        {
            if ( ptmp->next == NULL )
            {
                APPEND_ITEM(pkt, req->rcvd);
                goto ins_done;
            }
            ptmp = ptmp->next;
        }
        if ( ptmp->seq == pkt->seq )
            jrdp_pktfree(pkt);
        else
            INSERT_ITEM1_BEFORE_ITEM2( pkt, ptmp );
    }

ins_done:

    /* Find out how much is complete and remove sequenced control packets */
    while ( req->comp_thru && req->comp_thru->next && ( req->comp_thru->next->seq == ( req->comp_thru->seq + 1 ) ) )
    {
        /* We have added one more in sequence */
        printf( "Packets now received through %d\n", req->comp_thru->next->seq );

        if ( req->comp_thru->length == -1 )
        {
            /* Old comp_thru was a sequenced control packet */
            ptmp = req->comp_thru;
            req->comp_thru = req->comp_thru->next;
            req->rcvd_thru = req->comp_thru->seq;
            EXTRACT_ITEM( ptmp, req->rcvd );
            jrdp_pktfree(ptmp);
        }
        else
        {
            /* Old comp_thru was a data packet */
            req->comp_thru = req->comp_thru->next;
            req->rcvd_thru = req->comp_thru->seq;
        }

        /* Update outgoing packets */
        jrdp_headers(req);

        if ( req->svc_rwait > req->timeout.tv_sec )
        {
            req->timeout_adj.tv_sec = req->svc_rwait;
            req->timeout_adj.tv_usec = 0;
        }
        else
            req->timeout_adj = req->timeout;
        if ( jrdp__eqtime( now, zerotime ) )
            now = jrdp__gettimeofday();

        req->wait_till = jrdp__addtime( req->timeout_adj, now );
        req->retries_rem = req->retries;
    }

    /* See if there are any gaps - possibly toggle GAP status */
    if ( !req->comp_thru || req->comp_thru->next )
    {
        if ( req->status == JRDP_STATUS_ACTIVE)
            req->status = JRDP_STATUS_GAPS;
    }
    else if ( req->status == JRDP_STATUS_GAPS )
        req->status = JRDP_STATUS_ACTIVE;

    /* If incomplete, wait for more */
    if ( !(req->comp_thru) || ( req->comp_thru->seq != req->rcvd_tot ) )
    {
        //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
        goto check_for_pending;
    }

req_complete:

    if ( req->status == JRDP_STATUS_ACKPEND )
    {
        if ( req->peer.sin_family == AF_INET )
            printf( "ACTION: Acknowledging final packet to %s(%d)\n", inet_ntoa(req->peer.sin_addr), ntohs(req->peer.sin_port) );
        else
            printf( "ACTION: Acknowledging final packet\n" );
        /* Don't need to call jrdp_headers() to set ACK bit; we do not need an
         * ACK from the server, and no data packets will be transmitted.  */
        jrdp_xmit( snder, req, req->pwindow_sz );
    }

    req->status = JRDP_STATUS_COMPLETE;
    req->inpkt = req->rcvd;
    EXTRACT_ITEM( req, snder->activeQ );
    --snder->activeQ_len;
    //EXTERN_MUTEXED_UNLOCK(snder->activeQ);
    /* A complete response has been received */
    //EXTERN_MUTEXED_LOCK(snder->completeQ);
    APPEND_ITEM( req, snder->completeQ );
    //EXTERN_MUTEXED_UNLOCK(snder->completeQ);
    return JRDP_SUCCESS;
}

int
jrdp_retrieve( PJSENDER snder, PJREQ req, int ttwait_arg )
{
    fd_set          readfds;
    struct timeval  selwait_st; /* Time to wait for select       */
    int             tmp; /* Hold value returned by select */

    struct timeval  cur_time = bogustime;
    struct timeval  start_time = bogustime;
    struct timeval  time_elapsed = bogustime;
    struct timeval  ttwait = bogustime;

    if ( ttwait_arg < 0 )
        ttwait = infinitetime;
    else
    {
        ttwait.tv_sec = ttwait_arg / UFACTOR;
        ttwait.tv_usec = ttwait_arg % UFACTOR;
    }
    start_time = jrdp__gettimeofday();

    if ( !req && !snder->activeQ && !snder->completeQ )
    {
        printf( "Bad request.\n" );
        exit(-1);
    }

    if ( req && req->status == JRDP_STATUS_FREE)
    {
        printf( "Attempt to retrieve free JREQ. Bad request.\n");
        exit(-1);
    }

    if ( req && req->status == JRDP_STATUS_NOSTART )
    {
        printf( "Another bad request.\n" );
        exit(-1);
    }

 check_for_more:

    jrdp_process_active(snder);

    if ( !req && snder->completeQ )
      return 999;//PSUCCESS;
    if ( !req )
      goto restart_select;

    if ( ( req->status == JRDP_STATUS_COMPLETE) || ( req->status > 0 ) )
    {
        //EXTERN_MUTEXED_LOCK(snder->completeQ);
        EXTRACT_ITEM( req, snder->completeQ );
        //EXTERN_MUTEXED_UNLOCK(snder->completeQ);

        PJPACKET ptmp;
        if ( req->status > 0 )
            printf( "Request failed (error %d)!", req->status );
        else
            printf( "Packets received...");
        ptmp = req->rcvd;
        while ( ptmp )
        {
            printf( "Packet %d%s:\n", ptmp->seq, (ptmp->seq <= 0 ? " (not rcvd, but constructed)" : "" ) );
            ptmp = ptmp->next;
        }

        if ( req->status == JRDP_STATUS_COMPLETE )
            return 0;
        else
            return req->status;
    }

restart_select:
    cur_time = jrdp__gettimeofday();

    if ( jrdp__eqtime( ttwait, zerotime ) )
        return JRDP_PENDING;
    else if ( jrdp__eqtime( ttwait, infinitetime ) )
        selwait_st = jrdp__next_activeQ_timeout( snder, cur_time );
    else
    {
        assert( !jrdp__eqtime( ttwait, infinitetime ) && !jrdp__eqtime( ttwait, zerotime ) );
        time_elapsed = jrdp__subtime( cur_time, start_time );
        if ( jrdp__timeislater( time_elapsed, ttwait ) )
            return JRDP_PENDING;
        selwait_st = jrdp__mintime( jrdp__next_activeQ_timeout( snder, cur_time ), jrdp__subtime( ttwait, time_elapsed ) );
    }

    printf( "Waiting %ld.%06ld seconds for reply...", selwait_st.tv_sec, selwait_st.tv_usec );

    FD_ZERO(&readfds);
    FD_SET( snder->sock, &readfds);

    tmp = select( snder->sock + 1, &readfds, (fd_set*)0, (fd_set*)0, &selwait_st );

    if ( tmp >= 0 )
        goto check_for_more;

    if ( ( tmp == -1 ) /*&& ( errno == EINTR )*/ )
        goto restart_select;

    printf( "select() failed.\n" );

    return JRDP_SELECT_FAILED;
}

int
jrdp_retransmit_unacked_packets( PJSENDER snder, PJREQ req )
{
    assert( req->trns_tot > 0 );
    if ( req->prcvd_thru < req->trns_tot )
    {
        jrdp_headers(req);
        jrdp_xmit( snder, req, req->pwindow_sz );
    }
    return JRDP_SUCCESS;
}

int
jrdp_reply( int sock, PJREQ req, int flags, const char* message, int len )
{
    PJLISTENER lstner = rudp_find_matched_lstner(sock);

    int tmp;

    tmp = jrdp_pack( req, flags, message, len );
    if ( tmp )
        return tmp;
    if ( flags & JRDP_R_COMPLETE )
        return jrdp_respond( lstner, req, JRDP_R_COMPLETE );

    /* Check to see if any packets are done */
    if ( req->outpkt && req->outpkt->next )
    {
        PJPACKET tpkt; /* Temp to hold active packet */
        /* Hold out final packet */
        tpkt = req->outpkt->prev;
        EXTRACT_ITEM( tpkt, req->outpkt );
        tmp = jrdp_respond( lstner, req, JRDP_R_INCOMPLETE );
        APPEND_ITEM( tpkt, req->outpkt );
        if ( tmp )
            return tmp;
    }
    return JRDP_SUCCESS;
}

int
jrdp_respond( PJLISTENER lstner, PJREQ req, int flags )
{
    char        buf[JRDP_PKT_DSZ];
    int         retval = JRDP_SUCCESS;
    u_int16_t   cid = htons(req->cid);
    u_int16_t   nseq = 0;
    u_int16_t   ntotal = 0;
    u_int16_t   rcvdthru = 0;
    u_int16_t   bkoff = 0;
    int         stamp_clear_wait = 0;
    int         stamp_request_ack = 0;
    int         stamp_total = 0;
    PJPACKET    tpkt;

    *buf = '\0';


    if ( req->status == JRDP_STATUS_FREE )
    {
        printf( "Attempt to respond to free JREQ.\n");
        exit(-1);
    }

    if ( !req->outpkt )
    {
        printf( "It's a bad request.\n" );
        exit(-1);
    }

    more_to_process:

    if ( !req->trns )
        req->outpkt->seq = 1;
    else
        req->outpkt->seq = req->trns->prev->seq + 1;

    if ( ( flags & JRDP_R_COMPLETE ) && ( req->outpkt->next == NULL ) )
        req->trns_tot = req->outpkt->seq;
    nseq = htons( (u_short) req->outpkt->seq );

    if ( req->svc_rwait )
    {
        stamp_clear_wait = 1;
        req->svc_rwait = 0;
        req->svc_rwait_seq = req->outpkt->seq;
    }
    else if ( req->svc_rwait_seq > req->prcvd_thru )
        stamp_clear_wait = 1;
    else
        stamp_clear_wait = 0;


    if ( ( ( req->trns_tot && req->outpkt->seq == req->trns_tot ) && stamp_clear_wait ) || ( req->outpkt->seq == req->prcvd_thru + req->pwindow_sz ) )
    {
        req->retries_rem = 4;
        req->wait_till = jrdp__addtime( jrdp__gettimeofday(), req->timeout_adj );
        printf( "Pkt %d will request an ACK.\n", req->outpkt->seq );
        stamp_request_ack = 1;
    }
    else
        stamp_request_ack = 0;
    /* Stamp the total # of packets iff it's the last packet. */
    if ( req->trns_tot && ( req->outpkt->seq == req->trns_tot ) )
    {
        stamp_total = 1;
        ntotal = htons( (u_short) req->trns_tot );
    }
    else
        stamp_total = 0;

    unsigned char header_len;
    char* ctlptr;

    /* Stamp header size */
    if ( stamp_clear_wait )
        header_len = 13;
    else
        header_len = 11;

    if ( stamp_total )
        header_len += 2;

    req->outpkt->start -= header_len;
    req->outpkt->length += header_len;
    req->outpkt->start[4] = (char)header_len;

    req->outpkt->start[0] = (char) 129; /* version # */
    req->outpkt->start[2] = 0;
    if ( stamp_request_ack )
        req->outpkt->start[2] |= 0x01; /* flags: please-ack bit on. */
    if ( stamp_total )
        req->outpkt->start[2] |= 0x04;
    if ( stamp_clear_wait )
        req->outpkt->start[2] |= 0100; /* bit 6: clear wait (2 octet arg. too) */
    req->outpkt->start[3] = 0; /* no options */
    memcpy( req->outpkt->start + 5, &cid, sizeof(char)*2 ); /* Connection ID */
    memcpy( req->outpkt->start + 7, &nseq, sizeof(char)*2 ); /* Packet sequence number */
    rcvdthru = htons(req->rcvd_thru);
    memcpy( req->outpkt->start + 9, &rcvdthru, sizeof(char)*2 ); /* Received Through */
    /* Write options */
    ctlptr = req->outpkt->start + 11;
    if ( stamp_total )
    {
        memcpy( ctlptr, &ntotal, sizeof(char)*2 );
        ctlptr += 2;
    }
    if ( stamp_clear_wait )
    {
        bkoff = htons((u_short) req->svc_rwait);
        memcpy( ctlptr, &bkoff, sizeof(char)*2 );
        ctlptr += 2;
    }
    /* The end of writing the packet's header */


    /* Only send if packet not yet received. */
    /* Do honor the window of req->pwindow_sz packets.  */
    if ( !( flags & JRDP_R_NOSEND ) && ( req->outpkt->seq <= ( req->prcvd_thru + req->pwindow_sz ) ) && ( req->outpkt->seq > req->prcvd_thru ) )
        retval = jrdp_snd_pkt( lstner, req->outpkt, req );

    /* Add packet to req->trns */
    tpkt = req->outpkt;
    EXTRACT_ITEM( tpkt, req->outpkt );
    APPEND_ITEM( tpkt, req->trns );

    if ( req->outpkt )
        goto more_to_process;

    /* If complete then add request structure to done queue in case we have to retry. */
    if ( flags & JRDP_R_COMPLETE)
    {
        PJREQ match_in_runQ;

        //EXTERN_MUTEXED_LOCK(lstner->runQ);
        for ( match_in_runQ = lstner->runQ; match_in_runQ; match_in_runQ = match_in_runQ->next )
        {
            if ( match_in_runQ == req )
            {
                EXTRACT_ITEM( req, lstner->runQ );
                break;
            }
        }
        //EXTERN_MUTEXED_UNLOCK(lstner->runQ);
        if ( ( req->cid == 0 ) || ( lstner->replyQ_max_len <= 0 ) )
            jrdp_reqfree(req);
        else
        {
            //EXTERN_MUTEXED_LOCK(lstner->doneQ);
            if ( lstner->replyQ_len <= lstner->replyQ_max_len )
            {
                PREPEND_ITEM( req, lstner->doneQ );
                ++lstner->replyQ_len;
            }
            else
            {
                register PJREQ doneQ_lastitem = lstner->doneQ->prev;
                /* Add new request to start */
                PREPEND_ITEM( req, lstner->doneQ );

                if ( doneQ_lastitem->svc_rwait_seq > doneQ_lastitem->prcvd_thru )
                    printf( "Requested ack never received (%d of %d/%d ack)", doneQ_lastitem->prcvd_thru, doneQ_lastitem->svc_rwait_seq, doneQ_lastitem->trns_tot );
                /* Now do the removal and free it. */
                EXTRACT_ITEM( doneQ_lastitem, lstner->doneQ );
                jrdp_reqfree(doneQ_lastitem);
            }
            //EXTERN_MUTEXED_UNLOCK(lstner->doneQ);
        }
    }
    return retval;
}


int
rudp_connect( const char* dname, struct sockaddr_in* dest )
{
    PJSENDER snder = rudp_snderalloc();
    APPEND_ITEM( snder, rudp_snderQ );

    /* Open the local socket from which packets will be sent */
    if ( ( snder->sock = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
    {
        printf( "jrdp: Can't open client's sending socket.\n" );
        exit(-1);
    }


    if ( !dest || ( dest->sin_addr.s_addr == 0 ) )
    {
        if ( dname == NULL || *dname == '\0' )
        {
            printf( "No peer for sender.\n" );
            exit(-1);
        }

        if ( jrdp_hostname2name_addr( dname, NULL, &snder->peer_addr ) )
        {
            printf( "Bad host name.\n" );
            exit(-1);
        }
    }
    else
        memcpy( &snder->peer_addr, dest, sizeof(struct sockaddr_in) );

    if ( snder->peer_addr.sin_port == 0 )
    {
        printf( "No port to use.\n" );
        exit(-1);
    }

    if ( connect( snder->sock, (struct sockaddr*)&snder->peer_addr, sizeof(struct sockaddr_in) ) )
    {
        printf( "JRDP: connect() completed with error.\n" );
        rudp_disconnect(snder->sock);
        exit(-1);
    }

    return snder->sock;
}

int
rudp_disconnect( int sock )
{
    PJSENDER snder = rudp_find_matched_snder(sock);

    close(snder->sock);

    rudp_snderfree(snder);

    return 0;
}

int
rudp_send( int sock, int flags, const char* buf, int buflen, int ttwait )
{
    PJSENDER snder = rudp_find_matched_snder(sock);

    PJREQ req;
    if ( ( req = jrdp_reqalloc() ) == NULL )
    {
        printf( "Cannot alloc jreq.\n" );
        exit(-1);
    }
    /* Add text in BUFFER to the request. */
    if ( jrdp_pack( req, flags, buf, buflen ) )
    {
        printf( "Packing request failed.\n" );
        exit(-1);
    }

    PJPACKET ptmp;

    memcpy( &(req->peer), &snder->peer_addr, sizeof(struct sockaddr_in) );

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
    req->cid = rudp_nxt_cid(snder);

    if ( jrdp_headers(req) )
    {
        printf( "Add packet head failed.\n" );
        exit(-1);
    }
    req->status = JRDP_STATUS_ACTIVE;

    //EXTERN_MUTEXED_LOCK(snder->activeQ);
    APPEND_ITEM( req, snder->activeQ );
    ++snder->activeQ_len;
    req->wait_till = jrdp__addtime( jrdp__gettimeofday(), req->timeout_adj );
    if ( jrdp_xmit( snder, req, req->pwindow_sz ) )
    {
        printf( "Xmit packets failed.\n" );
        exit(-1);
    }
    //EXTERN_MUTEXED_UNLOCK(snder->activeQ);

    if ( ttwait )
        return jrdp_retrieve( snder, req, ttwait );
    else
        return JRDP_PENDING;
}

int
rudp_open_listen( u_int16_t port )
{
    printf( "\nEntering rudp_open_listen()\n" );
    struct sockaddr_in s_in = {AF_INET};
    s_in.sin_port = htons(port);
    int             on = 1;
    jrdp__accept = jrdp_accept;

    PJLISTENER lstner = rudp_lstneralloc();
    APPEND_ITEM( lstner, rudp_lstnerQ );

    if ( ( lstner->sock = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0)
    {
        printf( "rudp_open_listen: Can't open socket\n" );
        return -1;
    }

    if ( setsockopt( lstner->sock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0 )
        printf( "dirsrv: setsockopt (SO_REUSEADDR): error; continuing as best we can.\n" );
    
    s_in.sin_addr.s_addr = (u_int32_t) myaddress();

    if ( bind( lstner->sock, (struct sockaddr *)&s_in, sizeof(struct sockaddr_in) ) < 0 )
    {
        printf( "rudp_open_listen(): Can not bind socket\n" );
        return -1;
    }

    lstner->port = ntohs(s_in.sin_port);

    return lstner->sock;
}

PJLISTENER
rudp_lstneralloc(void)
{
    PJLISTENER    lstner;
    lstner = (PJLISTENER)malloc( sizeof(JLISTENER) );
    if ( !lstner )
        return NULL;
    ++rudp_jlstner_count;

    lstner->sock = -1;
    lstner->prvsock = -1;
    lstner->port = -1;

    lstner->req_count = 0;
    lstner->pkt_count = 0;

    lstner->pendingQ_len = 0;
    lstner->partialQ_len = 0;
    lstner->partialQ_max_len = 20;
    lstner->replyQ_len = 0;
    lstner->replyQ_max_len = 20;

    lstner->pendingQ = NULL;
    lstner->partialQ = NULL;
    lstner->runQ = NULL;
    lstner->doneQ = NULL;

    lstner->prev = NULL;
    lstner->next = NULL;

    return lstner;
}

PJSENDER
rudp_snderalloc(void)
{
    PJSENDER    snder;
    snder = (PJSENDER)malloc( sizeof(JSENDER) );
    if ( !snder )
        return NULL;
    ++rudp_jsnder_count;

    snder->sock = -1;
    snder->last_pid = getpid();
    memset( &(snder->peer_addr), '\000', sizeof(struct sockaddr_in) );

    snder->req_count = 0;
    snder->pkt_count = 0;
    snder->activeQ_len = 0;

    snder->activeQ = NULL;
    snder->completeQ = NULL;

    snder->prev = NULL;
    snder->next = NULL;

    return snder;
}

int
rudp_close_listen( int sock )
{
    PJLISTENER lstner = rudp_find_matched_lstner(sock);

    close(lstner->sock);

    rudp_lstnerfree(lstner);

    return 0;
}

void
rudp_lstnerfree( PJLISTENER lstner )
{
    free(lstner);

    return;
}

void
rudp_snderfree( PJSENDER snder )
{
    free(snder);
}

PJLISTENER
rudp_find_matched_lstner( int sock )
{
    PJLISTENER tmp;
    for ( tmp = rudp_lstnerQ; tmp; tmp = tmp->next )
    {
        if ( tmp->sock == sock )
            return tmp;
    }

    return NULL;
}

PJSENDER
rudp_find_matched_snder( int sock )
{
    PJSENDER tmp;
    for ( tmp = rudp_snderQ; tmp; tmp = tmp->next )
    {
        if ( tmp->sock == sock )
            return tmp;
    }

    return NULL;
}

u_int16_t
rudp_nxt_cid( PJSENDER snder )
{
    static u_int16_t    next_conn_id = 0;

    int                 pid = getpid();

    if ( !snder->last_pid )
    {
        printf( "rudp_nxt_cid(): strange behavior!\n" );
        exit(-1);
    }
    if ( snder->last_pid != pid )
    {
        printf( "maybe it's caused by client fork function, need further development.\n" );
        exit(-1);
    }

    if ( next_conn_id == 0 )
    {
        srand( pid + time(0) );
        next_conn_id = rand();
        snder->last_pid = pid;
    }
    if ( ++next_conn_id == 0 )
        ++next_conn_id;

    return (htons(next_conn_id));
}
