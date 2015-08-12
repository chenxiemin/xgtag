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
#include <sys/types.h>
#include <sys/stat.h>

#include "wpath.h"
#include "die.h"
#include "find.h"
#include "gpathop.h"
#include "strbuf.h"
#include "gparam.h"
#include "abs2rel.h"
#include "gtagsop.h"
#include "makepath.h"
#include "strbuf.h"
#include "assert.h"

struct WPath
{
    char db[MAXPATHLEN + 1];
    char root[MAXPATHLEN + 1];

    time_t modifyTime;

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

    // get modify time
	const char *path = makepath(db, dbname(GTAGS), NULL);
    if (NULL == path) {
        LOGE("Cannot make db path");
        return NULL;
    }

    // create wpath object
    PWPath pwpath = (PWPath)malloc(sizeof(struct WPath));
    memset(pwpath, 0, sizeof(struct WPath));

	struct stat statp;
	if (stat(path, &statp) < 0)
        pwpath->modifyTime = 0;
    else
        pwpath->modifyTime = statp.st_mtime;

	if (gpath_open(db, (int)mode) < 0)
		die("GPATH not found.");
    

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

const char *wpath_getID(PWPath pwpath, const char *src)
{
    if (NULL == pwpath || NULL == src) {
        LOGE("Invalid parameter");
        return NULL;
    }

    return gpath_path2fid(src, NULL);
}

int wpath_getIntID(PWPath pwpath, const char *src)
{
    const char *id = wpath_getID(pwpath, src);
    if (NULL == id)
        return -1;

    return atoi(id);
}

int wpath_nextID(PWPath pwpath)
{
    if (NULL == pwpath) {
        LOGE("Invalid parameter");
        return -1;
    }

    return gpath_nextkey();
}

int wpath_isExist(PWPath pwpath, const char *src)
{
    return NULL != wpath_getID(pwpath, src);
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
    return gpath_path2fid(pwpath->normalPath, NULL);
}

int wpath_isModified(PWPath pwpath, const char *src)
{
    if (NULL == pwpath || NULL == src) {
        LOGE("Invalid parameter");
        return 0;
    }

    struct stat statp;
    if (stat(src, &statp) < 0) {
        LOGE("Cannot stat src file: %s", src);
        return 0;
    }

    return pwpath->modifyTime < statp.st_mtime;
}

int wpath_deleteByID(PWPath pwpath, int id)
{
    if (NULL == pwpath || id < 1) {
        LOGE("Invalid parameter");
        return -1;
    }

    char fid[MAXFIDLEN];
    snprintf(fid, sizeof(fid), "%d", id);

    const char *path = gpath_fid2path(fid, NULL);
    if (NULL == path) {
        LOGE("Cannot find path by id: %s", fid);
        return -1;
    }
    // copy path
    STRBUF *pathBuf = strbuf_open(0);
    strbuf_puts(pathBuf, path);

    // delete
    gpath_delete(strbuf_value(pathBuf));

    // free buf
    strbuf_close(pathBuf);
    return 0;
}

const char *wpath_getDB(PWPath pwpath)
{
    assert(NULL != pwpath);
    return pwpath->db;
}

const char *wpath_getRoot(PWPath pwpath)
{
    assert(NULL != pwpath);
    return pwpath->root;
}

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

