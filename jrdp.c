
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include "jrdp.h"


int jreq_count = 0;
int jpkt_count = 0;
int jrdp_srvport = -1;
int jrdp_prvport = -1;
int jrdp_priority = 0;
int jrdp_sock = -1;
static char* myhname = NULL;
static long myhaddr = 0L;
struct sockaddr_in jrdp_client_address_port;

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
    req->prev = NULL;
    req->next = NULL;
    return req;
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

    if ( ( jrdp_srvport = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0)
    {
        printf( "jrdp_bind_port: Can't open socket\n" );
        return -1;
    }

    if ( setsockopt( jrdp_srvport, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0 )
        printf( "dirsrv: setsockopt (SO_REUSEADDR): error; continuing as best we can.\n" );
    
    s_in.sin_addr.s_addr = (u_int32_t) myaddress();

    if ( bind( jrdp_srvport, (struct sockaddr *)&s_in, sizeof(struct sockaddr_in) ) < 0 )
    {
        printf( "jrdp_bind_port(): Can not bind socket\n" );
        return -1;
    }

    return ( ntohs(s_in.sin_port) );
}

char*
jrdp_get_nxt(void)
{
    char dataBuf[JRDP_PKT_LEN_R];

    struct sockaddr_in  from;
    int                 fromlen = sizeof(struct sockaddr_in);
    int                 n = 0;
    fd_set              readfds; /* used for select */
    struct timeval      time_out = { 30, 30 };
    int                 tmp;


    FD_ZERO( &readfds );
    FD_SET( jrdp_srvport, &readfds );


    tmp = select( jrdp_srvport + 1, &readfds, (fd_set *)0, (fd_set *)0, &time_out );

    if ( tmp == 0 )
    {
        printf( "Server listen time out.\n" );
        exit(-1);
    }
    else if ( tmp < 0 )
    {
        printf( "Server select failed.\n" );
        exit(-1);
    }
    else
    {
        assert( FD_ISSET( jrdp_srvport, &readfds ) );
        n = recvfrom( jrdp_srvport, dataBuf, JRDP_PKT_LEN_R, 0, (struct sockaddr *)&from, (socklen_t*)&fromlen );

        if ( n <= 0 )
        {
            printf( "Bad rcvfrom %d.\n", n );
            exit(-1);
        }
        else
        {
            printf( "I receive: %s\n", dataBuf );
            exit(-1);
        }
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
        int stamp_window_size = ( req->flags & ARDP_FLAG_SEND_MY_WINDOW_SIZE ) && ( ptmp->seq == 1 );
        //int stamp_priority = ( req->priority || ardp_priority ) && ( ptmp->seq == 1 );
        //int request_queue_status = ardp_config.client_request_queue_status && ( ptmp->seq == 1 );
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
                /*stamp_priority || */stamp_window_size || req->rcvd_thru )
            new_hlength = 11;
        else
            new_hlength = 9;

        //if ( stamp_priority )
            //new_hlength += 2;
        if ( stamp_window_size )
            new_hlength += 2;
        if ( stamp_message_npkts )
            new_hlength += 2;

        /* Allocate space for the new header */
        ptmp->start = ptmp->text - new_hlength;
        ptmp->length += new_hlength - old_hlength;

        /* Fill out the header */
	    ptmp->start[0] = (char)129;
	    //ptmp->start[1] = ptmp->context_flags; /* fix this when doing security context */
	    ptmp->start[2] = 0; /* flags1 */
	    if (set_ack_bit) 
		ptmp->start[2] |= 0x01;
	    if (stamp_message_npkts) 
		ptmp->start[2] |= 0x04;
	    if (stamp_priority)
		ptmp->start[2] |= 0x08;
	    if (stamp_window_size)
		ptmp->start[2] |= 0x20;
	    /* Octet 3: one option */
	    if (request_queue_status)
		ptmp->start[3] = (unsigned char) 253; /* no arguments */
	    else
		ptmp->start[3] = 0;
	    ptmp->start[4] = (char) new_hlength;
	    /* Connection ID (octets 5 & 6) */
	    memcpy2(ptmp->start+5, &(req->cid));
	    /* Sequence number (octets 7 & 8) */
	    ftmp = htons(ptmp->seq);
	    memcpy2(ptmp->start+7, &ftmp);        
	    if (new_hlength > 9) {
		char *optiondata = ptmp->start + 11; /* where options go */
		/* Received through (octets 9 & 10) */
		ftmp = htons(req->rcvd_thru);
		memcpy2(ptmp->start + 9, &ftmp);  
		if (stamp_message_npkts) {
		    ftmp = htons(req->trns_tot);
		    memcpy2(optiondata, &ftmp);
		    optiondata += 2;
		}
		if (stamp_priority) {
		    if(req->priority) ftmp = htons(req->priority);
		    else ftmp = htons(ardp_priority);
		    memcpy2(optiondata, &ftmp);
		    optiondata += 2;
		} 
		if(stamp_window_size) {
		    ftmp = htons(req->window_sz);
		    memcpy2(optiondata, &ftmp);
		    optiondata += 2;
		}
		assert(optiondata == ptmp->start + new_hlength);
	    }
    }

    return 0;
}

int
jrdp_send( PJREQ req, const char* dname, struct sockaddr_in* dest, int ttwait )
{
    PJPACKET ptmp;

    if ( dname )
        strcpy( &req->peer_hostname, dname );

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
    req->cid = 131488; // jrdp_next_cid();

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
    //APPEND_ITEM( req, jrdp_activeQ );
    //++ardp_activeQ_len;
    //req->wait_till = add_times(ardp__gettimeofday(), req->timeout_adj);
    //if ( jrdp_xmit( req, req->pwindow_sz ) )
    {
        printf( "Xmit packets failed.\n" );
        exit(-1);
    }
    //EXTERN_MUTEXED_UNLOCK(ardp_activeQ);

    if ( ttwait )
        return 1;//ardp_retrieve(req,ttwait);
    else 
        return 999; // This request is still pending.







    /*
    int ns; // Number of bytes actually sent.

    struct sockaddr_in peer;
    memset( &peer, '\000', sizeof(struct sockaddr_in) );

    if ( jrdp_init() )
    {
        printf( "jrdp_send init failed.\n" );
        exit(-1);
    }

    ns = sendto( jrdp_sock, req, 256, 0, (struct sockaddr*)&peer, sizeof(struct sockaddr_in) );

    return 0;
    */
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

