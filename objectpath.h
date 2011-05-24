/*
 * $id$
 *
 * © Copyright IBM Corp. 2007
 *
 * THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
 * ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
 * CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
 *
 * You can obtain a current copy of the Eclipse Public License from
 * http://www.opensource.org/licenses/eclipse-1.0.php
 *
 * Author:        Sven Schuetz
 *
 * Description:
 *
 * Objectpath utility functions
 *
 */

#ifndef _OBJECTPATH_H
#define _OBJECTPATH_H

#include <sfcCommon/utilft.h>

UtilStringBuffer *normalizeObjectPathStrBuf(const CMPIObjectPath * cop);
char           *normalizeObjectPathChars(const CMPIObjectPath * cop);
char           *normalizeObjectPathCharsDup(const CMPIObjectPath * cop);
int             objectpathCompare(const CMPIObjectPath * cop1,
                                  const CMPIObjectPath * cop2);

#endif
/* MODELINES */
/* DO NOT EDIT BELOW THIS COMMENT */
/* Modelines are added by 'make pretty' */
/* -*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vi:set ts=2 sts=2 sw=2 expandtab: */
