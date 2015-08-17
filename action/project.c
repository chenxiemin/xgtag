/*
 * =====================================================================================
 *
 *       Filename:  project.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/10/2015 10:21:17 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */
#include <memory.h>
#include <stdlib.h>
#include <assert.h>

#include "project.h"
#include "die.h"
#include "opt.h"
#include "project_simple_opt.h"

PProjectContext project_open(int type, const char *root,
        const char *db, WPATH_MODE_T mode)
{
    return (PProjectContext)project_simple_open(type, root, db, mode);
}

void project_close(PProjectContext *ppcontext)
{
    if (NULL == ppcontext)
        return;

    project_simple_close((ProjectContextSimple *)*ppcontext);
    *ppcontext = NULL;
}

int project_add(PProjectContext pcontext, const char *file)
{
    if (NULL == pcontext || NULL == pcontext->add || NULL == file) {
        LOGE("Invalid argument");
        return -1;
    }

    return pcontext->add(pcontext, file);
}

int project_del(PProjectContext pcontext, const char *src)
{
    if (NULL == pcontext || NULL == src || NULL == pcontext->path) {
        LOGE("Invalid parameter");
        return -1;
    }

    if (NULL != pcontext->del) {
        return pcontext->del(pcontext, src);
    } else {
        // make delete set
        IDSET *delset = idset_open(wpath_nextID(pcontext->path));
        int id = wpath_getIntID(pcontext->path, src);
        if (id < 1) {
            LOGE("Cannot get id by src: %s", src);
            return -1;
        }
        idset_add(delset, id);

        int res = project_del_set(pcontext, delset);
        if (0 != res)
            LOGE("Cannot delete file set %d: %d", id, res);

        idset_close(delset);

        return res;
    }
}

int project_del_set(PProjectContext pcontext, IDSET *deleteFileIDSet)
{
    if (NULL == pcontext || NULL == pcontext->path
            || NULL == pcontext->delset || NULL == deleteFileIDSet ) {
        LOGE("Invalid parameter");
        return -1;
    }

    return pcontext->delset(pcontext, deleteFileIDSet);
}

int project_update(PProjectContext pcontext, const char *src)
{
    if (NULL == pcontext || NULL == src || NULL == pcontext->path) {
        LOGE("Invalid argument");
        return -1;
    }

    if (NULL != pcontext->upd) {
        return pcontext->upd(pcontext, src);
    } else {
        // default implementation
        if (wpath_isExist(pcontext->path, src)) {
            // delete file from project if exist
            int res = project_del(pcontext, src);
            if (0 != res) {
                LOGE("Cannot delete file %s: %d", src, res);
                return -1;
            }
        }

        // add source file
        return project_add(pcontext, src);
    }
}

int project_select(PProjectContext pcontext, const char *pattern,
        SEL_TYPE_T query, int db, POutput pout)
{
    if (NULL == pcontext || NULL == pout || NULL == pattern ||
            (GTAGS != db && GRTAGS != db)) {
        LOGE("Invalid argument");
        return -1;
    }

    return pcontext->sel(pcontext, pattern, query, db, pout);
}

