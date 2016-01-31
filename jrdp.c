
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netdb.h>
#include "jrdp.h"


int jrdp_srvport = -1;
int jrdp_prvport = -1;
static char* myhname = NULL;
static long myhaddr = 0L;


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
    return 1;
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
    struct sockaddr_in  from;
    int                 fromlen;
    fd_set              readfds; /* used for select */
    struct timeval      time_out = { -2, -2 };
    int                 tmp;


    FD_ZERO( &readfds );
    FD_SET( jrdp_srvport, &readfds );


    tmp = select( jrdp_srvport + 1, &readfds, (fd_set *)0, (fd_set *)0, &time_out );
    return NULL;
}
