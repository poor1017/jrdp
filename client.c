/*
 * This is a sample client program using the JRDP library.
 *
 * The client sends requests to the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


void p_command_line_preparse(int *argcp, char **argv);

int
main( int argc, char** argv )
{
    int num_of_children = 0;
    int status = 0;
    int total_messages = 0;
    pid_t* child_pid = NULL;
	pid_t pid;

    /* This parses the standard Prospero and GOST and ARDP arguments.
       This handles the -pc configuration arguments and the -D debugging
       flag.   Try -D9 for lots of detail about ARDP. */
    p_command_line_preparse(&argc, argv);
    ardp_initialize();

    for ( int j = 0; j < argc; j++) {
	if (argv[j][0] == '-') {
	    if (argv[j][1] == 'p')
		num_of_children = atoi(&(argv[j][2]));
	    else if (argv[j][1] == 'm')
		total_messages = atoi(&(argv[j][2]));
	} /* if */
    } /* for */

    if (!total_messages) {
	//puts(usage);
	exit(0);
    } /* if */
    child_pid = (pid_t *)malloc(sizeof(pid_t) * num_of_children);
    memset(child_pid, 0, sizeof(pid_t) * num_of_children);
    for ( int j = 0; j < num_of_children; j++) {
	child_pid[j] = fork();
	if (!child_pid[j]) {	/* Child process */
	    child_process(total_messages / num_of_children);
	    exit(0);
	} /* if */
    } /* for */

    for ( int j = 0; j < num_of_children; j++) {
	pid = wait(&status);
	if ((pid == -1) || (status % 256)) {
	    perror("client");
	    exit(1);
	} /* if */
    } /* for */
    exit(0);
}


void
p_command_line_preparse( int* argcp, char** argv )
{
    int num_args_used = 0;
    char **scanning; char **writing;

    /* modifies global data */
    for (scanning = writing = argv; *scanning; ++scanning) {
        /* If a - by itself, or --, then no more arguments */
        if (strequal(*scanning, "-") ||
                strnequal(*scanning, "--", 2)) {
            break;
        }
        /*
        if (strnequal(*scanning, "-D", 2)) {
            pfs_debug = 1; // Default debug level
            qsscanf(*scanning,"-D%d",&pfs_debug);
	    ardp_debug = pfs_debug;
            // eat this argument
            ++num_args_used;
	    
        }
        */
#ifdef ARDP_MAX_PRI
        else if (strnequal(*scanning, "-N", 2)) {
            ardp_priority = ARDP_MAX_PRI; /* Use this if no # given */
            qsscanf(*scanning,"-N%d", &ardp_priority);
            if(ardp_priority > ARDP_MAX_SPRI) 
                ardp_priority = ARDP_MAX_PRI;
            if(ardp_priority < ARDP_MIN_PRI) 
                ardp_priority = ARDP_MIN_PRI;
            /* eat this argument */
            ++num_args_used;
        }
#endif
        else if (strnequal(*scanning, "-pc", 3)) {
            p_command_line_config(*scanning + 3);
            ++num_args_used;
        }
        else {
            /* shift the arguments forward */
            assert (scanning >= writing);
            if (scanning > writing) {
                *writing = *scanning;
            }
            writing++;
        }
    }
    /* shift remaining arguments */
    while (*scanning)
        *writing++ = *scanning++;

    *writing = 0;               /* finish off the written list */
    *argcp -= num_args_used;    /* reset caller's ARGC appropriately, by
                                   removing all consumed arguments. */
}
