/*
 * This is a sample client program using the JRDP library.
 *
 * The client sends requests to the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>

#include "jrdp.h"

#define SAMPLE_DEFAULT_SUPER_SERVER_HOST (myhostname())
#define SAMPLE_DEFAULT_SERVER_PORT 4007

void p_command_line_preparse( int *argcp, char **argv );
void child_process( int num_loops );

int
main( int argc, char** argv )
{
    int j;
    int num_of_children = 0;
    int status = 0;
    int total_messages = 0;
    pid_t* child_pid = NULL;
	pid_t pid;

    p_command_line_preparse( &argc, argv );
    jrdp_initialize();

    for ( j = 0; j < argc; j++ )
    {
        if ( argv[j][0] == '-' )
        {
            if ( argv[j][1] == 'p' )
                num_of_children = atoi( &(argv[j][2]) );
            else if ( argv[j][1] == 'm' )
                total_messages = atoi( &(argv[j][2]) );
	    }
    }

    if ( !total_messages )
    {
        printf( "Need p and m parameters.\n" );
        exit(0);
    }

    child_pid = (pid_t*)malloc( sizeof(pid_t) * num_of_children );
    memset( child_pid, 0, sizeof(pid_t) * num_of_children );
    for ( j = 0; j < num_of_children; j++ )
    {
        //child_pid[j] = fork();
        //if ( !child_pid[j] )
        {
            child_process( total_messages / num_of_children );
            exit(0);
        }
    }

    for ( j = 0; j < num_of_children; j++ )
    {
        pid = wait(&status);
        if ( ( pid == -1 ) || ( status % 256 ) )
        {
            perror("client");
            exit(1);
        }
    }
    exit(0);
}


void
p_command_line_preparse( int* argcp, char** argv )
{
    int num_args_used = 0;
    char** scanning;
    char** writing;

    /* modifies global data */
    for ( scanning = writing = argv; *scanning; ++scanning )
    {
        /* If a - by itself, or --, then no more arguments */
        if ( strcmp( *scanning, "-" ) == 0 || strncmp( *scanning, "--", 2 ) == 0 )
            break;
        if ( strncmp( *scanning, "-D", 2 ) == 0 )
        {
            printf( "This option enables debug mode, but we do not support this now.\n" );
            // eat this argument
            ++num_args_used;
        }
#ifdef MAX_PRI
        else if ( strncmp( *scanning, "-N", 2 ) == 0 )
        {
            jrdp_priority = MAX_PRI; // Use this if no # given
            sscanf( *scanning, "-N%d", &jrdp_priority );
            if( jrdp_priority > MAX_SPRI )
                jrdp_priority = MAX_PRI;
            if( jrdp_priority < MIN_PRI )
                jrdp_priority = MIN_PRI;
            // eat this argument
            ++num_args_used;
        }
#endif
        else if ( strncmp( *scanning, "-pc", 3 ) == 0 )
        {
            printf( "Do not support -pc now.\n" );
            //p_command_line_config(*scanning + 3);
            ++num_args_used;
        }
        else
        {
            /* shift the arguments forward */
            assert( scanning >= writing );
            if ( scanning > writing )
            {
                *writing = *scanning;
            }
            writing++;
        }
    }
    /* shift remaining arguments */
    while ( *scanning )
        *writing++ = *scanning++;

    *writing = 0;
    *argcp -= num_args_used;

    return;
}


void child_process(int num_loops)
{
    //RREQ req;			/* Request to be sent */
    int length;
    int ret_val;
    char mach_name[80];
    char BUFFER[256];
    int i = 0;
    pid_t pid;
    void *dummy;

    sprintf( mach_name, "%s(%d)", SAMPLE_DEFAULT_SUPER_SERVER_HOST, SAMPLE_DEFAULT_SERVER_PORT );

    /* Loop N times */
    pid = getpid();
    for ( i = 1; i < num_loops + 1; ++i )
    {
        sprintf( BUFFER, "This is the client #%d sending to super server msg #%d\n", (int)pid, i );
        length = strlen(BUFFER);

        if ( ( ret_val = jrdp_send( BUFFER, mach_name, NULL, -1 ) ) )
        {
            printf( "jrdp_send, status err.\n" );
            exit(1);
        }
    }
}
