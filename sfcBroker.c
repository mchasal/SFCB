
/*
 * sfcBroker.c
 *
 * (C) Copyright IBM Corp. 2005-2007
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Adrian Schuur <schuur@de.ibm.com>
 * Contributions: Sven Schuetz <sven@de.ibm.com>
 *
 * Description:
 *
 * sfcBroker Main.
 *
 */

#include <stdio.h>
#include "native.h"
#include <sfcCommon/utilft.h>
#include "string.h"
#include "cimXmlParser.h"
// #include "brokerOs.c"

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "trace.h"
#include "msgqueue.h"
#include <pthread.h>

#include "sfcVersion.h"
#include "control.h"

#include <getopt.h>
#include <syslog.h>
#include <pwd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int             sfcBrokerPid = 0;

extern int      sfcbUseSyslog;

extern void     setExFlag(unsigned long f);
extern char    *parseTarget(const char *target);
extern UtilStringBuffer *instanceToString(CMPIInstance *ci, char **props);
extern int      init_sfcBroker();
extern CMPIBroker *Broker;
extern void     initProvProcCtl(int);
extern void     processTerminated(int pid);
extern int      httpDaemon(int argc, char *argv[], int sslMode);
extern void     processProviderMgrRequests();

extern int      stopNextProc();
extern int      testStartedProc(int pid, int *left);

extern void     uninitProvProcCtl();
extern void     uninitSocketPairs();
extern void     sunsetControl();
extern void     uninitGarbageCollector();

extern int      loadHostnameLib();
extern void     unloadHostnameLib();

extern TraceId  traceIds[];

extern unsigned long exFlags;
static int      startHttp = 0;

#ifdef HAVE_JDBC
static int      startDbp = 1;
#endif

char           *name;
extern int      collectStat;

extern unsigned long provSampleInterval;
extern unsigned long provTimeoutInterval;
extern unsigned provAutoGroup;

extern void     dumpTiming(int pid);

static char   **restartArgv;
static int      restartArgc;
static int      adaptersStopped = 0,
    providersStopped = 0,
    restartBroker = 0;

extern char    *configfile;

int trimws = 1;

typedef struct startedAdapter {
  struct startedAdapter *next;
  int             stopped;
  int             pid;
} StartedAdapter;

StartedAdapter *lastStartedAdapter = NULL;

typedef struct startedThreadAdapter {
  struct startedThreadAdapter *next;
  int             stopped;
  pthread_t       tid;
} StartedThreadAdapter;

StartedThreadAdapter *lastStartedThreadAdapter = NULL;

void
addStartedAdapter(int pid)
{
  StartedAdapter *sa = (StartedAdapter *) malloc(sizeof(StartedAdapter));

  sa->stopped = 0;
  sa->pid = pid;
  sa->next = lastStartedAdapter;
  lastStartedAdapter = sa;
}

static int
testStartedAdapter(int pid, int *left)
{
  StartedAdapter *sa = lastStartedAdapter;
  int             stopped = 0;

  *left = 0;
  while (sa) {
    if (sa->pid == pid)
      stopped = sa->stopped = 1;
    if (sa->stopped == 0)
      (*left)++;
    sa = sa->next;
  }
  return stopped;
}

static int
stopNextAdapter()
{
  StartedAdapter *sa = lastStartedAdapter;

  while (sa) {
    if (sa->stopped == 0) {
      sa->stopped = 1;
      kill(sa->pid, SIGUSR1);
      return sa->pid;
    }
    sa = sa->next;
  }
  return 0;
}

/* 3497096 :77022  */
extern pthread_mutex_t syncMtx; /* syncronize provider state */
extern int prov_rdy_state;      /* -1 indicates not ready */

static pthread_mutex_t sdMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sdCnd = PTHREAD_COND_INITIALIZER;
static int      stopping = 0;
extern int      remSem();

static void
stopBroker(void *p)
{
  struct timespec waitTime;
  int rc,sa=0,sp=0, count = 0;

  /* SF 3497096 bugzilla 77022 */
  /* stopping is set to prevent other threads calling this routine */
  pthread_mutex_lock(&syncMtx);
  if (stopping) {
     printf("Stopping sfcb is in progress. Please wait...\n");
     pthread_mutex_unlock(&syncMtx);
     return;
  }
  else {
    stopping=1;
    pthread_mutex_unlock(&syncMtx);
  }

  /* Look for providers ready status. A 5 seconds wait is performed to
   * avoid a hang here in the event of provider looping, crashing etc
  */
  for (;;) {
      pthread_mutex_lock(&syncMtx);
      if (prov_rdy_state == -1) {
        if (count >= 5) break; /* lock will be released later */
         pthread_mutex_unlock(&syncMtx);
         sleep(1);
         count++;
       }
       else break; /* lock will be released later */
  }

  stopLocalConnectServer();

  for (;;) {

    if (adaptersStopped == 0) {
      pthread_mutex_lock(&sdMtx);
      waitTime.tv_sec = time(NULL) + 1; //5
      waitTime.tv_nsec = 0;
      if (sa == 0)
        fprintf(stderr, "--- Stopping adapters\n");
      sa++;
      if (stopNextAdapter()) {
        rc = pthread_cond_timedwait(&sdCnd, &sdMtx, &waitTime);
      } else {
        /*
         * no adapters found 
         */
        adaptersStopped = 1;
      }
      pthread_mutex_unlock(&sdMtx);
    }

    if (adaptersStopped) {
      pthread_mutex_lock(&sdMtx);
      waitTime.tv_sec = time(NULL) + 1; //5
      waitTime.tv_nsec = 0;
      if (sp == 0)
        fprintf(stderr, "--- Stopping providers\n");
      sp++;
      if (stopNextProc()) {
        rc = pthread_cond_timedwait(&sdCnd, &sdMtx, &waitTime);
      }
      // else providersStopped=1;
      pthread_mutex_unlock(&sdMtx);
    }
    if (providersStopped)
      break;
  }
  remSem();

  uninit_sfcBroker();
  uninitProvProcCtl();
  uninitSocketPairs();
  sunsetControl();
  uninitGarbageCollector();
  closeLogging();
  free((void *)sfcBrokerStart);

  pthread_mutex_unlock(&syncMtx);

  _SFCB_TRACE_STOP();

  unloadHostnameLib();

  if (restartBroker) {
    char           *emsg = strerror(errno);
    execvp("sfcbd", restartArgv);
    fprintf(stderr, "--- execv for restart problem: %s\n", emsg);
    abort();
  }

  else
    exit(0);
}

static void
signalBroker(void *p)
{
  pthread_mutex_lock(&sdMtx);
  pthread_cond_signal(&sdCnd);
  pthread_mutex_unlock(&sdMtx);
}

#define LOCAL_SFCB

static void
startLocalConnectServer()
{
#ifdef LOCAL_SFCB
  void            localConnectServer();
  pthread_t       t;
  pthread_attr_t  tattr;

  pthread_attr_init(&tattr);
  pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
  pthread_create(&t, &tattr, (void *(*)(void *)) localConnectServer, NULL);
#endif
}

static void
handleSigquit(int sig)
{

  pthread_t       t;
  pthread_attr_t  tattr;

  if (sfcBrokerPid == currentProc) {
    fprintf(stderr, "--- Winding down %s\n", processName);
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &tattr, (void *(*)(void *)) stopBroker, NULL);
    uninitGarbageCollector();
  }
}

static void
handleSigHup(int sig)
{

  pthread_t       t;
  pthread_attr_t  tattr;

  if (sfcBrokerPid == currentProc) {
    restartBroker = 1;
    fprintf(stderr, "--- Restarting %s\n", processName);
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &tattr, (void *(*)(void *)) stopBroker, NULL);
    // uninit_sfcBroker();
  }
}

static void
handleSigChld(int sig)
{

  const int       oerrno = errno;
  pid_t           pid;
  int             status,
                  left;
  pthread_t       t;
  pthread_attr_t  tattr;

  for (;;) {
    pid = wait3(&status, WNOHANG, (struct rusage *) 0);
    if ((int) pid == 0)
      break;
    if ((int) pid < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        // mlogf(M_INFO,M_SHOW, "pid: %d continue \n", pid);
        continue;
      }
      if (errno != ECHILD)
        perror("child wait");
      break;
    } else {
      // mlogf(M_INFO,M_SHOW,"sigchild %d\n",pid);
      if (testStartedAdapter(pid, &left)) {
        if (left == 0) {
          fprintf(stderr, "--- Adapters stopped\n");
          adaptersStopped = 1;
        }
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &tattr, (void *(*)(void *)) signalBroker, NULL);
      } else if (testStartedProc(pid, &left)) {
        if (left == 0) {
          fprintf(stderr, "--- Providers stopped\n");
          providersStopped = 1;
        }
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &tattr, (void *(*)(void *)) signalBroker, NULL);
      }
    }
  }
  errno = oerrno;
}

#ifdef NEEDS_CLEANUP
static void
handleSigterm(int sig)
{

  if (!terminating) {
    fprintf(stderr, "--- %s - %d exiting due to signal %d\n", processName,
            currentProc, sig);
    dumpTiming(currentProc);
  }
  terminating = 1;
  if (providerProcess)
    kill(currentProc, SIGKILL);
  exit(1);
}
#endif

static void
handleSigSegv(int sig)
{
  fprintf(stderr, "-#- %s - %d exiting due to a SIGSEGV signal\n",
          processName, currentProc);
}
/*
 * static void handleSigAbort(int sig) { fprintf(stderr,"%s: exiting due
 * to a SIGABRT signal - %d\n", processName, currentProc); kill(0,
 * SIGTERM); } 
 */

#ifndef LOCAL_CONNECT_ONLY_ENABLE

static int
startHttpd(int argc, char *argv[], int sslMode)
{
  int             pid,
                  sfcPid = currentProc;
  int             httpSFCB,
                  rc;
  char           *httpUser;
  uid_t           httpuid;
  struct passwd  *passwd;

  // Get/check http user info
  if (getControlBool("httpUserSFCB", &httpSFCB)) {
    mlogf(M_ERROR, M_SHOW,
          "--- Error retrieving http user info from config file.\n");
    exit(2);
  }
  if (httpSFCB) {
    // This indicates that we should use the SFCB user by default
    httpuid = -1;
  } else {
    // Get the user specified in the config file
    if (getControlChars("httpUser", &httpUser)) {
      mlogf(M_ERROR, M_SHOW,
            "--- Error retrieving http user info from config file.\n");
      exit(2);
    } else {
      errno = 0;
      passwd = getpwnam(httpUser);
      if (passwd) {
        httpuid = passwd->pw_uid;
      } else {
        mlogf(M_ERROR, M_SHOW,
              "--- Couldn't find http username %s requested in SFCB config file. Errno: %d\n",
              httpUser, errno);
        exit(2);
      }
    }
  }

  pid = fork();
  if (pid < 0) {
    char           *emsg = strerror(errno);
    mlogf(M_ERROR, M_SHOW, "-#- http fork: %s", emsg);
    exit(2);
  }
  if (pid == 0) {
    currentProc = getpid();
    if (httpuid != -1) {
      // Set the real and effective uids
      rc = setreuid(httpuid, httpuid);
      if (rc == -1) {
        mlogf(M_ERROR, M_SHOW, "--- Changing uid for http failed.\n");
        exit(2);
      }
    }

    if (httpDaemon(argc, argv, sslMode)) {
      kill(sfcPid, 3);          /* if port in use, shutdown */
    }

    closeSocket(&sfcbSockets, cRcv, "startHttpd");
    closeSocket(&resultSockets, cAll, "startHttpd");
  } else {
    addStartedAdapter(pid);
    return 0;
  }
  return 0;
}

#endif                          // LOCAL_CONNECT_ONLY_ENABLE

#ifdef HAVE_JDBC

extern int      dbpDaemon(int argc, char *argv[], int sslMode,
                          int sfcbPid);
static int
startDbpd(int argc, char *argv[], int sslMode)
{
  int             pid,
                  sfcPid = currentProc;
  // sleep(2);
  pid = fork();
  if (pid < 0) {
    perror("dbpd fork");
    exit(2);
  }
  if (pid == 0) {
    currentProc = getpid();
    dbpDaemon(argc, argv, sslMode, sfcPid);
    closeSocket(&sfcbSockets, cRcv, "startHttpd");
    closeSocket(&resultSockets, cAll, "startHttpd");
  } else {
    addStartedAdapter(pid);
    return 0;
  }
  return 0;
}

#endif

static void
usage(int status)
{
  if (status != 0)
    fprintf(stderr, "Try '%s --help' for more information.\n", name);

  else {
    static const char *help[] = {
      "",
      "Options:",
      " -c, --config-file=<FILE>        use alternative configuration file",
      " -d, --daemon                    run in the background",
      " -h, --help                      display this message and exit",
      " -k, --color-trace               color the trace output of each process",
      " -l, --syslog-level=<LOGLEVEL>   specify the level for syslog",
      "                                 LOGLEVEL can be LOG_INFO, LOG_DEBUG, or LOG_ERR",
      "                                 LOG_ERR is the default",
      " -s, --collect-stats             turn on runtime statistics collecting",
      " -t, --trace-components=<N|?>    activate component-level tracing messages where",
      "                                 N is an OR-ed bitmask integer defining the",
      "                                 components to trace; ? lists the available",
      "                                 components with their bitmask and exits",
      " -v, --version                   output version information and exit",
      " -i, --disable-repository-default-inst-prov To disable entry into the default provider",
      "",
      "For SBLIM package updates and additional information, please see",
      "    the SBLIM homepage at http://sblim.sourceforge.net"
    };

    int             i;

    fprintf(stdout, "Usage: %s [options]\n", name);
    for (i = 0; i < sizeof(help) / sizeof(char *); i++)
      fprintf(stdout, "%s\n", help[i]);
  }

  exit(status);
}

/* SF 3462309 : Check if there is an instance of sfcbd running; use procfs */
static int
sfcb_is_running()
{
    #define STRBUF_LEN 512
    #define BUF_LEN 30
    struct dirent *dp = NULL;
    char *strbuf = malloc(STRBUF_LEN);
    char *buf = malloc(BUF_LEN);
    int mypid = getpid();
    int ret = 0;

    DIR *dir = opendir("/proc");
    while ((dp = readdir(dir)) != NULL) {
        if (isdigit(dp->d_name[0])) {
            sprintf(buf, "/proc/%s/exe", dp->d_name);
            memset(strbuf, 0, STRBUF_LEN);
            if (readlink(buf, strbuf, STRBUF_LEN) == -1) continue;
            if (strstr(strbuf, "sfcbd") != NULL) {
                ret = strtol(dp->d_name, NULL, 0);
                if (ret == mypid) { ret = 0; continue; }
                break;
             }
        }
     }

     closedir(dir);
     free(buf);
     free(strbuf);
     return(ret);
}


static void
version()
{
  fprintf(stdout, "%s " sfcHttpDaemonVersion "\n", name);

  exit(0);
}

int
main(int argc, char *argv[])
{
  int             c,
                  i;
  long            tmask = 0,
      sslMode = 0,
      sslOMode = 0,
      tracelevel = 0;
  char           *tracefile = NULL;
#ifdef HAVE_UDS
  int             enableUds = 0;
#endif
  int             enableHttp = 0,
      enableHttps = 0,
      useChunking = 0,
      doBa = 0,
      enableInterOp = 0,
      httpLocalOnly = 0;
  int             syslogLevel = LOG_ERR;
  long            dSockets,
                  pSockets;
  char           *pauseStr;

  sfcbUseSyslog=1;
  /* SF 3462309 - If there is an instance running already, return */
  int pid_found = 0;
  if ((pid_found = sfcb_is_running()) != 0) {
      mlogf(M_ERROR, M_SHOW, " --- A previous instance of sfcbd [%d] is running. Exiting.\n", pid_found);
      exit(1);
  }

  name = strrchr(argv[0], '/');
  if (name != NULL)
    ++name;
  else
    name = argv[0];

  collectStat = 0;
  colorTrace = 0;
  processName = "sfcbd";
  provPauseStr = getenv("SFCB_PAUSE_PROVIDER");
  httpPauseStr = getenv("SFCB_PAUSE_CODEC");
  currentProc = sfcBrokerPid = getpid();
  restartArgc = argc;
  restartArgv = argv;

  exFlags = 0;

  static struct option const long_options[] = {
    {"config-file", required_argument, 0, 'c'},
    {"daemon", no_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"color-trace", no_argument, 0, 'k'},
    {"collect-stats", no_argument, 0, 's'},
    {"syslog-level", required_argument, 0, 'l'},
    {"trace-components", required_argument, 0, 't'},
    {"version", no_argument, 0, 'v'},
    {"disable-repository-default-inst-provider", no_argument, 0, 'i'},
    {0, 0, 0, 0}
  };

  while ((c =
          getopt_long(argc, argv, "c:dhkst:vil:", long_options,
                      0)) != -1) {
    switch (c) {
    case 0:
      break;

    case 'c':
      configfile = strdup(optarg);
      break;

    case 'd':
      daemon(0, 0);
      currentProc = sfcBrokerPid = getpid();    /* req. on some systems */
      break;

    case 'h':
      usage(0);

    case 'k':
      colorTrace = 1;
      break;

    case 's':
      collectStat = 1;
      break;

    case 't':
      if (*optarg == '?') {
        fprintf(stdout, "---   Traceable Components:     Int       Hex\n");
        for (i = 0; traceIds[i].id; i++)
          fprintf(stdout, "--- \t%18s:    %d\t0x%05X\n", traceIds[i].id,
                  traceIds[i].code, traceIds[i].code);
        exit(0);
      } else if (isdigit(*optarg)) {
        char           *ep;
        tmask = strtol(optarg, &ep, 0);
      } else {
        fprintf(stderr,
                "Try %s -t ? for a list of the trace components and bitmasks.\n",
                name);
        exit(1);
      }
      break;

    case 'v':
      version();

    case 'i':
      disableDefaultProvider = 1;
      break;

    case 'l':
      if (strcmp(optarg, "LOG_ERR") == 0) {
        syslogLevel = LOG_ERR;
      } else if (strcmp(optarg, "LOG_INFO") == 0) {
        syslogLevel = LOG_INFO;
      } else if (strcmp(optarg, "LOG_DEBUG") == 0) {
        syslogLevel = LOG_DEBUG;
      } else {
        fprintf(stderr, "Invalid value for syslog-level.\n");
        usage(3);
      }
      break;

    default:
      usage(3);
    }
  }

  if (optind < argc) {
    fprintf(stderr, "SFCB not started: unrecognized config property %s\n",
            argv[optind]);
    usage(1);
  }

  startLogging(syslogLevel,1);

  mlogf(M_INFO, M_SHOW, "--- %s V" sfcHttpDaemonVersion " started - %d\n",
        name, currentProc);

  //get the creation timestamp for the sequence context
  struct timeval  tv;
  struct timezone tz;
  gettimeofday(&tv, &tz);
  struct tm cttm;
  sfcBrokerStart = (char *) malloc(15 * sizeof(char));
  memset((void *)sfcBrokerStart, 0, 15 * sizeof(char));
  if (gmtime_r(&tv.tv_sec, &cttm) != NULL) {
    strftime((char *)sfcBrokerStart, 15, "%Y%m%d%H%M%S", &cttm);
  }

  if (collectStat) {
    mlogf(M_INFO, M_SHOW, "--- Statistics collection enabled\n");
    remove("sfcbStat");
  }

  setupControl(configfile);

  _SFCB_TRACE_INIT();

  if (tmask == 0) {
    /*
     * trace mask not specified, check in config file 
     */
    getControlNum("traceMask", &tmask);
  }

  if (getControlNum("traceLevel", &tracelevel) || tracelevel == 0) {
    /*
     * no tracelevel found in config file, use default 
     */
    tracelevel = 1;
  }
  if (getenv("SFCB_TRACE_FILE") == NULL &&
      getControlChars("traceFile", &tracefile) == 0) {
    /*
     * only set tracefile from config file if not specified via env 
     */
    _SFCB_TRACE_SETFILE(tracefile);
  }
  _SFCB_TRACE_START(tracelevel, tmask);
  
  // SFCB_DEBUG
#ifndef SFCB_DEBUG
  if (tmask)
    mlogf(M_ERROR, M_SHOW,
          "--- SCFB_DEBUG not configured. -t %d ignored\n", tmask);
#endif

  if ((pauseStr = getenv("SFCB_PAUSE_PROVIDER"))) {
    printf("--- Provider pausing for: %s\n", pauseStr);
  }

  if (getControlBool("enableHttp", &enableHttp))
    enableHttp = 1;

#ifdef HAVE_UDS
  if (getControlBool("enableUds", &enableUds))
    enableUds = 1;
#endif

#if defined USE_SSL
  if (getControlBool("enableHttps", &enableHttps))
    enableHttps = 0;

  sslMode = enableHttps;
#ifdef HAVE_UDS
  sslOMode = sslMode & !enableHttp & !enableUds;
#else
  sslOMode = sslMode & !enableHttp;
#endif
#else
  mlogf(M_INFO, M_SHOW, "--- SSL not configured\n");
  enableHttps = 0;
  sslMode = 0;
  sslOMode = 0;
#endif

  if (getControlBool("useChunking", &useChunking))
    useChunking = 0;
  if (useChunking == 0)
    mlogf(M_INFO, M_SHOW, "--- Chunking disabled\n");

  if (getControlBool("doBasicAuth", &doBa))
    doBa = 0;
  if (!doBa)
    mlogf(M_INFO, M_SHOW, "--- User authentication disabled\n");

  if (getControlBool("enableInterOp", &enableInterOp))
    enableInterOp = 0;

  if (getControlNum("httpProcs", (long *) &dSockets))
    dSockets = 10;
  if (getControlNum("provProcs", (long *) &pSockets))
    pSockets = 16;

  if (getControlBool("httpLocalOnly", &httpLocalOnly))
    httpLocalOnly = 0;
  if (httpLocalOnly)
    mlogf(M_INFO, M_SHOW,
          "--- External HTTP connections disabled; using loopback only\n");

  if (getControlNum
      ("providerSampleInterval", (long *) &provSampleInterval))
    provSampleInterval = 30;
  if (getControlNum
      ("providerTimeoutInterval", (long *) &provTimeoutInterval))
    provTimeoutInterval = 60;
  if (getControlBool("providerAutoGroup", (int *) &provAutoGroup))
    provAutoGroup = 1;

  resultSockets = getSocketPair("sfcbd result");
  sfcbSockets = getSocketPair("sfcbd sfcb");

  if (enableInterOp == 0)
    mlogf(M_INFO, M_SHOW, "--- InterOp namespace disabled\n");
  else
    exFlags = exFlags | 2;

  if ((enableInterOp && pSockets < 4) || pSockets < 3) {
    /*
     * adjusting provider number 
     */
    if (enableInterOp) {
      pSockets = 4;
    } else {
      pSockets = 3;
    }
    mlogf(M_INFO, M_SHOW,
          "--- Max provider process number adjusted to %d\n", pSockets);
  }

  /* Check for whitespace trimming option */
  if (getControlBool("trimWhitespace", &trimws)) {
    trimws = 0;
  }

  if ((enableHttp || enableHttps) && dSockets > 0) {
    startHttp = 1;
  }

  if (loadHostnameLib() == -1) {
     printf("--- Failed to load sfcCustomLib. Exiting\n");
     exit(1);
  }

  initSem(pSockets);
  initProvProcCtl(pSockets);
  init_sfcBroker();
  initSocketPairs(pSockets, dSockets);

  setSignal(SIGQUIT, handleSigquit, 0);
  setSignal(SIGINT, handleSigquit, 0);

  setSignal(SIGTERM, handleSigquit, 0);
  setSignal(SIGHUP, handleSigHup, 0);

  atexit(uninitGarbageCollector);

  startLocalConnectServer();

#ifndef LOCAL_CONNECT_ONLY_ENABLE
  if (startHttp) {
    startHttpd(argc, argv, sslMode);
  }
#endif                          // LOCAL_CONNECT_ONLY_ENABLE

  // Display the configured request handlers
  char rtmsg[20]=" ";
#ifdef HANDLER_CIMXML
  strcat(rtmsg,"CIMxml ");
#endif
#ifdef HANDLER_CIMRS
  strcat(rtmsg,"CIMrs ");
#endif
mlogf(M_INFO, M_SHOW, "--- Request handlers enabled:%s\n",rtmsg);

#ifdef HAVE_JDBC
  // Start dbProtocol-Daemon
  if (startDbp) {
    if (sslMode)
      startDbpd(argc, argv, 1);
    if (!sslOMode)
      startDbpd(argc, argv, 0);
  }
#endif

  setSignal(SIGSEGV, handleSigSegv, SA_ONESHOT);
  setSignal(SIGCHLD, handleSigChld, 0);

  processProviderMgrRequests();

  return 0;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
