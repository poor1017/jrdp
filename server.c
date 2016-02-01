/*
 * This is a sample server program using the JRDP library.
 * 
 * This server receives and replies to requests.
 *
 * This server runs forever; have to kill it.
 */


#include <stdio.h>
#include "jrdp.h" 

#define SAMPLE_DEFAULT_SERVER_PORT 4007

int
main( int argc, char** argv )
{
    int i;
    int p;
    //RREQ req;
    char port[80];
    int retval;
    char BUFFER[256];
    int length = 0;

    printf( "JRDP sample server starting up. Don't forget to manually kill this\n" );
    printf( "when you're done, or it will go on running forever.\n" );

    jrdp_initialize();

    sprintf( port, "#%d", SAMPLE_DEFAULT_SERVER_PORT );

    p = jrdp_bind_port(port);

    printf( "\nSample JRDP Server: listening on port %d\n", p );

    for( i = 1; ; ++i )
    {
        char* data = jrdp_get_nxt();
        /*
        req = ardp_get_nxt();

        printf("ARDP sample server: Got a request:\n"); 
        get_data(req,stdout);  

        sprintf(BUFFER, "This is the sample ARDP server sending reply # %d.\n",i);
        length = strlen(BUFFER); 

        if ((retval= ardp_breply(req, ARDP_R_COMPLETE, BUFFER, length)))
        {
            rfprintf(stderr,"ardp_breply() failed witherror number %d \n",
            retval);
            exit(1);
        }
        */
    }
}
