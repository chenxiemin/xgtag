/*
 * =====================================================================================
 *
 *       Filename:  wpath.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/14/2015 10:30:56 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _WPATH_H_
#define _WPATH_H_

#include "strbuf.h"

typedef struct WPath *PWPath;

typedef enum
{
    WPATH_MODE_READ,
    WPATH_MODE_CREATE,
    WPATH_MODE_MODIFY
} WPATH_MODE_T;

typedef enum
{
    WPATH_SOURCE_TYPE_IGNORE,
    WPATH_SOURCE_TYPE_SOURCE,
    WPATH_SOURCE_TYPE_OTHER
} WPATH_SOURCE_TYPE_T;

PWPath wpath_open(const char *db, const char *root, WPATH_MODE_T mode);

void wpath_close(PWPath *ppwpath);

int wpath_GetID(PWPath pwpath, const char *src, STRBUF *out);

// put source file into path
// return fid for successful, return NULL for failed
const char *wpath_put(PWPath pwpath, const char *src);

#if 0
// get file id from source
const char *wpath_getID(PWPath pwpath, const char *src);
#endif

#if 0
int wpath_exist(PWPath pwpath, const char *src);
#endif

WPATH_SOURCE_TYPE_T wpath_getSourceType(const char *src);

#endif

