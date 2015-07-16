/*
 * =====================================================================================
 *
 *       Filename:  wpath.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/14/2015 10:31:15 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "wpath.h"
#include "die.h"
#include "find.h"
#include "gpathop.h"
#include "strbuf.h"
#include "gparam.h"
#include "abs2rel.h"

struct WPath
{
    char db[MAXPATHLEN + 1];
    char root[MAXPATHLEN + 1];

    // for temporary use
    char realPath[MAXPATHLEN + 1];
    char normalPath[MAXPATHLEN + 1];
    char rootSlash[MAXPATHLEN + 1];
};

PWPath wpath_open(const char *db, const char *root, WPATH_MODE_T mode)
{
    if (NULL == db || NULL == root) {
        LOGE("Invalid parameter");
        return NULL;
    }
    if (strlen(db) > MAXPATHLEN || strlen(root) > MAXPATHLEN) {
        LOGE("path too long");
        return NULL;
    }

	if (gpath_open(db, (int)mode) < 0)
		die("GPATH not found.");
    
    PWPath pwpath = (PWPath)malloc(sizeof(struct WPath));
    memset(pwpath, 0, sizeof(struct WPath));

    strcpy(pwpath->db, db);
    strcpy(pwpath->root, root);
    sprintf(pwpath->rootSlash, "%s/", root);
    return pwpath;
}

void wpath_close(PWPath *ppwpath)
{
    if (NULL == ppwpath || NULL == *ppwpath)
        return;

	gpath_close();

    // cleanup context
    free(*ppwpath);
    *ppwpath = NULL;
}

int wpath_GetID(PWPath pwpath, const char *src, STRBUF *out)
{
    if (NULL == pwpath || NULL == src || NULL == out)
        return -1;

    const char *fid = gpath_path2fid(src, NULL);
    if (NULL == fid)
        return -1;

    strbuf_reset(out);
    strbuf_puts(out, src);
    return 0;
}

const char *wpath_put(PWPath pwpath, const char *src)
{
    // check whether path exist to follow symbol link
    char *res = realpath(src, pwpath->realPath);
    if (NULL == res) {
        LOGE("read link failed %s to %s", src, pwpath->realPath);
        return NULL;
    }
    res = normalize(pwpath->realPath, pwpath->rootSlash,
            pwpath->db, pwpath->normalPath, MAXPATHLEN);
    if (NULL == res) {
        LOGE("normailize path failed %s to %s", src, pwpath->realPath);
        return NULL;
    }
    // always use real path instead
    if (NULL != gpath_path2fid(pwpath->normalPath, NULL)) {
        LOGE("duplicated symbol link detected: %s %s", src, pwpath->normalPath);
        return NULL;
    }

    gpath_put(pwpath->normalPath, GPATH_SOURCE);
#if 1
    return gpath_path2fid(pwpath->normalPath, NULL);
#else
    return 0;
#endif
}

#if 0
const char *wpath_getID(PWPath pwpath, const char *src)
{
    if (NULL = pwpath || NULL == src) {
        LOGE("Invalid parameter");
        return -1;
    }

    return gpath_path2fid(pwpath->normalPath, NULL);
}
#endif

#if 0
int wpath_exist(PWPath pwpath, const char *src)
{
    if (NULL == pwpath || NULL == src) {
        LOGE("Invalid parameter");
        return 0;
    }

    // check whether path exist to follow symbol link
    char *res = realpath(src, pwpath->realPath);
    if (NULL == res) {
        LOGE("read link failed %s to %s", src, pwpath->realPath);
        return 0;
    }
    res = normalize(pwpath->realPath, pwpath->rootSlash,
            pwpath->db, pwpath->normalPath, MAXPATHLEN);
    if (NULL == res) {
        LOGE("normailize path failed %s to %s", src, pwpath->realPath);
        return 0;
    }
    // always use real path instead
    if (NULL != gpath_path2fid(pwpath->normalPath, NULL)) {
        LOGE("duplicated symbol link detected: %s %s", src, pwpath->normalPath);
        return 1;
    }

    return 0;
}
#endif

WPATH_SOURCE_TYPE_T wpath_getSourceType(const char *src)
{
    if (NULL == src)
        return WPATH_SOURCE_TYPE_IGNORE;
    if (skipthisfile(src))
        return WPATH_SOURCE_TYPE_IGNORE;
    if (test("b", src))
        return WPATH_SOURCE_TYPE_IGNORE;
    if (*src == ' ')
        return WPATH_SOURCE_TYPE_IGNORE;

    if (issourcefile(src))
        return WPATH_SOURCE_TYPE_SOURCE;
    else
        return WPATH_SOURCE_TYPE_OTHER;
}

