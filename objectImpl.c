
/*
 * objectimpl.c
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
 * Author:     Adrian Schuur <schuur@de.ibm.com>
 *
 * Description:
 *
 * Internal implementation support for cim objects.
 *
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "objectImpl.h"
#include "array.h"
#include "utilft.h"
#include "trace.h" 
#include "config.h"

#ifdef SFCB_IX86
#define SFCB_ASM(x) asm(x)
#else
#define SFCB_ASM(x)
#endif

//#define DEB(x) x
#define DEB(x) 

//static ClSection *newClSection();

static ClClass *newClassH(ClObjectHdr * hdr, const char *cn, const char *pa);
static ClClass *rebuildClassH(ClObjectHdr * hdr, ClClass * cls, void *area);

static char *addQualifierToString(stringControl * sc, ClObjectHdr * hdr,
                                  ClQualifier * q, int sb);
static ClQualifier makeClQualifier(ClObjectHdr * hdr, const char *id,
                                   CMPIData d);
static int addClQualifier(ClObjectHdr * hdr, ClSection * qlfs,
                          ClQualifier * nq);
//static int insQualifier(ClObjectHdr * hdr, ClSection * qlfs,ClQualifier * nq);

//static CMPIData _ClDataString(char *str, CMPIValueState s);
//static CMPIData _ClDataSint32(long n, CMPIValueState s);

static void clearClSection(ClSection * s);
static void *ensureClSpace(ClObjectHdr * hdr, ClSection * sct, int size,
                           int iSize);
extern void dateTime2chars(CMPIDateTime * dt, CMPIStatus * rc, char *str_time);
extern char *pathToChars(CMPIObjectPath * cop, CMPIStatus * rc, char *str);

static ClString nls = { 0 };

/*
static void setDefaultMalloc()
{
    malloc = malloc;
    realloc = realloc;
    free = free;
}
*/
/*
static CMPIData _ClDataString(char *str, CMPIValueState s)
{
    CMPIData d;
    d.type = CMPI_chars;
    d.state = s;
    if (s & CMPI_nullValue) d.value.sint64 = 0;
    else d.value.chars = str;
    return d;
}
*/
/*
static CMPIData _ClDataSint32(long n, CMPIValueState s)
{
    CMPIData d;
    d.type = CMPI_sint32;
    d.state = s;
    if (s & CMPI_nullValue) d.value.sint64 = 0;
    else d.value.sint32 = n;
    return d;
}
*/
static void clearClSection(ClSection * s)
{
   memset(s, 0, sizeof(*s));
}

const char *ClObjectGetClString(ClObjectHdr * hdr, ClString * id)
{
   ClStrBuf *buf;

   if (id->id == 0) return NULL;
   if MALLOCED (hdr->strBufOffset) buf = (ClStrBuf *) hdr->strBufOffset;
   else buf = (ClStrBuf *) ((char *) hdr + abs(hdr->strBufOffset));
   return &(buf->buf[buf->sIndex[id->id - 1]]);
}

const CMPIData *ClObjectGetClArray(ClObjectHdr * hdr, ClArray * id)
{
   ClArrayBuf *buf;

   if (id->id == 0) return NULL;
   if MALLOCED(hdr->arrayBufOffset) buf = (ClArrayBuf *) hdr->arrayBufOffset;
   else buf = (ClArrayBuf *) ((char *) hdr + abs(hdr->arrayBufOffset));
   return &(buf->buf[buf->aIndex[id->id - 1]]);
}

void *ClObjectGetClSection(ClObjectHdr * hdr, ClSection * s)
{
   if MALLOCED(s->offset) return (void *) s->offset;
   return (void *) ((char *) hdr + abs(s->offset));
}

static void *ensureClSpace(ClObjectHdr * hdr, ClSection * sct, int size,
                           int iSize)
{
   void *p;

   if (sct->offset == 0) {
      p = (void *) (sct->offset = (long) malloc((sct->max = iSize) * size));
      hdr->flags |= HDR_Rebuild;
   }
   else if (sct->used >= sct->max) {
      if MALLOCED(sct->offset)
         sct->offset=(long)realloc((void*)sct->offset,(sct->max*=2)*size);
      else {
         void *f,*t;
         f=((char*)hdr)+abs(sct->offset);
         t=malloc(sct->max*2*size);
         memcpy(t,f,sct->max*size);
         sct->max*=2;
         sct->offset=(long)t;
//         sct->offset=(long)malloc((sct->max*=2)*size);
      }   
      p = (void *) sct->offset;
      hdr->flags |= HDR_Rebuild;
   }
   else {
      if MALLOCED(sct->offset) p=(void*)sct->offset;
      else p=((char*)hdr)+abs(sct->offset);
   }
   return p;
}


static long addClString(ClObjectHdr * hdr, const char *str)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "addClString");

   int nmax = 16, l = strlen(str) + 1;
   ClStrBuf *buf;

   if (hdr->strBufOffset == 0) {
      nmax = 256;
      for (; nmax <= l; nmax *= 2);
      buf = (ClStrBuf *) (hdr->strBufOffset = (long)
                          malloc(((nmax - 1) * sizeof(char)) +
                                 sizeof(ClStrBuf)));
      buf->bMax = nmax;
      buf->bUsed = buf->iUsed = 0;
      buf->iOffset = (long)
          (buf->sIndex = (long *) malloc(sizeof(long) * 16));
      buf->iMax = 16;
      hdr->flags |= HDR_Rebuild;
   }

   else {
      int malloced = MALLOCED(hdr->strBufOffset);
      if (malloced) buf = (ClStrBuf *) hdr->strBufOffset;
      else buf = (ClStrBuf *) (((char *) hdr) + abs(hdr->strBufOffset));

      if (buf->iUsed >= abs(buf->iMax)) {
         nmax = abs(buf->iMax) * 2;
         if (buf->iMax > 0) {
            if (!malloced) {
               void *idx = (void *) buf->sIndex;
               buf->iOffset = (long)
                   (buf->sIndex = (long *) malloc(nmax * sizeof(long)));
               memcpy((void *) buf->sIndex, idx, buf->iMax * sizeof(long));
            }
            else buf->iOffset = (long) (buf->sIndex = (long *)
                        realloc((void *) buf->sIndex,nmax * sizeof(long)));
         }
         else buf->iOffset = (long)
                (buf->sIndex = (long *) malloc(nmax * sizeof(long)));
         buf->iMax = nmax;
         hdr->flags |= HDR_Rebuild;
      }

      if (buf->bUsed + l >= abs(buf->bMax)) {
         nmax = abs(buf->bMax);
         for (; nmax <= buf->bUsed + l; nmax *= 2);
         if (buf->bMax > 0) {
            if (!malloced) {
               hdr->strBufOffset = (long)
                   malloc(((nmax - 1) * sizeof(char)) + sizeof(ClStrBuf));
               memcpy((void *) hdr->strBufOffset, buf, buf->bMax + sizeof(ClStrBuf));
            }
            else hdr->strBufOffset = (long) (realloc((void *) hdr->strBufOffset,
                 ((nmax - 1) * sizeof(char)) + sizeof(ClStrBuf)));
         }
         else hdr->strBufOffset = (long)
                malloc(((nmax - 1) * sizeof(char)) + sizeof(ClStrBuf));
         buf = (ClStrBuf *) hdr->strBufOffset;
         buf->bMax = nmax;
         hdr->flags |= HDR_Rebuild;
      }
   }

   strcpy(buf->buf + buf->bUsed, str);
   buf->sIndex[buf->iUsed++] = buf->bUsed;
   buf->bUsed += l;

   _SFCB_RETURN(buf->iUsed);
}


static long addClArray(ClObjectHdr * hdr, CMPIData d)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "addClArray");

   struct native_array *ar = (struct native_array *) d.value.array;
   int nmax = 16, l = ar->size + 1, i, m;
   ClArrayBuf *buf;
   CMPIData td, *dp;

   if (hdr->arrayBufOffset == 0) {
      nmax = 16;
      for (; nmax <= l; nmax *= 2);
      buf = (ClArrayBuf *) (hdr->arrayBufOffset = (long)
             malloc(((nmax - 1) * sizeof(CMPIData)) + sizeof(ClArrayBuf)));
      buf->bMax = nmax;
      buf->bUsed = buf->iUsed = 0;
      buf->iOffset = (long) (buf->aIndex = (long *) malloc(sizeof(long) * 16));
      buf->iMax = 16;
      hdr->flags |= HDR_Rebuild;
   }

   else {
      int malloced = MALLOCED(hdr->arrayBufOffset);
      if (malloced) buf = (ClArrayBuf *) hdr->arrayBufOffset;
      else buf = (ClArrayBuf *) (((char *) hdr) + abs(hdr->arrayBufOffset));

      if (buf->iUsed >= abs(buf->iMax)) {
         nmax = abs(buf->iMax) * 2;
         if (buf->iMax > 0) {
            if (!malloced) {
               void *idx = (void *) buf->aIndex;
               buf->iOffset = (long)
                   (buf->aIndex = (long *) malloc(nmax * sizeof(long)));
               memcpy((void *) buf->aIndex, idx, buf->iMax * sizeof(long));
            }
            else {
               buf->iOffset = (long)
                  (buf->aIndex = (long *)realloc((void *) buf->aIndex, nmax * sizeof(long)));
            }   
         }
         else {
            buf->iOffset = (long) 
             (buf->aIndex = (long *) malloc(nmax * sizeof(long)));
         }   
         buf->iMax = nmax;
         hdr->flags |= HDR_Rebuild;
      }

      if (buf->bUsed + l >= abs(buf->bMax)) {
         nmax = abs(buf->bMax);
         for (; nmax <= buf->bUsed + l; nmax *= 2);
         if (buf->bMax > 0) {
            if (!malloced) {
               hdr->arrayBufOffset = (long)
                   malloc(((nmax - 1) * sizeof(CMPIData)) + sizeof(ClArrayBuf));
               memcpy((void *) hdr->arrayBufOffset, buf,
                      buf->bMax + sizeof(ClArrayBuf));
            }
            else hdr->arrayBufOffset = (long) (realloc ((void *) hdr->arrayBufOffset,
                  ((nmax - 1) * sizeof(CMPIData)) + sizeof(ClArrayBuf)));
         }
         else hdr->arrayBufOffset = (long)
                malloc(((nmax - 1) * sizeof(CMPIData)) + sizeof(ClArrayBuf));
         buf = (ClArrayBuf *) hdr->arrayBufOffset;
         buf->bMax = nmax;
         hdr->flags |= HDR_Rebuild;
      }
   }

   td.state = 0;
   td.type = (ar->type == CMPI_string) ? CMPI_chars : ar->type;
   td.value.sint32 = ar->size; 
   buf->aIndex[buf->iUsed++] = buf->bUsed;
   buf->buf[buf->bUsed++] = td;

   for (i = 0, dp = buf->buf + buf->bUsed, m = ar->size; i < m; i++) {
      td.state = ar->data[i].state;
      if (ar->type == CMPI_chars && ((td.state & CMPI_nullValue) == 0)) {
         td.value.chars = (char *) addClString(hdr, ar->data[i].value.chars);
         td.type = CMPI_string;
      }
      else if (ar->type == CMPI_string && ((td.state & CMPI_nullValue) == 0)) {
         td.value.chars = (char *) addClString(hdr, ar->data[i].value.string->hdl);
         td.type = CMPI_string;
      }
      else {
         td.value = ar->data[i].value;
         td.type =ar->type&~CMPI_ARRAY;
      }
      dp[i] = td;
   }

   buf->bUsed += m;

   _SFCB_RETURN(buf->iUsed);
}

static void freeStringBuf(ClObjectHdr * hdr)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "freeStringBuf");

   ClStrBuf *buf;
   if (hdr->strBufOffset == 0)
      return;
   if MALLOCED
      (hdr->strBufOffset) buf = (ClStrBuf *) hdr->strBufOffset;
   else
      buf = (ClStrBuf *) (((char *) hdr) + abs(hdr->strBufOffset));

   if MALLOCED
      (buf->iOffset) free((void *) buf->iOffset);
   if MALLOCED
      (hdr->strBufOffset) free((void *) hdr->strBufOffset);

   _SFCB_EXIT();
}

static void freeArrayBuf(ClObjectHdr * hdr)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "freeArrayBuf");

   ClArrayBuf *buf;
   if (hdr->arrayBufOffset == 0)
      return;
   if MALLOCED
      (hdr->arrayBufOffset) buf = (ClArrayBuf *) hdr->arrayBufOffset;
   else
      buf = (ClArrayBuf *) (((char *) hdr) + abs(hdr->arrayBufOffset));

   if MALLOCED
      (buf->iOffset) free((void *) buf->iOffset);
   if MALLOCED
      (hdr->arrayBufOffset) free((void *) hdr->arrayBufOffset);

   _SFCB_EXIT();
}

static long sizeStringBuf(ClObjectHdr * hdr)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "sizeStringBuf");

   ClStrBuf *buf;
   long sz = 0;
   if (hdr->strBufOffset == 0)
      _SFCB_RETURN(0);

   if MALLOCED
      (hdr->strBufOffset) buf = (ClStrBuf *) hdr->strBufOffset;
   else
      buf = (ClStrBuf *) (((char *) hdr) + abs(hdr->strBufOffset));

   sz = sizeof(*buf) + buf->bUsed + (buf->iUsed * sizeof(*buf->sIndex));
   DEB(printf("--- sizeStringBuf: %lu-%p\n", sz, (void *) sz));

   _SFCB_RETURN(sz);
}

static long sizeArrayBuf(ClObjectHdr * hdr)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "sizeArrayBuf");

   ClArrayBuf *buf;
   long sz = 0;
   if (hdr->arrayBufOffset == 0)
      _SFCB_RETURN(0);

   if MALLOCED(hdr->arrayBufOffset) buf = (ClArrayBuf *) hdr->arrayBufOffset;
   else buf = (ClArrayBuf *) (((char *) hdr) + abs(hdr->arrayBufOffset));

   sz = sizeof(*buf) + (buf->bUsed * sizeof(CMPIData)) +
       (buf->iUsed * sizeof(*buf->aIndex));

   DEB(printf("--- sizeArrayBuf: %lu-%p\n", sz, (void *) sz));

   _SFCB_RETURN(sz);
}

static void replaceClString(ClObjectHdr * hdr, int id, const char *str)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "replaceClString");

   char *ts, *fs;
   long i, l, u;
   ClStrBuf *fb;

   if MALLOCED(hdr->strBufOffset) fb = (ClStrBuf *) hdr->strBufOffset;
   else fb = (ClStrBuf *) (((char *) hdr) + abs(hdr->strBufOffset));

   ts = (char *) malloc(fb->bUsed);
   fs = &fb->buf[0];

   for (u = i = 0; i < fb->iUsed; i++) {
      if (i != id - 1) {
         char *f = fs + fb->sIndex[i];
         l = strlen(f) + 1;
         fb->sIndex[i] = u;
         memcpy(ts + u, f, l);
         u += l;
      }
   }
   memcpy(fs, ts, u);
   fb->bUsed = u;
   free(ts);

   i = addClString(hdr, str);
   if MALLOCED (hdr->strBufOffset) fb = (ClStrBuf *) hdr->strBufOffset;
   else fb = (ClStrBuf *) (((char *) hdr) + abs(hdr->strBufOffset));

   fb->iUsed--;
   fb->sIndex[id - 1] = fb->sIndex[i-1];

  _SFCB_EXIT();
}

static void replaceClArray(ClObjectHdr * hdr, int id, CMPIData d)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "replaceClArray");

   CMPIData *ts, *fs;
   long i, l, u;
   ClArrayBuf *fb;

   if MALLOCED (hdr->arrayBufOffset) fb = (ClArrayBuf *) hdr->arrayBufOffset;
   else fb = (ClArrayBuf *) (((char *) hdr) + abs(hdr->arrayBufOffset));

   ts = (CMPIData *) malloc(fb->bUsed * sizeof(CMPIData));
   fs = &fb->buf[0];

   for (u = i = 0; i < fb->iUsed; i++) {
      if (i != id - 1) {
         CMPIData *f = fs + fb->aIndex[i];
         l = (f->value.sint32 + 1) * sizeof(CMPIData);
         fb->aIndex[i] = u;
         memcpy(ts + u, f, l);
         u += f->value.sint32 + 1;
      }
   }
   memcpy(fs, ts, u * sizeof(CMPIData));
   fb->bUsed = u;
   free(ts);

   i = addClArray(hdr, d);
   if MALLOCED(hdr->arrayBufOffset) fb = (ClArrayBuf *) hdr->arrayBufOffset;
   else fb = (ClArrayBuf *) (((char *) hdr) + abs(hdr->arrayBufOffset));

   fb->iUsed--;
   fb->aIndex[id - 1] = i;

   _SFCB_EXIT();
}


static int copyStringBuf(int ofs, int sz, ClObjectHdr * th, ClObjectHdr * fh)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "copyStringBuf");

   ClStrBuf *fb, *tb;
   long l, il;
   
   if (fh->strBufOffset == 0) _SFCB_RETURN(ofs);

   tb = (ClStrBuf *) (((char *) th) + ofs);
   if MALLOCED (fh->strBufOffset) fb = (ClStrBuf *) fh->strBufOffset;
   else fb = (ClStrBuf *) (((char *) fh) + abs(fh->strBufOffset));

   l = sizeof(*fb) + fb->bUsed;
   il = fb->iUsed * sizeof(*fb->sIndex);

   memcpy(tb, fb, l);
   tb->bMax = tb->bUsed;
   th->strBufOffset = -ofs;
   ofs += l;

   memcpy(((char *) th) + ofs, fb->sIndex, il);
   tb->iMax = tb->iUsed;
   tb->iOffset = -ofs;
   tb->sIndex = (long *) (((char *) th) + ofs);

   _SFCB_RETURN(l + il);
}

static int copyArrayBuf(int ofs, int sz, ClObjectHdr * th, ClObjectHdr * fh)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "copyArrayBuf");

   ClArrayBuf *fb, *tb;
   long l, il;

   if (fh->arrayBufOffset == 0) _SFCB_RETURN(ofs);

   tb = (ClArrayBuf *) (((char *) th) + ofs);
   if MALLOCED(fh->arrayBufOffset) fb = (ClArrayBuf *) fh->arrayBufOffset;
   else fb = (ClArrayBuf *) (((char *) fh) + abs(fh->arrayBufOffset));
   
   l = sizeof(*fb) + (fb->bUsed * sizeof(CMPIData));
   il = fb->iUsed * sizeof(*fb->aIndex);
   
   memcpy(tb, fb, l);
   tb->bMax = tb->bUsed;
   th->arrayBufOffset = -ofs;
   ofs += l;
       
   memcpy(((char *) th) + ofs, fb->aIndex, il);
   tb->iMax = tb->iUsed;
   tb->iOffset = -ofs;
   tb->aIndex = (long *) (((char *) th) + ofs);

   _SFCB_RETURN(l + il);
}



//string concatenation

static char *cat2string(stringControl * sc, const char *str)
{
   int nmax, l = strlen(str) + 1;
   if (str != NULL || sc->str == NULL) {
      if (sc->str == NULL) {
         for (nmax = sc->max; nmax <= sc->used + l; nmax *= 2);
         sc->str = (char *) malloc((sc->max = nmax));
      }
      else if (l + sc->used >= sc->max) {
         for (nmax = sc->max; nmax <= sc->used + l; nmax *= 2);
         sc->str = (char *) realloc(sc->str, (sc->max = nmax));
      }
      strcpy(sc->str + sc->used, str);
      sc->used += l - 1;
   }
   return sc->str;
}


//-------------------------------------------------------
//-----
//-- data support
//-----
//-------------------------------------------------------

static char *datatypeToString(CMPIData * d, char **array)
{
   static char *Array = "[]";
   CMPIType type = d->type & ~CMPI_ARRAY;

   if (d->type & CMPI_ARRAY)
      *array = Array;
   else
      *array = NULL;

   switch (type) {
   case CMPI_chars:
   case CMPI_string:
      return "string";
   case CMPI_sint64:
      return "sint64";
   case CMPI_uint64:
      return "uint64";
   case CMPI_sint32:
      return "sint32";
   case CMPI_uint32:
      return "uint32";
   case CMPI_sint16:
      return "sint16";
   case CMPI_uint16:
      return "uint16";
   case CMPI_uint8:
      return "uint8";
   case CMPI_sint8:
      return "sint8";
   case CMPI_boolean:
      return "boolean";
   case CMPI_ref:
      return "reference";
   case CMPI_dateTime:
      return "dateTime";
   }
   return "*??*";
}

static char *dataValueToString(ClObjectHdr * hdr, CMPIData * d)
{
   static char *True = "true", *False = "false";
   switch (d->type) {
   case CMPI_chars:
      return (char *) ClObjectGetClString(hdr, (ClString *) & d->value.chars);
   case CMPI_sint32:
      return "sint32";
   case CMPI_boolean:
      return d->value.boolean ? True : False;
   }
   return "*??*";
}



//-------------------------------------------------------
//-----
//-- qualifier support
//-----
//-------------------------------------------------------

static int locateQualifier(ClObjectHdr * hdr, ClSection * qlfs, const char *id)
{
   int i;
   ClQualifier *q;

   if MALLOCED
      (qlfs->offset) q = (ClQualifier *) qlfs->offset;
   else
      q = (ClQualifier *) (((char *) hdr) + abs(qlfs->offset));

   for (i = 0; i > qlfs->used; i++) {
      if (strcasecmp(id, ClObjectGetClString(hdr, &(q + i)->id)) == 0)
         return i + 1;
   }
   return 0;
}

static int addClQualifier(ClObjectHdr * hdr, ClSection * qlfs, ClQualifier * nq)
{
   int i;
   ClQualifier *q;

   if ((i = locateQualifier(hdr, qlfs, ClObjectGetClString(hdr, &nq->id))) == 0) {
      q = (ClQualifier *) ensureClSpace(hdr, qlfs, sizeof(*q), 4);
      q = q + (qlfs->used++);
      *q = *nq;
      return qlfs->used;
   }
   else
      return 0;
}

static ClQualifier makeClQualifier(ClObjectHdr * hdr, const char *id,
                                   CMPIData d)
{
   ClQualifier q;

   q.id.id = addClString(hdr, id);
   if (d.type == CMPI_chars && (d.state & CMPI_nullValue) == 0) {
      d.value.chars = (char *) addClString(hdr, d.value.chars);
   }
   else if (d.type == CMPI_string && (d.state & CMPI_nullValue) == 0) {
      d.type = CMPI_chars;
      d.value.chars = (char *) addClString(hdr, (char *) d.value.string->hdl);
   }
   else if ((d.type & CMPI_ARRAY) && (d.state & CMPI_nullValue) == 0) {
      d.value.chars = (char *) addClArray(hdr, d);
   }

   q.data = d;
   return q;
}

int ClClassAddQualifier(ClObjectHdr * hdr, ClSection * qlfs,
                        const char *id, CMPIData d)
{
   ClQualifier q;

   if (hdr->type == HDR_Class) {
      ClClass *cls = (ClClass *) hdr;
      if (strcasecmp(id, "Abstract") == 0) 
         cls->quals |= ClClass_Q_Abstract;
      else if (strcasecmp(id, "Association") == 0)
         cls->quals |= ClClass_Q_Association;
      else if (strcasecmp(id, "Indication") == 0)
         cls->quals |= ClClass_Q_Indication;
      else if (strcasecmp(id, "Deprecated") == 0)
         cls->quals |= ClClass_Q_Deprecated;
      else {
         q = makeClQualifier(hdr, id, d);
         return addClQualifier(hdr, qlfs, &q);
      }
   }

   q = makeClQualifier(hdr, id, d);
   return addClQualifier(hdr, qlfs, &q);
}

int ClClassAddPropertyQualifier(ClObjectHdr * hdr, ClProperty * p,
                                const char *id, CMPIData d)
{
   if (strcasecmp(id, "key") == 0)
      p->quals |= ClProperty_Q_Key;
   else if (strcasecmp(id, "propagated") == 0)
      p->quals |= ClProperty_Q_Propagated;
   else if (strcasecmp(id, "deprecated") == 0)
      p->quals |= ClProperty_Q_Deprecated;
   else if (strcasecmp(id, "embeddedobject") == 0)
      p->quals |= ClProperty_Q_EmbeddedObject;

   else
      return ClClassAddQualifier(hdr, &p->qualifiers, id, d);
   return 0;
}

int ClClassAddMethodQualifier(ClObjectHdr * hdr, ClMethod * m,
                                const char *id, CMPIData d)
{
   return ClClassAddQualifier(hdr, &m->qualifiers, id, d);
}

int ClClassAddMethParamQualifier(ClObjectHdr * hdr, ClParameter * p,
                                const char *id, CMPIData d)
{
   return ClClassAddQualifier(hdr, &p->qualifiers, id, d);
}

static void freeQualifiers(ClObjectHdr * hdr, ClSection * s)
{
   if MALLOCED
      (s->offset) free((void *) s->offset);
}

static char *addQualifierToString(stringControl * sc, ClObjectHdr * hdr,
                                  ClQualifier * q, int sb)
{
   int o = sc->used;

   if (sb & 2)
      cat2string(sc, "  [");
   else
      cat2string(sc, ", ");
   cat2string(sc, ClObjectGetClString(hdr, &q->id));
   if (q->data.state != CMPI_nullValue) {
      cat2string(sc, "(");
      cat2string(sc, dataValueToString(hdr, &q->data));
      cat2string(sc, ")");
   }
   if (sb & 1)
      cat2string(sc, "]");
   return sc->str + o;
}

static long sizeQualifiers(ClObjectHdr * hdr, ClSection * s)
{
   long sz;

   sz = s->used * sizeof(ClQualifier);
   DEB(printf("--- sizeQualifiers: %lu-%p\n", sz, (void *) sz));
   return sz;
}

static int copyQualifiers(int ofs, int max, char *to, ClSection * ts,
                          ClObjectHdr * from, ClSection * fs)
{
   ClQualifier *q;
   int l = ts->used * sizeof(ClQualifier);
   ts->max = ts->used;

   q = (ClQualifier *) ClObjectGetClSection(from, fs);

   memcpy(to + ofs, q, l);
   ts->offset = -ofs;
   return l;
}

static int ClGetQualifierAt(ClClass * cls, ClQualifier *q, int id, CMPIData * data, char **name)
{
   if (data) *data = (q + id)->data;
   if (data->type== CMPI_chars) {
      const char *str =
          ClObjectGetClString(&cls->hdr, (ClString *) & data->value.chars);
      data->value.string = native_new_CMPIString(str, NULL);
      data->type = CMPI_string;
   }
   else if (data->type & CMPI_ARRAY) {
      data->value.dataPtr.ptr = (void *) ClObjectGetClArray(&cls->hdr,
            (ClArray *) & data->value.array);
   }
   if (name) *name = strdup(ClObjectGetClString(&cls->hdr, &(q + id)->id));
   return 0;
}

int ClClassGetQualifierAt(ClClass * cls, int id, CMPIData * data, char **name)
{
   ClQualifier *q;
   q = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &cls->qualifiers);
   if (id < 0 || id > cls->qualifiers.used) return 1;
   return ClGetQualifierAt(cls,q,id,data,name);   
}

int ClClassGetQualifierCount(ClClass * cls)
{
   ClQualifier *p;
   p = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &cls->qualifiers);
   return cls->qualifiers.used;
}


//-------------------------------------------------------
//-----
//-- parameter support
//-----
//-------------------------------------------------------

static int locateParameter(ClObjectHdr * hdr, ClSection * prms, const char *id)
{
   int i;
   ClParameter *p;

   if MALLOCED(prms->offset) 
      p = (ClParameter *) prms->offset;
   else p = (ClParameter *) (((char *) hdr) + abs(prms->offset));

   for (i = 0; i > prms->used; i++) {
      if (strcasecmp(id, ClObjectGetClString(hdr, &(p + i)->id)) == 0)
         return i + 1;
   }
   return 0;
}

int ClClassGetMethParamQualifierCount(ClClass * cls, ClParameter *p)
{
   return p->qualifiers.used;
}

static int addClParameter(ClObjectHdr * hdr, ClSection * prms, ClParameter * np)
{
   int i;
   ClParameter *p;

   if ((i = locateParameter(hdr, prms, ClObjectGetClString(hdr, &np->id))) == 0) {
      p = (ClParameter *) ensureClSpace(hdr, prms, sizeof(*p), 4);
      p = p + (prms->used++);
      *p = *np;
      return prms->used;
   }
   else return 0;
}

static ClParameter makeClParameter(ClObjectHdr * hdr, const char *id,
                                   CMPIParameter cp)
{
   ClParameter p;

   memset(&p,0,sizeof(p));
   p.id.id = addClString(hdr, id);
   p.parameter = cp;
   return p;
}

int ClClassAddMethParameter(ClObjectHdr * hdr, ClMethod * m,
                        const char *id, CMPIParameter cp)
{
   ClParameter p;

   p = makeClParameter(hdr, id, cp);
   return addClParameter(hdr, &m->parameters, &p);
}

static long sizeParameters(ClObjectHdr * hdr, ClSection * s)
{
   int l;
   long sz = s->used * sizeof(ClParameter);
   ClParameter *p = (ClParameter *) ClObjectGetClSection(hdr, s);

   for (l = s->used; l > 0; l--, p++) {
      if (p->qualifiers.used)
         sz += sizeQualifiers(hdr, &p->qualifiers);
   }      
   DEB(printf("--- sizeParameters: %lu-%p\n", sz, (void *) sz));
   return sz;
}

static long copyParameters(int ofs, int max, char *to, ClSection * ts,
                           ClObjectHdr * from, ClSection * fs)
{
   ClParameter *fp = (ClParameter *) ClObjectGetClSection(from, fs);
   ClParameter *tp = (ClParameter *) (to + ofs);
   int i;
   int l = ts->used * sizeof(ClParameter);
   if (l == 0)
      return 0;
   ts->max = ts->used;

   memcpy(tp, fp, l);
   ts->offset = -ofs;

   for (i = ts->used; i > 0; i--, fp++, tp++) {
      if (tp->qualifiers.used)
         l += copyQualifiers(ofs + l, max, to, &tp->qualifiers, from,
                             &fp->qualifiers);
   }                          

   return l;
}

static void freeParameters(ClObjectHdr * hdr, ClSection * s)
{
   ClParameter *p;
   int i, l;
   _SFCB_ENTER(TRACE_OBJECTIMPL, "freeParameters");

   p = (ClParameter *) ClObjectGetClSection(hdr, s);
   
   if (p) for (i = 0, l = p->qualifiers.used; i < l; i++)
         freeQualifiers(hdr, &p->qualifiers);
   if MALLOCED(s->offset) free((void *) s->offset);
   
   _SFCB_EXIT();
}

//-------------------------------------------------------
//-----
//-- method support
//-----
//-------------------------------------------------------

int ClClassLocateMethod(ClObjectHdr * hdr, ClSection * mths, const char *id)
{
   int i;
   ClMethod *m;

   if MALLOCED
      (mths->offset) m = (ClMethod *) mths->offset;
   else
      m = (ClMethod *) (((char *) hdr) + abs(mths->offset));

   for (i = 0; i < mths->used; i++) {
      if (strcasecmp(id, ClObjectGetClString(hdr, &(m + i)->id)) == 0)
         return i + 1;
   }
   return 0;
}

static int addClassMethodH(ClObjectHdr * hdr, ClSection * mths,
                              const char *id, CMPIType t)
{
   int i;
   ClMethod *m;

   _SFCB_ENTER(TRACE_OBJECTIMPL, "addClassMethodH");

   if ((i = ClClassLocateMethod(hdr, mths, id)) == 0) {
      m = (ClMethod *) ensureClSpace(hdr, mths, sizeof(*m), 8);
      m = m + (mths->used++);
      clearClSection(&m->qualifiers);
      m->id.id = addClString(hdr, id);
      m->quals = 0;
      m->originId=0;
      m->type = t;
      _SFCB_RETURN(mths->used);
   }

   else {
      if MALLOCED (mths->offset) m = (ClMethod *) mths->offset;
      else m = (ClMethod *) ((char*)hdr + abs(mths->offset));
      (m + i - 1)->type = t;
      _SFCB_RETURN(i);
   }
}

static long sizeMethods(ClObjectHdr * hdr, ClSection * s)
{
   int l;
   long sz = s->used * sizeof(ClMethod);
   ClMethod *m = (ClMethod *) ClObjectGetClSection(hdr, s);

   for (l = s->used; l > 0; l--, m++) {
      if (m->qualifiers.used)
         sz += sizeQualifiers(hdr, &m->qualifiers);
      if (m->parameters.used)
         sz += sizeParameters(hdr, &m->parameters);
   }      
   DEB(printf("--- sizeMethods: %lu-%p\n", sz, (void *) sz));
   return sz;
}

static long copyMethods(int ofs, int max, char *to, ClSection * ts,
                           ClObjectHdr * from, ClSection * fs)
{
   ClMethod *fp = (ClMethod *) ClObjectGetClSection(from, fs);
   ClMethod *tp = (ClMethod *) (to + ofs);
   int i;
   int l = ts->used * sizeof(ClMethod);
   if (l == 0)
      return 0;
   ts->max = ts->used;

   memcpy(tp, fp, l);
   ts->offset = -ofs;

   for (i = ts->used; i > 0; i--, fp++, tp++) {
      if (tp->qualifiers.used)
         l += copyQualifiers(ofs + l, max, to, &tp->qualifiers, from,
                             &fp->qualifiers);
      if (tp->parameters.used)
         l += copyParameters(ofs + l, max, to, &tp->parameters, from,
                             &fp->parameters);
   }                          

   return l;
}

static void freeMethods(ClObjectHdr * hdr, ClSection * s)
{
   ClMethod *m;
   int i, l;
   _SFCB_ENTER(TRACE_OBJECTIMPL, "freeMethods");

   m = (ClMethod *) ClObjectGetClSection(hdr, s);
   
   if (m) for (i = 0, l = m->qualifiers.used; i < l; i++)
         freeQualifiers(hdr, &m->qualifiers);
   if (m) for (i = 0, l = m->parameters.used; i < l; i++)
         freeParameters(hdr, &m->parameters);
   if MALLOCED(s->offset) free((void *) s->offset);
   
   _SFCB_EXIT();
}

int ClClassGetMethQualifierCount(ClClass * cls, int id)
{
   ClMethod *m;

   m = (ClMethod *) ClObjectGetClSection(&cls->hdr, &cls->methods);
   if (id < 0 || id > cls->methods.used) return 1;
   return (m + id)->qualifiers.used;
}

int ClClassGetMethParameterCount(ClClass * cls, int id)
{
   ClMethod *m;

   m = (ClMethod *) ClObjectGetClSection(&cls->hdr, &cls->methods);
   if (id < 0 || id > cls->methods.used) return 1;
   return (m + id)->parameters.used;
}

int ClClassGetMethParmQualifierCount(ClClass * cls, ClMethod *m, int id)
{
   ClParameter *p;

   p = (ClParameter *) ClObjectGetClSection(&cls->hdr, &m->parameters);
   if (id < 0 || id > p->qualifiers.used) return 1;
   return (p + id)->qualifiers.used;
}

int ClClassGetMethParamQualifierAt(ClClass * cls, ClParameter *parm, int id, CMPIData *data, char **name)
{
   ClQualifier *q;
   q = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &parm->qualifiers);
   if (id < 0 || id > parm->qualifiers.used) return 1;
   return ClGetQualifierAt(cls,q,id,data,name); 
}



//-------------------------------------------------------
//-----
//--  Class/Instance Property support
//-----
//-------------------------------------------------------

int ClObjectLocateProperty(ClObjectHdr * hdr, ClSection * prps, const char *id)
{
   int i;
   ClProperty *p;

   if MALLOCED
      (prps->offset) p = (ClProperty *) prps->offset;
   else
      p = (ClProperty *) (((char *) hdr) + abs(prps->offset));

   for (i = 0; i < prps->used; i++) {
      if (strcasecmp(id, ClObjectGetClString(hdr, &(p + i)->id)) == 0)
         return i + 1;
   }
   return 0;
}

static void freeProperties(ClObjectHdr * hdr, ClSection * s)
{
   ClProperty *p;
   int i, l;
   _SFCB_ENTER(TRACE_OBJECTIMPL, "freeProperties");

   p = (ClProperty *) ClObjectGetClSection(hdr, s);
   
   if (p) for (i = 0, l = p->qualifiers.used; i < l; i++)
         freeQualifiers(hdr, &p->qualifiers);
   if MALLOCED(s->offset) free((void *) s->offset);
   
   _SFCB_EXIT();
}

static char *addPropertyToString(stringControl * sc, ClObjectHdr * hdr,
                                 ClProperty * p)
{
   int l, i, sb = 2, o = sc->used;
   ClQualifier *q;
   char *array = NULL;

   q = (ClQualifier *) ClObjectGetClSection(hdr, &p->qualifiers);
   if ((l = p->qualifiers.used)) {
      for (i = 0; i < l; sb = 0, i++) {
         if (i == l - 1)
            sb |= 1;
         addQualifierToString(sc, hdr, q + i, sb);
      }
      cat2string(sc, "\n");
   }

   cat2string(sc, " ");
   cat2string(sc, datatypeToString(&p->data, &array));
   cat2string(sc, " ");
   cat2string(sc, ClObjectGetClString(hdr, &p->id));
   if (array)
      cat2string(sc, array);
   cat2string(sc, ";\n");
   return sc->str + o;
}

int instance2xml(CMPIInstance * ci, UtilStringBuffer * sb, unsigned int flags);

static long sizeProperties(ClObjectHdr * hdr, ClSection * s)
{
   int l;
   long sz = s->used * sizeof(ClProperty);
   ClProperty *p = (ClProperty *) ClObjectGetClSection(hdr, s);
   UtilStringBuffer *sb;

   for (l = s->used; l > 0; l--, p++) {
      if (hdr->type & HDR_Instance && p->quals & ClProperty_Q_EmbeddedObject) {
         if ((p->flags & ClProperty_EmbeddedObjectAsString)==0) {
            p->flags|=ClProperty_EmbeddedObjectAsString;
            sb = UtilFactory->newStrinBuffer(1024);
            instance2xml(p->data.value.inst,sb,0);
            p->data.value.dataPtr.length = addClString(hdr, sb->ft->getCharPtr(sb));
            sb->ft->release(sb);
         }
      }
      if (p->qualifiers.used)
         sz += sizeQualifiers(hdr, &p->qualifiers);
   }      
   DEB(printf("--- sizeProperties: %lu-%p\n", sz, (void *) sz));
   return sz;
}

static long copyProperties(int ofs, int max, char *to, ClSection * ts,
                           ClObjectHdr * from, ClSection * fs)
{
   ClProperty *fp = (ClProperty *) ClObjectGetClSection(from, fs);
   ClProperty *tp = (ClProperty *) (to + ofs);
   int i;
   int l = ts->used * sizeof(ClProperty);
   if (l == 0)
      return 0;
   ts->max = ts->used;

   memcpy(tp, fp, l);
   ts->offset = -ofs;

   for (i = ts->used; i > 0; i--, fp++, tp++)
      if (tp->qualifiers.used)
         l += copyQualifiers(ofs + l, max, to, &tp->qualifiers, from,
                             &fp->qualifiers);

   return l;
}


void showClHdr(void *ihdr)
{
   struct obj {
      ClObjectHdr *hdr;
   } *x;
   x = ihdr;
   printf("ClObjectHdr: %p->%p\n", ihdr, x->hdr);
   printf("\tsize:  %lu\n", x->hdr->size);
   printf("\ttype:  %d\n", x->hdr->type);
   printf("\tflags: %d\n", x->hdr->flags);
   printf("\tsbo:   %p-%d\n", (void *) x->hdr->strBufOffset,
          abs(x->hdr->strBufOffset));
   printf("\tabo:   %p-%d\n", (void *) x->hdr->arrayBufOffset,
          abs(x->hdr->arrayBufOffset));
}


static int addObjectPropertyH(ClObjectHdr * hdr, ClSection * prps,
                              const char *id, CMPIData d)
{
   int i;
   ClProperty *p;
   CMPIData od;
   CMPIStatus st;

   _SFCB_ENTER(TRACE_OBJECTIMPL, "addObjectPropertyH");

   if ((i = ClObjectLocateProperty(hdr, prps, id)) == 0) {
      p = (ClProperty *) ensureClSpace(hdr, prps, sizeof(*p), 8);
      p = p + (prps->used++);
      clearClSection(&p->qualifiers);
      p->id.id = addClString(hdr, id);
      p->quals = p->flags = 0;
      p->originId=0;
      
      if (d.type == CMPI_chars && (d.state & CMPI_nullValue) == 0) {
         p->data = d; 
         p->data.value.chars = (char *) addClString(hdr, d.value.chars);
      }
      else if (d.type == CMPI_string && (d.state & CMPI_nullValue) == 0) {
         p->data = d; 
         if (d.value.string) 
            p->data.value.chars = (char*)addClString(hdr, (char*)d.value.string->hdl);
         else p->data.value.chars=NULL;
         p->data.type=CMPI_chars;
      }
      else if (d.type == CMPI_dateTime && (d.state & CMPI_nullValue) == 0) {
         char chars[26];
         dateTime2chars(d.value.dateTime, NULL, chars);
         p->data = d;
         p->data.value.chars = (char *) addClString(hdr, chars);
      }
      else if (d.type == CMPI_ref && (d.state & CMPI_nullValue) == 0) {
         char str[4096] = { 0 };
         pathToChars(d.value.ref, &st, str);
         p->data = d;
         p->data.value.chars = (char *) addClString(hdr, str);
      }
      else if ((d.type & CMPI_ARRAY) && (d.state & CMPI_nullValue) == 0) {
         p->data = d;
         p->data.value.chars = (char *) addClArray(hdr, d);
      }
      else if (hdr->type == HDR_Instance && d.type == CMPI_instance && 
         (d.state & CMPI_nullValue) == 0) {
//      printf("added embedded object %d %s\n",getpid(),id);
         p->data = d;
         p->data.value.dataPtr.length=-1; 
         p->quals |= ClProperty_Q_EmbeddedObject;
         p ->flags &= ~ClProperty_EmbeddedObjectAsString;
         hdr->flags |= HDR_ContainsEmbeddedObject;
      }
      else p->data = d;
      _SFCB_RETURN(prps->used);
   }

   else {
      if MALLOCED (prps->offset) p = (ClProperty *) prps->offset;
      else p = (ClProperty *) ((char*)hdr + abs(prps->offset));
      od = (p + i - 1)->data;
      
      if (od.type == CMPI_chars && (d.state & CMPI_nullValue) == 0) {
      
         if ((p + i - 1)->quals && ClProperty_Q_EmbeddedObject) 
            _SFCB_RETURN(-CMPI_RC_ERR_TYPE_MISMATCH);
            
         if (d.type != CMPI_chars) {
            replaceClString(hdr, (int) od.value.chars, "");
            (p + i - 1)->data = d;
         }
         else if (od.value.chars) {
            (p + i - 1)->data = d;
            replaceClString(hdr, (int) od.value.chars, d.value.chars);
            (p + i - 1)->data.value.chars=od.value.chars;
         }
         else {
            (p + i - 1)->data = d;
            (p + i - 1)->data.value.chars =
                (char *) addClString(hdr, d.value.chars);
         }
      }
      
      else if (d.type == CMPI_dateTime && (d.state & CMPI_nullValue) == 0) {
         char chars[26];
         dateTime2chars(d.value.dateTime, NULL, chars);
         replaceClString(hdr, (int) od.value.chars, chars);
         (p + i - 1)->data.value.chars=od.value.chars;
      }

      else if (d.type == CMPI_ref && (d.state & CMPI_nullValue) == 0) {
         char chars[4096] = { 0 };
         pathToChars(d.value.ref, &st, chars);
         replaceClString(hdr, (int) od.value.chars, chars);
         (p + i - 1)->data.value.chars=od.value.chars;
      }
      
      else if ((od.type & CMPI_ARRAY) && (d.state & CMPI_nullValue) == 0) {
         (p + i - 1)->data = d;
         replaceClArray(hdr, (int) od.value.array, d);
      }
      
      else if (hdr->type == HDR_Instance && d.type == CMPI_instance && 
         (d.state & CMPI_nullValue) == 0) {
         if (((p + i - 1)->quals && ClProperty_Q_EmbeddedObject) == 0 ) 
            _SFCB_RETURN(-CMPI_RC_ERR_TYPE_MISMATCH);
         (p + i - 1)->flags &= ~ClProperty_EmbeddedObjectAsString;
         (p + i - 1)->data = d;
         hdr->flags |= HDR_ContainsEmbeddedObject;
         (p + i - 1)->data.value.dataPtr.length=-1; 
      }
      else(p + i - 1)->data = d;
      
      _SFCB_RETURN(i);
   }
}

static void ClObjectRelocateStringBuffer(ClObjectHdr * hdr, ClStrBuf * buf)
{
   if (buf == NULL) return;
   if MALLOCED(hdr->strBufOffset) buf = (ClStrBuf *) hdr->strBufOffset;
   else buf = (ClStrBuf *) ((char *) hdr + abs(hdr->strBufOffset));
   buf->sIndex = (long *) ((char *) hdr + abs(buf->iOffset));
}

static void ClObjectRelocateArrayBuffer(ClObjectHdr * hdr, ClArrayBuf * buf)
{
   if (buf == NULL) return;
   if MALLOCED(hdr->arrayBufOffset) buf = (ClArrayBuf *) hdr->arrayBufOffset;
   else buf = (ClArrayBuf *) ((char *) hdr + abs(hdr->arrayBufOffset));
   buf->aIndex = (long *) ((char *) hdr + abs(buf->iOffset));
}


//-------------------------------------------------------
//-----
//--  Class support
//-----
//-------------------------------------------------------


static ClClass *newClassH(ClObjectHdr * hdr, const char *cn, const char *pa)
{
   ClClass *cls = (ClClass *) malloc(sizeof(ClClass));
   if (hdr == NULL) hdr = &cls->hdr;
   
   memset(cls, 0, sizeof(ClClass));
   hdr->type = HDR_Class;
   if (cn) cls->name.id = addClString(hdr, cn);
   else cls->name = nls;
   
   cls->parents=0;
   if (pa) {
      cls->parent.id = addClString(hdr, pa);
      cls->parents=1;
   }   
   else cls->parent = nls;
   cls->quals = 0;
   cls->reserved=0;

   clearClSection(&cls->qualifiers);
   clearClSection(&cls->properties);
   clearClSection(&cls->methods);

   return cls;
}

unsigned char ClClassAddGrandParent(ClClass * cls, char *gp)
{
   long id;
   
   id=addClString(&cls->hdr, gp);
   cls->parents++;
   return (unsigned char)id;
} 

ClClass *ClClassNew(const char *cn, const char *pa)
{
   return newClassH(NULL, cn, pa);
}

static long sizeClassH(ClObjectHdr * hdr, ClClass * cls)
{
   long sz = sizeof(*cls);

   sz += sizeQualifiers(hdr, &cls->qualifiers);
   sz += sizeProperties(hdr, &cls->properties);
   sz += sizeMethods(hdr, &cls->methods);
   sz += sizeStringBuf(hdr);
   sz += sizeArrayBuf(hdr);

   return sz;
}

unsigned long ClSizeClass(ClClass * cls)
{
   return sizeClassH(&cls->hdr, cls);
}

static ClClass *rebuildClassH(ClObjectHdr * hdr, ClClass * cls, void *area)
{
   int ofs = sizeof(ClClass);
   int sz = ClSizeClass(cls);
   ClClass *nc = area ? (ClClass *) area : (ClClass *) malloc(sz);

   *nc = *cls;
   ofs += copyQualifiers(ofs, sz, (char *) nc, &nc->qualifiers, hdr,
                         &cls->qualifiers);
   ofs += copyProperties(ofs, sz, (char *) nc, &nc->properties, hdr,
                         &cls->properties);
   ofs += copyMethods(ofs, sz, (char*)nc, &nc->methods, hdr,
                         &cls->methods);
                         
   ofs += copyStringBuf(ofs, sz, &nc->hdr, hdr);
   ofs += copyArrayBuf(ofs, sz, &nc->hdr, hdr);
   
   //printArrayBuf(&nc->hdr);
   
   nc->hdr.size = sz;

   return nc;
}

ClClass *ClClassRebuildClass(ClClass * cls, void *area)
{
   return rebuildClassH(&cls->hdr, cls, area);
}

void ClClassRelocateClass(ClClass * cls)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClClassRelocateClass");
   ClObjectRelocateStringBuffer(&cls->hdr, (ClStrBuf *) cls->hdr.strBufOffset);
   ClObjectRelocateArrayBuffer(&cls->hdr,  (ClArrayBuf *) cls->hdr.arrayBufOffset);
   _SFCB_EXIT();
}

void ClClassFreeClass(ClClass * cls)
{
   if (cls->hdr.flags & HDR_Rebuild) {
      freeQualifiers(&cls->hdr, &cls->qualifiers);
      freeProperties(&cls->hdr, &cls->properties);
      freeMethods(&cls->hdr,&cls->methods);
      freeStringBuf(&cls->hdr);
      freeArrayBuf(&cls->hdr);
   }
   free(cls);
}

char *ClClassToString(ClClass * cls)
{
   stringControl sc = { NULL, 0, 32 };
   ClProperty *p;
   int sb = 2, i, l;
   ClQualifier *q;
   unsigned long quals;

   q = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &cls->qualifiers);
   quals = cls->quals;
   if ((l = cls->qualifiers.used)) {
      for (i = 0; i < l; sb = 0, i++) {
         if ((quals == 0) && (i == l - 1))
            sb |= 1;
         addQualifierToString(&sc, &cls->hdr, q + i, sb);
      }
      if (quals) {
         cat2string(&sc, "\n   ");
         if (quals & ClClass_Q_Abstract)
            cat2string(&sc, ",Abstract");
         if (quals & ClClass_Q_Association)
            cat2string(&sc, ",Association");
         if (quals & ClClass_Q_Indication)
            cat2string(&sc, ",Indication");
         if (quals & ClClass_Q_Deprecated)
            cat2string(&sc, ",Deprecated");
         cat2string(&sc, "]");
      }
      cat2string(&sc, "\n");
   }

   cat2string(&sc, "class ");
   cat2string(&sc, ClObjectGetClString(&cls->hdr, &cls->name));
   if (cls->parent.id) {
      cat2string(&sc, ":");
      cat2string(&sc, ClObjectGetClString(&cls->hdr, &cls->parent));
   }
   cat2string(&sc, " {\n");

   p = (ClProperty *) ClObjectGetClSection(&cls->hdr, &cls->properties);
   for (i = 0, l = cls->properties.used; i < l; i++) {
      addPropertyToString(&sc, &cls->hdr, p + i);
   }

   cat2string(&sc, "};\n");

   return sc.str;
}


//
//--- Class Properties
//

int ClClassAddProperty(ClClass * cls, const char *id, CMPIData d)
{
   ClSection *prps = &cls->properties;
   return addObjectPropertyH(&cls->hdr, prps, id, d);
}

int ClClassGetPropertyCount(ClClass * cls)
{
   return cls->properties.used;
}

int ClClassGetPropertyAt(ClClass * cls, int id, CMPIData * data, char **name,
                         unsigned long *quals)
{
   ClProperty *p;
   p = (ClProperty *) ClObjectGetClSection(&cls->hdr, &cls->properties);
   if (id < 0 || id > cls->properties.used)
      return 1;
   if (data) *data = (p + id)->data;
   if (name) *name = strdup(ClObjectGetClString(&cls->hdr, &(p + id)->id));
   if (quals) *quals = (p + id)->quals;
   
   if (data->state & CMPI_nullValue)  {
      data->value.uint64=0;
   }
   else if (data->type == CMPI_chars) {
      const char *str =
          ClObjectGetClString(&cls->hdr, (ClString *) & data->value.chars);
      data->value.string = native_new_CMPIString(str, NULL);
      data->type = CMPI_string;
   }
   else if (data->type == CMPI_dateTime) {
      const char *str =
         ClObjectGetClString(&cls->hdr, (ClString *) & data->value.chars);
      data->value.dateTime = native_new_CMPIDateTime_fromChars(str, NULL); 
   }
   else if (data->type & CMPI_ARRAY) {
      data->value.dataPtr.ptr = (void *) ClObjectGetClArray(&cls->hdr,
            (ClArray *) & data->value.array);
   }
   return 0;
}

int ClClassGetPropQualifierCount(ClClass * cls, int id)
{
   ClProperty *p;

   p = (ClProperty *) ClObjectGetClSection(&cls->hdr, &cls->properties);
   if (id < 0 || id > cls->properties.used) return 1;
   return (p + id)->qualifiers.used;
}

int ClClassGetPropQualifierAt(ClClass * cls, int id, int qid, CMPIData * data,
                              char **name)
{
   ClProperty *p;
   ClQualifier *q;
   
   p = (ClProperty *) ClObjectGetClSection(&cls->hdr, &cls->properties);
   if (id < 0 || id > cls->properties.used) return 1;
   p = p + id;

   q = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &p->qualifiers);
   if (qid < 0 || qid > p->qualifiers.used) return 1;
   return ClGetQualifierAt(cls,q,qid,data,name); 
}

//
//--- Class Methods
//

int ClClassAddMethod(ClClass * cls, const char *id, CMPIType t)
{
   ClSection *mths = &cls->methods;
   return addClassMethodH(&cls->hdr, mths, id, t);
}

int ClClassGetMethodCount(ClClass * cls)
{
   return cls->methods.used;
}

int ClClassGetMethodAt(ClClass * cls, int id, CMPIType * data, char **name,
                         unsigned long *quals)
{
   ClMethod *m;
   m = (ClMethod *) ClObjectGetClSection(&cls->hdr, &cls->methods);
   if (id < 0 || id > cls->methods.used) return 1;
   if (data) *data = (m + id)->type;
   if (name) *name = strdup(ClObjectGetClString(&cls->hdr, &(m + id)->id));
   if (quals) *quals = (m + id)->quals;
   return 0;
}

int ClClassGetMethQualifierAt(ClClass * cls, ClMethod *m, int qid, CMPIData * data,
                              char **name)
{
   ClQualifier *q;

   q = (ClQualifier *) ClObjectGetClSection(&cls->hdr, &m->qualifiers);
   if (qid < 0 || qid > m->qualifiers.used) return 1;
   return ClGetQualifierAt(cls,q,qid,data,name); 
}

int ClClassGetMethParameterAt(ClClass * cls, ClMethod *m, int pid, CMPIParameter * parm,
                              char **name)
{
   ClParameter *p;

   p = (ClParameter *) ClObjectGetClSection(&cls->hdr, &m->parameters);
   if (pid < 0 || pid > m->parameters.used) return 1;
   if (parm) *parm = (p + pid)->parameter;
   if (name) *name = strdup(ClObjectGetClString(&cls->hdr, &(p + pid)->id));
   return 0;
}



//-------------------------------------------------------
//-----
//--  Instance support
//-----
//-------------------------------------------------------

int isInstance(const CMPIInstance *ci) 
{
   ClInstance *inst = (ClInstance *) ci->hdl;
   if (inst->hdr.type == HDR_Instance) return 1;
   return 0;  
}

static ClInstance *newInstanceH(const char *ns, const char *cn)
{
   ClInstance *inst = (ClInstance *) malloc(sizeof(ClInstance));

   memset(inst, 0, sizeof(ClInstance));
   inst->hdr.type = HDR_Instance;
   if (ns) inst->nameSpace.id = addClString(&inst->hdr, ns);
   else inst->nameSpace = nls;
   if (cn) inst->className.id = addClString(&inst->hdr, cn);
   else inst->className = nls;
   inst->quals = 0;
   inst->parents=0;
   inst->reserved=0;
   inst->path = NULL;

   clearClSection(&inst->qualifiers);
   clearClSection(&inst->properties);

   return inst;
}

ClInstance *ClInstanceNew(const char *ns, const char *cn)
{
   return newInstanceH(ns, cn);
}

static long sizeInstanceH(ClObjectHdr * hdr, ClInstance * inst)
{
   long sz = sizeof(*inst);
   
   sz += sizeQualifiers(hdr, &inst->qualifiers);
   sz += sizeProperties(hdr, &inst->properties);
   sz += sizeStringBuf(hdr);
   sz += sizeArrayBuf(hdr);

   return sz;
}

unsigned long ClSizeInstance(ClInstance * inst)
{
   return sizeInstanceH(&inst->hdr, inst);
}

static ClInstance *rebuildInstanceH(ClObjectHdr * hdr, ClInstance * inst,
                                    void *area)
{
   int ofs = sizeof(ClInstance);
   int sz = ClSizeInstance(inst);
   ClInstance *ni = area ? (ClInstance *) area : (ClInstance *) malloc(sz);

   *ni = *inst;
   ofs += copyQualifiers(ofs, sz, (char *) ni, &ni->qualifiers, hdr,
                         &inst->qualifiers);
   ofs += copyProperties(ofs, sz, (char *) ni, &ni->properties, hdr,
                         &inst->properties);
   ofs += copyStringBuf(ofs, sz, &ni->hdr, hdr);
   ofs += copyArrayBuf(ofs, sz, &ni->hdr, hdr);
   ni->hdr.size = sz;

   return ni;
}

ClInstance *ClInstanceRebuild(ClInstance * inst, void *area)
{
   return rebuildInstanceH(&inst->hdr, inst, area);
}

void ClInstanceRelocateInstance(ClInstance * inst)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClInstanceRelocateInstance");
   ClObjectRelocateStringBuffer(&inst->hdr,(ClStrBuf*)inst->hdr.strBufOffset);
   ClObjectRelocateArrayBuffer(&inst->hdr,(ClArrayBuf*)inst->hdr.arrayBufOffset);
   _SFCB_EXIT();
}

void ClInstanceFree(ClInstance * inst)
{
   if (inst->hdr.flags & HDR_Rebuild) {
      freeQualifiers(&inst->hdr, &inst->qualifiers);
      freeProperties(&inst->hdr, &inst->properties);
      freeStringBuf(&inst->hdr);
      freeArrayBuf(&inst->hdr);
   }
   free(inst);
}

char *ClInstanceToString(ClInstance * inst)
{
   stringControl sc = { NULL, 0, 32 };
   ClProperty *p;
   int sb = 2, i, l;
   ClQualifier *q;

   q = (ClQualifier *) ClObjectGetClSection(&inst->hdr, &inst->qualifiers);
   if ((l = inst->qualifiers.used)) {
      for (i = 0; i < l; sb = 0, i++) {
         if (i == l - 1)
            sb |= 1;
         addQualifierToString(&sc, &inst->hdr, q + i, sb);
      }
      cat2string(&sc, "\n");
   }

   cat2string(&sc, "Instance of ");
   cat2string(&sc, ClObjectGetClString(&inst->hdr, &inst->className));
   cat2string(&sc, " {\n");

   p = (ClProperty *) ClObjectGetClSection(&inst->hdr, &inst->properties);
   for (i = 0, l = inst->properties.used; i < l; i++) {
      addPropertyToString(&sc, &inst->hdr, p + i);
   }

   cat2string(&sc, "};\n");

   return sc.str;
}

int ClInstanceGetPropertyCount(ClInstance * inst)
{
   ClProperty *p;
   p = (ClProperty *) ClObjectGetClSection(&inst->hdr, &inst->properties);
   return inst->properties.used;
}

int ClInstanceGetPropertyAt(ClInstance * inst, int id, CMPIData * data,
                            char **name, unsigned long *quals)
{
   ClProperty *p;
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClInstanceGetPropertyAt");

   p = (ClProperty *) ClObjectGetClSection(&inst->hdr, &inst->properties);
   if (id < 0 || id > inst->properties.used)
      _SFCB_RETURN(1);
   if (data) *data = (p + id)->data;
   if (name) *name = strdup(ClObjectGetClString(&inst->hdr, &(p + id)->id));
   if (quals) *quals = (p + id)->quals;
   if (data->type == CMPI_chars) {
      const char *str =
          ClObjectGetClString(&inst->hdr, (ClString *) & data->value.chars);
      data->value.string = native_new_CMPIString(str, NULL);
      data->type = CMPI_string;
   }
   if (data->type == CMPI_dateTime) {
      const char *str =
          ClObjectGetClString(&inst->hdr, (ClString *) & data->value.chars);
      data->value.dateTime = native_new_CMPIDateTime_fromChars(str, NULL);
   }
   if (data->type & CMPI_ARRAY) {
      data->value.dataPtr.ptr = (void *) ClObjectGetClArray(&inst->hdr,
            (ClArray *) & data->value.array);
   }
   _SFCB_RETURN(0);
}

int ClInstanceAddProperty(ClInstance * inst, const char *id, CMPIData d)
{
   ClSection *prps = &inst->properties;
   return addObjectPropertyH(&inst->hdr, prps, id, d);
}

const char *ClInstanceGetClassName(ClInstance * inst)
{
   return ClObjectGetClString(&inst->hdr, &inst->className);
}

const char *ClInstanceGetNameSpace(ClInstance * inst)
{
   return ClObjectGetClString(&inst->hdr, &inst->nameSpace);
}

const char *ClGetStringData(CMPIInstance * ci, int id)
{
   ClInstance *inst = (ClInstance *) ci->hdl;   
   ClString sid={id};
   return ClObjectGetClString(&inst->hdr, &sid);
}



//-------------------------------------------------------
//-----
//--  ObjectPath support
//-----
//-------------------------------------------------------

static ClObjectPath *newObjectPathH(const char *ns, const char *cn)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "newObjectPathH");

   ClObjectPath *op = (ClObjectPath *) malloc(sizeof(ClObjectPath));

   memset(op, 0, sizeof(ClObjectPath));
   op->hdr.type = HDR_ObjectPath;
   if (ns)
      op->nameSpace.id = addClString(&op->hdr, ns);
   else
      op->nameSpace = nls;
   if (cn)
      op->className.id = addClString(&op->hdr, cn);
   else
      op->className = nls;

   op->hostName = nls;

   clearClSection(&op->properties);

   _SFCB_RETURN(op);
}

ClObjectPath *ClObjectPathNew(const char *ns, const char *cn)
{
   return newObjectPathH(ns, cn);
}

static long sizeObjectPathH(ClObjectHdr * hdr, ClObjectPath * op)
{
   long sz = sizeof(*op);

   _SFCB_ENTER(TRACE_OBJECTIMPL, "sizeObjectPathH");

   sz += sizeProperties(hdr, &op->properties);
   sz += sizeStringBuf(hdr);

   _SFCB_RETURN(sz);
}

unsigned long ClSizeObjectPath(ClObjectPath * op)
{
   return sizeObjectPathH(&op->hdr, op);
}

static ClObjectPath *rebuildObjectPathH(ClObjectHdr * hdr, ClObjectPath * op,
                                        void *area)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "rebuildObjectPathH");

   int ofs = sizeof(ClObjectPath);
   int sz = ClSizeObjectPath(op);
   ClObjectPath *nop = area ? (ClObjectPath *) area :
       (ClObjectPath *) malloc(sz);

   *nop = *op;
   ofs += copyProperties(ofs, sz, (char *) nop, &nop->properties, hdr,
                         &op->properties);
   ofs += copyStringBuf(ofs, sz, &nop->hdr, hdr);
   nop->hdr.size = sz;

   _SFCB_RETURN(nop);
}

ClObjectPath *ClObjectPathRebuild(ClObjectPath * op, void *area)
{
   return rebuildObjectPathH(&op->hdr, op, area);
}

void ClObjectPathRelocateObjectPath(ClObjectPath * op)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClObjectPathRelocateObjectPath");
   ClObjectRelocateStringBuffer(&op->hdr, (ClStrBuf *) op->hdr.strBufOffset);
   ClObjectRelocateArrayBuffer(&op->hdr, (ClArrayBuf *) op->hdr.arrayBufOffset);
   _SFCB_EXIT();
}

void ClObjectPathFree(ClObjectPath * op)
{
   if (op->hdr.flags & HDR_Rebuild) {
      freeProperties(&op->hdr, &op->properties);
      freeStringBuf(&op->hdr);
   }
   free(op);
}

char *ClObjectPathToString(ClObjectPath * op)
{
   stringControl sc = { NULL, 0, 32 };
//    ClProperty *p;
//    int sb = 2, i, l;

   cat2string(&sc, "***ObjectPath not done yet***");
/*    cat2string(&sc, ClObjectGetClString(&op->hdr, &op->name));
    cat2string(&sc, " {\n");

    p = (ClProperty *) ClObjectGetClSection(&op->hdr, &op->properties);
    for (i = 0, l = op->properties.used; i < l; i++) {
        addPropertyToString(&sc, &op->hdr, p + i);
    }

    cat2string(&sc, "};\n");
*/
   return sc.str;
}

int ClObjectPathGetKeyCount(ClObjectPath * op)
{
   ClProperty *p;
   p = (ClProperty *) ClObjectGetClSection(&op->hdr, &op->properties);
   return op->properties.used;
}

int ClObjectPathGetKeyAt(ClObjectPath * op, int id, CMPIData * data,
                         char **name)
{
   ClProperty *p; 

   p = (ClProperty *) ClObjectGetClSection(&op->hdr, &op->properties);
   if (id < 0 || id > op->properties.used) return 1;
   if (data) *data = (p + id)->data;
   if (name) *name = (char*)ClObjectGetClString(&op->hdr, &(p + id)->id);
   if (data->type == CMPI_chars) {
      const char *str =
          ClObjectGetClString(&op->hdr, (ClString *) & data->value.chars);
      data->value.string = native_new_CMPIString(str, NULL);
      data->type = CMPI_string;
   }
   return 0;
}

int ClObjectPathAddKey(ClObjectPath * op, const char *id, CMPIData d)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClObjectPathAddKey");

   ClSection *prps = &op->properties;
   _SFCB_RETURN(addObjectPropertyH(&op->hdr, prps, id, d));
}

void ClObjectPathSetHostName(ClObjectPath * op, const char *hn)
{
   if (op->hostName.id)
      replaceClString(&op->hdr, op->hostName.id, hn);
   else if (hn)
      op->hostName.id = addClString(&op->hdr, hn);
   else
      op->hostName = nls;
}

const char *ClObjectPathGetHostName(ClObjectPath * op)
{
   return ClObjectGetClString(&op->hdr, &op->hostName);
}

void ClObjectPathSetNameSpace(ClObjectPath * op, const char *ns)
{
   if (op->nameSpace.id)
      replaceClString(&op->hdr, op->nameSpace.id, ns);
   else if (ns)
      op->nameSpace.id = addClString(&op->hdr, ns);
   else
      op->nameSpace = nls;
}

const char *ClObjectPathGetNameSpace(ClObjectPath * op)
{
   return ClObjectGetClString(&op->hdr, &op->nameSpace);
}

void ClObjectPathSetClassName(ClObjectPath * op, const char *cn)
{
   if (op->className.id)
      replaceClString(&op->hdr, op->className.id, cn);
   else if (cn)
      op->className.id = addClString(&op->hdr, cn);
   else
      op->className = nls;
}

const char *ClObjectPathGetClassName(ClObjectPath * op)
{
   return ClObjectGetClString(&op->hdr, &op->className);
}



//-------------------------------------------------------
//-----
//--  Args support
//-----
//-------------------------------------------------------

static ClArgs *newArgsH()
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "newArgsH");
   ClArgs *arg = (ClArgs *) malloc(sizeof(ClArgs));

   memset(arg, 0, sizeof(ClArgs));
   arg->hdr.type = HDR_Args;
   clearClSection(&arg->properties);

   _SFCB_RETURN(arg);
}

ClArgs *ClArgsNew()
{
   return newArgsH();
}

static long sizeArgsH(ClObjectHdr * hdr, ClArgs * arg)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "sizeArgsH");
   long sz = sizeof(*arg);

   sz += sizeProperties(hdr, &arg->properties);
   sz += sizeStringBuf(hdr);
   sz += sizeArrayBuf(hdr);

   _SFCB_RETURN(sz);
}

unsigned long ClSizeArgs(ClArgs * arg)
{
   return sizeArgsH(&arg->hdr, arg);
}

static ClArgs *rebuildArgsH(ClObjectHdr * hdr, ClArgs * arg, void *area)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "rebuildArgsH");
   int ofs = sizeof(ClArgs);
   int sz = ClSizeArgs(arg);
   ClArgs *nop = area ? (ClArgs *) area : (ClArgs *) malloc(sz);

   *nop = *arg;
   ofs += copyProperties(ofs, sz, (char *) nop, &nop->properties, hdr,
                         &arg->properties);
   ofs += copyStringBuf(ofs, sz, &nop->hdr, hdr);
   ofs += copyArrayBuf(ofs, sz, &nop->hdr, hdr);
   nop->hdr.size = sz;

   _SFCB_RETURN(nop);
}

ClArgs *ClArgsRebuild(ClArgs * arg, void *area)
{
   return rebuildArgsH(&arg->hdr, arg, area);
}

void ClArgsRelocateArgs(ClArgs * arg)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClArgsRelocateArgs");
   
   ClObjectRelocateStringBuffer(&arg->hdr,(ClStrBuf*)arg->hdr.strBufOffset);
   ClObjectRelocateArrayBuffer(&arg->hdr,(ClArrayBuf*)arg->hdr.arrayBufOffset);
   _SFCB_EXIT();
}

void ClArgsFree(ClArgs * arg)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClArgsFree");
   if (arg->hdr.flags & HDR_Rebuild) {
      freeProperties(&arg->hdr, &arg->properties);
      freeStringBuf(&arg->hdr);
      freeArrayBuf(&arg->hdr);
   }
   free(arg);
   _SFCB_EXIT();
}

int ClArgsGetArgCount(ClArgs * arg)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClArgsGetArgCount");
   ClProperty *p;
   p = (ClProperty *) ClObjectGetClSection(&arg->hdr, &arg->properties);
   _SFCB_RETURN(arg->properties.used);
}

int ClArgsGetArgAt(ClArgs * arg, int id, CMPIData * data, char **name)
{
   ClProperty *p;
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClArgsGetArgAt");

   p = (ClProperty *) ClObjectGetClSection(&arg->hdr, &arg->properties);
   if (id < 0 || id > arg->properties.used)
      return 1;
   if (data) *data = (p + id)->data;
   if (name) *name = strdup(ClObjectGetClString(&arg->hdr, &(p + id)->id));
   if (data->type == CMPI_chars) {
      const char *str = ClObjectGetClString(&arg->hdr, (ClString *) & data->value.chars);
      data->value.string = native_new_CMPIString(str, NULL);
      data->type = CMPI_string;
   }
   if (data->type == CMPI_dateTime) {
      const char *str =
          ClObjectGetClString(&arg->hdr, (ClString *) & data->value.chars);
      data->value.dateTime = native_new_CMPIDateTime_fromChars(str, NULL);
   }
   if (data->type & CMPI_ARRAY) {
      data->value.dataPtr.ptr = (void *) ClObjectGetClArray(&arg->hdr,
         (ClArray *) & data->value.array);
   }
   _SFCB_RETURN(0);
}

int ClArgsAddArg(ClArgs * arg, const char *id, CMPIData d)
{
   _SFCB_ENTER(TRACE_OBJECTIMPL, "ClArgsAddArg");
   ClSection *prps = &arg->properties;
   _SFCB_RETURN(addObjectPropertyH(&arg->hdr, prps, id, d));
}

void dumpClass(char *msg, CMPIConstClass *cls)
{
   ClClass *cl=(ClClass*)cls->hdl;
   char *buf=(char*)cl;
   printf("classDump: %s\n",msg); 
   printf("strBuf: %p arrayBuf %p\n",buf+abs(cl->hdr.strBufOffset),buf+abs(cl->hdr.arrayBufOffset));
   dump("strBuf",buf+abs(cl->hdr.strBufOffset),sizeof(ClStrBuf));
   dump("arrayBuf",buf+abs(cl->hdr.arrayBufOffset),sizeof(ClArrayBuf));
   dump(msg, buf, cl->hdr.size);
}

/*
char *ClArgToString(ClArg * cls)
{
    stringControl sc = { NULL, 0, 32 };
    ClProperty *p;
    int sb = 2, i, l;
    ClQualifier *q;

    cat2string(&sc, "CMPIArgs ");
    cat2string(&sc, ClObjectGetClString(&cls->hdr, &cls->name));
    cat2string(&sc, " {\n");

    p = (ClProperty *) ClObjectGetClSection(&cls->hdr, &cls->properties);
    for (i = 0, l = cls->properties.used; i < l; i++) {
        addPropertyToString(&sc, &cls->hdr, p + i);
    }

    cat2string(&sc, "};\n");

    return sc.str;
}
*/
//#define MAIN_TEST
#ifdef MAIN_TEST

extern CMPIArray *NewCMPIArray(CMPICount size, CMPIType type, CMPIStatus * rc);
extern CMPIArgs *NewCMPIArgs(CMPIStatus * rc);
extern CMPIInstance *NewCMPIInstance(CMPIObjectPath * cop, CMPIStatus * rc);
extern CMPIObjectPath *NewCMPIObjectPath(const char *nameSpace,
                                         const char *className,
                                         CMPIStatus * rc);
extern unsigned long getArgsSerializedSize(CMPIArgs * args);
extern unsigned long getInstanceSerializedSize(CMPIInstance * ci);

int main()
{
   int val = 37, s;
   //  SFCB_ASM("int $3");
   {
      CMPIObjectPath *cop = NewCMPIObjectPath("root", "myClass", NULL);
      CMPIInstance *inst = NewCMPIInstance(cop, NULL);
      CMPIArgs *arg = NewCMPIArgs(NULL);
      CMPIArray *ar = NewCMPIArray(1, CMPI_sint32, NULL);
      CMSetArrayElementAt(ar, 0, &val, CMPI_sint32);
      printf("CMPI_sint32A: %p\n", (void *) CMPI_sint32A);
      CMPIData ad = CMGetArrayElementAt(ar, 0, NULL);
      printf("ad.sin32: %d\n", ad.value.sint32);
      CMAddArg(arg, "test", &ar, CMPI_sint32A);
      CMPIData d = CMGetArg(arg, "test", NULL);
      ad = CMGetArrayElementAt(d.value.array, 0, NULL);
      printf("ad.sin32: %d\n", ad.value.sint32);
      s = getArgsSerializedSize(arg);

      CMPIArgs *narg = CMClone(arg, NULL);
      s = getArgsSerializedSize(arg);
   }
 
   {
      CMPIObjectPath *cop = NewCMPIObjectPath("root", "myClass", NULL);
      CMPIInstance *inst = NewCMPIInstance(cop, NULL);
      CMPIArray *ar = NewCMPIArray(1, CMPI_sint32, NULL);
      CMSetArrayElementAt(ar, 0, &val, CMPI_sint32);
      printf("CMPI_sint32A: %p\n", (void *) CMPI_sint32A);
      CMPIData ad = CMGetArrayElementAt(ar, 0, NULL);
      printf("ad.sin32: %d\n", ad.value.sint32);
      CMSetProperty(inst, "test", &ar, CMPI_sint32A);
      CMPIData d = CMGetProperty(inst, "test", NULL);
      ad = CMGetArrayElementAt(d.value.array, 0, NULL);
      printf("ad.sin32: %d\n", ad.value.sint32);
      s = getInstanceSerializedSize(inst);

      CMPIInstance *ninst = CMClone(inst, NULL);
      s = getInstanceSerializedSize(ninst);
   }

   return 0;
}
#endif
