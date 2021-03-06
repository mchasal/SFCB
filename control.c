
/*
 * control.c
 *
 * (C) Copyright IBM Corp. 2005
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
 * sfcb.cfg config parser.
 *
 */

#include <sfcCommon/utilft.h>
#include "support.h"
#include "mlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef SFCB_CONFDIR
#define SFCB_CONFDIR "/etc/sfcb"
#endif

#ifndef SFCB_STATEDIR
#define SFCB_STATEDIR "/var/lib/sfcb"
#endif

#ifndef SFCB_LIBDIR
#define SFCB_LIBDIR "/usr/lib"
#endif

typedef struct control {
  char           *id;
  int             type;
  char           *strValue;
  int             dupped;
} Control;

static UtilHashTable *ct = NULL;

char           *configfile = NULL;

// Control initial values
// { property, type, value}
// Type: 0=string, 1=num, 2=bool, 3=unstripped string
Control         init[] = {
  {"httpPort", 1, "5988"},
  {"enableHttp", 2, "true"},
  {"enableUds", 2, "true"},
  {"httpProcs", 1, "8"},
  {"httpsPort", 1, "5989"},
  {"enableHttps", 2, "false"},
  {"httpLocalOnly", 2, "false"},
  {"httpUserSFCB", 2, "true"},
  {"httpUser", 0, ""},
#ifdef HAVE_SLP
  {"enableSlp", 2, "true"},
  {"slpRefreshInterval", 1, "600"},
#endif
  {"provProcs", 1, "32"},
  {"sfcbCustomLib",   0, "sfcCustomLib"},
  {"basicAuthLib", 0, "sfcBasicAuthentication"},
  {"basicAuthEntry",   0, "_sfcBasicAuthenticate"},
  {"doBasicAuth", 2, "false"},
  {"doUdsAuth", 2, "false"},

  {"useChunking", 2, "false"},
  {"chunkSize", 1, "50000"},

  {"trimWhitespace",      2, "true"},

  {"keepaliveTimeout", 1, "15"},
  {"keepaliveMaxRequest", 1, "10"},
  {"selectTimeout", 1, "5"},

  {"providerSampleInterval", 1, "30"},
  {"providerTimeoutInterval", 1, "60"},
  {"providerAutoGroup", 2, "true"},
  {"providerDefaultUserSFCB", 2, "true"},
  {"providerDefaultUser", 0, ""},

  {"sslKeyFilePath", 0, SFCB_CONFDIR "/file.pem"},
  {"sslCertificateFilePath", 0, SFCB_CONFDIR "/server.pem"},
  {"sslCertList", 0, SFCB_CONFDIR "/clist.pem"},
  {"sslCiphers", 0, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"},

  {"registrationDir", 0, SFCB_STATEDIR "/registration"},
  {"providerDirs", 3, SFCB_LIBDIR " " CMPI_LIBDIR " " LIBDIR},  /* 3:
                                                                 * unstripped 
                                                                 */

  {"enableInterOp", 2, "true"},
  {"sslClientTrustStore", 0, SFCB_CONFDIR "/client.pem"},
  {"sslClientCertificate", 0, "ignore"},
  {"sslIndicationReceiverCert", 0, "ignore" },
  {"certificateAuthLib", 0, "sfcCertificateAuthentication"},
  {"localSocketPath", 0, "/tmp/sfcbLocalSocket"},
  {"httpSocketPath", 0, "/tmp/sfcbHttpSocket"},
  {"socketPathGroupPerm",   0, NULL},

  {"traceFile", 0, "stderr"},
  {"traceLevel", 1, "0"},
  {"traceMask", 1, "0"},

  {"httpMaxContentLength", 1, "100000000"},
  {"validateMethodParamTypes", 2, "false"},
  {"maxMsgLen", 1, "10000000"},
  {"networkInterface", 3, NULL},
  {"DeliveryRetryInterval",1,"20"},
  {"DeliveryRetryAttempts",1,"3"},
  {"SubscriptionRemovalTimeInterval",1,"2592000"},
  {"SubscriptionRemovalAction",1,"2"},
  {"indicationDeliveryThreadLimit",1,"30"},
  {"indicationDeliveryThreadTimeout",1,"0"},
  {"MaxListenerDestinations",1,"100"},
  {"MaxActiveSubscriptions",1,"100"},
  {"indicationCurlTimeout",1,"10"},
};

void
sunsetControl()
{
  int             i,
                  m;
  for (i = 0, m = sizeof(init) / sizeof(Control); i < m; i++) {
    if (init[i].dupped)
      if(init[i].dupped) {
        free(init[i].strValue);
        init[i].dupped = 0;
      }
  }
  if (ct) {
    ct->ft->release(ct);
    ct=NULL;
  }
}

int
setupControl(char *fn)
{
  FILE           *in;
  char            fin[1024],
                 *stmt = NULL;
  int             i,
                  m,
                  n = 0,
      err = 0;
  CntlVals        rv;
  char *configFile;

  if (ct)
    return 0;

  ct = UtilFactory->newHashTable(61, UtilHashTable_charKey |
                                 UtilHashTable_ignoreKeyCase);

  for (i = 0, m = sizeof(init) / sizeof(Control); i < m; i++) {
    ct->ft->put(ct, init[i].id, &init[i]);
  }

  if (fn) {
    if (strlen(fn) >= sizeof(fin))
      mlogf(M_ERROR,M_SHOW, "--- \"%s\" too long\n", fn);
    strncpy(fin,fn,sizeof(fin));
  } 
  else if ((configFile = getenv("SFCB_CONFIG_FILE")) != NULL && configFile[0] != '\0') {
    if (strlen(configFile) >= sizeof(fin))
      mlogf(M_ERROR,M_SHOW, "--- \"%s\" too long\n", configFile);
    strncpy(fin,configFile,sizeof(fin));
  } else {
    strncpy(fin, SFCB_CONFDIR "/sfcb.cfg", sizeof(fin));
  }
  fin[sizeof(fin)-1] = '\0';

  if (fin[0] == '/')
    mlogf(M_INFO, M_SHOW, "--- Using %s\n", fin);
  else
    mlogf(M_INFO, M_SHOW, "--- Using ./%s\n", fin);
  in = fopen(fin, "r");
  if (in == NULL) {
    mlogf(M_ERROR, M_SHOW, "--- %s not found\n", fin);
    return -2;
  }

  while (fgets(fin, 1024, in)) {
    n++;
    if (stmt)
      free(stmt);
    stmt = strdup(fin);
    switch (cntlParseStmt(fin, &rv)) {
    case 0:
    case 1:
      mlogf(M_ERROR, M_SHOW,
            "--- control statement not recognized: \n\t%d: %s\n", n, stmt);
      err = 1;
      break;
    case 2:
      for (i = 0; i < sizeof(init) / sizeof(Control); i++) {
        if (strcmp(rv.id, init[i].id) == 0) {
          if (init[i].type == 3) {
            /*
             * unstripped character string 
             */
            init[i].strValue = strdup(rv.val);
            if (strchr(init[i].strValue, '\n'))
              *(strchr(init[i].strValue, '\n')) = 0;
            init[i].dupped = 1;
          } else {
            init[i].strValue = strdup(cntlGetVal(&rv));
            init[i].dupped = 1;
          }
          goto ok;
        }
      }
      mlogf(M_ERROR, M_SHOW, "--- invalid control statement: \n\t%d: %s\n",
            n, stmt);
      err = 1;
    ok:
      break;
    case 3:
      break;
    }
  }
  if (stmt)
    free(stmt);

  fclose(in);

  if (err) {
    mlogf(M_INFO, M_SHOW,
          "--- Broker terminated because of previous error(s)\n");
    exit(1);
  }

  return 0;
}

int
getControlChars(char *id, char **val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == 0 || ctl->type == 3) {
      *val = ctl->strValue;
      return 0;
    }
    rc = -2;
  }
  *val = NULL;
  return rc;
}

int
getControlNum(char *id, long *val)
{
  Control        *ctl;
  int             rc = -1;

  if (ct == NULL) {
    setupControl(configfile);
  }

  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == 1) {
      *val = strtol(ctl->strValue, NULL, 0);
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}

int
getControlUNum(char *id, unsigned int *val)
{
  Control        *ctl;
  int             rc = -1;
  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == 1 && isdigit(ctl->strValue[0])) {
      unsigned long   tmp = strtoul(ctl->strValue, NULL, 0);
      if (tmp < UINT_MAX) {
        *val = tmp;
        return 0;
      }
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}

int
getControlBool(char *id, int *val)
{
  Control        *ctl;
  int             rc = -1;
  if ((ctl = ct->ft->get(ct, id))) {
    if (ctl->type == 2) {
      *val = strcasecmp(ctl->strValue, "true") == 0;
      return 0;
    }
    rc = -2;
  }
  *val = 0;
  return rc;
}
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
