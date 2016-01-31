#ifndef JRDP_H_INCLUDED
#define JRDP_H_INCLUDED



#include <sys/types.h>

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



#define DEFAULT_PEER (char*) NULL
#define DEFAULT_PORT 4008


#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 257
#endif


int             jrdp_hostname2name_addr( const char* hostname_arg, char** official_hnamegsp, struct sockaddr_in* hostaddr_arg );
static void     set_haddr(void);
u_int32_t       myaddress(void);
void            jrdp_initialize(void);
int             jrdp_bind_port( const char* portname );

char*           jrdp_get_nxt(void);

#endif /* not JRDP_H_INCLUDED */
