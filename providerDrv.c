
/*
 * $Id: providerDrv.c,v 1.83 2010/02/04 19:20:38 smswehla Exp $
 *
 * © Copyright IBM Corp. 2005, 2007
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * CMPI style provider driver.
 *
 */

#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include "trace.h"
#include "cmpi/cmpidt.h"
#include "providerMgr.h"
#include "providerRegister.h"
#include <sfcCommon/utilft.h>
#include "cimXmlParser.h"
#include "support.h"
#include "msgqueue.h"
#include "constClass.h"
#include "native.h"
#include "queryOperation.h"
#include "selectexp.h"
#include "control.h"
#include "config.h"
#include "constClass.h"

#ifdef HAVE_QUALREP
#include "qualifier.h"
#endif

#ifdef HAVE_INDICATIONS
#define SFCB_INCL_INDICATION_SUPPORT 1
#endif

#define PROVCHARS(p) (p && *((char*)p)) ? (char*)p : NULL

char           *opsName[];

#ifdef SFCB_DEBUG

#define TIMING_PREP \
   int uset=0; \
   struct rusage us,ue,cs,ce; \
   struct timeval sv,ev;

#define TIMING_START(req,pInfo)						\
      if (pInfo && req && (*_ptr_sfcb_trace_mask & TRACE_RESPONSETIMING) ) {\
         gettimeofday(&sv,NULL);\
         getrusage(RUSAGE_SELF,&us);\
         getrusage(RUSAGE_CHILDREN,&cs);\
         uset=1;\
      }

#define TIMING_STOP(req,pInfo)					\
      if (uset) { \
	gettimeofday(&ev,NULL); \
	getrusage(RUSAGE_SELF,&ue); \
	getrusage(RUSAGE_CHILDREN,&ce); \
	_sfcb_trace(1, __FILE__, __LINE__, \
		    _sfcb_format_trace("-#- Provider  %.5u %s-%s real: %f user: %f sys: %f children user: %f children sys: %f \n",  \
				       req->sessionId, \
				       opsName[req->operation], \
				       pInfo->providerName, \
				       timevalDiff(&sv,&ev), \
				       timevalDiff(&us.ru_utime,&ue.ru_utime), \
				       timevalDiff(&us.ru_stime,&ue.ru_stime), \
				       timevalDiff(&cs.ru_utime,&ce.ru_utime), \
				       timevalDiff(&cs.ru_stime,&ce.ru_stime) \
				       )); \
	uset=0;\
      }
#else

#define TIMING_PREP
#define TIMING_START(req,pInfo)
#define TIMING_STOP(req,pInfo)

#endif

/* default if no particular dlopen flags are specified */
#ifndef PROVIDERLOAD_DLFLAG
#define PROVIDERLOAD_DLFLAG (RTLD_NOW | RTLD_GLOBAL)
#endif

#ifndef HAVE_OPTIMIZED_ENUMERATION
     /* not a special provider, perform class name substitution if call is for a
        parent class of the class the provider is registered for */
#define REPLACE_CN(info,path)  \
  if (info->className && info->className[0] != '$') {             \
    char * classname = CMGetCharPtr(CMGetClassName(path,NULL));   \
    char * namespace = CMGetCharPtr(CMGetNameSpace(path,NULL));         \
    if (classname && namespace && strcasecmp(info->className,classname)) { \
      CMPIObjectPath * provPath = CMNewObjectPath(Broker,namespace,info->className,NULL); \
      if (provPath && CMClassPathIsA(Broker,provPath,classname,NULL)) { \
        _SFCB_TRACE(1, ("--- Replacing class name %s",info->className)); \
        path = provPath;                                                \
      }  }  }
#endif

extern CMPIBroker *Broker;

extern unsigned long exFlags;
extern ProviderRegister *pReg;
extern ProviderInfo *classProvInfoPtr;

extern void     processProviderInvocationRequests(char *);
extern CMPIObjectPath *relocateSerializedObjectPath(void *area);
extern MsgSegment setInstanceMsgSegment(CMPIInstance *op);
extern MsgSegment setArgsMsgSegment(CMPIArgs * args);
extern MsgSegment setConstClassMsgSegment(CMPIConstClass * cl);
extern void     getSerializedConstClass(CMPIConstClass * cl, void *area);
extern void     getSerializedArgs(CMPIArgs * cl, void *area);
extern CMPIConstClass *relocateSerializedConstClass(void *area);
extern CMPIInstance *relocateSerializedInstance(void *area);
extern CMPIConstClass *relocateSerializedConstClass(void *area);
extern CMPIArgs *relocateSerializedArgs(void *area);
extern MsgSegment setArgsMsgSegment(CMPIArgs * args);
extern void     dump(char *msg, void *a, int l);
extern void     showClHdr(void *ihdr);
extern ProvIds  getProvIds(ProviderInfo * info);
extern int      xferLastResultBuffer(CMPIResult *result, int to, int rc);
extern void     setResultQueryFilter(CMPIResult *result, QLStatement * qs);
extern CMPIArray *getKeyListAndVerifyPropertyList(CMPIObjectPath *,
                                                  char **props, int *ok,
                                                  CMPIStatus *rc);
extern void     dumpTiming(int pid);
static BinResponseHdr *errorCharsResp(int rc, char *msg);
static int      sendResponse(int requestor, BinResponseHdr * hdr);

extern CMPISelectExp *NewCMPISelectExp(const char *queryString,
                                       const char *language,
                                       const char *sns,
                                       CMPIArray **projection,
                                       CMPIStatus *rc);
NativeSelectExp *activFilters = NULL;
extern void     setStatus(CMPIStatus *st, CMPIrc rc, char *msg);

static ProviderProcess *provProc = NULL,
    *curProvProc = NULL;
static int      provProcMax = 0;
static int      idleThreadStartHandled = 0;

ProviderInfo   *activProvs = NULL;

static void increaseInUseSem(int id);
static void decreaseInUseSem(int id);

unsigned long   provSampleInterval = 10;
unsigned long   provTimeoutInterval = 25;
unsigned        provAutoGroup = 0;
static int      stopping = 0;

void            uninitProvProcCtl();
extern void     uninitSocketPairs();
extern void     sunsetControl();
extern void     uninitGarbageCollector();

static BinResponseHdr *err_crash_resp; /* holds generic "we crashed" response */
static long ecr_len;
static long makeSafeResponse(BinResponseHdr *hdr, BinResponseHdr **out);

typedef struct parms {
  int             requestor;
  BinRequestHdr  *req;
  ProviderInfo   *pInfo;
  struct parms   *next,
                 *prev;
} Parms;
static Parms   *activeThreadsFirst = NULL,
    *activeThreadsLast = NULL;

/*
 * old version support 
 */
typedef CMPIStatus (*authorizeFilterPreV1)

 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    (CMPIIndicationMI * mi, const CMPIContext *ctx, CMPIResult *result,
     const CMPISelectExp *se, const char *ns, const CMPIObjectPath * op,
     const char *user);

typedef CMPIStatus (*mustPollPreV1)

 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    (CMPIIndicationMI * mi, const CMPIContext *ctx, CMPIResult *result,
     const CMPISelectExp *se, const char *ns, const CMPIObjectPath * op);

typedef CMPIStatus (*activateFilterPreV1)

 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    (CMPIIndicationMI * mi, const CMPIContext *ctx, CMPIResult *result,
     const CMPISelectExp *se, const char *ns, const CMPIObjectPath * op,
     CMPIBoolean first);

typedef CMPIStatus (*deActivateFilterPreV1)

 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    (CMPIIndicationMI * mi, const CMPIContext *ctx, CMPIResult *result,
     const CMPISelectExp *se, const char *ns, const CMPIObjectPath * op,
     CMPIBoolean last);

void
libraryName(const char *dir, const char *location, char *fullName,
            int buf_size)
{
#if defined(CMPI_PLATFORM_WIN32_IX86_MSVC)
  if (dir) {
    snprintf(fullName, buf_size, "%s\\%s.dll", dir, location);
  } else {
    snprintf(fullName, buf_size, "%s.dll", location);
  }
#elif defined(CMPI_PLATFORM_LINUX_GENERIC_GNU)
  if (dir) {
    snprintf(fullName, buf_size, "%s/lib%s.so", dir, location);
  } else {
    snprintf(fullName, buf_size, "lib%s.so", location);
  }
#elif defined(CMPI_OS_HPUX)
  if (dir) {
    snprintf(fullName, buf_size, "%s/lib%s.so", dir, location);
  } else {
    snprintf(fullName, buf_size, "lib%s.so", location);
  }
#elif defined(CMPI_OS_OS400)
  if (dir) {
    strcpy(fullName, location);
#elif defined(CMPI_OS_DARWIN)
  if (dir) {
    snprintf(fullName, buf_size, "%s/lib%s.dylib", dir, location);
  } else {
    snprintf(fullName, buf_size, "lib%s.dylib", location);
  }
#else
  if (dir) {
    snprintf(fullName, buf_size, "%s/lib%s.so", dir, location);
  } else {
    snprintf(fullName, buf_size, "lib%s.so", location);
  }
#endif
}

int
testStartedProc(int pid, int *left)
{
  ProviderProcess *pp = provProc;
  ProviderInfo   *info;
  int             i,
                  stopped = 0;

  *left = 0;
  for (i = 0; i < provProcMax; i++) {
    if ((pp + i)->pid == pid) {
      stopped = 1;
      (pp + i)->pid = 0;
      info = (pp + i)->firstProv;
      if (pReg)
         pReg->ft->resetProvider(pReg, pid);
    }
    if ((pp + i)->pid != 0)
      (*left)++;
  }

  if (pid == classProvInfoPtr->pid) {
    stopped = 1;
    classProvInfoPtr->pid = 0;
  }
  if (classProvInfoPtr->pid != 0)
    (*left)++;

  return stopped;
}

int
stopNextProc()
{
  ProviderProcess *pp = provProc;
  int             i,
                  done = 0,
      t;

  for (i = provProcMax - 1; i; i--) {
    if ((pp + i)->pid) {
      kill((pp + i)->pid, SIGUSR1);
      return (pp + i)->pid;
    }
  }

  if (done == 0) {
    if (classProvInfoPtr && classProvInfoPtr->pid) {
      t = classProvInfoPtr->pid;
      kill(classProvInfoPtr->pid, SIGUSR1);
      done = 1;
      return t;
    }
  }

  return 0;
}

 static int getActivProvCount() {
   ProviderInfo* tmp;
   int count = 0;
   for (tmp = activProvs; tmp; tmp = tmp->next)
     count++;
   return count;
 }

 static void increaseInUseSem(int id)
 {
   _SFCB_ENTER(TRACE_PROVIDERDRV, "increaseInUseSem");
  
   if (semAcquireUnDo(sfcbSem,PROV_GUARD(id))) {
     mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
           id, strerror(errno));
     _SFCB_ABORT();
   }
   if (semReleaseUnDo(sfcbSem,PROV_INUSE(id))) {
     mlogf(M_ERROR,M_SHOW,"-#- Fatal error increasing inuse semaphore for %d, reason: %s\n",
           id, strerror(errno));
     _SFCB_ABORT();
   }
   if (semReleaseUnDo(sfcbSem,PROV_GUARD(id))) {
     mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
           id, strerror(errno));
     _SFCB_ABORT();
   }
   _SFCB_EXIT();
 }

 static void decreaseInUseSem(int id)
 {
   _SFCB_ENTER(TRACE_PROVIDERDRV, "decreaseInUseSem");
  
   if (semAcquireUnDo(sfcbSem,PROV_GUARD(id))) {
     mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
           id, strerror(errno));
     _SFCB_ABORT();
   }
   if (semGetValue(sfcbSem,PROV_INUSE(id)) > 0) {
     if (semAcquireUnDo(sfcbSem,PROV_INUSE(id))) {
       mlogf(M_ERROR,M_SHOW,"-#- Fatal error decreasing inuse semaphore for %d, reason: %s\n",
             id, strerror(errno));
       _SFCB_ABORT();
     }
   }
   if (semReleaseUnDo(sfcbSem,PROV_GUARD(id))) {
     mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
           id, strerror(errno));
     _SFCB_ABORT();
   }
   _SFCB_EXIT();
 }

 typedef struct _provLibAndTypes {
   void* lib;
#define INST  1
#define ASSOC 2
#define METH  4
#define IND   8
   int types; /* bitmask for each type */
 } ProvLibAndTypes;

 /* hasBeenCleaned returns < 0 if the cleanup function for type for a provider
   (represented by plib, the handle from it's dlopen()) has been called

   index is an out param; it's the index for plib in list (or the next available
   position if it's not yet in list)

   Details:
   we need to take special care when cleaning up, since we only want to call
   the cleanup function (for each MI) only once, even if there are multiple
   classes handled by the same provider.  Also, we can't just toggle a
   "cleanedUp" flag for the whole proc, since we may be grouping. So we need
   to track cleanup calls for each type for each provider in the proc.
 */

 static int hasBeenCleaned(ProvLibAndTypes* list, int type, void* plib, int* index) {
   int rc = 0;
   int i;
   for (i=0; list[i].lib > 0; i++) {
     if (list[i].lib == plib) {
       rc = list[i].types & type;
       break;
     }
   }
   *index = i;
   return rc;
 }

static void
stopProc(void *p)
{
  ProviderInfo   *pInfo;
  CMPIContext    *ctx = NULL;

  int apc = getActivProvCount();
  ProvLibAndTypes cleanedProvs[apc];
  int i;
  for (i=0; i < apc; i++) { cleanedProvs[i].lib = 0; cleanedProvs[i].types = 0; }
  int cpli = 0; /* the index into cleanedProvs for a given prov lib */

  ctx = native_new_CMPIContext(MEM_NOT_TRACKED, NULL);
  for (pInfo = activProvs; pInfo; pInfo = pInfo->next,cpli++) {
    if (pInfo->classMI ) pInfo->classMI->ft->cleanup(pInfo->classMI, ctx);
    if (pInfo->instanceMI && (hasBeenCleaned(cleanedProvs, INST, pInfo->library, &cpli) == 0)) {
      pInfo->instanceMI->ft->cleanup(pInfo->instanceMI, ctx, 1);
      cleanedProvs[cpli].lib = pInfo->library;
      cleanedProvs[cpli].types |= INST;
    }
    if (pInfo->associationMI && (hasBeenCleaned(cleanedProvs, ASSOC, pInfo->library, &cpli) == 0)) {
      pInfo->associationMI->ft->cleanup(pInfo->associationMI, ctx, 1);
      cleanedProvs[cpli].lib = pInfo->library;
      cleanedProvs[cpli].types |= ASSOC;
    }
    if (pInfo->methodMI && (hasBeenCleaned(cleanedProvs, METH, pInfo->library, &cpli) == 0)) {
      pInfo->methodMI->ft->cleanup(pInfo->methodMI, ctx, 1);
      cleanedProvs[cpli].lib = pInfo->library;
      cleanedProvs[cpli].types |= METH;
    }
    if (pInfo->indicationMI && (hasBeenCleaned(cleanedProvs, IND, pInfo->library, &cpli) == 0)) {
      pInfo->indicationMI->ft->disableIndications(pInfo->indicationMI,
						  ctx);
      pInfo->indicationMI->ft->cleanup(pInfo->indicationMI, ctx, 1);
      cleanedProvs[cpli].lib = pInfo->library;
      cleanedProvs[cpli].types |= IND;
    }
  }
  mlogf(M_INFO, M_SHOW, "---  stopped %s %d\n", processName, getpid());
  ctx->ft->release(ctx);

  //uninit_sfcBroker(); /* 3497096 */
  //uninitProvProcCtl();
  //uninitSocketPairs();
  //sunsetControl();
  //uninitGarbageCollector();

  exit(0);
}

static void handleSigPipe(int sig) 
{
  // Got a sigpipe, but we don't want to do anything about it because it could
  // cause the provider to unload improperly.
  mlogf(M_ERROR,M_SHOW, "-#- %s - %d provider received a SIGPIPE signal, ignoring\n",
    processName, currentProc);
}


static void
handleSigSegv(int sig)
{
  Parms          *threads = activeThreadsFirst;
  int dmy = -1;

  mlogf(M_ERROR, M_SHOW,
        "-#- %s - %d provider exiting due to a SIGSEGV signal\n",
        processName, currentProc);
  while (threads) {
    spSendResult(&threads->requestor, &dmy, err_crash_resp, ecr_len);
    threads=threads->next;
  }
  abort(); /* force cord dump */
}

static void
handleSigUsr1(int sig)
{
  pthread_t       t;
  pthread_attr_t  tattr;

  stopping = 1;
  pthread_attr_init(&tattr);
  pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
  pthread_create(&t, &tattr, (void *(*)(void *)) stopProc, NULL);
}

/*
 * ------------- --- Provider Loading support --- ------------- 
 */

void
initProvProcCtl(int p)
{
  int             i;

  mlogf(M_INFO, M_SHOW, "--- Max provider procs: %d\n", p);
  provProcMax = p;
  provProc = (ProviderProcess *) calloc(p, sizeof(*provProc));
  for (i = 0; i < p; i++)
    provProc[i].id = i;
}

void
uninitProvProcCtl()
{
  free(provProc);
}

static pthread_mutex_t idleMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t activeMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t idleCnd = PTHREAD_COND_INITIALIZER;

void           *
providerIdleThread()
{
  struct timespec idleTime;
  time_t          next;
  int             rc,
                  val,
                  doNotExit,
                  noBreak = 1;
  ProviderInfo   *pInfo;
  ProviderProcess *proc;
  CMPIContext    *ctx = NULL;
  CMPIStatus      crc;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "providerIdleThread");

  idleThreadStartHandled = 1;

  for (;;) {
    idleTime.tv_sec = time(&next) + provSampleInterval;
    idleTime.tv_nsec = 0;

    _SFCB_TRACE(1,
                ("--- providerIdleThread cycle restarted %d",
                 currentProc));
    pthread_mutex_lock(&idleMtx);
    rc = pthread_cond_timedwait(&idleCnd, &idleMtx, &idleTime);
    if (stopping)  /* sfcb main told us we're shutting down */
      return NULL;
    if (rc == ETIMEDOUT) {  /* we hit providerSampleInterval timeout */
      time_t          now;
      time(&now);
      pInfo = activProvs;
      doNotExit = 0;
      crc.rc = 0;
      noBreak = 1;
      if (pInfo) {
        proc = curProvProc;
        if (proc) {
          if (semAcquireUnDo(sfcbSem,PROV_GUARD(proc->id))) {
            mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
                  proc->id, strerror(errno));
            _SFCB_ABORT();
          }
          if ((val=semGetValue(sfcbSem,PROV_INUSE(proc->id)))==0) {            
	    /* providerTimeoutInterval reached? */
            if ((now - proc->lastActivity) > provTimeoutInterval) { 
              ctx = native_new_CMPIContext(MEM_TRACKED, NULL);
              noBreak = 0;

	      int apc = getActivProvCount();
	      ProvLibAndTypes cleanedProvs[apc];
	      int i;
	      for (i=0; i < apc; i++) { cleanedProvs[i].lib = 0; cleanedProvs[i].types = 0; }
	      int cpli = 0; /* the index into cleanedProvs for a given prov lib */

	      /* loop through all provs in proc & perform cleanup as needed */
              for (crc.rc = 0, pInfo = activProvs; pInfo;
                   pInfo = pInfo->next,cpli++) {
                  if (pInfo->library == NULL)
                    continue;

                  if (crc.rc == 0 && pInfo->instanceMI && (hasBeenCleaned(cleanedProvs, INST, pInfo->library, &cpli) == 0)) {
                    crc =
                        pInfo->instanceMI->ft->cleanup(pInfo->instanceMI,
                                                       ctx, 0);
		    if (crc.rc==CMPI_RC_OK) {
		      cleanedProvs[cpli].lib = pInfo->library;
		      cleanedProvs[cpli].types |= INST;
		    }
		  }
                  if (crc.rc == 0 && pInfo->associationMI && (hasBeenCleaned(cleanedProvs, ASSOC, pInfo->library, &cpli) == 0)) {
                    crc =
                        pInfo->associationMI->ft->cleanup(pInfo->
                                                          associationMI,
                                                          ctx, 0);
		    if (crc.rc==CMPI_RC_OK) {
		      cleanedProvs[cpli].lib = pInfo->library;
		      cleanedProvs[cpli].types |= ASSOC;
		    }
		  }
                  if (crc.rc == 0 && pInfo->methodMI && (hasBeenCleaned(cleanedProvs, METH, pInfo->library, &cpli) == 0)) {
                    crc =
                        pInfo->methodMI->ft->cleanup(pInfo->methodMI, ctx,
                                                     0);
		    if (crc.rc==CMPI_RC_OK) {
		      cleanedProvs[cpli].lib = pInfo->library;
		      cleanedProvs[cpli].types |= METH;
		    }
		  }
                  if (crc.rc == 0 && pInfo->indicationMI && (hasBeenCleaned(cleanedProvs, IND, pInfo->library, &cpli) == 0)) {
                    crc =
                        pInfo->indicationMI->ft->cleanup(pInfo->indicationMI, ctx,
                                                     0);
		    if (crc.rc==CMPI_RC_OK) {
		      cleanedProvs[cpli].lib = pInfo->library;
		      cleanedProvs[cpli].types |= IND;
		    }
		  }

                  _SFCB_TRACE(1,
                              ("--- Cleanup rc: %d %s-%d", crc.rc,
                               processName, currentProc));
                  if (crc.rc == CMPI_RC_NEVER_UNLOAD)
                    doNotExit = 1; /* stop idle monitoring */
                  if (crc.rc == CMPI_RC_DO_NOT_UNLOAD)
                    doNotExit = noBreak = 1;
                  if (crc.rc == CMPI_RC_OK) {
                    _SFCB_TRACE(1,
                                ("--- Unloading provider %s-%d",
                                 pInfo->providerName, currentProc));
                    dlclose(pInfo->library);
                    pInfo->library = NULL;
                    pInfo->instanceMI = NULL;
                    pInfo->associationMI = NULL;
                    pInfo->methodMI = NULL;
                    pInfo->indicationMI = NULL;
                    pInfo->initialized = 0;
                    pthread_mutex_destroy(&pInfo->initMtx);
                  } else
                    doNotExit = 1;
                }
	      /* exit unless prov asks us not to, or returned bad cleanup rc */
              if (doNotExit == 0) {
                dumpTiming(currentProc);
                _SFCB_TRACE(1,
                            ("--- Exiting %s-%d", processName,
                             currentProc));
                exit(0);
              }
            }
          }
          if (semReleaseUnDo(sfcbSem,PROV_GUARD(proc->id))) {
            mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
                  proc->id, strerror(errno));
            _SFCB_ABORT();
          }
        }
      }
    }
    pthread_mutex_unlock(&idleMtx);
    if (noBreak == 0)
      break;
  }
  _SFCB_TRACE(1,
              ("--- Stopping idle-monitoring due to provider request %s-%d",
               processName, currentProc));

  _SFCB_RETURN(NULL);
}

static CMPIStatus
getInstanceMI(ProviderInfo * info, CMPIInstanceMI ** mi, CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getInstanceMI");

  if (info->instanceMI == NULL)
    info->instanceMI =
        loadInstanceMI(info->providerName, info->library, Broker, ctx,
                       &st);
  if (info->instanceMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->instanceMI;
  }
  _SFCB_RETURN(st);
}

static CMPIStatus
getAssociationMI(ProviderInfo * info, CMPIAssociationMI ** mi,
                 CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getAssociationMI");

  if (info->associationMI == NULL)
    info->associationMI =
        loadAssociationMI(info->providerName, info->library, Broker, ctx,
                          &st);
  if (info->associationMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->associationMI;
  }
  _SFCB_RETURN(st);
}

static CMPIStatus
getIndicationMI(ProviderInfo * info, CMPIIndicationMI ** mi,
                CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getIndicationMI");

  if (info->indicationMI == NULL)
    info->indicationMI =
        loadIndicationMI(info->providerName, info->library, Broker, ctx,
                         &st);
  if (info->indicationMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->indicationMI;
  }
  _SFCB_RETURN(st);
}

static CMPIStatus
getMethodMI(ProviderInfo * info, CMPIMethodMI ** mi, CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getMethodMI");

  if (info->methodMI == NULL)
    info->methodMI =
        loadMethodMI(info->providerName, info->library, Broker, ctx, &st);
  if (info->methodMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->methodMI;
  }
  _SFCB_RETURN(st);
}

static CMPIStatus
getClassMI(ProviderInfo * info, CMPIClassMI ** mi, CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getClassMI");

  if (info->classMI == NULL)
    info->classMI =
        loadClassMI(info->providerName, info->library, Broker, ctx, &st);
  if (info->classMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->classMI;
  }
  _SFCB_RETURN(st);
}

static CMPIStatus
getPropertyMI(ProviderInfo * info, CMPIPropertyMI ** mi, CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getPropertyMI");

  if (info->propertyMI == NULL)
    info->propertyMI =
        loadPropertyMI(info->providerName, info->library, Broker, ctx,
                       &st);
  if (info->propertyMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->propertyMI;
  }
  _SFCB_RETURN(st);
}

#ifdef HAVE_QUALREP
static CMPIStatus
getQualifierDeclMI(ProviderInfo * info, CMPIQualifierDeclMI ** mi,
                   CMPIContext *ctx)
{
  CMPIStatus      st = { CMPI_RC_OK, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getQualiferMI");

  if (info->qualifierDeclMI == NULL)
    info->qualifierDeclMI =
        loadQualifierDeclMI(info->providerName, info->library, Broker, ctx,
                            &st);
  if (info->qualifierDeclMI == NULL && st.rc == CMPI_RC_OK) {
    st.rc = CMPI_RC_ERR_FAILED;
  } else {
    *mi = info->qualifierDeclMI;
  }
  _SFCB_RETURN(st);
}
#endif

/*
 * see if we're under the max allowed number of loaded providers. if so,
 * fork() for the new provider I think this can be broken down into
 * smaller functions 
 */
static int
getProcess(ProviderInfo * info, ProviderProcess ** proc)
{
  int             i,
                  rc;
  static int      seq = 0;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "getProcess");

  if (provAutoGroup && info->group == NULL) {
    /*
     * implicitly put all providers in a module in a virtual group 
     */
    info->group = strdup(info->location);
  }

  if (info->group) {
    for (i = 0; i < provProcMax; i++) {
      if ((provProc + i) && provProc[i].pid &&
          provProc[i].group
          && strcmp(provProc[i].group, info->group) == 0) {
        if (semAcquireUnDo(sfcbSem,PROV_GUARD(provProc[i].id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
                provProc[i].id, strerror(errno));
          _SFCB_ABORT();
        }
        /* double checking pattern required to prevent race ! */
        if ((provProc+i) && provProc[i].pid &&
            provProc[i].group && strcmp(provProc[i].group,info->group)==0) {
          info->pid=provProc[i].pid;
          info->providerSockets=provProc[i].providerSockets;
          _SFCB_TRACE(1,("--- Process %d shared by %s and %s",provProc[i].pid,info->providerName,
                         provProc[i].firstProv->providerName));
          if (provProc[i].firstProv) info->next=provProc[i].firstProv;
          else info->next = NULL;
          provProc[i].firstProv=info;
          info->proc=provProc+i;
          if (info->unload<provProc[i].unload) provProc[i].unload=info->unload;
          if (semReleaseUnDo(sfcbSem,PROV_GUARD(provProc[i].id))) {
            mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
                  provProc[i].id, strerror(errno));
            _SFCB_ABORT();
          }
          _SFCB_RETURN(provProc[i].pid);
        }
        if (semReleaseUnDo(sfcbSem,PROV_GUARD(provProc[i].id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
                provProc[i].id, strerror(errno));
          _SFCB_ABORT();
        }
      }
    }
  }

  for (i = 0; i < provProcMax; i++) {
    if (provProc[i].pid == 0) {
      *proc = provProc + i;
      providerSockets = sPairs[(*proc)->id];

      (*proc)->providerSockets = info->providerSockets = providerSockets;
      (*proc)->group = info->group;
      (*proc)->unload = info->unload;
      (*proc)->firstProv = info;
      info->proc = *proc;
      info->next = NULL;

      (*proc)->pid = info->pid = fork();

      if (info->pid < 0) {
        perror("provider fork");
        _SFCB_ABORT();
      }

      if (info->pid == 0) {

        currentProc = getpid();
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        setSignal(SIGCHLD, SIG_DFL, 0);
        setSignal(SIGTERM, SIG_IGN, 0);
        setSignal(SIGHUP, SIG_IGN, 0);
        setSignal(SIGPIPE, handleSigPipe,0);
        setSignal(SIGUSR1, handleSigUsr1, 0);

        setSignal(SIGSEGV, handleSigSegv, SA_ONESHOT);

        // If requested, change the uid of the provider
        if (info->uid != -1) {
          _SFCB_TRACE(1,
                      ("--- Changing uid of provider, %s, to %d(%s)",
                       info->providerName, info->uid, info->user));
          // Set the real and effective uids
          rc = setreuid(info->uid, info->uid);
          if (rc == -1) {
            mlogf(M_ERROR, M_SHOW, "--- Changing uid for %s failed.\n",
                  info->providerName);
            _SFCB_RETURN(-1);
          }
        }

        curProvProc = (*proc);
        resultSockets = sPairs[(*proc)->id + ptBase];

        _SFCB_TRACE(1, ("--- Forked started for %s %d %d-%lu",
                        info->providerName, currentProc,
                        providerSockets.receive,
                        getInode(providerSockets.receive)));
        processName = strdup(info->providerName);
        providerProcess = 1;
        info->proc = *proc;
        info->pid = currentProc;

        /* The guard semaphore may never increase beyond 1, unless it is relased more often than
           acquired. Therefore it is cleaner to acquire it than to set it to 0 unconditionally, 
           which can lead to a race, but we will also check the value to be sure.
        */
        if (semAcquireUnDo(sfcbSem,PROV_GUARD((*proc)->id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
                (*proc)->id, strerror(errno));
          _SFCB_ABORT();
        }
        if (semGetValue(sfcbSem,PROV_GUARD((*proc)->id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error, guard semaphore for %d is not zero\n",
                (*proc)->id);
          _SFCB_ABORT();              
        }
        if (semSetValue(sfcbSem,PROV_INUSE((*proc)->id),0)) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error resetting inuse semaphore for %d, reason: %s\n",
                (*proc)->id, strerror(errno));
          _SFCB_ABORT();
        }
        if (semSetValue(sfcbSem,PROV_ALIVE((*proc)->id),0)) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error resetting alive semaphore for %d, step 1, reason: %s\n",
                (*proc)->id, strerror(errno));
          _SFCB_ABORT();
        }
        if (semReleaseUnDo(sfcbSem,PROV_ALIVE((*proc)->id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error resetting alive semaphore for %d, step 2, reason: %s\n",
                (*proc)->id, strerror(errno));
          _SFCB_ABORT();
        }
        if (semReleaseUnDo(sfcbSem,PROV_GUARD((*proc)->id))) {
          mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
                (*proc)->id, strerror(errno));
          _SFCB_ABORT();
        }

        char msg[1024];
        snprintf(msg,1023, "*** Provider %s(%d) exiting due to a SIGSEGV signal",
                 processName, currentProc);
        BinResponseHdr* buf = errorCharsResp(CMPI_RC_ERR_FAILED, msg);

        ecr_len = makeSafeResponse(buf, &err_crash_resp);
	free(buf);

        processProviderInvocationRequests(info->providerName);
        _SFCB_RETURN(0);
      }

      else {
        info->startSeq = ++seq;
      }
      _SFCB_TRACE(1, ("--- Fork provider OK %s %d %d", info->providerName,
                      info->pid, i));
      _SFCB_RETURN(info->pid);
    }
  }

  *proc = NULL;
  _SFCB_RETURN(-1);
}

// I think we should break this function into two subfunctions:
// something like isLoaded() and doForkProvider()
int
forkProvider(ProviderInfo * info, char **msg)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "forkProvider");
  ProviderProcess *proc;
  ProviderInfo   *pInfo;
  int             val,
                  rc;

  if (info->pid) {
    proc = info->proc;
    if (semAcquireUnDo(sfcbSem,PROV_GUARD(proc->id))) {
      mlogf(M_ERROR,M_SHOW,"-#- Fatal error acquiring semaphore for %d, reason: %s\n",
            proc->id, strerror(errno));
      _SFCB_ABORT();
    }
    if ((val=semGetValue(sfcbSem,PROV_ALIVE(proc->id))) > 0) {
      if (semReleaseUnDo(sfcbSem,PROV_GUARD(proc->id))) {
        mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
              proc->id, strerror(errno));
        _SFCB_ABORT();
      }
      _SFCB_TRACE(1, ("--- Provider %s still loaded",info->providerName));
      _SFCB_RETURN(CMPI_RC_OK);
    }

    info->pid = 0;
    for (pInfo = proc->firstProv; pInfo; pInfo = pInfo->next) {
      pInfo->pid = 0;
    }
    proc->firstProv = NULL;
    proc->pid = 0;
    proc->group = NULL;

    if (semReleaseUnDo(sfcbSem,PROV_GUARD(proc->id))) {
      mlogf(M_ERROR,M_SHOW,"-#- Fatal error releasing semaphore for %d, reason: %s\n",
            proc->id, strerror(errno));
      _SFCB_ABORT();
    }
    _SFCB_TRACE(1, ("--- Provider has been unloaded prevously, will reload"));
  }

  _SFCB_TRACE(1, ("--- Forking provider for %s", info->providerName));

  if (getProcess(info, &proc) > 0) {

    LoadProviderReq sreq = BINREQ(OPS_LoadProvider, 3);

    BinRequestContext binCtx;
    BinResponseHdr *resp;

    memset(&binCtx, 0, sizeof(BinRequestContext));
    sreq.className = setCharsMsgSegment(info->className);
    sreq.libName = setCharsMsgSegment(info->location);
    sreq.provName = setCharsMsgSegment(info->providerName);
    sreq.parameters = setCharsMsgSegment(info->parms);
    sreq.hdr.flags = info->type;
    sreq.unload = info->unload;
    sreq.hdr.provId = getProvIds(info).ids;

    binCtx.bHdr = &sreq.hdr;
    binCtx.bHdrSize = sizeof(sreq);
    binCtx.provA.socket = info->providerSockets.send;
    binCtx.provA.ids = getProvIds(info);
    binCtx.chunkedMode = binCtx.xmlAs = binCtx.noResp = 0;

    _SFCB_TRACE(1, ("--- Invoke loader"));

    resp = invokeProvider(&binCtx);
    resp->rc--;
    if (msg) {
      if (resp->rc) {
        *msg = strdup((char *) resp->object[0].data);
      } else
        *msg = NULL;
    }

    rc = resp->rc;
    _SFCB_TRACE(1, ("--- rc: %d", resp->rc));

    if (resp)
      free(resp);
    _SFCB_RETURN(rc);
  }
  _SFCB_RETURN(CMPI_RC_ERR_FAILED);
}

typedef struct provHandler {
  BinResponseHdr *(*handler) (BinRequestHdr *, ProviderInfo * info,
                              int requestor);
} ProvHandler;

static long 
makeSafeResponse(BinResponseHdr* hdr, BinResponseHdr** out) 
{
  int i, rvl=0, ol, size;
  long len;
  char str_time[26];
  BinResponseHdr *outHdr = NULL;

  size = sizeof(BinResponseHdr) + ((hdr->count - 1) * sizeof(MsgSegment));

  if (hdr->rvValue) {
    switch(hdr->rv.type) {
    case CMPI_string:
      if (hdr->rv.value.string) {
	if (hdr->rv.value.string->hdl) {
	  hdr->rv.value.string= hdr->rv.value.string->hdl; 
	}
	else hdr->rv.value.string=NULL;
      }

      hdr->rv.type=CMPI_chars;
      /* note: a break statement is NOT missing here... */
    case CMPI_chars:
      hdr->rvEnc=setCharsMsgSegment((char*)hdr->rv.value.string);
      rvl=hdr->rvEnc.length;
      break;
    case CMPI_dateTime:
      dateTime2chars(hdr->rv.value.dateTime, NULL, str_time);
      hdr->rvEnc.type=MSG_SEG_CHARS;
      hdr->rvEnc.length=rvl=26;
      hdr->rvEnc.data=&str_time;
      break;
    case CMPI_ref:
      mlogf(M_ERROR,M_SHOW,"-#- not supporting refs\n");
      abort();
    default: ;
    }
  }

  for (len = size, i = 0; i < hdr->count; i++) {
    /* add padding length to calculation */
    len += (hdr->object[i].type == MSG_SEG_CHARS ? PADDED_LEN(hdr->object[i].length) : hdr->object[i].length);
  }

  outHdr = malloc(len +rvl + 8);
  memcpy(outHdr, hdr, size);

  if (rvl) {
    ol = hdr->rvEnc.length;
    len=size;
    switch (hdr->rvEnc.type) {
    case MSG_SEG_CHARS:
      memcpy(((char *) outHdr) + len, hdr->rvEnc.data, ol);
      outHdr->rvEnc.data = (void *) len;
      len += ol;
      break;
    } 
    size=len;
  }

  for (len = size, i = 0; i < hdr->count; i++) {
    ol = hdr->object[i].length;
    switch (hdr->object[i].type) {
    case MSG_SEG_OBJECTPATH:
      getSerializedObjectPath((CMPIObjectPath *) hdr->object[i].data,
			      ((char *) outHdr) + len);
      outHdr->object[i].data = (void *) len;
      len += ol;
      break;
    case MSG_SEG_INSTANCE:
      getSerializedInstance((CMPIInstance *) hdr->object[i].data,
			    ((char *) outHdr) + len);
      outHdr->object[i].data = (void *) len;
      len += ol;
      break;
    case MSG_SEG_CHARS:
      memcpy(((char *) outHdr) + len, hdr->object[i].data, ol);
      outHdr->object[i].data = (void *) len;
      outHdr->object[i].length = PADDED_LEN(ol);
      len += outHdr->object[i].length;
      break;
    case MSG_SEG_CONSTCLASS:
      getSerializedConstClass((CMPIConstClass *) hdr->object[i].data,
			      ((char *) outHdr) + len);
      outHdr->object[i].data = (void *) len;
      len += ol;
      break;
    case MSG_SEG_ARGS:
      getSerializedArgs((CMPIArgs *) hdr->object[i].data,
			((char *) outHdr) + len);
      outHdr->object[i].data = (void *) len;
      len += ol;
      break;

#ifdef HAVE_QUALREP
    case MSG_SEG_QUALIFIER:
      getSerializedQualifier((CMPIQualifierDecl *) hdr->object[i].data,
			     ((char *) outHdr) + len);
      outHdr->object[i].data = (void *) len;
      len += ol;
      break;
#endif
    default:
      mlogf(M_ERROR,M_SHOW,"--- bad sendResponse request %d\n", hdr->object[i].type);
      abort();
    }
  }

  *out = outHdr;
  return len;
}

static int sendResponse(int requestor, BinResponseHdr * hdr)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "sendResponse");
  int dmy=-1;
  BinResponseHdr* buf = (void*)&dmy;
  long l = makeSafeResponse(hdr, &buf);

  _SFCB_TRACE(1, ("--- Sending result %p to %d-%lu size %lu",
		  buf, requestor,getInode(requestor), l));

  spSendResult(&requestor, &dmy, buf, l);
  free(buf);
  _SFCB_RETURN(0);
}

int
sendResponseChunk(CMPIArray *r, int requestor, CMPIType type)
{
  int             i,
                  count;
  int             rslt;
  BinResponseHdr *resp;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "sendResponseChunk");

  count = CMGetArrayCount(r, NULL);
  resp = (BinResponseHdr *)
      calloc(1,
             sizeof(BinResponseHdr) + ((count - 1) * sizeof(MsgSegment)));

  resp->moreChunks = 1;
  resp->rc = 1;
  resp->count = count;
  for (i = 0; i < count; i++)
    if (type == CMPI_instance)
      resp->object[i] =
          setInstanceMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                inst);
    else
      resp->object[i] =
          setObjectPathMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                  ref);

  rslt = sendResponse(requestor, resp);
  if (resp)
    free(resp);
  _SFCB_RETURN(rslt);
}

static BinResponseHdr *
errorResp(CMPIStatus *rci)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "errorResp");
  BinResponseHdr *resp =
      (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
  resp->moreChunks = 0;
  resp->rc = rci->rc + 1;
  resp->count = 1;
  resp->object[0] =
      setCharsMsgSegment(rci->msg ? (char *) rci->msg->hdl : "");
  _SFCB_RETURN(resp)
}

static BinResponseHdr *
errorCharsResp(int rc, char *msg)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "errorCharsResp");
  BinResponseHdr *resp = (BinResponseHdr *) calloc(1,
                                                   sizeof(BinResponseHdr) +
                                                   strlen(msg) + 4);
  strcpy((char *) (resp + 1), msg ? msg : "");
  resp->rc = rc + 1;
  resp->count = 1;
  resp->object[0] = setCharsMsgSegment((char *) (resp + 1));
  _SFCB_RETURN(resp);
}

char          **
makePropertyList(int n, MsgSegment * ms)
{
  char          **l;
  int             i;

  // if (n==1 && ms[0].data==NULL) return NULL;
  l = (char **) malloc(sizeof(char *) * (n + 1));

  for (i = 0; i < n; i++)
    l[i] = (char *) ms[i].data;
  l[n] = NULL;
  return l;
}

static BinResponseHdr *
deleteClass(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "deleteClass");
  TIMING_PREP;
  DeleteClassReq *req = (DeleteClassReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->classMI->ft->deleteClass(info->classMI, ctx, result, path);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
getClass(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  TIMING_PREP;
  GetClassReq *req = (GetClassReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;
  int             i;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "getClass");

  char           *cn = CMGetClassName(path, NULL)->hdl;
  char           *ns = CMGetNameSpace(path, NULL)->hdl;
  _SFCB_TRACE(1, ("--- Namespace %s ClassName %s", ns, cn));

  if (req->hdr.flags & FL_localOnly)
    flgs |= CMPI_FLAG_LocalOnly;
  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  if (req->hdr.flags & FL_includeClassOrigin)
    flgs |= CMPI_FLAG_IncludeClassOrigin;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count>GC_REQ_REG_SEGMENTS) 
       props=makePropertyList(req->hdr.count - GC_REQ_REG_SEGMENTS,req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
    info->classMI->ft->getClass(info->classMI, ctx, result, path, (const char**) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  r = native_result2array(result);
  if (rci.rc == CMPI_RC_OK) {
    count = 1;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count - 1) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;
    for (i = 0; i < count; i++)
      resp->object[i] =
          setConstClassMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                  dataPtr.ptr);
  } else
    resp = errorResp(&rci);
  if (props)
    free(props);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
createClass(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "createClass");
  TIMING_PREP;
  CreateClassReq *req = (CreateClassReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIConstClass *cls = relocateSerializedConstClass(req->cls.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->classMI->ft->createClass(info->classMI, ctx, result, path,
                                     cls);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
enumClassNames(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  TIMING_PREP;
  EnumClassNamesReq *req = (EnumClassNamesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = req->hdr.flags;
  int             i;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "enumClassNames");

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));

  // rci = info->instanceMI->ft->enumInstanceNames(info->instanceMI, ctx,
  // result,
  TIMING_START(hdr, info)
      rci = info->classMI->ft->enumClassNames(info->classMI, ctx, result,
                                              path);
  TIMING_STOP(hdr, info)
      r = native_result2array(result);

  _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    if (r)
      count = CMGetArrayCount(r, NULL);
    else
      count = 0;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count ? count -
                                       1 : 0) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;
    for (i = 0; i < count; i++)
      resp->object[i] =
          setObjectPathMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                  ref);
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
enumClasses(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "enumClasses");
  TIMING_PREP;
  EnumClassesReq *req = (EnumClassesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = req->hdr.flags;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));

  TIMING_START(hdr, info)
      rci =
      info->classMI->ft->enumClasses(info->classMI, ctx, result, path);
  TIMING_STOP(hdr, info)
      r = native_result2array(result);

  _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

#ifdef HAVE_QUALREP

static BinResponseHdr *
enumQualifiers(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "enumQualifiers");
  TIMING_PREP;
  EnumQualifiersReq *req = (EnumQualifiersReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPICount       count;
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = req->hdr.flags;
  int             i;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));

  TIMING_START(hdr, info)
      rci =
      info->qualifierDeclMI->ft->enumQualifiers(info->qualifierDeclMI, ctx,
                                                result, path);
  TIMING_STOP(hdr, info)
      r = native_result2array(result);

  _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    if (r)
      count = CMGetArrayCount(r, NULL);
    else
      count = 0;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count ? count -
                                       1 : 0) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;
    for (i = 0; i < count; i++) {
      resp->object[i] =
          setQualifierMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                 dataPtr.ptr);
    }
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
setQualifier(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "setQualifier");
  TIMING_PREP;
  SetQualifierReq *req = (SetQualifierReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIQualifierDecl *q = relocateSerializedQualifier(req->qualifier.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->qualifierDeclMI->ft->setQualifier(info->qualifierDeclMI, ctx,
                                              result, path, q);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
getQualifier(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getQualifier");
  TIMING_PREP;
  GetQualifierReq *req = (GetQualifierReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  int             i;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->qualifierDeclMI->ft->getQualifier(info->qualifierDeclMI, ctx,
                                              result, path);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  r = native_result2array(result);

  if (rci.rc == CMPI_RC_OK) {
    count = 1;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count - 1) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;
    for (i = 0; i < count; i++)
      resp->object[i] =
          setQualifierMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                 dataPtr.ptr);
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
deleteQualifier(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "deleteQualifier");
  TIMING_PREP;
  DeleteQualifierReq *req = (DeleteQualifierReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->qualifierDeclMI->ft->deleteQualifier(info->qualifierDeclMI,
                                                 ctx, result, path);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}
#endif

static BinResponseHdr *
getProperty(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getProperty");
  TIMING_PREP;
  GetPropertyReq *req = (GetPropertyReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  CMPIData        data;
  CMPIInstance   *inst =
      internal_new_CMPIInstance(MEM_TRACKED, NULL, NULL, 1);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  if (info->propertyMI == NULL)
    info->propertyMI =
        loadPropertyMI(info->providerName, info->library, Broker, ctx,
                       &rci);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->propertyMI->ft->getProperty(info->propertyMI, ctx, result,
                                        path,
                                        (const char *) req->name.data);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  r = native_result2array(result);

  if (rci.rc == CMPI_RC_OK) {
    count = 1;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count - 1) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;

    data = CMGetArrayElementAt(r, 0, NULL);
    inst->ft->setProperty(inst, (const char *) req->name.data, &data.value,
                          data.type);
    resp->object[0] = setInstanceMsgSegment(inst);
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
setProperty(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "setProperty");
  TIMING_PREP;
  SetPropertyReq *req = (SetPropertyReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIInstance   *inst = relocateSerializedInstance(req->inst.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIString     *pName;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  CMPIData        data;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  data = inst->ft->getPropertyAt(inst, 0, &pName, NULL);

  if (info->propertyMI == NULL)
    info->propertyMI =
        loadPropertyMI(info->providerName, info->library, Broker, ctx,
                       &rci);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->propertyMI->ft->setProperty(info->propertyMI, ctx, result,
                                        path, (const char *) pName->hdl,
                                        data);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  CMRelease(pName);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
invokeMethod(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "invokeMethod");
  TIMING_PREP;
  InvokeMethodReq *req = (InvokeMethodReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  char           *method = (char *) req->method.data;
  CMPIArgs       *in,
                 *tIn = relocateSerializedArgs(req->in.data);
  CMPIArgs       *out = TrackedCMPIArgs(NULL);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (info->parms)
    ctx->ft->addEntry(ctx, "sfcbProviderParameters",
                      (CMPIValue *) info->parms, CMPI_chars);

  if (req->hdr.count > IM_REQ_REG_SEGMENTS) {
    int             i,
                    s,
                    n;
    CMPIString     *name;
    in = CMNewArgs(Broker, NULL);
    BinRequestHdr  *r = (BinRequestHdr *) req;
    for (n = IM_REQ_REG_SEGMENTS, i = 0, s = CMGetArgCount(tIn, NULL); i < s; i++) {
      CMPIData        d = CMGetArgAt(tIn, i, &name, NULL);
      if (d.type == CMPI_instance) {
        d.value.inst = relocateSerializedInstance(r->object[n++].data);
      }
      CMAddArg(in, (char *) name->hdl, &d.value, d.type);
    }
  } else
    in = tIn;

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci = info->methodMI->ft->invokeMethod
      (info->methodMI, ctx, result, path, method, in, out);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  r = native_result2array(result);
  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    memset(&resp->rv, 0, sizeof(resp->rv));
    if (r) {
      count = CMGetArrayCount(r, NULL);
      resp->rvValue = 1;
      if (count) {
        resp->rv = CMGetArrayElementAt(r, 0, NULL);
      }
    }

    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = 1;
    resp->object[0] = setArgsMsgSegment(out);
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
getInstance(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "getInstance");
  TIMING_PREP;
  GetInstanceReq *req = (GetInstanceReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIArray      *r;
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;
  int             i;

  if (req->hdr.flags & FL_localOnly)
    flgs |= CMPI_FLAG_LocalOnly;
  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  if (req->hdr.flags & FL_includeClassOrigin)
    flgs |= CMPI_FLAG_IncludeClassOrigin;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count > GI_REQ_REG_SEGMENTS)
    props = makePropertyList(req->hdr.count - GI_REQ_REG_SEGMENTS, req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->getInstance(info->instanceMI, ctx, result,
                                        path, (const char **) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  r = native_result2array(result);

  if (rci.rc == CMPI_RC_OK) {
    if (r && CMGetArrayCount(r, NULL) > 0) {
      count = 1;

      resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                       ((count - 1) * sizeof(MsgSegment)));
      resp->moreChunks = 0;
      resp->rc = 1;
      resp->count = count;
      for (i = 0; i < count; i++)
        resp->object[i] =
            setInstanceMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                  inst);
    } else {
      rci.rc = CMPI_RC_ERR_NOT_FOUND;
      rci.msg = NULL;
      resp = errorResp(&rci);
    }
  } else
    resp = errorResp(&rci);
  if (props)
    free(props);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
deleteInstance(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "deleteInstance");
  TIMING_PREP;
  DeleteInstanceReq *req = (DeleteInstanceReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->deleteInstance(info->instanceMI, ctx, result,
                                           path);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->count = 0;
    resp->moreChunks = 0;
    resp->rc = 1;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
createInstance(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "createInstance");
  TIMING_PREP;
  CreateInstanceReq *req = (CreateInstanceReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIInstance   *inst = relocateSerializedInstance(req->instance.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIArray      *r;
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  int             i;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->createInstance(info->instanceMI, ctx, result,
                                           path, inst);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));
  r = native_result2array(result);

  if (rci.rc == CMPI_RC_OK) {
    count = 1;
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr) +
                                     ((count - 1) * sizeof(MsgSegment)));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = count;
    for (i = 0; i < count; i++)
      resp->object[i] =
          setObjectPathMsgSegment(CMGetArrayElementAt(r, i, NULL).value.
                                  ref);
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
modifyInstance(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "modifyInstance");
  TIMING_PREP;
  ModifyInstanceReq *req = (ModifyInstanceReq *) hdr;
  CMPIObjectPath *path = relocateSerializedObjectPath(req->path.data);
  CMPIInstance   *inst = relocateSerializedInstance(req->instance.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPICount       count;
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;

  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count > MI_REQ_REG_SEGMENTS)
    props = makePropertyList(req->hdr.count - MI_REQ_REG_SEGMENTS, req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->modifyInstance(info->instanceMI, ctx, result,
                                           path, inst,
                                           (const char **) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    count = 1;
    resp =
        (BinResponseHdr *) calloc(1,
                                  sizeof(BinResponseHdr) -
                                  sizeof(MsgSegment));
    resp->moreChunks = 0;
    resp->rc = 1;
    resp->count = 0;
  } else
    resp = errorResp(&rci);
  if (props)
    free(props);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
enumInstances(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "enumInstances");
  TIMING_PREP;
  EnumInstancesReq *req = (EnumInstancesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;

#ifndef HAVE_OPTIMIZED_ENUMERATION
  REPLACE_CN(info,path);
#endif

  if (req->hdr.flags & FL_localOnly)
    flgs |= CMPI_FLAG_LocalOnly;
  if (req->hdr.flags & FL_deepInheritance)
    flgs |= CMPI_FLAG_DeepInheritance;
  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  if (req->hdr.flags & FL_includeClassOrigin)
    flgs |= CMPI_FLAG_IncludeClassOrigin;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count > EI_REQ_REG_SEGMENTS)
    props = makePropertyList(req->hdr.count - EI_REQ_REG_SEGMENTS, req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->enumerateInstances(info->instanceMI, ctx,
                                               result, path,
                                               (const char **) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (props)
    free(props);
  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
enumInstanceNames(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "enumInstanceNames");
  TIMING_PREP;
  EnumInstanceNamesReq *req = (EnumInstanceNamesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

#ifndef HAVE_OPTIMIZED_ENUMERATION
  REPLACE_CN(info,path);
#endif

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->enumerateInstanceNames(info->instanceMI, ctx,
                                                   result, path);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

CMPIValue
queryGetValue(QLPropertySource * src, char *name, QLOpd * type)
{
  CMPIInstance   *ci = (CMPIInstance *) src->data;
  CMPIStatus      rc;
  CMPIData        d = ci->ft->getProperty(ci, name, &rc);
  CMPIValue       v = { (long long) 0 };

  if (rc.rc == CMPI_RC_OK) {
    if (d.state == CMPI_nullValue) {
       *type=QL_Null;
    } else {

    if ((d.type & CMPI_SINT) == CMPI_SINT) {
      if (d.type == CMPI_sint32)
        v.sint64 = d.value.sint32;
      else if (d.type == CMPI_sint16)
        v.sint64 = d.value.sint16;
      else if (d.type == CMPI_sint8)
        v.sint64 = d.value.sint8;
      else
        v.sint64 = d.value.sint64;
      *type = QL_Integer;
    } else if (d.type & CMPI_UINT) {
      if (d.type == CMPI_uint32)
        v.uint64 = d.value.uint32;
      else if (d.type == CMPI_uint16)
        v.uint64 = d.value.uint16;
      else if (d.type == CMPI_uint8)
        v.uint64 = d.value.uint8;
      else
        v.uint64 = d.value.uint64;
      *type = QL_UInteger;
    }

    else
      switch (d.type) {
      case CMPI_string:
        *type = QL_Chars;
        v.chars = (char *) d.value.string->hdl;
        break;
      case CMPI_boolean:
        *type = QL_Boolean;
        v.boolean = d.value.boolean;
        break;
      case CMPI_real64:
        *type = QL_Double;
        v.real64 = d.value.real64;
        break;
      case CMPI_real32:
        *type = QL_Double;
        v.real64 = d.value.real32;
        break;
      case CMPI_char16:
        *type = QL_Char;
        v.char16 = d.value.char16;
        break;
      case CMPI_instance:
        *type = QL_Inst;
        v.inst = d.value.inst;
        break;
      default:
        *type = QL_Invalid;
      }
    }
  } else
    *type = QL_NotFound;
  return v;
}

static BinResponseHdr *
execQuery(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "execQuery");
  TIMING_PREP;
  ExecQueryReq *req = (ExecQueryReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  int             irc;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->instanceMI->ft->execQuery(info->instanceMI, ctx, result, path,
                                      PROVCHARS(req->query.data),
                                      PROVCHARS(req->queryLang.data));
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_ERR_NOT_SUPPORTED) {
    QLStatement    *qs;
    CMPIArray      *kar;
    CMPICount       i,
                    c;
    int             ok = 1;

    qs = parseQuery(MEM_TRACKED, (char *) req->query.data,
                    (char *) req->queryLang.data, NULL, NULL, &irc);
    if (irc) {
      rci.rc = CMPI_RC_ERR_INVALID_QUERY;
      resp = errorResp(&rci);
      _SFCB_RETURN(resp);
    }

#ifndef HAVE_OPTIMIZED_ENUMERATION
    REPLACE_CN(info,path);
#endif

    qs->propSrc.getValue = queryGetValue;
    qs->propSrc.sns = qs->sns;
    // qs->cop=CMNewObjectPath(Broker,"*",qs->fClasses[0],NULL);
    qs->cop = path;

    if (qs->allProps) {
      CMPIConstClass *cc =
          getConstClass(CMGetNameSpace(qs->cop, NULL)->hdl,
                        CMGetClassName(qs->cop, NULL)->hdl);
      kar = cc->ft->getKeyList(cc);
    } else {
      kar =
          getKeyListAndVerifyPropertyList(qs->cop, qs->spNames, &ok, NULL);
    }

    if (ok) {
      c = kar->ft->getSize(kar, NULL);
      qs->keys = (char **) malloc((c + 1) * sizeof(char *));

      for (i = 0; i < c; i++)
        qs->keys[i] =
            (char *) kar->ft->getElementAt(kar, i, NULL).value.string->hdl;
      qs->keys[c] = NULL;

      setResultQueryFilter(result, qs);
      _SFCB_TRACE(1,
                  ("--- Calling enumerateInstances provider %s",
                   info->providerName));
      TIMING_START(hdr, info)
          rci =
          info->instanceMI->ft->enumerateInstances(info->instanceMI, ctx,
                                                   result, path, NULL);
      TIMING_STOP(hdr, info)
          _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));
      free(qs->keys);
    } else
      rci.rc = CMPI_RC_OK;

    kar->ft->release(kar);
    qs->ft->release(qs);
  }

  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
associators(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "associators");
  TIMING_PREP;
  AssociatorsReq *req = (AssociatorsReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;

  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  if (req->hdr.flags & FL_includeClassOrigin)
    flgs |= CMPI_FLAG_IncludeClassOrigin;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count > AI_REQ_REG_SEGMENTS)
    props = makePropertyList(req->hdr.count - AI_REQ_REG_SEGMENTS, req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->associationMI->ft->associators(info->associationMI, ctx,
                                           result, path,
                                           PROVCHARS(req->assocClass.data),
                                           PROVCHARS(req->resultClass.
                                                     data),
                                           PROVCHARS(req->role.data),
                                           PROVCHARS(req->resultRole.data),
                                           (const char **) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (props)
    free(props);
  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
references(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "references");
  TIMING_PREP;
  ReferencesReq *req = (ReferencesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;
  char          **props = NULL;

  if (req->hdr.flags & FL_includeQualifiers)
    flgs |= CMPI_FLAG_IncludeQualifiers;
  if (req->hdr.flags & FL_includeClassOrigin)
    flgs |= CMPI_FLAG_IncludeClassOrigin;
  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  if (req->hdr.count > RI_REQ_REG_SEGMENTS)
    props = makePropertyList(req->hdr.count - RI_REQ_REG_SEGMENTS, req->properties);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->associationMI->ft->references(info->associationMI, ctx, result,
                                          path,
                                          PROVCHARS(req->resultClass.data),
                                          PROVCHARS(req->role.data),
                                          (const char **) props);
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (props)
    free(props);
  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
associatorNames(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "associatorNames");
  TIMING_PREP;
  AssociatorNamesReq *req = (AssociatorNamesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->associationMI->ft->associatorNames(info->associationMI, ctx,
                                               result, path,
                                               PROVCHARS(req->assocClass.
                                                         data),
                                               PROVCHARS(req->resultClass.
                                                         data),
                                               PROVCHARS(req->role.data),
                                               PROVCHARS(req->resultRole.
                                                         data));
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
referenceNames(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "referenceNames");
  TIMING_PREP;
  ReferenceNamesReq *req = (ReferenceNamesReq *) hdr;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  CMPIResult     *result =
      native_new_CMPIResult(requestor < 0 ? 0 : requestor, 0, NULL);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  BinResponseHdr *resp;
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIRole, (CMPIValue *) req->userRole.data, 
                    CMPI_chars);

  _SFCB_TRACE(1, ("--- Calling provider %s", info->providerName));
  TIMING_START(hdr, info)
      rci =
      info->associationMI->ft->referenceNames(info->associationMI, ctx,
                                              result, path,
                                              PROVCHARS(req->resultClass.
                                                        data),
                                              PROVCHARS(req->role.data));
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    xferLastResultBuffer(result, abs(requestor), 1);
    return NULL;
  } else
    resp = errorResp(&rci);

  _SFCB_RETURN(resp);
}

#ifdef SFCB_INCL_INDICATION_SUPPORT

static BinResponseHdr *
activateFilter(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV | TRACE_INDPROVIDER, "activateFilter");
  TIMING_PREP;
  IndicationReq *req = (IndicationReq *) hdr;
  BinResponseHdr *resp = NULL;
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  NativeSelectExp *se = NULL,
      *prev = NULL;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIFlags       flgs = 0;
  char           *type = (char *) req->type.data;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  _SFCB_TRACE(1,
              ("--- pid: %d activFilters %p %s", currentProc, activFilters,
               processName));
  for (se = activFilters; se; se = se->next) {
    if (se->filterId == req->filterId)
      break;
  }

  _SFCB_TRACE(1, ("--- selExp found: %p", se));
  if (se == NULL) {
    char           *query = (char *) req->query.data;
    char           *lang = (char *) req->language.data;
    char           *sns = (char *) req->sns.data;

    se = (NativeSelectExp *) NewCMPISelectExp(query, lang, sns, NULL,
                                              &rci);
    if (rci.rc != CMPI_RC_OK) {
      mlogf(M_DEBUG, M_SHOW, "Failed to parse query (%s). Error code(%d).\n", query, rci.rc);
      resp = errorResp(&rci);
      _SFCB_RETURN(resp);
    }
    else if (se == NULL) {
      /*
       * If se is NULL, rci.rc should have an error code set, but check for
       * the NULL case just in case something went wrong.
       */
      mlogf(M_ERROR, M_SHOW, "Unknown error parsing query (%s).\n", query);
      rci.rc = CMPI_RC_ERR_FAILED;
      resp = errorResp(&rci);
      _SFCB_RETURN(resp);
    }

    se->filterId = req->filterId;
    prev = se->next = activFilters;
    activFilters = se;
    _SFCB_TRACE(1, ("--- new selExp:  %p", se));
  }

  if (info->indicationMI == NULL) {
    CMPIStatus      st;
    setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED,
              "Provider does not support indications");
    resp = errorResp(&st);
    _SFCB_RETURN(resp);
  }

  _SFCB_TRACE(1, ("--- Calling authorizeFilter %s", info->providerName));
  TIMING_START(hdr, info)
      if (info->indicationMI->ft->ftVersion < 100) {
    authorizeFilterPreV1 fptr =
        (authorizeFilterPreV1) info->indicationMI->ft->authorizeFilter;
    rci = fptr(info->indicationMI, ctx, result,
               (CMPISelectExp *) se, type, path,
               PROVCHARS(req->principal.data));
  } else {
    rci = info->indicationMI->ft->authorizeFilter(info->indicationMI, ctx,
                                                  (CMPISelectExp *) se,
                                                  type, path,
                                                  PROVCHARS(req->principal.
                                                            data));
  }
  TIMING_STOP(hdr, info)
      _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

  if (rci.rc == CMPI_RC_OK) {
    _SFCB_TRACE(1, ("--- Calling mustPoll %s", info->providerName));
    TIMING_START(hdr, info)
        if (info->indicationMI->ft->ftVersion < 100) {
      mustPollPreV1   fptr =
          (mustPollPreV1) info->indicationMI->ft->mustPoll;
      rci = fptr(info->indicationMI, ctx, result,
                 (CMPISelectExp *) se, type, path);
    } else {
      rci = info->indicationMI->ft->mustPoll(info->indicationMI, ctx,
                                             (CMPISelectExp *) se, type,
                                             path);
    }
    TIMING_STOP(hdr, info)
        _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

    _SFCB_TRACE(1, ("--- Calling activateFilter %s", info->providerName));
    TIMING_START(hdr, info)
        if (info->indicationMI->ft->ftVersion < 100) {
      activateFilterPreV1 fptr =
          (activateFilterPreV1) info->indicationMI->ft->activateFilter;
      rci = fptr(info->indicationMI, ctx, result,
                 (CMPISelectExp *) se, type, path, 1);
    } else {
      rci = info->indicationMI->ft->activateFilter(info->indicationMI, ctx,
                                                   (CMPISelectExp *) se,
                                                   type, path, 1);
    }
    TIMING_STOP(hdr, info)
        _SFCB_TRACE(1, ("--- Back from provider rc: %d", rci.rc));

    if (rci.rc == CMPI_RC_OK) {
      increaseInUseSem(info->provIds.procId);
      resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
      resp->rc = 1;
    }
  }

  if (rci.rc != CMPI_RC_OK) {
    activFilters = prev;
    resp = errorResp(&rci);
    _SFCB_TRACE(1, ("--- Not OK rc: %d", rci.rc));
  } else {
    _SFCB_TRACE(1, ("--- OK activFilters: %p", activFilters));
  }
  _SFCB_TRACE(1,
              ("---  pid: %d activFilters %p", currentProc, activFilters));

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
deactivateFilter(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV | TRACE_INDPROVIDER, "deactivateFilter");
  TIMING_PREP;
  IndicationReq *req = (IndicationReq *) hdr;
  BinResponseHdr *resp = NULL;
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  NativeSelectExp *se = NULL, *prev = NULL;
  CMPIObjectPath *path =
      relocateSerializedObjectPath(req->objectPath.data);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIResult     *result = native_new_CMPIResult(0, 1, NULL);
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
  resp->rc = 1;

  _SFCB_TRACE(1,
              ("---  pid: %d activFilters %p", currentProc, activFilters));
  if (info->indicationMI == NULL || activFilters == NULL)
    _SFCB_RETURN(resp);

  for (se = activFilters; se; prev = se, se = se->next) {
    //_SFCB_TRACE(1, ("---- se->filterid:%p, req=>filterid:%p, se:%p, activFilters:%p, prev:%p", se->filterId, req->filterId, se, activFilters, prev));
    if (se->filterId == req->filterId) {
      if (activFilters == NULL) {
        _SFCB_TRACE(1,
                    ("--- Calling disableIndications %s",
                     info->providerName));
        info->indicationEnabled = 0;
        TIMING_START(hdr, info)
            info->indicationMI->ft->disableIndications(info->indicationMI,
                                                       ctx);
        TIMING_STOP(hdr, info)
      }

      _SFCB_TRACE(1,
                  ("--- Calling deactivateFilter %s", info->providerName));
      TIMING_START(hdr, info)
          if (info->indicationMI->ft->ftVersion < 100) {
        deActivateFilterPreV1 fptr =
            (deActivateFilterPreV1) info->indicationMI->ft->
            deActivateFilter;
        rci =
            fptr(info->indicationMI, ctx, result, (CMPISelectExp *) se, "",
                 path, 1);
      } else {
        rci =
            info->indicationMI->ft->deActivateFilter(info->indicationMI,
                                                     ctx,
                                                     (CMPISelectExp *) se,
                                                     "", path, 1);
      }
      TIMING_STOP(hdr, info)
      if (rci.rc == CMPI_RC_OK) {
        decreaseInUseSem(info->provIds.procId);
        resp->rc = 1;
        /*79580-3498496*/
        if (prev == NULL) {
           activFilters = activFilters->next;
        }
        else {
           prev->next = se->next;
        }
        _SFCB_TRACE(1, ("---- pid:%d, freeing: %p", currentProc, se));
        CMRelease((CMPISelectExp *)se);
        _SFCB_RETURN(resp);
      }

      if (resp)
        free(resp);
      resp = errorResp(&rci);
      _SFCB_RETURN(resp);
    }
  }

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
enableIndications(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV | TRACE_INDPROVIDER, "enableIndications");
  TIMING_PREP;
  IndicationReq *req = (IndicationReq *) hdr;
  BinResponseHdr *resp = NULL;
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  // CMPIObjectPath *path =
  // relocateSerializedObjectPath(req->objectPath.data);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  if (info->indicationMI == NULL) {
    CMPIStatus      st;
    setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED,
              "Provider does not support indications");
    resp = errorResp(&st);
    _SFCB_RETURN(resp);
  }

  if (info->indicationEnabled == 0 && rci.rc == CMPI_RC_OK) {
    info->indicationEnabled = 1;
    TIMING_START(hdr, info)
        info->indicationMI->ft->enableIndications(info->indicationMI, ctx);
    TIMING_STOP(hdr, info)
  }

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->rc = 1;
  }
  if (rci.rc != CMPI_RC_OK) {
    resp = errorResp(&rci);
    _SFCB_TRACE(1, ("--- Not OK rc: %d", rci.rc));
  }

  _SFCB_RETURN(resp);
}

static BinResponseHdr *
disableIndications(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV | TRACE_INDPROVIDER, "disableIndications");
  TIMING_PREP;
  IndicationReq *req = (IndicationReq *) hdr;
  BinResponseHdr *resp = NULL;
  CMPIStatus      rci = { CMPI_RC_OK, NULL };
  // CMPIObjectPath *path =
  // relocateSerializedObjectPath(req->objectPath.data);
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);
  CMPIFlags       flgs = 0;

  ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                    CMPI_uint32);
  ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) req->principal.data,
                    CMPI_chars);
  ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & hdr->sessionId,
                    CMPI_uint32);

  if (info->indicationMI == NULL) {
    CMPIStatus      st;
    setStatus(&st, CMPI_RC_ERR_NOT_SUPPORTED,
              "Provider does not support indications");
    resp = errorResp(&st);
    _SFCB_RETURN(resp);
  }

  if (info->indicationEnabled == 1 && rci.rc == CMPI_RC_OK) {
    info->indicationEnabled = 0;
    TIMING_START(hdr, info)
        info->indicationMI->ft->disableIndications(info->indicationMI,
                                                   ctx);
    TIMING_STOP(hdr, info)
  }

  if (rci.rc == CMPI_RC_OK) {
    resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
    resp->rc = 1;
  }
  if (rci.rc != CMPI_RC_OK) {
    resp = errorResp(&rci);
    _SFCB_TRACE(1, ("--- Not OK rc: %d", rci.rc));
  }

  _SFCB_RETURN(resp);
}

#endif

static BinResponseHdr *
opNotSupported(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  BinResponseHdr *resp;
  CMPIStatus      rci = { CMPI_RC_ERR_NOT_SUPPORTED, NULL };
  _SFCB_ENTER(TRACE_PROVIDERDRV, "opNotSupported");

  mlogf(M_ERROR, M_SHOW, "--- opNotSupported\n");
  resp = errorResp(&rci);
  _SFCB_RETURN(resp);
}

void
appendStr(char **base, const char *str, const char *arg)
{
  if (*base == NULL) {
    *base = sfcb_snprintf("%s %s", str, arg);
  } else {
    char           *tmp = sfcb_snprintf("%s; %s %s", *base, str, arg);
    free(*base);
    *base = tmp;
  }
}

int
initProvider(ProviderInfo * info, unsigned int sessionId, char **errorStr)
{
  CMPIInstanceMI *mi = NULL;
  int             rc = 0;
  CMPIStatus      st;
  char           *errstr = NULL;
  unsigned int    flgs = 0;
  CMPIContext    *ctx = native_new_CMPIContext(MEM_TRACKED, info);

  _SFCB_ENTER(TRACE_PROVIDERDRV, "initProvider");

  pthread_mutex_lock(&info->initMtx);
  if (info->initialized == 0) {

    ctx->ft->addEntry(ctx, CMPIInvocationFlags, (CMPIValue *) & flgs,
                      CMPI_uint32);
    ctx->ft->addEntry(ctx, CMPIPrincipal, (CMPIValue *) "$$", CMPI_chars);
    ctx->ft->addEntry(ctx, "CMPISessionId", (CMPIValue *) & sessionId,
                      CMPI_uint32);
    if (info->parms) {
      ctx->ft->addEntry(ctx, "sfcbProviderParameters",
                        (CMPIValue *) info->parms, CMPI_chars);
    }

    if (info->type & INSTANCE_PROVIDER) {
      st = getInstanceMI(info, &mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Instance MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
    if (info->type & ASSOCIATION_PROVIDER) {
      st = getAssociationMI(info, (CMPIAssociationMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Association MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
    if (info->type & METHOD_PROVIDER) {
      st = getMethodMI(info, (CMPIMethodMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Method MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
    if (info->type & INDICATION_PROVIDER) {
      st = getIndicationMI(info, (CMPIIndicationMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Indication MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
    if (info->type & CLASS_PROVIDER) {
      st = getClassMI(info, (CMPIClassMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Class MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
    if (info->type & PROPERTY_PROVIDER) {
      st = getPropertyMI(info, (CMPIPropertyMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Property MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
#ifdef HAVE_QUALREP
    if (info->type & QUALIFIER_PROVIDER) {
      st = getQualifierDeclMI(info, (CMPIQualifierDeclMI **) & mi, ctx);
      rc |= st.rc;
      if (st.rc != CMPI_RC_OK && st.msg != NULL) {
        appendStr(&errstr, "Error from Qualifier MI Factory:",
                  CMGetCharsPtr(st.msg, NULL));
      }
    }
#endif

    if (rc) {
      rc = -2;
      if (errstr != NULL) {
        *errorStr =
            sfcb_snprintf
            ("Error initializing provider %s from %s for class %s.  %s",
             info->providerName, info->location, info->className, errstr);
      } else {
        *errorStr =
            sfcb_snprintf
            ("Error initializing provider %s from %s for class %s.",
             info->providerName, info->location, info->className);
      }
    } else {
      info->initialized = 1;
      *errorStr = NULL;
    }
  }
  pthread_mutex_unlock(&info->initMtx);
  if (errstr != NULL)
    free(errstr);

  _SFCB_RETURN(rc);
}

static int
doLoadProvider(ProviderInfo * info, char *dlName, int dlName_length)
{
  char           *dirs,
                 *dir,
                 *dirlast,
                 *dircpy;
  char           *fullname;
  int             fullname_max_length = 0;
  struct stat     stbuf;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "doLoadProvider");

  if (getControlChars("providerDirs", &dirs) != 0) {
    mlogf(M_ERROR, M_SHOW, "*** No provider directories configured.\n");
    abort();
  }

  libraryName(NULL, (char *) info->location, dlName, dlName_length);

  dircpy = strdup(dirs);
  fullname_max_length = strlen(dircpy) + strlen(dlName) + 2;    /* sufficient 
                                                                 */
  fullname = malloc(fullname_max_length);
  dir = strtok_r(dircpy, " \t", &dirlast);
  info->library = NULL;
  while (dir) {
    libraryName(dir, (char *) info->location, fullname,
                fullname_max_length);
    if (stat(fullname, &stbuf) == 0) {
      info->library = dlopen(fullname, PROVIDERLOAD_DLFLAG);
      if (info->library == NULL) {
        mlogf(M_ERROR,M_SHOW,"*** dlopen: %s error: %s\n", fullname, dlerror());
      } else {
        _SFCB_TRACE(1, ("--- Loaded provider library %s for %s-%d",
                        fullname, info->providerName, currentProc));
      }
      break;
    }
    dir = strtok_r(NULL, " \t", &dirlast);
  }
  free(dircpy);
  free(fullname);

  if (info->library == NULL) {
    _SFCB_RETURN(-1);
  }

  info->initialized = 0;
  pthread_mutex_init(&info->initMtx, NULL);

  _SFCB_RETURN(0);
}

static BinResponseHdr *
loadProvider(BinRequestHdr * hdr, ProviderInfo * info, int requestor)
{
  _SFCB_ENTER(TRACE_PROVIDERDRV, "loadProvider");

  LoadProviderReq *req = (LoadProviderReq *) hdr;
  BinResponseHdr *resp;
  char            dlName[512];

  _SFCB_TRACE(1,
              ("--- Loading provider %s %s %s",
               (char *) req->className.data, (char *) req->provName.data,
               (char *) req->libName.data));

  info = (ProviderInfo *) calloc(1, sizeof(*info));

  info->className = strdup((char *) req->className.data);
  info->location = strdup((char *) req->libName.data);
  info->providerName = strdup((char *) req->provName.data);
  if (req->parameters.data)
    info->parms = strdup((char *) req->parameters.data);
  info->type = req->hdr.flags;
  info->unload = req->unload;
  info->providerSockets = providerSockets;
  info->provIds.ids = hdr->provId;

  switch (doLoadProvider(info, dlName, 512)) {
  case -1:{
      char            msg[740];
      snprintf(msg, 739, "*** Failed to load %s for %s", dlName,
               info->providerName);
      mlogf(M_ERROR, M_SHOW, "%s\n", msg);
      resp = errorCharsResp(CMPI_RC_ERR_FAILED, msg);
      free(info);
      _SFCB_RETURN(resp);
    }
  default:
    if (activProvs)
      info->next = activProvs;
    activProvs = info;
    break;
  }

  resp = (BinResponseHdr *) calloc(1, sizeof(BinResponseHdr));
  resp->rc = 1;
  resp->count = 0;

  _SFCB_RETURN(resp);
}

static ProvHandler pHandlers[] = {
  {opNotSupported},             // dummy
  {getClass},                   // OPS_GetClass 1
  {getInstance},                // OPS_GetInstance 2
  {deleteClass},                // OPS_DeleteClass 3
  {deleteInstance},             // OPS_DeleteInstance 4
  {createClass},                // OPS_CreateClass 5
  {createInstance},             // OPS_CreateInstance 6
  {opNotSupported},             // OPS_ModifyClass 7
  {modifyInstance},             // OPS_ModifyInstance 8
  {enumClasses},                // OPS_EnumerateClasses 9
  {enumClassNames},             // OPS_EnumerateClassNames 10
  {enumInstances},              // OPS_EnumerateInstances 11
  {enumInstanceNames},          // OPS_EnumerateInstanceNames 12
  {execQuery},                  // OPS_ExecQuery 13
  {associators},                // OPS_Associators 14
  {associatorNames},            // OPS_AssociatorNames 15
  {references},                 // OPS_References 16
  {referenceNames},             // OPS_ReferenceNames 17
  {getProperty},                // OPS_GetProperty 18
  {setProperty},                // OPS_SetProperty 19
#ifdef HAVE_QUALREP
  {getQualifier},               // OPS_GetQualifier 20
  {setQualifier},               // OPS_SetQualifier 21
  {deleteQualifier},            // OPS_DeleteQualifier 22
  {enumQualifiers},             // OPS_EnumerateQualifiers 23
#else
  {opNotSupported},             // OPS_GetQualifier 20
  {opNotSupported},             // OPS_SetQualifier 21
  {opNotSupported},             // OPS_DeleteQualifier 22
  {opNotSupported},             // OPS_EnumerateQualifiers 23
#endif
  {invokeMethod},               // OPS_InvokeMethod 24
  {loadProvider},               // OPS_LoadProvider 25
  {NULL},                       // OPS_PingProvider 26
  {NULL},                       // OPS_IndicationLookup 27
#ifdef SFCB_INCL_INDICATION_SUPPORT
  {activateFilter},             // OPS_ActivateFilter 28
  {deactivateFilter},           // OPS_DeactivateFilter 29
  {disableIndications},         // OPS_DisableIndications 30
  {enableIndications}           // OPS_EnableIndications 31
#else
  {NULL},                       // OPS_ActivateFilter 28
  {NULL},                       // OPS_DeactivateFilter 29
  {NULL},                       // OPS_DisableIndications 30
  {NULL}                        // OPS_EnableIndications 31
#endif
};

char           *opsName[] = {
  "dummy",
  "GetClass",
  "GetInstance",
  "DeleteClass",
  "DeleteInstance",
  "CreateClass",
  "CreateInstance",
  "ModifyClass",
  "ModifyInstance",
  "EnumerateClasses",
  "EnumerateClassNames",
  "EnumerateInstances",
  "EnumerateInstanceNames",
  "ExecQuery",
  "Associators",
  "AssociatorNames",
  "References",
  "ReferenceNames",
  "GetProperty",
  "SetProperty",
  "GetQualifier",
  "SetQualifier",
  "DeleteQualifier",
  "EnumerateQualifiers",
  "InvokeMethod",
  "LoadProvider",
  "PingProvider",
  "IndicationLookup",
  "ActivateFilter",
  "DeactivateFilter",
  "DisableIndications",
  "EnableIndications",
};

static void    *
processProviderInvocationRequestsThread(void *prms)
{
  BinResponseHdr *resp = NULL;
  ProviderInfo   *pInfo;
  ProvHandler     hdlr;
  Parms          *parms = (Parms *) prms;
  BinRequestHdr  *req = parms->req;
  int             i,
                  requestor = 0,
      initRc = 0;
  char           *errstr = NULL;
  char msg[1024];

  _SFCB_ENTER(TRACE_PROVIDERDRV,
              "processProviderInvocationRequestsThread");

  /* Convert offsets in request header back into
   * real pointers. Set empty chars segments to
   * NULL. */
  for (i = 0; i < req->count; i++) {
    if (req->object[i].length)
      req->object[i].data =
          (void *) ((long) req->object[i].data + (char *) req);
    else if (req->object[i].type == MSG_SEG_CHARS)
      req->object[i].data = NULL;
  }

  if (req->operation != OPS_LoadProvider) {
    if (req->provId == NULL) {
      mlogf(M_ERROR,M_SHOW,"-#- no provider id specified for request --- terminating process (%d).\n", currentProc);
      snprintf(msg,1023, "*** Provider id not specified (%d), exiting",
               currentProc);
      resp = errorCharsResp(CMPI_RC_ERR_FAILED, msg);
      sendResponse(abs(parms->requestor), resp);
      free(resp);
      exit(-1);
    }

    /* Update lastActivity time for the process and for
     * the specific provider being requested. */
    time(&curProvProc->lastActivity);

    if (activProvs == NULL) {
      /* only load provider allowed, exiting should allow for recovery via reload */
      mlogf(M_ERROR,M_SHOW,"-#- potential race condition in provider reload --- terminating process (%d).\n", currentProc);
      snprintf(msg,1023, "*** Provider not yet loaded (%d), exiting",
               currentProc);
      resp = errorCharsResp(CMPI_RC_ERR_FAILED, msg);
      sendResponse(abs(parms->requestor), resp);
      free(resp);
      exit(-1); 
    }

    for (pInfo = activProvs; pInfo; pInfo = pInfo->next) {
      if (pInfo->provIds.ids == req->provId) {
        pInfo->lastActivity = curProvProc->lastActivity;
        break;
      }
    }
    if (pInfo == NULL) {
      /* probably a race, however this provider is still alive, keep it running */
      mlogf(M_ERROR,M_SHOW,"-#- misdirected provider request (%d) --- skipping request, keep process (%d).\n", req->operation, currentProc);
      if (req->operation == OPS_InvokeMethod) {
        fprintf(stderr,"method: %s",(char*)((InvokeMethodReq*)req)->method.data);
      }
      snprintf(msg,1023, "*** Misdirected provider request (%d)",
               currentProc);
      resp = errorCharsResp(CMPI_RC_ERR_FAILED, msg);
      sendResponse(abs(parms->requestor), resp);
      free(resp);
      _SFCB_RETURN(NULL);
    }

    if (pInfo->library == NULL) {
      char            dlName[512];
      mlogf(M_INFO, M_SHOW, "--- Reloading provider\n");
      doLoadProvider(pInfo, dlName, 512);
    }

    initRc = initProvider(pInfo, req->sessionId, &errstr);
    _SFCB_TRACE(1, ("--- Provider initialization rc %d", initRc));

  } else {
    pInfo = NULL;
  }

  if (initRc) {
    mlogf(M_ERROR, M_SHOW, "%s", errstr);
    _SFCB_TRACE(1, (errstr));
    resp = errorCharsResp(CMPI_RC_ERR_FAILED, errstr);
    free(errstr);
    errstr = NULL;
  } else {
    _SFCB_TRACE(1, ("--- Provider request for op:%s pInfo:%p prov:%x",
                    opsName[req->operation], pInfo, req->provId));

    if (req->flags & FL_chunked)
      requestor = parms->requestor;
    else
      requestor = -parms->requestor;

    hdlr = pHandlers[req->operation];

    pthread_mutex_lock(&activeMtx);
    parms->pInfo = pInfo;
    ENQ_BOT_LIST(parms, activeThreadsFirst, activeThreadsLast, next, prev);
    pthread_mutex_unlock(&activeMtx);

    resp = hdlr.handler(req, pInfo, requestor);

    pthread_mutex_lock(&activeMtx);
    DEQ_FROM_LIST(parms, activeThreadsFirst, activeThreadsLast, next,
                  prev);
    pthread_mutex_unlock(&activeMtx);

    _SFCB_TRACE(1,
                ("--- Provider request for %s DONE",
                 opsName[req->operation]));
  }

  if (resp) {
    if (req->options & BRH_NoResp) {
      _SFCB_TRACE(1, ("--- response suppressed"));
    } else {
      sendResponse(parms->requestor, resp);
      if (req->operation == OPS_LoadProvider && resp->rc == 2)
        exit(-1);
    }

    /* SF:2727918, Bugzilla:51733 - memory leak fix */
#ifdef HAVE_QUALREP
    if ((req->operation == OPS_GetQualifier) 
         || (req->operation == OPS_EnumerateQualifiers)) {
      for (i = 0; i < resp->count; i++) {
        /* SF:3546279 - only free on successful return */
        if (resp->object[i].data && resp->object[i].type == MSG_SEG_QUALIFIER) {
          free(resp->object[i].data);
          resp->object[i].data = NULL;
        }
     }
    }
    free(resp);
    resp = NULL;
#else
    free(resp);
#endif
  }

  tool_mm_flush();

  if (pInfo && idleThreadStartHandled == 0) {
    if (idleThreadStartHandled == 0 && req->operation != OPS_PingProvider) {
      if (pInfo->unload == 0) {
        pthread_attr_t  tattr;
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&pInfo->idleThread, &tattr,
                       (void *(*)(void *)) providerIdleThread, NULL);
        idleThreadId = pInfo->idleThread;
      } else /* provider is marked "unload: never" in providerRegister */
        pInfo->idleThread = 0;
      idleThreadStartHandled = 1;
    }
    time(&pInfo->lastActivity);
    curProvProc->lastActivity = pInfo->lastActivity;
  }

  if ((req->options & BRH_Internal) == 0)
    close(abs(parms->requestor));
  free(parms);
  free(req);

  _SFCB_RETURN(NULL);
}

int
pauseProvider(char *name)
{
  int             rc = 0;
  char           *n,
                 *m;
  if (noProvPause)
    return 0;
  if (provPauseStr == NULL)
    return 0;
  else {
    char           *p;
    p = m = strdup(provPauseStr);
    while (*p) {
      *p = tolower(*p);
      p++;
    }
  }
  if (name) {
    char           *p;
    int             l = strlen(name);
    p = n = strdup(name);
    while (*p) {
      *p = tolower(*p);
      p++;
    }
    if ((p = strstr(m, n)) != NULL) {
      if ((p == m || *(p - 1) == ',') && (p[l] == ',' || p[l] == 0))
        rc = 1;
    }
    free(m);
    free(n);
    return rc;
  }
  free(m);
  noProvPause = 1;
  return 0;
}

void
processProviderInvocationRequests(char *name)
{
  unsigned long   rl;
  Parms          *parms;
  int             rc,
                  debugMode = 0,
      once = 1;
  pthread_t       t;
  pthread_attr_t  tattr;
  MqgStat         mqg;

  _SFCB_ENTER(TRACE_PROVIDERDRV, "processProviderInvocationRequests");

  pthread_attr_init(&tattr);
  pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

  debugMode = pauseProvider(name);
  for (;;) {
    _SFCB_TRACE(1, ("--- Waiting for provider request to R%d-%lu",
                    providerSockets.receive,
                    getInode(providerSockets.receive)));
    parms = (Parms *) malloc(sizeof(*parms));
    memset(parms, 0, sizeof(*parms));

    rc = spRecvReq(&providerSockets.receive, &parms->requestor,
                   (void **) &parms->req, &rl, &mqg);
    if (mqg.rdone) {
      int             debug_break = 0;
      if (rc != 0) {
        mlogf(M_ERROR,M_SHOW, "spRecvReq returned error %d. Skipping message.\n", rc);
        free(parms);
        continue;
      }

      _SFCB_TRACE(1, ("--- Got something op:%d-prov:%p on R%d-%lu",
                      parms->req->operation, parms->req->provId,
                      providerSockets.receive,
                      getInode(providerSockets.receive)));

      if (once && debugMode && parms->req->operation != OPS_LoadProvider)
        for (;;) {
          if (debug_break)
            break;
          fprintf(stdout, "-#- Pausing for provider: %s -pid: %d\n", name,
                  currentProc);
          once = 0;
          sleep(5);
        }

      if (parms->req->operation == OPS_LoadProvider || debugMode) {
        processProviderInvocationRequestsThread(parms);
      } else {
	int pcrc = pthread_create(&t, &tattr, (void *(*)(void *))
				  processProviderInvocationRequestsThread,
				  (void *) parms);
	if (pcrc) 
	  mlogf(M_ERROR,M_SHOW,"pthread_create() failed for handling provider request\n");

      }
    } else {
      free(parms);
    }
  }
  _SFCB_EXIT();
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
