/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
/* milli_httpd - pretty small HTTP server
** A combination of
** micro_httpd - really small HTTP server
** and
** mini_httpd - small HTTP server
**
** Copyright ?1999,2000 by Jef Poskanzer <jef@acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <assert.h>
#include <flash_ioctl.h>
#include <sys/ioctl.h>
#include <netconf.h>

typedef unsigned int __u32;   // 1225 ham
#include <ra_ioctl.h>

#include <httpd.h>
//2008.08 magic{
#include <nvram/bcmnvram.h>	//2008.08 magic
#include <arpa/inet.h>	//2008.08 magic

#define csprintf(fmt, args...) do{\
	FILE *cp = fopen("/dev/console", "w");\
	if(cp) {\
		fprintf(cp, fmt, ## args);\
		fclose(cp);\
	}\
}while(0)

#define eprintf2(fmt, args...) do{\
        FILE *ffp = fopen("/tmp/detect_wrong.log", "a+");\
        if(ffp) {\
                fprintf(ffp, fmt, ## args);\
                fclose(ffp);\
        }\
}while(0)

//2008.08 magic}

#ifdef vxworks
static void fcntl(int a, int b, int c) {}
#include <signal.h>
#include <ioLib.h>
#include <sockLib.h>
extern int snprintf(char *str, size_t count, const char *fmt, ...);
extern int strcasecmp(const char *s1, const char *s2); 
extern int strncasecmp(const char *s1, const char *s2, size_t n); 
extern char *strsep(char **stringp, char *delim);
#define socklen_t 		int
#define main			milli
#else
#include <error.h>
#include <sys/signal.h>
#endif

// Added by Joey for ethtool
#include <net/if.h>
#include <ethtool-util.h>
#ifndef SIOCETHTOOL
#define SIOCETHTOOL 0x8946
#endif

#define SIOCDEVPRIVATE  0x89F0
#define ETCPHYRD        14
//#define SIOCGETCPHYRD (SIOCDEVPRIVATE + ETCPHYRD)
#define SIOCGETCPHYRD   0x89FE
//#include "etioctl.h"


#define SERVER_NAME "httpd"
#define SERVER_PORT 80
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

/* A multi-family sockaddr. */
typedef union {
    struct sockaddr sa;
    struct sockaddr_in sa_in;
    } usockaddr;

#include "queue.h"
#define MAX_CONN_ACCEPT 64
#define MAX_CONN_TIMEOUT 60

typedef struct conn_item {
	TAILQ_ENTRY(conn_item) entry;
	int fd;
	usockaddr usa;
} conn_item_t;

typedef struct conn_list {
	TAILQ_HEAD(, conn_item) head;
	int count;
} conn_list_t;

/* Globals. */
static FILE *conn_fp;
static char auth_userid[AUTH_MAX];
static char auth_passwd[AUTH_MAX];
static char auth_realm[AUTH_MAX];
#ifdef TRANSLATE_ON_FLY
char Accept_Language[16];
#endif //TRANSLATE_ON_FLY


/* Forwards. */
static int initialize_listen_socket( usockaddr* usaP );
static int auth_check( char* dirname, char* authorization, char* url);
static void send_authenticate( char* realm );
static void send_error( int status, char* title, char* extra_header, char* text );
static void send_headers( int status, char* title, char* extra_header, char* mime_type );
static int b64_decode( const char* str, unsigned char* space, int size );
static int match( const char* pattern, const char* string );
static int match_one( const char* pattern, int patternlen, const char* string );
static void handle_request(void);

/* added by Joey */
//2008.08 magic{
//int redirect = 1;
int redirect = 0;	
int change_passwd = 0;	
int reget_passwd = 0;	
char url[128];
//2008.08 magic}
char wan_if[16];
int http_port=SERVER_PORT;

/* Added by Joey for handle one people at the same time */
unsigned int login_ip=0; // the logined ip
time_t login_timestamp=0; // the timestamp of the logined ip
unsigned int login_ip_tmp=0; // the ip of the current session.
unsigned int login_try=0;
unsigned int last_login_ip = 0;	// the last logined ip 2008.08 magic

// 2008.08 magic {
time_t request_timestamp = 0;
time_t turn_off_auth_timestamp = 0;
int temp_turn_off_auth = 0;	// for QISxxx.htm pages

void http_login(unsigned int ip, char *url);
void http_login_timeout(unsigned int ip);
void http_logout(unsigned int ip);

static int
initialize_listen_socket( usockaddr* usaP )
    {
    int listen_fd;
    int i;

    memset( usaP, 0, sizeof(usockaddr) );
    usaP->sa.sa_family = AF_INET;
    usaP->sa_in.sin_addr.s_addr = htonl( INADDR_ANY );
    usaP->sa_in.sin_port = htons( http_port );

    listen_fd = socket( usaP->sa.sa_family, SOCK_STREAM, 0 );
    if ( listen_fd < 0 )
	{
	perror( "socket" );
	return -1;
	}
    (void) fcntl( listen_fd, F_SETFD, 1 );
    i = 1;
    if ( setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*) &i, sizeof(i) ) < 0 )
	{
	close(listen_fd);
	perror( "setsockopt" );
	return -1;
	}
    if ( bind( listen_fd, &usaP->sa, sizeof(struct sockaddr_in) ) < 0 )
	{
	close(listen_fd);
	perror( "bind" );
	return -1;
	}
    if ( listen( listen_fd, 1024 ) < 0 )
	{
	close(listen_fd);
	perror( "listen" );
	return -1;
	}
    return listen_fd;
    }


static int
auth_check( char* dirname, char* authorization ,char* url)
    {
    char authinfo[500];
    char* authpass;
    int l;

    /* Is this directory unprotected? */
    if ( !strlen(auth_passwd) )
	/* Yes, let the request go through. */
	return 1;

    /* Basic authorization info? */
    if ( !authorization || strncmp( authorization, "Basic ", 6 ) != 0) 
    {
	printf("### No authorization in http request with %s! ###\n", url);
	send_authenticate( dirname );
	//if(login_ip != 0)	// 2008.08 magic
	http_logout(login_ip_tmp);
	last_login_ip = 0;	// 2008.08 magic
	return 0;
    }

    /* Decode it. */
    l = b64_decode( &(authorization[6]), authinfo, sizeof(authinfo) );
    authinfo[l] = '\0';
    /* Split into user and password. */
    authpass = strchr( authinfo, ':' );
    if ( authpass == (char*) 0 ) {
	/* No colon?  Bogus auth info. */
	printf("### No password information in http request! ###\n");
	send_authenticate( dirname );
	http_logout(login_ip_tmp);
	return 0;
    }
    *authpass++ = '\0';

    /* Is this the right user and password? */
    if ( strcmp( auth_userid, authinfo ) == 0 && strcmp( auth_passwd, authpass ) == 0 )
    {
    	//fprintf(stderr, "login check : %x %x\n", login_ip, last_login_ip);
    	/* Is this is the first login after logout */
    	if (login_ip==0 && last_login_ip==login_ip_tmp)
    	{

		printf("### After logout, The first login ip is the last login ip! ###\n");
		send_authenticate(dirname);
		last_login_ip=0;
		return 0;
    	}
	return 1;
    }

    printf("### userid or password is wrong! ###\n");
    send_authenticate( dirname );
    http_logout(login_ip_tmp);
    return 0;
    }


static void
send_authenticate( char* realm )
    {
    char header[10000];

    (void) snprintf(
	header, sizeof(header), "WWW-Authenticate: Basic realm=\"%s\"", realm );
    send_error( 401, "Unauthorized", header, "Authorization required." );
    }


static void
send_error( int status, char* title, char* extra_header, char* text )
    {
    send_headers( status, title, extra_header, "text/html" );
    (void) fprintf( conn_fp, "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\n<BODY BGCOLOR=\"#cc9999\"><H4>%d %s</H4>\n", status, title, status, title );
    (void) fprintf( conn_fp, "%s\n", text );
    (void) fprintf( conn_fp, "</BODY></HTML>\n" );
    (void) fflush( conn_fp );
    }


static void
send_headers( int status, char* title, char* extra_header, char* mime_type )
    {
    time_t now;
    char timebuf[100];

    (void) fprintf( conn_fp, "%s %d %s\r\n", PROTOCOL, status, title );
    (void) fprintf( conn_fp, "Server: %s\r\n", SERVER_NAME );
    now = time( (time_t*) 0 );
    (void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );
    (void) fprintf( conn_fp, "Date: %s\r\n", timebuf );
    if ( extra_header != (char*) 0 )
	(void) fprintf( conn_fp, "%s\r\n", extra_header );
    if ( mime_type != (char*) 0 )
	(void) fprintf( conn_fp, "Content-Type: %s\r\n", mime_type );
    (void) fprintf( conn_fp, "Connection: close\r\n" );
    (void) fprintf( conn_fp, "\r\n" );
    }


/* Base-64 decoding.  This represents binary data as printable ASCII
** characters.  Three 8-bit binary bytes are turned into four 6-bit
** values, like so:
**
**   [11111111]  [22222222]  [33333333]
**
**   [111111] [112222] [222233] [333333]
**
** Then the 6-bit values are represented using the characters "A-Za-z0-9+/".
*/

static int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };

/* Do base-64 decoding on a string.  Ignore any non-base64 bytes.
** Return the actual number of bytes generated.  The decoded size will
** be at most 3/4 the size of the encoded, and may be smaller if there
** are padding characters (blanks, newlines).
*/
static int
b64_decode( const char* str, unsigned char* space, int size )
    {
    const char* cp;
    int space_idx, phase;
    int d, prev_d=0;
    unsigned char c;

    space_idx = 0;
    phase = 0;
    for ( cp = str; *cp != '\0'; ++cp )
	{
	d = b64_decode_table[(int)*cp];
	if ( d != -1 )
	    {
	    switch ( phase )
		{
		case 0:
		++phase;
		break;
		case 1:
		c = ( ( prev_d << 2 ) | ( ( d & 0x30 ) >> 4 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 2:
		c = ( ( ( prev_d & 0xf ) << 4 ) | ( ( d & 0x3c ) >> 2 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 3:
		c = ( ( ( prev_d & 0x03 ) << 6 ) | d );
		if ( space_idx < size )
		    space[space_idx++] = c;
		phase = 0;
		break;
		}
	    prev_d = d;
	    }
	}
    return space_idx;
    }


/* Simple shell-style filename matcher.  Only does ? * and **, and multiple
** patterns separated by |.  Returns 1 or 0.
*/
int
match( const char* pattern, const char* string )
    {
    const char* or;

    for (;;)
	{
	or = strchr( pattern, '|' );
	if ( or == (char*) 0 )
	    return match_one( pattern, strlen( pattern ), string );
	if ( match_one( pattern, or - pattern, string ) )
	    return 1;
	pattern = or + 1;
	}
    }


static int
match_one( const char* pattern, int patternlen, const char* string )
    {
    const char* p;

    for ( p = pattern; p - pattern < patternlen; ++p, ++string )
	{
	if ( *p == '?' && *string != '\0' )
	    continue;
	if ( *p == '*' )
	    {
	    int i, pl;
	    ++p;
	    if ( *p == '*' )
		{
		/* Double-wildcard matches anything. */
		++p;
		i = strlen( string );
		}
	    else
		/* Single-wildcard matches anything but slash. */
		i = strcspn( string, "/" );
	    pl = patternlen - ( p - pattern );
	    for ( ; i >= 0; --i )
		if ( match_one( p, pl, &(string[i]) ) )
		    return 1;
	    return 0;
	    }
	if ( *p != *string )
	    return 0;
	}
    if ( *string == '\0' )
	return 1;
    return 0;
    }

int do_fwrite(const char *buffer, int len, FILE *stream)
{
	int n = len;
	int r = 0;
 
	while (n > 0) {
		r = fwrite(buffer, 1, n, stream);
		if ((r == 0) && (errno != EINTR)) return -1;
		buffer += r;
		n -= r;
	}

	return r;
}
 
void do_file(char *path, FILE *stream)
{
	FILE *fp;
	char buf[1024];
	int nr;

	if ((fp = fopen(path, "r")) != NULL) {
		while ((nr = fread(buf, 1, sizeof(buf), fp)) > 0)
			do_fwrite(buf, nr, stream);
		fclose(fp);
	}
}

/*
void
do_file(char *path, FILE *stream)
{
	FILE *fp;
	int c;

	if (!(fp = fopen(path, "r")))
		return;
	while ((c = getc(fp)) != EOF)
		fputc(c, stream);
	fclose(fp);
}
*/

static void
handle_request(void)
{
    char line[10000], *cur;
    char *method, *path, *protocol, *authorization, *boundary, *alang;
    char *cp;
    char *file;
    int len;
    struct mime_handler *handler;
    int cl = 0, flags;

    /* Initialize the request variables. */
    authorization = boundary = NULL;
    bzero( line, sizeof line );

    /* Parse the first line of the request. */
    if ( fgets( line, sizeof(line), conn_fp ) == (char*) 0 ) {
	send_error( 400, "Bad Request", (char*) 0, "No request found." );
	return;
    }

    method = path = line;
    strsep(&path, " ");
    //while (*path == ' ') path++;
    while (path && *path == ' ') path++;        // oleg patch
    protocol = path;
    strsep(&protocol, " ");
    //while (*protocol == ' ') protocol++;
    while (protocol && *protocol == ' ') protocol++;    // oleg pat
    cp = protocol;
    strsep(&cp, " ");
    if ( !method || !path || !protocol ) {
	send_error( 400, "Bad Request", (char*) 0, "Can't parse request." );
	return;
    }
    cur = protocol + strlen(protocol) + 1;

#ifdef TRANSLATE_ON_FLY
        memset(Accept_Language, 0, sizeof(Accept_Language));
#endif


    
    /* Parse the rest of the request headers. */
    while ( fgets( cur, line + sizeof(line) - cur, conn_fp ) != (char*) 0 )
	{
		

	if ( strcmp( cur, "\n" ) == 0 || strcmp( cur, "\r\n" ) == 0 ){
	    break;
	}
#ifdef TRANSLATE_ON_FLY
        else if ( strncasecmp( cur, "Accept-Language:",16) ==0){
                char *p;
                struct language_table *pLang;
                char lang_buf[256];
                memset(lang_buf, 0, sizeof(lang_buf));
                alang = &cur[16];
                strcpy(lang_buf, alang);
                p = lang_buf;
                while(p != NULL)
                {
                        p = strtok (p, "\r\n ,;");
                        if (p == NULL)  break;
								//2008.11 magic{
									int i,len=strlen(p);
										
										for(i=0;i<len;++i)
										    if(isupper(p[i])){
												p[i]=tolower(p[i]);
											  }													
								//2008.11 magic}
                        for (pLang = language_tables; pLang->Lang != NULL; ++pLang)
                        {
                                if(strcasecmp(p, pLang->Lang)==0)
                                {
                                        snprintf(Accept_Language,sizeof(Accept_Language),"%s",pLang->Target_Lang);
                                        if (is_firsttime ())    {
//                                                printf ("x_Setting != 1, preferred_lang -> %s\n", Accept_Language);
                                                nvram_set_x ("", "preferred_lang", Accept_Language);
                                        }
                                        break;
                                }
                        }

                        if (Accept_Language[0] != 0)    {
                                break;
                        }
                        p+=strlen(p)+1;
                }

                if (Accept_Language[0] == 0)    {
                        // If all language setting of user's browser are not supported, use English.
//                        printf ("Auto detect language failed. Use English.\n");                        
			strcpy (Accept_Language, "EN");

	// 2008.10 magic {
				if(is_firsttime())
					nvram_set("preferred_lang", "EN");
	// 2008.10 magic }
                }
        }
#endif


	else if ( strncasecmp( cur, "Authorization:", 14 ) == 0 )
	    {
	    cp = &cur[14];
	    cp += strspn( cp, " \t" );
	    authorization = cp;
	    cur = cp + strlen(cp) + 1;
	    }
	else if (strncasecmp( cur, "Content-Length:", 15 ) == 0) {
		cp = &cur[15];
		cp += strspn( cp, " \t" );
		cl = strtoul( cp, NULL, 0 );
	}
	else if ((cp = strstr( cur, "boundary=" ))) {
            boundary = &cp[9];
	    for( cp = cp + 9; *cp && *cp != '\r' && *cp != '\n'; cp++ );
	    *cp = '\0';
	    cur = ++cp;
	}

	}

    if ( strcasecmp( method, "get" ) != 0 && strcasecmp(method, "post") != 0 ) {
	send_error( 501, "Not Implemented", (char*) 0, "That method is not implemented." );
	return;
    }
    if ( path[0] != '/' ) {
	send_error( 400, "Bad Request", (char*) 0, "Bad filename." );
	return;
    }
    file = &(path[1]);
    len = strlen( file );
    if ( file[0] == '/' || strcmp( file, ".." ) == 0 || strncmp( file, "../", 3 ) == 0 || strstr( file, "/../" ) != (char*) 0 || strcmp( &(file[len-3]), "/.." ) == 0 ) {
	send_error( 400, "Bad Request", (char*) 0, "Illegal filename." );
	return;
    }

//2008.08 magic{
	if(file[0] == '\0' || file[len-1] == '/')
		file = "index.asp";
	
// 2007.11 James. {
	char *query;
	int file_len;
	
	memset(url, 0, 128);
	if((query = index(file, '?')) != NULL){
		file_len = strlen(file)-strlen(query);
		
		strncpy(url, file, file_len);
	}
	else
	{
		strcpy(url, file);
	}
// 2007.11 James. }
	
	http_login_timeout(login_ip_tmp);	// 2008.07 James.
	
	if(http_port == SERVER_PORT && http_login_check() == 3){
		if((strstr(url, ".htm") != NULL
					&& !(!strcmp(url, "error_page.htm")
						|| (strstr(url, "QIS_") != NULL && nvram_match("x_Setting", "0") && login_ip == 0)
						|| (strstr(url, "survey.htm") != NULL && nvram_match("r_Setting", "0") && login_ip == 0)//0925
						|| (strstr(url, "ureip.asp") != NULL && nvram_match("r_Setting", "0") && login_ip == 0)//1202
						|| (strstr(url, "Logout.asp") != NULL && nvram_match("r_Setting", "0") && login_ip == 0)//1202
						|| (strstr(url, "Advanced_OperationMode_Content.asp") != NULL && nvram_match("r_Setting", "0") && login_ip == 0)//0625
						|| !strcmp(url, "gotoHomePage.htm")
						|| !strcmp(url, "ure_success.htm")
						|| !strcmp(url, "remote.asp")
						)
					)
				|| (strstr(url, ".asp") != NULL && login_ip != 0)
				){
			file = "Nologin.asp";
			
			memset(url, 0, 128);
			strcpy(url, file);
		}
	}
	
	for(handler = &mime_handlers[0]; handler->pattern; handler++) 
	{
// 2007.11 James. for the correct result of match(). {
		//if (match(handler->pattern, file)) 
		if(match(handler->pattern, url))
// 2007.11 James. for the correct result of match(). }
		{
// 2007.11 James. for QISxxx.htm pages {
			request_timestamp = time((time_t *)0);
			
			int login_state = http_login_check();

			if((login_state == 1 || login_state == 2)
					&& !nvram_match("x_Setting", "1") //2008.11 magic
					&& ( !strcmp(url, "survey.htm")	// 1008 update
					/* modify QIS authentication flow */
					//&& (strstr(url, "QIS_") != NULL   // to avoid the interference of the other logined browser. 2008.11 magic
					//		|| !strcmp(url, "survey.htm")	// 1008 update
							|| !strcmp(url, "ureip.asp")	// 1202 update
							|| !strcmp(url, "Logout.asp")	// 1202 update
							|| !strcmp(url, "aplist.asp")	// 1008 update
							|| !strcmp(url, "wlconn_apply.htm")	// 1013 update
							|| !strcmp(url, "result_of_get_changed_status.asp")
							|| !strcmp(url, "result_of_get_changed_status_QIS.asp")
							|| !strcmp(url, "detectWAN.asp")
							|| !strcmp(url, "WPS_info.asp")
							|| !strcmp(url, "WAN_info.asp")
							|| !strcmp(url, "result_of_detect_client.asp")
                                                	|| !strcmp(url, "start_apply.htm")
                                                	|| !strcmp(url, "start_apply2.htm")
// 2010.09 James. {
							|| !strcmp(url, "detectWAN2.asp")
							|| !strcmp(url, "automac.asp")
							|| !strcmp(url, "setting_lan.htm")
// 2010.09 James. }
							)
					){
				turn_off_auth_timestamp = request_timestamp;
				temp_turn_off_auth = 1;
				redirect = 0;
			}
			else if(!strcmp(url, "error_page.htm")
					|| !strcmp(url, "jquery.js") // 2010.09 James.
					|| !strcmp(url, "Nologin.asp")
					|| !strcmp(url, "gotoHomePage.htm")
					|| !strcmp(url, "ure_success.htm")
					|| !strcmp(url, "remote.asp")
					){
				;	// do nothing.
			}
			else if(login_state == 2
					&& !strcmp(url, "Logout.asp")){
				turn_off_auth_timestamp = 0;
				temp_turn_off_auth = 0;
				redirect = 1;
			}
			else if(strstr(url, ".asp") != NULL
					|| strstr(url, ".cgi") != NULL
					|| strstr(url, ".htm") != NULL
					|| strstr(url, ".CFG") != NULL
					|| strstr(url, "QIS_") != NULL  /* modify QIS authentication flow */
					){
				switch(login_state){
					case 0:
						return;
					case 1:
					case 2:
						turn_off_auth_timestamp = 0;
						temp_turn_off_auth = 0;
						redirect = 0;
						break;
					case 3:
						printf("System error! the url would be changed to Nologin.asp in this case!\n");
						break;
					default:
						printf("System error! the login_state is wrong!\n");
				}
			}
			else if(login_state == 2
					&& temp_turn_off_auth
					&& (unsigned long)(request_timestamp-turn_off_auth_timestamp) > 10
					){
				http_logout(login_ip_tmp);
				turn_off_auth_timestamp = 0;
				temp_turn_off_auth = 0;
				redirect = 0;
			}
/*W7 IOT patch start*/
                        if (handler->auth) {
                                if (!temp_turn_off_auth) {
                                        handler->auth(auth_userid, auth_passwd, auth_realm);
                                        if (!auth_check(auth_realm, authorization, url))
                                                return;
                                }

                                if (!redirect)
                                {
                                        if (is_firsttime ())
                                        {
                                                if (strstr(url, "QIS_wizard.htm"))
                                                        ;
                                                else if (       strcasestr(url, ".asp") != NULL ||
                                                                strcasestr(url, ".cgi") != NULL ||
                                                                strcasestr(url, ".htm") != NULL ||
                                                                strcasestr(url, ".CFG") != NULL )
                                                        http_login(login_ip_tmp, url);
                                        }
                                        else
                                                http_login(login_ip_tmp, url);
                                }
                        }

/*W7 IOT patch end*/

			if(strcasecmp(method, "post") == 0 && !handler->input){
				send_error(501, "Not Implemented", NULL, "That method is not implemented.");
				return;
			}
// 2007.11 James. for QISxxx.htm pages }
			
			if(handler->input){
				handler->input(file, conn_fp, cl, boundary);
#if defined(linux)
				if((flags = fcntl(fileno(conn_fp), F_GETFL)) != -1 &&
						fcntl(fileno(conn_fp), F_SETFL, flags | O_NONBLOCK) != -1){
					/* Read up to two more characters */
					if(fgetc(conn_fp) != EOF)
						(void)fgetc(conn_fp);
					
					fcntl(fileno(conn_fp), F_SETFL, flags);
				}
#elif defined(vxworks)
				flags = 1;
				if(ioctl(fileno(conn_fp), FIONBIO, (int) &flags) != -1){
					/* Read up to two more characters */
					if(fgetc(conn_fp) != EOF)
						(void)fgetc(conn_fp);
					flags = 0;
					ioctl(fileno(conn_fp), FIONBIO, (int) &flags);
				}
#endif
			}
			
			send_headers( 200, "Ok", handler->extra_header, handler->mime_type );
			if(handler->output){
				handler->output(file, conn_fp);
			}
			
			break;
		}
	}
	
	if(!handler->pattern) 
		send_error( 404, "Not Found", (char*) 0, "File not found." );
	
	if(!strcmp(file, "Logout.asp")){
		http_logout(login_ip_tmp);
	}
	
	if(!strcmp(file, "Reboot.asp")){
		system("reboot");
	}
//2008.08 magic}
}

//2008 magic{
void http_login_cache(usockaddr *u){
	struct in_addr temp_ip_addr;
	char *temp_ip_str;
	
	login_ip_tmp = (unsigned int)(u->sa_in.sin_addr.s_addr);
	temp_ip_addr.s_addr = login_ip_tmp;
	temp_ip_str = inet_ntoa(temp_ip_addr);
}


void http_login(unsigned int ip, char *url){
	struct in_addr login_ip_addr;
	char *login_ip_str;
	
	if(http_port != SERVER_PORT || ip == 0x100007f)
		return;
	
	login_ip = ip;
	last_login_ip = 0;
	
	login_ip_addr.s_addr = login_ip;
	login_ip_str = inet_ntoa(login_ip_addr);
	nvram_set("login_ip_srt", login_ip_str);
	
	if(strcmp(url, "result_of_get_changed_status.asp")
			&& strcmp(url, "WPS_info.asp")
			&& strcmp(url, "WAN_info.asp")) //2008.11 magic
		login_timestamp = time((time_t *)0);
	
	char login_ipstr[32], login_timestampstr[32];
	
	memset(login_ipstr, 0, 32);
	sprintf(login_ipstr, "%u", login_ip);
	nvram_set("login_ip", login_ipstr);
	
	if(strcmp(url, "result_of_get_changed_status.asp")
			&& strcmp(url, "WPS_info.asp")
			&& strcmp(url, "WAN_info.asp")){//2008.11 magic
		memset(login_timestampstr, 0, 32);
		sprintf(login_timestampstr, "%lu", login_timestamp);
		nvram_set("login_timestamp", login_timestampstr);
	}
}

// 0: can not login, 1: can login, 2: loginer, 3: not loginer.
int http_login_check(void){
	time_t now;
	
	if(http_port != SERVER_PORT || login_ip_tmp == 0x100007f)
		//return 1;
		return 0;	// 2008.01 James.
	
	//http_login_timeout(login_ip_tmp);	// 2008.07 James.
	
	if(login_ip == 0)
		return 1;
	else if(login_ip == login_ip_tmp)
		return 2;
	
	return 3;
}

void http_login_timeout(unsigned int ip)
{
	time_t now;
	
	time(&now);
	
// 2007.10 James. for really logout. {
	//if (login_ip!=ip && (unsigned long)(now-login_timestamp) > 60) //one minitues
	if((login_ip != 0 && login_ip != ip) && (unsigned long)(now-login_timestamp) > 60) //one minitues
// 2007.10 James }
	{
		http_logout(login_ip);
	}
}

void http_logout(unsigned int ip){
	if(ip == login_ip){
		last_login_ip = login_ip;
		login_ip = 0;
		login_timestamp = 0;
		
		nvram_set("login_ip", "");
		nvram_set("login_timestamp", "");
		
// 2008.03 James. {
		if(change_passwd == 1){
			change_passwd = 0;
			reget_passwd = 1;
		}
// 2008.03 James. }
	}
}
//2008 magic}


int is_auth(void)
{
	if (http_port==SERVER_PORT ||
		strcmp(nvram_get_x("PrinterStatus", "usb_webhttpcheck_x"), "1")==0) return 1;
	else return 0;
}

//#define EVBOARD         1       // tmp add for ev-board

int esw_fd;

int
switch_init(void)
{
        esw_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (esw_fd < 0) {
                perror("socket");
                return -1;
        }
        return 0;
}

void
switch_fini(void)
{
        close(esw_fd);
}

int ra3052_reg_read(int offset, int *value)
{
        struct ifreq ifr;
        esw_reg reg;

        if (value == NULL)
                return -1;
        reg.off = offset;
        strncpy(ifr.ifr_name, "eth2", 5);
        ifr.ifr_data = &reg;
        if (-1 == ioctl(esw_fd, RAETH_ESW_REG_READ, &ifr)) {
                perror("ioctl");
                close(esw_fd);
                return -1;
        }
        *value = reg.val;
        return 0;
}

int is_phyconnected(void)       // ASUS add
{
        int val = 0, idx = 1, ret;

        if(switch_init() < 0)
                return 0;

        if(ra3052_reg_read(0x80, &val) != 0)
                return 0;

        //printf("get val: %x\n", val);   // tmp test
/*
#ifdef EVBOARD
        printf("ev: "); // tmp test

        ret = ((val & (idx << 25))) >> 25;

#else   // rt-n12U
*/
        //ret = ((val & (idx << 29))) >> 29;
        ret = ((val & (idx << 25))) >> 25;	// N10P WLLLL
//#endif
        switch_fini();
        if(ret)
        {
                //nvram_set_x("", "wan_status_t", "Connected");
                ;
        }
        else
        {
                nvram_set_x("", "wan_status_t", "Disconnected");
                nvram_set_x("", "wan_reason_t", "Cable is not attached");
        }

        return ret;
}

int is_fileexist(char *filename)
{
	FILE *fp;

	fp=fopen(filename, "r");

	if (fp==NULL) return 0;
	fclose(fp);
	return 1;
}

int is_connected(void)
{
	FILE *fp;
	char line[128], *reason;

	/* check if physical connection exist */
	if (!is_phyconnected()) return 0;
	
	/* check if connection static is CONNECTED */
	if (strcmp(nvram_get_x("WANIPAddress", "wan_status_t"), "Disconnected")==0)
	{
		fp = fopen("/tmp/wanstatus.log", "r");
		if (fp!=NULL)
		{
			fgets(line, sizeof(line), fp);
			reason = strchr(line, ',');
			if (reason!=NULL) nvram_set_x("", "wan_reason_t", reason+1);
			fclose(fp);
		}
		
		return 0;
	}
        //printf("Connected\n");
	return 1;		
}

int is_firsttime(void)
{
	if (strcmp(nvram_get_x("General", "x_Setting"), "1")==0) return 0;
	else return 1;
}

#ifdef TRANSLATE_ON_FLY
int
load_dictionary (char *lang, pkw_t pkw)
{
        char dfn[16];
        char *p, *q;
        FILE *dfp = NULL;
        int dict_size = 0;
        struct timeval tv1, tv2;
        const char *eng_dict = "EN.dict";
#ifndef RELOAD_DICT
        static char loaded_dict[12] = {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
#endif  // RELOAD_DICT

        gettimeofday (&tv1, NULL);
        if (lang == NULL || (lang != NULL && strlen (lang) == 0))       {
                // if "lang" is invalid, use English as default
                snprintf (dfn, sizeof (dfn), eng_dict);
        } else {
                snprintf (dfn, sizeof (dfn), "%s.dict", lang);
        }

#ifndef RELOAD_DICT
//        printf ("loaded_dict (%s) v.s. dfn (%s)\n", loaded_dict, dfn);
        if (strcmp (dfn, loaded_dict) == 0)     {
                return 1;
        }
        release_dictionary (pkw);
#endif  // RELOAD_DICT

        do      {
//                 printf("Open (%s) dictionary file.\n", dfn);
                dfp = fopen (dfn, "r");
                if (dfp != NULL)        {
#ifndef RELOAD_DICT
                        snprintf (loaded_dict, sizeof (loaded_dict), "%s", dfn);
#endif  // RELOAD_DICT
                        break;
                }

//                printf ("Open (%s) failure. errno %d (%s)\n", dfn, errno, strerror (errno));
                if (dfp == NULL && strcmp (dfn, eng_dict) == 0) {
                        return 0;
                } else {
                        // If we can't open specified language file, use English as default
                        snprintf (dfn, sizeof (dfn), eng_dict);
                }
        } while (1);

        memset (pkw, 0, sizeof (kw_t));
        fseek (dfp, 0L, SEEK_END);
        dict_size = ftell (dfp) + 128;
//        printf ("dict_size %d\n", dict_size);
        REALLOC_VECTOR (pkw->idx, pkw->len, pkw->tlen, sizeof (unsigned char*));
        pkw->buf = q = malloc (dict_size);

        fseek (dfp, 0L, SEEK_SET);
        while (!feof (dfp))     {
                // if pkw->idx is not enough, add 32 item to pkw->idx
                REALLOC_VECTOR (pkw->idx, pkw->len, pkw->tlen, sizeof (unsigned char*));

                fscanf (dfp, "%[^\n]%*c", q);
                if ((p = strchr (q, '=')) != NULL)      {
                        pkw->idx[pkw->len] = q;
                        pkw->len++;
                        q = p + strlen (p);
                        *q = '\0';
                        q++;
                }
        }

        fclose (dfp);
        gettimeofday (&tv2, NULL);
	//printf("Load %d keywords spent %ldms\n", pkw->len, ((tv2.tv_sec * 1000000 + tv2.tv_usec) - (tv1.tv_sec * 1000000 + tv1.tv_usec)) / 1000);

        return 1;
}


void
release_dictionary (pkw_t pkw)
{
        if (pkw == NULL)        {
                return;
        }

        pkw->len = pkw->tlen = 0;

        if (pkw->idx != NULL)   {
                free (pkw->idx);
                pkw->idx = NULL;
        }

        if (pkw->buf != NULL)   {
                free (pkw->buf);
                pkw->buf = NULL;
        }
}

char*
search_desc (pkw_t pkw, char *name)
{
        int i;
        char *p, *ret = NULL;

        if (pkw == NULL || (pkw != NULL && pkw->len <= 0))      {
                return NULL;
        }
        for (i = 0; i < pkw->len; ++i)  {
                p = pkw->idx[i];
                if (strncmp (name, p, strlen (name)) == 0)      {
                        ret = p + strlen (name);
                        break;
                }
        }

        return ret;
}
#endif //TRANSLATE_ON_FLY

void reapchild()	// 0527 add
{
	signal(SIGCHLD, reapchild);
	wait(NULL);
}

int main(int argc, char **argv)
{
	usockaddr usa;
	int listen_fd;
	socklen_t sz = sizeof(usa);
	char pidfile[32];
	fd_set active_rfds;
	conn_list_t pool;

	// Added by Joey for handling WAN Interface 
	// usage: httpd [wan interface] [port]
	if (argc>2) http_port=atoi(argv[2]);
	if (argc>1) strcpy(wan_if, argv[1]);
	else strcpy(wan_if, "");

	//websSetVer();

	//2008.08 magic
	nvram_unset("login_timestamp");
	nvram_unset("login_ip");
	nvram_unset("login_ip_str");

	/* Ignore broken pipes */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, reapchild);	// 0527 add

	/* Initialize listen socket */
	if ((listen_fd = initialize_listen_socket(&usa)) < 2) {
		fprintf(stderr, "can't bind to any address\n" );
		exit(errno);
	}

#if !defined(DEBUG) && !defined(vxworks)
	{
	FILE *pid_fp;
	/* Daemonize and log PID */
	//if (http_port==SERVER_PORT)
	//{
/*
	printf("httpd daemonize\n");	// tmp test
		if (daemon(1, 1) == -1) 
		{
			perror("daemon");
			exit(errno);
		}
*/
	//}
	if (http_port==SERVER_PORT)
		strcpy(pidfile, "/var/run/httpd.pid");
	else sprintf(pidfile, "/var/run/httpd-%d.pid", http_port);

	if (!(pid_fp = fopen(pidfile, "w"))) {
		perror(pidfile);
		return errno;
	}
	fprintf(pid_fp, "%d", getpid());
	fclose(pid_fp);
	}
#endif

	/* Init connection pool */
	FD_ZERO(&active_rfds);
	TAILQ_INIT(&pool.head);
	pool.count = 0;

	/* Loop forever handling requests */
	for (;;) {
		int max_fd, count;
		struct timeval tv;
		fd_set rfds;
		conn_item_t *item, *next;

		rfds = active_rfds;
		if (pool.count < MAX_CONN_ACCEPT) {
			FD_SET(listen_fd, &rfds);
			max_fd = listen_fd;
		} else  max_fd = -1;
		TAILQ_FOREACH(item, &pool.head, entry)
			max_fd = (item->fd > max_fd) ? item->fd : max_fd;

		/* Wait for new connection or incoming request */
		tv.tv_sec = MAX_CONN_TIMEOUT;
		tv.tv_usec = 0;
		while ((count = select(max_fd + 1, &rfds, NULL, NULL, &tv)) < 0 && errno == EINTR)
			continue;
		if (count < 0) {
			perror("select");
 			return errno;
 		}

		/* Check and accept new connection */
		if (count && FD_ISSET(listen_fd, &rfds)) {
			item = malloc(sizeof(*item));
			if (item == NULL) {
				perror("malloc");
				return errno;
			}
			while ((item->fd = accept(listen_fd, &item->usa.sa, &sz)) < 0 && item->fd == EINTR)
				continue;
			if (item->fd < 0) {
				perror("accept");
				return errno;
			}

			/* Add to active connections */
			FD_SET(item->fd, &active_rfds);
			TAILQ_INSERT_TAIL(&pool.head, item, entry);
			pool.count++;

			/* Continue waiting over again */
			continue;
		}

		/* Check and process pending or expired requests */
		TAILQ_FOREACH_SAFE(item, &pool.head, entry, next) {
			if (count && !FD_ISSET(item->fd, &rfds))
				continue;

			/* Delete from active connections */
			FD_CLR(item->fd, &active_rfds);
			TAILQ_REMOVE(&pool.head, item, entry);
			pool.count--;

			/* Process request if any or close timed out */
			if (count) {
				if (!(conn_fp = fdopen(item->fd, "r+"))) {
					perror("fdopen");
					return errno;
				}
				http_login_cache(&item->usa);
				handle_request();
				fflush(conn_fp);
				fclose(conn_fp);

				/* Skip the rest of */
				if (--count == 0)
					next = NULL;
			} else
				close(item->fd);

			free(item);
 		}
	}

	shutdown(listen_fd, 2);
	close(listen_fd);

	return 0;
}

# if 0
int main(int argc, char **argv)
{
	usockaddr usa;
	int listen_fd;
	int conn_fd;
	socklen_t sz = sizeof(usa);
	char pidfile[32];

	// Added by Joey for handling WAN Interface 
	// usage: httpd [wan interface] [port]
	if (argc>2) http_port=atoi(argv[2]);
	if (argc>1) strcpy(wan_if, argv[1]);
	else strcpy(wan_if, "");

	//websSetVer();

	//2008.08 magic
	nvram_unset("login_timestamp");
	nvram_unset("login_ip");
	nvram_unset("login_ip_str");

	/* Ignore broken pipes */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, reapchild);	// 0527 add

	/* Initialize listen socket */
	if ((listen_fd = initialize_listen_socket(&usa)) < 2) {
		fprintf(stderr, "can't bind to any address\n" );
		exit(errno);
	}

#if !defined(DEBUG) && !defined(vxworks)
	{
	FILE *pid_fp;
	/* Daemonize and log PID */
	//if (http_port==SERVER_PORT)
	//{
/*
	printf("httpd daemonize\n");	// tmp test
		if (daemon(1, 1) == -1) 
		{
			perror("daemon");
			exit(errno);
		}
*/
	//}
	if (http_port==SERVER_PORT)
		strcpy(pidfile, "/var/run/httpd.pid");
	else sprintf(pidfile, "/var/run/httpd-%d.pid", http_port);

	if (!(pid_fp = fopen(pidfile, "w"))) {
		perror(pidfile);
		return errno;
	}
	fprintf(pid_fp, "%d", getpid());
	fclose(pid_fp);
	}
#endif

	/* Loop forever handling requests */
	for (;;) {
		if ((conn_fd = accept(listen_fd, &usa.sa, &sz)) < 0) {
			perror("accept");
			return errno;
		}
		if (!(conn_fp = fdopen(conn_fd, "r+"))) {
			perror("fdopen");
			return errno;
		}		
		http_login_cache(&usa);
		handle_request();
		fflush(conn_fp);
		fclose(conn_fp);
	}

	shutdown(listen_fd, 2);
	close(listen_fd);

	return 0;
}
#endif
