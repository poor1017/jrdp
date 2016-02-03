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

void get_data( PJREQ req, FILE* out );

int
main( int argc, char** argv )
{
    int i;
    int p;
    PJREQ req;
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
        req = jrdp_get_nxt_blocking();

        printf( "\nJRDP sample server: Got a request:\n");


        get_data(req,stdout);  

        sprintf(BUFFER, "This is the sample ARDP server sending reply # %d.\n",i);
        length = strlen(BUFFER); 

        /*
        if ((retval= ardp_breply(req, ARDP_R_COMPLETE, BUFFER, length)))
        {
            rfprintf(stderr,"ardp_breply() failed witherror number %d \n",
            retval);
            exit(1);
        }
        */
    }
}

void
get_data( PJREQ req, FILE* out )
{
    struct jpacket* pptr;
    int data_length = 0; // length of the payload.
    int total_data = 0;

    printf( "Retrieving data from received JRDP message:\n");
    for ( pptr = req->rcvd; pptr; pptr = pptr->next )
    {
        data_length = pptr->length - ( pptr->text - pptr->start );
        total_data += data_length;
        /* Arguments to fwrite(): pointer, size, nitems, stream */
        //fwrite(pptr->text, sizeof (char), data_length, out);
        printf( "%s", pptr->text );
    }
    printf( "The message contained %d bytes of data (payload).\n", total_data );
}
