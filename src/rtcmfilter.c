/*
 * Filter to clean up the output from a GNSS data source such as the U-Blox
 * ZED-F9P.
 *
 * The filter connects to the data source and reads the stream of messages
 * that it sends out.  It takes any RTCM messages or fragments of the same
 * and writes them to the Standard Output channel stdout.  It discards any
 * other messages.  This is useful when your data source is putting out
 * all sorts of messages but you are sending to an NTRIP caster that is
 * only expecting RTCM messages.  Such casters have been known to fail when
 * presented with a large number of non-RTCM messages.  Filtering them out
 * locally also reduces your outgoing network bandwidth requirements.
 *
 * A verbose mode is provided to aid debugging a new installation.  In this
 * mode the filter displays the first 50 RTCM messages and any other messages
 * that appear between them.  To prevent endless output, if no RTCM messages
 * are seen, it displays the first 50 input buffers.
 *
 * This program is a hacked version of the BKG NTRIP server, which is
 * distributed here:  https://software.rtcm-ntrip.org/.  That's distributed
 * under the GNU General Public Licence, so this hacked version is too.
 * Parts not belonging to BKG are Copyright 2019 Simon Ritchie.
 *
 * The original header for the BKG software follows.
 *
 * Copyright (c) 2003...2007
 * German Federal Agency for Cartography and Geodesy (BKG)
 *
 * Developed for Networked Transport of RTCM via Internet Protocol (NTRIP)
 * for streaming GNSS data over the Internet.
 *
 * Designed by Informatik Centrum Dortmund http://www.icd.de
 *
 * The BKG disclaims any liability nor responsibility to any person or
 * entity with respect to any loss or damage caused, or alleged to be
 * caused, directly or indirectly by the use and application of the NTRIP
 * technology.
 *
 * For latest information and updates, access:
 * http://igs.bkg.bund.de/index_ntrip_down.htm
 *
 * BKG, Frankfurt, Germany, February 2007
 * E-mail: euref-ip@bkg.bund.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* CVS revision and version */
static char revisionstr[] = "$Revision: 1.50 $";
static char datestr[]     = "$Date: 2010/01/21 09:00:49 $";

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#ifdef WINDOWSVERSION
  #include <winsock2.h>
  #include <io.h>
  #include <sys/stat.h>
  #include <windows.h>
  typedef SOCKET sockettype;
  typedef u_long in_addr_t;
  typedef size_t socklen_t;
  typedef u_short uint16_t;
#else
  typedef int sockettype;
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <sys/termios.h>
  #define closesocket(sock) close(sock)
  #define INVALID_HANDLE_VALUE -1
  #define INVALID_SOCKET -1
#endif

#include "rtcmfilter.h"

#ifndef COMPILEDATE
#define COMPILEDATE " built " __DATE__
#endif

#define ALARMTIME (2*60)	/* timeout after 2 minutes */

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0 /* prevent compiler errors */
#endif
#ifndef O_EXLOCK
#define O_EXLOCK 0 /* prevent compiler errors */
#endif

enum MODE { SERIAL = 1, TCPSOCKET = 2, INFILE = 3, SISNET = 4, UDPSOCKET = 5,
CASTER = 6, LAST };

enum OUTMODE { HTTP = 1, RTSP = 2, NTRIP1 = 3, UDP = 4, END };

#define AGENTSTRING     "NTRIP NtripServerPOSIX"
#define BUFSZ           1024
#define DEFAULTOUTBUFSZ 2048
#define SZ              64

/* default socket source */
#define SERV_HOST_ADDR  "localhost"
#define SERV_TCP_PORT   2101

/* default destination */
#define NTRIP_CASTER    "www.euref-ip.net"
#define NTRIP_PORT      2101

#define SISNET_SERVER   "131.176.49.142"
#define SISNET_PORT     7777

#define RTP_VERSION     2
#define TIME_RESOLUTION 125

int verboseMode                = 0;
int addNewline                 = FALSE;

static int ttybaud             = 19200;
#ifndef WINDOWSVERSION
static const char *ttyport     = "/dev/gps";
#else
static const char *ttyport     = "COM1";
#endif
static const char *filepath    = "/dev/stdin";
static enum MODE inputmode     = INFILE;
static int sisnet              = 31;
static int gps_file            = -1;
static sockettype gps_socket   = INVALID_SOCKET;
static sockettype socket_tcp   = INVALID_SOCKET;
static sockettype socket_udp   = INVALID_SOCKET;
#ifndef WINDOWSVERSION
static int gps_serial          = INVALID_HANDLE_VALUE;
static int sigpipe_received    = 0;
#else
HANDLE gps_serial              = INVALID_HANDLE_VALUE;
#endif
static int sigalarm_received   = 0;
static int sigint_received     = 0;
static int reconnect_sec       = 1;
static int udp_cseq            = 1;

static int inputFromFile = FALSE;

/* Forward references */
static void send_receive_loop();
static void usage(int, char *);
static int  encode(char *buf, int size, const char *user, const char *pwd);
static int  send_to_caster(char *input, sockettype socket, int input_size);
static void close_session(const char *caster_addr, const char *mountpoint,
  int session, char *rtsp_ext, int fallback);
static int  reconnect(int rec_sec, int rec_sec_max);
static void handle_sigint(int sig);
static void setup_signal_handler(int sig, void (*handler)(int));
#ifndef WINDOWSVERSION
static int  openserial(const char * tty, int blocksz, int baud);
static void handle_sigpipe(int sig);
static void handle_alarm(int sig);
#else
static HANDLE openserial(const char * tty, int baud);
#endif


/*
* main
*
* Main entry point for the program.  Processes command-line arguments and
* prepares for action.
*
* Parameters:
*     argc : integer        : Number of command-line arguments.
*     argv : array of char  : Command-line arguments as an array of
*                             zero-terminated pointers to strings.
*
* Return Value:
*     The function does not return a value (although its return type is int).
*
* Remarks:
*
*/
int main(int argc, char **argv)
{
  int                c;
  int                size = DEFAULTOUTBUFSZ; /* for setting send buffer size */
  struct             sockaddr_in caster;
  const char *       proxyhost = "";
  unsigned int       proxyport = 0;
  /*** INPUT ***/
  const char *       casterinhost = 0;
  unsigned int       casterinport = 0;
  const char *       inhost = 0;
  unsigned int       inport = 0;

  char               get_extension[SZ] = "";

  struct hostent *   he;

  const char *       sisnetpassword = "";
  const char *       sisnetuser = "";

  const char *       stream_name = 0;
  const char *       stream_user = 0;
  const char *       stream_password = 0;

  const char *       recvrid= 0;
  const char *       recvrpwd = 0;

  const char *       initfile = NULL;

  int                bindmode = 0;

  /*** OUTPUT ***/
  unsigned int       casteroutport = NTRIP_PORT;
  const char *       outhost = 0;
  unsigned int       outport = 0;
  char               post_extension[SZ] = "";

  const char *       ntrip_str = "";

  const char *       user = "";
  const char *       password = "";

  int                outputmode = NTRIP1;

  struct sockaddr_in casterRTP;
  struct sockaddr_in local;
  int                client_port = 0;
  int                server_port = 0;
  unsigned int       session = 0;
  socklen_t          len = 0;
  int                i = 0;

  char               szSendBuffer[BUFSZ];
  char               authorization[SZ];
  int                nBufferBytes = 0;
  char *             dlim = " \r\n=";
  char *             token;
  char *             tok_buf[BUFSZ];

  int                reconnect_sec_max = 0;

  setbuf(stdout, 0);
  setbuf(stdin, 0);
  setbuf(stderr, 0);

  {
  char *a;
  int i = 0;
  for(a = revisionstr+11; *a && *a != ' '; ++a)
    revisionstr[i++] = *a;
  revisionstr[i] = 0;
  datestr[0] = datestr[7];
  datestr[1] = datestr[8];
  datestr[2] = datestr[9];
  datestr[3] = datestr[10];
  datestr[5] = datestr[12];
  datestr[6] = datestr[13];
  datestr[8] = datestr[15];
  datestr[9] = datestr[16];
  datestr[4] = datestr[7] = '-';
  datestr[10] = 0;
  }

  /* setup signal handler for CTRL+C */
  setup_signal_handler(SIGINT, handle_sigint);
#ifndef WINDOWSVERSION
  /* setup signal handler for boken pipe */
  setup_signal_handler(SIGPIPE, handle_sigpipe);
  /* setup signal handler for timeout */
  setup_signal_handler(SIGALRM, handle_alarm);
  alarm(ALARMTIME);
#else
  /* winsock initialization */
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(1,1), &wsaData))
  {
    fprintf(stderr, "Could not init network access.\n");
    return 20;
  }
#endif

  /* get and check program arguments */
  if(argc <= 1)
  {
    usage(2, argv[0]);
    exit(1);
  }
  while((c = getopt(argc, argv,
  		  "vnM:i:h:b:s:H:P:f:x:y:l:u:V:D:U:W:O:E:F:R:B")) != EOF)
    {
    switch (c)
    {
    case 'v':
    	verboseMode = 1;
    	break;
    case 'n':
    	addNewline = TRUE;
    	break;
    case 'M': /*** InputMode ***/
      if(!strcmp(optarg, "serial"))         inputmode = SERIAL;
      else if(!strcmp(optarg, "tcpsocket")) inputmode = TCPSOCKET;
      else if(!strcmp(optarg, "file"))      inputmode = INFILE;
      else if(!strcmp(optarg, "sisnet"))    inputmode = SISNET;
      else if(!strcmp(optarg, "udpsocket")) inputmode = UDPSOCKET;
      else if(!strcmp(optarg, "caster"))    inputmode = CASTER;
      else inputmode = atoi(optarg);
      if((inputmode == 0) || (inputmode >= LAST))
      {
        fprintf(stderr, "ERROR: can't convert <%s> to a valid InputMode\n",
        optarg);
        usage(-1, argv[0]);
      }
      break;
    case 'i': /* serial input device */
      ttyport = optarg;
      break;
    case 'B': /* bind to incoming UDP stream */
      bindmode = 1;
      break;
    case 'V': /* Sisnet data server version number */
      if(!strcmp("3.0", optarg))      sisnet = 30;
      else if(!strcmp("3.1", optarg)) sisnet = 31;
      else if(!strcmp("2.1", optarg)) sisnet = 21;
      else
      {
        fprintf(stderr, "ERROR: unknown SISNeT version <%s>\n", optarg);
        usage(-2, argv[0]);
      }
      break;
    case 'b': /* serial input baud rate */
      ttybaud = atoi(optarg);
      if(ttybaud <= 1)
      {
        fprintf(stderr, "ERROR: can't convert <%s> to valid serial baud rate\n",
          optarg);
        usage(1, argv[0]);
      }
      break;
    case 's': /* File name for input data simulation from file */
      filepath = optarg;
      inputFromFile = TRUE;
      break;
    case 'f': /* name of an initialization file */
      initfile = optarg;
      break;
    case 'x': /* user ID to access incoming stream */
      recvrid = optarg;
      break;
    case 'y': /* password to access incoming stream */
      recvrpwd = optarg;
      break;
    case 'u': /* Sisnet data server user ID */
      sisnetuser = optarg;
      break;
    case 'l': /* Sisnet data server password */
      sisnetpassword = optarg;
      break;
    case 'H': /* Input host address*/
      casterinhost = optarg;
      break;
    case 'P': /* Input port */
      casterinport = atoi(optarg);
      if(casterinport <= 1 || casterinport > 65535)
      {
        fprintf(stderr, "ERROR: can't convert <%s> to a valid port number\n",
          optarg);
        usage(1, argv[0]);
      }
      break;
    case 'D': /* Source caster mountpoint for stream input */
     stream_name = optarg;
     break;
    case 'U': /* Source caster user ID for input stream access */
     stream_user = optarg;
     break;
    case 'W': /* Source caster password for input stream access */
     stream_password = optarg;
     break;
    case 'E': /* Proxy Server */
      proxyhost = optarg;
      break;
    case 'F': /* Proxy port */
      proxyport = atoi(optarg);
      break;
    case 'R':  /* maximum delay between reconnect attempts in seconds */
       reconnect_sec_max = atoi(optarg);
       break;
    case 'h': /* print help screen */
    case '?':
      usage(0, argv[0]);
      break;
    default:
      usage(2, argv[0]);
      break;
    }
  }

  argc -= optind;
  argv += optind;

  /*** argument analysis ***/
  if(argc > 0)
  {
    fprintf(stderr, "ERROR: Extra args on command line: ");
    for(; argc > 0; argc--)
    {
      fprintf(stderr, " %s", *argv++);
    }
    fprintf(stderr, "\n");
    usage(1, argv[0]);                   /* never returns */
  }

  while(inputmode != LAST)
  {
    int input_init = 1;
    if(sigint_received) break;
    /*** InputMode handling ***/
    switch(inputmode)
    {
    case INFILE:
      {
    	// If the file is called "-", take input from stdin.  This is for integration
    	// testing - stdin can be driven by a program that simulates a serial source.
    	if (strcmp("-", filepath) == 0) {
    		gps_file = STDIN_FILENO;
    		inputFromFile = FALSE;
    		int fileAccessMode = fcntl(gps_file, F_GETFL, 0);
    		fileAccessMode &= O_NONBLOCK;
    		fcntl(gps_file, F_SETFL, fileAccessMode);
            fprintf(stderr, "input is from stdin\n");
    	}
    	else
    	{
    	  // The input is from a text file.  This feature is for testing.
    	  inputFromFile = TRUE;
    	  if((gps_file = open(filepath, O_RDONLY)) < 0)
          {
            perror("ERROR: opening input file");
            exit(1);
          }
#ifndef WINDOWSVERSION
          /* set blocking inputmode in case it was not set
            (seems to be sometimes for fifo's) */
          fcntl(gps_file, F_SETFL, 0);
#endif
          if (verboseMode) {
            printf("file input: file = %s\n", filepath);
          }
    	}
      }
      break;
    case SERIAL: /* open serial port */
      {
#ifndef WINDOWSVERSION
        gps_serial = openserial(ttyport, 1, ttybaud);
#else
        gps_serial = openserial(ttyport, ttybaud);
#endif
        if(gps_serial == INVALID_HANDLE_VALUE) exit(1);
        if (verboseMode) {
          printf("serial input: device = %s, speed = %d\n", ttyport, ttybaud);
        }
        if(initfile)
        {
          char buffer[1024];
          FILE *fh;
          int i;

          if((fh = fopen(initfile, "r")))
          {
            while((i = fread(buffer, 1, sizeof(buffer), fh)) > 0)
            {
#ifndef WINDOWSVERSION
              if((write(gps_serial, buffer, i)) != i)
              {
                perror("WARNING: sending init file");
                input_init = 0;
                break;
              }
#else
              DWORD nWrite = -1;
              if(!WriteFile(gps_serial, buffer, sizeof(buffer), &nWrite, NULL))
              {
                fprintf(stderr,"ERROR: sending init file \n");
                input_init = 0;
                break;
              }
              i = (int)nWrite;
#endif
            }
            if(i < 0)
            {
              perror("ERROR: reading init file");
              reconnect_sec_max = 0;
              input_init = 0;
              break;
            }
            fclose(fh);
          }
          else
          {
            fprintf(stderr, "ERROR: can't read init file <%s>\n", initfile);
            reconnect_sec_max = 0;
            input_init = 0;
            break;
          }
        }
      }
      break;
    case TCPSOCKET: case UDPSOCKET: case SISNET: case CASTER:
      {
        if(inputmode == SISNET)
        {
          if(!inhost) inhost = SISNET_SERVER;
          if(!inport) inport = SISNET_PORT;
        }
        else if(inputmode == CASTER)
        {
          if(!inport) inport = NTRIP_PORT;
          if(!inhost) inhost = NTRIP_CASTER;
        }
        else if((inputmode == TCPSOCKET) || (inputmode == UDPSOCKET))
        {
          if(!inport) inport = SERV_TCP_PORT;
          if(!inhost) inhost = SERV_HOST_ADDR;
        }

        if(!(he = gethostbyname(inhost)))
        {
          fprintf(stderr, "ERROR: Input host <%s> unknown\n", inhost);
          usage(-2, argv[0]);
        }

        if((gps_socket = socket(AF_INET, inputmode == UDPSOCKET
        ? SOCK_DGRAM : SOCK_STREAM, 0)) == INVALID_SOCKET)
        {
          fprintf(stderr,
          "ERROR: can't create socket for incoming data stream\n");
          exit(1);
        }

        memset((char *) &caster, 0x00, sizeof(caster));
        if(!bindmode)
          memcpy(&caster.sin_addr, he->h_addr, (size_t)he->h_length);
        caster.sin_family = AF_INET;
        caster.sin_port = htons(inport);

        fprintf(stderr, "%s input: host = %s, port = %d, %s%s%s%s%s\n",
        inputmode == CASTER ? "caster" : inputmode == SISNET ? "sisnet" :
        inputmode == TCPSOCKET ? "tcp socket" : "udp socket",
        bindmode ? "127.0.0.1" : inet_ntoa(caster.sin_addr),
        inport, stream_name ? "stream = " : "", stream_name ? stream_name : "",
        initfile ? ", initfile = " : "", initfile ? initfile : "",
        bindmode ? "binding mode" : "");

        if(bindmode)
        {
          if(bind(gps_socket, (struct sockaddr *) &caster, sizeof(caster)) < 0)
          {
            fprintf(stderr, "ERROR: can't bind input to port %d\n", inport);
            reconnect_sec_max = 0;
            input_init = 0;
            break;
          }
        } /* connect to input-caster or proxy server*/
        else if(connect(gps_socket, (struct sockaddr *)&caster, sizeof(caster)) < 0)
        {
          fprintf(stderr, "WARNING: can't connect input to %s at port %d\n",
          inet_ntoa(caster.sin_addr), inport);
          input_init = 0;
          break;
        }

        if(stream_name) /* input from Ntrip Version 1.0 caster*/
        {
          int init = 0;

          /* set socket buffer size */
          setsockopt(gps_socket, SOL_SOCKET, SO_SNDBUF, (const char *) &size,
            sizeof(const char *));
          if(stream_user && stream_password)
          {
            /* leave some space for login */
            nBufferBytes=snprintf(szSendBuffer, sizeof(szSendBuffer)-40,
            "GET %s/%s HTTP/1.0\r\n"
            "User-Agent: %s/%s\r\n"
            "Connection: close\r\n"
            "Authorization: Basic ", get_extension, stream_name,
            AGENTSTRING, revisionstr);
            /* second check for old glibc */
            if(nBufferBytes > (int)sizeof(szSendBuffer)-40 || nBufferBytes < 0)
            {
              fprintf(stderr, "ERROR: Source caster request too long\n");
              input_init = 0;
              reconnect_sec_max =0;
              break;
            }
            nBufferBytes += encode(szSendBuffer+nBufferBytes,
              sizeof(szSendBuffer)-nBufferBytes-4, stream_user, stream_password);
            if(nBufferBytes > (int)sizeof(szSendBuffer)-4)
            {
              fprintf(stderr,
              "ERROR: Source caster user ID and/or password too long\n");
              input_init = 0;
              reconnect_sec_max =0;
              break;
            }
            szSendBuffer[nBufferBytes++] = '\r';
            szSendBuffer[nBufferBytes++] = '\n';
            szSendBuffer[nBufferBytes++] = '\r';
            szSendBuffer[nBufferBytes++] = '\n';
          }
          else
          {
            nBufferBytes = snprintf(szSendBuffer, sizeof(szSendBuffer),
            "GET %s/%s HTTP/1.0\r\n"
            "User-Agent: %s/%s\r\n"
            "Connection: close\r\n"
            "\r\n", get_extension, stream_name, AGENTSTRING, revisionstr);
          }
          if((send(gps_socket, szSendBuffer, (size_t)nBufferBytes, 0))
          != nBufferBytes)
          {
            fprintf(stderr, "WARNING: could not send Source caster request\n");
            input_init = 0;
            break;
          }
          nBufferBytes = 0;
          /* check Source caster's response */
          while(!init && nBufferBytes < (int)sizeof(szSendBuffer)
          && (nBufferBytes += recv(gps_socket, szSendBuffer,
          sizeof(szSendBuffer)-nBufferBytes, 0)) > 0)
          {
            if(strstr(szSendBuffer, "\r\n"))
            {
              if(!strstr(szSendBuffer, "ICY 200 OK"))
              {
                int k;
                fprintf(stderr,
                "ERROR: could not get requested data from Source caster: ");
                for(k = 0; k < nBufferBytes && szSendBuffer[k] != '\n'
                  && szSendBuffer[k] != '\r'; ++k)
                {
                  fprintf(stderr, "%c", isprint(szSendBuffer[k])
                  ? szSendBuffer[k] : '.');
                }
                fprintf(stderr, "\n");
                if(!strstr(szSendBuffer, "SOURCETABLE 200 OK"))
                {
                  reconnect_sec_max =0;
                }
                input_init = 0;
                break;
              }
              else init = 1;
            }
          }
        } /* end input from Ntrip Version 1.0 caster */

        if(initfile && inputmode != SISNET)
        {
          char buffer[1024];
          FILE *fh;
          int i;

          if((fh = fopen(initfile, "r")))
          {
            while((i = fread(buffer, 1, sizeof(buffer), fh)) > 0)
            {
              if((send(gps_socket, buffer, (size_t)i, 0)) != i)
              {
                perror("WARNING: sending init file");
                input_init = 0;
                break;
              }
            }
            if(i < 0)
            {
              perror("ERROR: reading init file");
              reconnect_sec_max = 0;
              input_init = 0;
              break;
            }
            fclose(fh);
          }
          else
          {
            fprintf(stderr, "ERROR: can't read init file <%s>\n", initfile);
            reconnect_sec_max = 0;
            input_init = 0;
            break;
          }
        }
      }
      if(inputmode == SISNET)
      {
        int i, j;
        char buffer[1024];

        i = snprintf(buffer, sizeof(buffer), sisnet >= 30 ? "AUTH,%s,%s\r\n"
          : "AUTH,%s,%s", sisnetuser, sisnetpassword);
        if((send(gps_socket, buffer, (size_t)i, 0)) != i)
        {
          perror("WARNING: sending authentication for SISNeT data server");
          input_init = 0;
          break;
        }
        i = sisnet >= 30 ? 7 : 5;
        if((j = recv(gps_socket, buffer, i, 0)) != i && strncmp("*AUTH", buffer, 5))
        {
          fprintf(stderr, "WARNING: SISNeT connect failed:");
          for(i = 0; i < j; ++i)
          {
            if(buffer[i] != '\r' && buffer[i] != '\n')
            {
              fprintf(stderr, "%c", isprint(buffer[i]) ? buffer[i] : '.');
            }
          }
          fprintf(stderr, "\n");
          input_init = 0;
          break;
        }
        if(sisnet >= 31)
        {
          if((send(gps_socket, "START\r\n", 7, 0)) != i)
          {
            perror("WARNING: sending Sisnet start command");
            input_init = 0;
            break;
          }
        }
      }
      /*** receiver authentication  ***/
      if (recvrid && recvrpwd && ((inputmode == TCPSOCKET)
      || (inputmode == UDPSOCKET)))
      {
        if (strlen(recvrid) > (BUFSZ-3))
        {
          fprintf(stderr, "ERROR: Receiver ID too long\n");
          reconnect_sec_max = 0;
          input_init = 0;
          break;
        }
        else
        {
          fprintf(stderr, "Sending user ID for receiver...\n");
          nBufferBytes = recv(gps_socket, szSendBuffer, BUFSZ, 0);
          strcpy(szSendBuffer, recvrid);
          strcat(szSendBuffer,"\r\n");
          if(send(gps_socket,szSendBuffer, strlen(szSendBuffer), MSG_DONTWAIT) < 0)
          {
            perror("WARNING: sending user ID for receiver");
            input_init = 0;
            break;
          }
        }

        if (strlen(recvrpwd) > (BUFSZ-3))
        {
          fprintf(stderr, "ERROR: Receiver password too long\n");
          reconnect_sec_max = 0;
          input_init = 0;
          break;
        }
        else
        {
          fprintf(stderr, "Sending user password for receiver...\n");
          nBufferBytes = recv(gps_socket, szSendBuffer, BUFSZ, 0);
          strcpy(szSendBuffer, recvrpwd);
          strcat(szSendBuffer,"\r\n");
          if(send(gps_socket, szSendBuffer, strlen(szSendBuffer), MSG_DONTWAIT) < 0)
          {
            perror("WARNING: sending user password for receiver");
            input_init = 0;
            break;
          }
        }
      }
      break;
    default:
      usage(-1, argv[0]);
      break;
    }

    /* ----- main part ----- */
    int output_init = TRUE, fallback = FALSE;

    send_receive_loop();

    exit(0);

    while((input_init))
    {
#ifndef WINDOWSVERSION
      if((sigalarm_received) || (sigint_received) || (sigpipe_received)) break;
#else
      if((sigalarm_received) || (sigint_received)) break;
#endif

      send_receive_loop();
    }
    if( (reconnect_sec_max || fallback) && !sigint_received )
      reconnect_sec = reconnect(reconnect_sec, reconnect_sec_max);
    else inputmode = LAST;
  }
  return 0;
}



static void send_receive_loop()
{
  int      nodata = FALSE;
  char     buffer[BUFSZ] = { 0 };
  char     sisnetbackbuffer[200];
  char     szSendBuffer[BUFSZ] = "";
  int      nBufferBytes = 0;

   /* RTSP / RTP Mode */
  int      isfirstpacket = 1;
  struct   timeval now;
  struct   timeval last = {0,0};
  long int sendtimediff;

  /* data transmission */
  fprintf(stderr,"transfering data ...\n");
  int  send_recv_success = 0;
#ifdef WINDOWSVERSION
  time_t _begin = 0, nodata_current = 0;
#endif

  // Loop processing messages until the data stream runs out
  // or the program receives a signal.
  while(TRUE)
  {
    if(send_recv_success < 3) send_recv_success++;
    if(!nodata)
    {
#ifndef WINDOWSVERSION
      alarm(ALARMTIME);
#else
      time(&nodata_begin);
#endif
    }
    else
    {
      nodata = FALSE;
#ifdef WINDOWSVERSION
      time(&nodata_current);
      if(difftime(nodata_current, nodata_begin) >= ALARMTIME)
      {
        sigalarm_received = 1;
        fprintf(stderr, "ERROR: more than %d seconds no activity\n", ALARMTIME);
      }
#endif
    }
    /* signal handling*/
#ifdef WINDOWSVERSION
    if((sigalarm_received) || (sigint_received)) break;
#else
    if((sigalarm_received) || (sigint_received) || (sigpipe_received)) break;
#endif
    if(nBufferBytes == 0)
    {
      if(inputmode == SISNET && sisnet <= 30)
      {
        int i;
        /* a somewhat higher rate than 1 second to get really each block */
        /* means we need to skip double blocks sometimes */
        struct timeval tv = {0,700000};
        select(0, 0, 0, 0, &tv);
        memcpy(sisnetbackbuffer, buffer, sizeof(sisnetbackbuffer));
        i = (sisnet >= 30 ? 5 : 3);
        if((send(gps_socket, "MSG\r\n", i, 0)) != i)
        {
          perror("WARNING: sending SISNeT data request failed");
          return;
        }
      }
      /*** receiving data ****/
      if(inputmode == INFILE) {
        nBufferBytes = read(gps_file, buffer, sizeof(buffer));
        if (nBufferBytes == 0 && inputFromFile) {
          break;
        }
      }
      else if(inputmode == SERIAL)
      {
#ifndef WINDOWSVERSION
        nBufferBytes = read(gps_serial, buffer, sizeof(buffer));
#else
        DWORD nRead = 0;
        if(!ReadFile(gps_serial, buffer, sizeof(buffer), &nRead, NULL))
        {
          fprintf(stderr,"ERROR: reading serial input failed\n");
          return;
        }
        nBufferBytes = (int)nRead;
#endif
      }
      else
      {
#ifdef WINDOWSVERSION
        nBufferBytes = recv(gps_socket, buffer, sizeof(buffer), 0);
#else
        nBufferBytes = read(gps_socket, buffer, sizeof(buffer));
#endif
      }

      if(nBufferBytes == 0)
      {
        fprintf(stderr, "WARNING: no data received from input\n");
        nodata = TRUE;
#ifndef WINDOWSVERSION
        sleep(3);
#else
        Sleep(3*1000);
#endif
        continue;
      }
      else if((nBufferBytes < 0) && (!sigint_received))
      {
        perror("WARNING: reading input failed");
        return;
      }
      /* we can compare the whole buffer, as the additional bytes
         remain unchanged */
      if(inputmode == SISNET && sisnet <= 30 &&
      !memcmp(sisnetbackbuffer, buffer, sizeof(sisnetbackbuffer)))
      {
        nBufferBytes = 0;
      }
    }
    if(nBufferBytes < 0) {
      // A read error of some sort.
      return;
    }

    if(send_recv_success == 3) {
    	reconnect_sec = 1;
    }

    /*
     * Ignore any messages in the input buffer that are not RTCM, send any RTCM message or
     * fragments to stdout and free the output buffer.
     */
    Buffer inputBuffer;
    inputBuffer.content = buffer;
    inputBuffer.length = nBufferBytes;

    if (displayingBuffers) {
    	displayBuffer(&inputBuffer);
    }
    Buffer * outputBuffer = getRtcmDataBlocks(inputBuffer);

    // If the input buffer contains any RTCM messages (complete or fragments) write it to stdout.
    if (outputBuffer == NULL) {
    	if (verboseMode > 0 && displayingBuffers()) {
			fprintf(stderr, "\noutput buffer is null after processing\n");
		}
    	// Signal that the buffer is processed.
    	nBufferBytes = 0;
    	continue;
    }

    if (outputBuffer->length == 0) {
		if (verboseMode > 0 && displayingBuffers()) {
			fprintf(stderr, "\noutput buffer is empty after processing\n");
		}
		// Signal that the buffer is processed.
		nBufferBytes = 0;
		continue;
	}

	if (verboseMode > 0 && displayingBuffers()) {
		fprintf(stderr, "\nwriting buffer - length %ld\n", outputBuffer->length);
	}

	for (int i = 0; i < outputBuffer->length; i++) {
		putc(outputBuffer->content[i], stdout);
	}

	// Free the output buffer.
	freeBuffer(outputBuffer);
	outputBuffer = NULL;

    // Signal that the buffer is processed.
    nBufferBytes = 0;
  }

  return;
}


/********************************************************************
 * openserial
 *
 * Open the serial port with the given device name and configure it for
 * reading NMEA data from a GPS receiver.
 *
 * Parameters:
 *     tty     : pointer to    : A zero-terminated string containing the device
 *               unsigned char   name of the appropriate serial port.
 *     blocksz : integer       : Block size for port I/O  (ifndef WINDOWSVERSION)
 *     baud :    integer       : Baud rate for port I/O
 *
 * Return Value:
 *     The function returns a file descriptor for the opened port if successful.
 *     The function returns -1 / INVALID_HANDLE_VALUE in the event of an error.
 *
 * Remarks:
 *
 ********************************************************************/
#ifndef WINDOWSVERSION
static int openserial(const char * tty, int blocksz, int baud)
{
  struct termios termios;

/*** opening the serial port ***/
  gps_serial = open(tty, O_RDWR | O_NONBLOCK | O_EXLOCK);
  if(gps_serial < 0)
  {
    perror("ERROR: opening serial connection");
    return (-1);
  }

/*** configuring the serial port ***/
  if(tcgetattr(gps_serial, &termios) < 0)
  {
    perror("ERROR: get serial attributes");
    return (-1);
  }
  termios.c_iflag = 0;
  termios.c_oflag = 0;          /* (ONLRET) */
  termios.c_cflag = CS8 | CLOCAL | CREAD;
  termios.c_lflag = 0;
  {
    int cnt;
    for(cnt = 0; cnt < NCCS; cnt++)
      termios.c_cc[cnt] = -1;
  }
  termios.c_cc[VMIN] = blocksz;
  termios.c_cc[VTIME] = 2;

#if (B4800 != 4800)
/* Not every system has speed settings equal to absolute speed value. */
  switch (baud)
  {
  case 300:
    baud = B300;
    break;
  case 1200:
    baud = B1200;
    break;
  case 2400:
    baud = B2400;
    break;
  case 4800:
    baud = B4800;
    break;
  case 9600:
    baud = B9600;
    break;
  case 19200:
    baud = B19200;
    break;
  case 38400:
    baud = B38400;
    break;
#ifdef B57600
  case 57600:
    baud = B57600;
    break;
#endif
#ifdef B115200
  case 115200:
    baud = B115200;
    break;
#endif
#ifdef B230400
  case 230400:
    baud = B230400;
    break;
#endif
  default:
    fprintf(stderr, "WARNING: Baud settings not useful, using 19200\n");
    baud = B19200;
    break;
  }
#endif

  if(cfsetispeed(&termios, baud) != 0)
  {
    perror("ERROR: setting serial speed with cfsetispeed");
    return (-1);
  }
  if(cfsetospeed(&termios, baud) != 0)
  {
    perror("ERROR: setting serial speed with cfsetospeed");
    return (-1);
  }
  if(tcsetattr(gps_serial, TCSANOW, &termios) < 0)
  {
    perror("ERROR: setting serial attributes");
    return (-1);
  }
  if(fcntl(gps_serial, F_SETFL, 0) == -1)
  {
    perror("WARNING: setting blocking inputmode failed");
  }
  return (gps_serial);
}
#else
static HANDLE openserial(const char * tty, int baud)
{
  char compath[15] = "";

  snprintf(compath, sizeof(compath), "\\\\.\\%s", tty);
  if((gps_serial = CreateFile(compath, GENERIC_WRITE|GENERIC_READ
  , 0, 0, OPEN_EXISTING, 0, 0)) == INVALID_HANDLE_VALUE)
  {
    fprintf(stderr, "ERROR: opening serial connection\n");
    return (INVALID_HANDLE_VALUE);
  }

  DCB dcb;
  memset(&dcb, 0, sizeof(dcb));
  char str[100];
  snprintf(str,sizeof(str),
  "baud=%d parity=N data=8 stop=1 xon=off octs=off rts=off",
  baud);

  COMMTIMEOUTS ct = {1000, 1, 0, 0, 0};

  if(!BuildCommDCB(str, &dcb))
  {
    fprintf(stderr, "ERROR: outputBuffer serial attributes\n");
    return (INVALID_HANDLE_VALUE);
  }
  else if(!SetCommState(gps_serial, &dcb))
  {
    fprintf(stderr, "ERROR: set serial attributes\n");
    return (INVALID_HANDLE_VALUE);
  }
  else if(!SetCommTimeouts(gps_serial, &ct))
  {
    fprintf(stderr, "ERROR: set serial timeouts\n");
    return (INVALID_HANDLE_VALUE);
  }

  return (gps_serial);
}
#endif

/********************************************************************
* usage
*
* Send a usage message to standard error and quit the program.
*
* Parameters:
*     None.
*
* Return Value:
*     The function does not return a value.
*
* Remarks:
*
*********************************************************************/
#ifdef __GNUC__
__attribute__ ((noreturn))
#endif /* __GNUC__ */
void usage(int rc, char *name)
{
  fprintf(stderr, "Version %s (%s) GPL" COMPILEDATE "\nUsage:\n%s [OPTIONS]\n",
    revisionstr, datestr, name);
  fprintf(stderr, "PURPOSE\n");
  fprintf(stderr, "   The purpose of this program is to pick up a GNSS data stream (Input, Source)\n");
  fprintf(stderr, "   from either\n\n");
  fprintf(stderr, "     1. a Serial port, or\n");
  fprintf(stderr, "     2. an IP server, or\n");
  fprintf(stderr, "     3. a File, or\n");
  fprintf(stderr, "     4. a SISNeT Data Server, or\n");
  fprintf(stderr, "     5. a UDP server, or\n");
  fprintf(stderr, "     6. an NTRIP Version 1.0 Caster\n\n");
  fprintf(stderr, "   and forward any RTCM messages from that incoming stream (Output, Destination) to the standard output channel.\n\n");
  fprintf(stderr, "OPTIONS\n");
  fprintf(stderr, "   -h|? print this help screen\n\n");
  fprintf(stderr, "   -v verbose mode\n\n");
  fprintf(stderr, "   -n add newline after every message - useful during testing, but could confuse the caster if used in production.\n\n");
  fprintf(stderr, "    -E <ProxyHost>       Proxy server host name or address, required i.e. when\n");
  fprintf(stderr, "                         running the program in a proxy server protected LAN,\n");
  fprintf(stderr, "                         optional\n");
  fprintf(stderr, "    -F <ProxyPort>       Proxy server IP port, required i.e. when running\n");
  fprintf(stderr, "                         the program in a proxy server protected LAN, optional\n");
  fprintf(stderr, "    -R <maxDelay>        Reconnect mechanism with maximum delay between reconnect\n");
  fprintf(stderr, "                         attemts in seconds, default: no reconnect activated,\n");
  fprintf(stderr, "                         optional\n\n");
  fprintf(stderr, "    -M <InputMode> Sets the input mode (1 = Serial Port, 2 = IP server,\n");
  fprintf(stderr, "       3 = File, 4 = SISNeT Data Server, 5 = UDP server, 6 = NTRIP Caster),\n");
  fprintf(stderr, "       mandatory\n\n");
  fprintf(stderr, "       <InputMode> = 1 (Serial Port):\n");
  fprintf(stderr, "       -i <Device>       Serial input device, default: %s, mandatory if\n", ttyport);
  fprintf(stderr, "                         <InputMode>=1\n");
  fprintf(stderr, "       -b <BaudRate>     Serial input baud rate, default: 19200 bps, mandatory\n");
  fprintf(stderr, "                         if <InputMode>=1\n");
  fprintf(stderr, "       -f <InitFile>     Name of initialization file to be send to input device,\n");
  fprintf(stderr, "                         optional.\n\n");
  fprintf(stderr, "                         If the filename is \"-\", input is from the stdin channel, non-blocking.\n");
  fprintf(stderr, "       <InputMode> = 2|5 (IP port | UDP port):\n");
  fprintf(stderr, "       -B Bind to incoming UDP stream, optional for <InputMode> = 5\n\n");
  fprintf(stderr, "       <InputMode> = 3 (File):\n");
  fprintf(stderr, "       -s <File>         File name to simulate stream by reading data from (log)\n");
  fprintf(stderr, "                         file, default is %s, mandatory for <InputMode> = 3\n\n", filepath);
  fprintf(stderr, "       <InputMode> = 4 (SISNeT Data Server):\n");
  fprintf(stderr, "       -H <SisnetHost>   SISNeT Data Server name or address,\n");
  fprintf(stderr, "                         default: 131.176.49.142, mandatory if <InputMode> = 4\n");
  fprintf(stderr, "       -P <SisnetPort>   SISNeT Data Server port, default: 7777, mandatory if\n");
  fprintf(stderr, "                         <InputMode> = 4\n");
  fprintf(stderr, "       -u <SisnetUser>   SISNeT Data Server user ID, mandatory if <InputMode> = 4\n");
  fprintf(stderr, "       -l <SisnetPass>   SISNeT Data Server password, mandatory if <InputMode> = 4\n");
  fprintf(stderr, "       -V <SisnetVers>   SISNeT Data Server Version number, options are 2.1, 3.0\n");
  fprintf(stderr, "                         or 3.1, default: 3.1, mandatory if <InputMode> = 4\n\n");
  fprintf(stderr, "       <InputMode> = 6 (NTRIP Version 1.0 Caster):\n");
  fprintf(stderr, "       -H <SourceHost>   Source caster name or address, default: 127.0.0.1,\n");
  fprintf(stderr, "                         mandatory if <InputMode> = 6\n");
  fprintf(stderr, "       -P <SourcePort>   Source caster port, default: 2101, mandatory if\n");
  fprintf(stderr, "                         <InputMode> = 6\n");
  fprintf(stderr, "       -D <SourceMount>  Source caster mountpoint for stream input, mandatory if\n");
  fprintf(stderr, "                         <InputMode> = 6\n");
  fprintf(stderr, "       -U <SourceUser>   Source caster user Id for input stream access, mandatory\n");
  fprintf(stderr, "                         for protected streams if <InputMode> = 6\n");
  fprintf(stderr, "       -W <SourcePass>   Source caster password for input stream access, mandatory\n");
  fprintf(stderr, "                         for protected streams if <InputMode> = 6\n\n");
  fprintf(stderr, "       -N <STR-record>   Sourcetable STR-record\n");
  fprintf(stderr, "                         optional for NTRIP Version 2.0 in RTSP/RTP and TCP/IP mode\n\n");
  fprintf(stderr, "       -v                verbose mode - displays the first 50 RTCM messages (if none arrive, it displays the first 1000 non-RTCM messages.)\n");
  exit(rc);
} /* usage */


/********************************************************************/
/* signal handling                                                  */
/********************************************************************/
#ifdef __GNUC__
static void handle_sigint(int sig __attribute__((__unused__)))
#else /* __GNUC__ */
static void handle_sigint(int sig)
#endif /* __GNUC__ */
{
  sigint_received  = 1;
  fprintf(stderr, "WARNING: SIGINT received - ntripserver terminates\n");
}

#ifndef WINDOWSVERSION
#ifdef __GNUC__
static void handle_alarm(int sig __attribute__((__unused__)))
#else /* __GNUC__ */
static void handle_alarm(int sig)
#endif /* __GNUC__ */
{
  sigalarm_received = 1;
  fprintf(stderr, "ERROR: more than %d seconds no activity\n", ALARMTIME);
}

#ifdef __GNUC__
static void handle_sigpipe(int sig __attribute__((__unused__)))
#else /* __GNUC__ */
static void handle_sigpipe(int sig)
#endif /* __GNUC__ */
{
  sigpipe_received = 1;
}
#endif /* WINDOWSVERSION */

static void setup_signal_handler(int sig, void (*handler)(int))
{
#if _POSIX_VERSION > 198800L
  struct sigaction action;

  action.sa_handler = handler;
  sigemptyset(&(action.sa_mask));
  sigaddset(&(action.sa_mask), sig);
  action.sa_flags = 0;
  sigaction(sig, &action, 0);
#else
  signal(sig, handler);
#endif
  return;
} /* setupsignal_handler */


/********************************************************************
 * base64-encoding                                                  *
*******************************************************************/
static const char encodingTable [64] =
{
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
  'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
  'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
  'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

/* does not buffer overrun, but breaks directly after an error */
/* returns the number of required bytes */
static int encode(char *buf, int size, const char *user, const char *pwd)
{
  unsigned char inbuf[3];
  char *out = buf;
  int i, sep = 0, fill = 0, bytes = 0;

  while(*user || *pwd)
  {
    i = 0;
    while(i < 3 && *user) inbuf[i++] = *(user++);
    if(i < 3 && !sep)    {inbuf[i++] = ':'; ++sep; }
    while(i < 3 && *pwd)  inbuf[i++] = *(pwd++);
    while(i < 3)         {inbuf[i++] = 0; ++fill; }
    if(out-buf < size-1)
      *(out++) = encodingTable[(inbuf [0] & 0xFC) >> 2];
    if(out-buf < size-1)
      *(out++) = encodingTable[((inbuf [0] & 0x03) << 4)
               | ((inbuf [1] & 0xF0) >> 4)];
    if(out-buf < size-1)
    {
      if(fill == 2)
        *(out++) = '=';
      else
        *(out++) = encodingTable[((inbuf [1] & 0x0F) << 2)
                 | ((inbuf [2] & 0xC0) >> 6)];
    }
    if(out-buf < size-1)
    {
      if(fill >= 1)
        *(out++) = '=';
      else
        *(out++) = encodingTable[inbuf [2] & 0x3F];
    }
    bytes += 4;
  }
  if(out-buf < size)
    *out = 0;
  return bytes;
}/* base64 Encoding */


/********************************************************************
 * send message to caster                                           *
*********************************************************************/
static int send_to_caster(char *input, sockettype socket, int input_size)
{
 int send_error = 1;

  if((send(socket, input, (size_t)input_size, 0)) != input_size)
  {
    fprintf(stderr, "WARNING: could not send full header to Destination caster\n");
    send_error = 0;
  }
#ifndef NDEBUG
  else
  {
    fprintf(stderr, "\nDestination caster request:\n");
    fprintf(stderr, "%s", input);
  }
#endif
  return send_error;
}/* send_to_caster */


/********************************************************************
 * reconnect                                                        *
*********************************************************************/
int reconnect(int rec_sec, int rec_sec_max)
{
  fprintf(stderr,"reconnect in <%d> seconds\n\n", rec_sec);
  rec_sec *= 2;
  if (rec_sec > rec_sec_max) rec_sec = rec_sec_max;
#ifndef WINDOWSVERSION
  sleep(rec_sec);
  sigpipe_received = 0;
#else
  Sleep(rec_sec*1000);
#endif
  sigalarm_received = 0;
  return rec_sec;
} /* reconnect */


/********************************************************************
 * close session                                                    *
*********************************************************************/
static void close_session(const char *caster_addr, const char *mountpoint,
int session, char *rtsp_ext, int fallback)
{
  int  size_send_buf;
  char send_buf[BUFSZ];

  if(!fallback)
  {
    if((gps_socket != INVALID_SOCKET) &&
       ((inputmode == TCPSOCKET) || (inputmode == UDPSOCKET) ||
       (inputmode == CASTER)    || (inputmode == SISNET)))
    {
      if(closesocket(gps_socket) == -1)
      {
        perror("ERROR: close input device ");
        exit(0);
      }
      else
      {
        gps_socket = -1;
#ifndef NDEBUG
        fprintf(stderr, "close input device: successful\n");
#endif
      }
    }
    else if((gps_serial != INVALID_HANDLE_VALUE) && (inputmode == SERIAL))
    {
#ifndef WINDOWSVERSION
      if(close(gps_serial) == INVALID_HANDLE_VALUE)
      {
        perror("ERROR: close input device ");
        exit(0);
      }
#else
      if(!CloseHandle(gps_serial))
      {
        fprintf(stderr, "ERROR: close input device ");
        exit(0);
      }
#endif
      else
      {
        gps_serial = INVALID_HANDLE_VALUE;
#ifndef NDEBUG
        fprintf(stderr, "close input device: successful\n");
#endif
      }
    }
    else if((gps_file != -1) && (inputmode == INFILE))
    {
      if(close(gps_file) == -1)
      {
        perror("ERROR: close input device ");
        exit(0);
      }
      else
      {
        gps_file = -1;
#ifndef NDEBUG
        fprintf(stderr, "close input device: successful\n");
#endif
      }
    }
  }

  if(socket_udp != INVALID_SOCKET)
  {
    if(udp_cseq > 2)
    {
      size_send_buf = snprintf(send_buf, sizeof(send_buf),
        "TEARDOWN rtsp://%s%s/%s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Session: %u\r\n"
        "\r\n",
        caster_addr, rtsp_ext, mountpoint, udp_cseq++, session);
      if((size_send_buf >= (int)sizeof(send_buf)) || (size_send_buf < 0))
      {
        fprintf(stderr, "ERROR: Destination caster request to long\n");
        exit(0);
      }
      send_to_caster(send_buf, socket_tcp, size_send_buf); strcpy(send_buf,"");
      size_send_buf = recv(socket_tcp, send_buf, sizeof(send_buf), 0);
      send_buf[size_send_buf] = '\0';
#ifndef NDEBUG
      fprintf(stderr, "Destination caster response:\n%s", send_buf);
#endif
    }
    if(closesocket(socket_udp)==-1)
    {
      perror("ERROR: close udp socket");
      exit(0);
    }
    else
    {
      socket_udp = -1;
#ifndef NDEBUG
      fprintf(stderr, "close udp socket: successful\n");
#endif
    }
  }

  if(socket_tcp != INVALID_SOCKET)
  {
    if(closesocket(socket_tcp) == -1)
    {
      perror("ERROR: close tcp socket");
      exit(0);
    }
    else
    {
      socket_tcp = -1;
#ifndef NDEBUG
      fprintf(stderr, "close tcp socket: successful\n");
#endif
    }
  }
} /* close_session */
