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

typedef struct
{
    struct ProjectContext super; // inherit super class

    struct put_func_data data;
} ProjectContextDefault;

static void project_parser_cb(int type, const char *tag,
        int lno, const char *path, const char *line_image, void *arg);

// simple project operation functions
int project_simple_add(void *thiz, const char *file);
int project_simple_del_set(void *thiz, IDSET *delset);

PProjectContext project_open(int type, const char *root,
        const char *db, WPATH_MODE_T mode)
{
    ProjectContextDefault *pcontext = (ProjectContextDefault *)
        malloc(sizeof(ProjectContextDefault));
    if (NULL == pcontext) {
        LOGE("Cannot create project context");
        return NULL;
    }
    memset(pcontext, 0, sizeof(ProjectContextDefault));
    LOGD("open project at root %s db %s: %p", root, db, pcontext);

	int openflags = GlobalOptions.cflag ? GTAGS_COMPACT : 0;
    // open  gtags
	pcontext->data.gtop[GTAGS] = gtags_open(
            db, root, GTAGS, (int)mode, openflags);
    pcontext->data.gtop[GTAGS]->flags = GTAGS_EXTRACTMETHOD;

    // open grtags
	pcontext->data.gtop[GRTAGS] = gtags_open(
            db, root, GRTAGS, (int)mode, openflags);
	pcontext->data.gtop[GRTAGS]->flags = pcontext->data.gtop[GTAGS]->flags;

    // fill operation
    pcontext->super.add = project_simple_add;
    pcontext->super.delset = project_simple_del_set;

    return (PProjectContext)pcontext;
}

void project_close(PProjectContext *ppcontext)
{
    if (NULL == ppcontext || NULL == *ppcontext)
        return;

    LOGD("close project at: %p", *ppcontext);
    ProjectContextDefault *pdcontext = (ProjectContextDefault *)*ppcontext;

    // close tags
	gtags_close(pdcontext->data.gtop[GTAGS]);
	gtags_close(pdcontext->data.gtop[GRTAGS]);

    // free context
    free(*ppcontext);
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

static void project_parser_cb(int type, const char *tag,
        int lno, const char *path, const char *line_image, void *arg)
{
    assert(NULL != tag && NULL != path && NULL != arg);

    ProjectContextDefault *pcontext = (ProjectContextDefault *)arg;

	const struct put_func_data *pdata = &(pcontext->data);
	GTOP *gtop;

	switch (type) {
	case PARSER_DEF:
		gtop = pdata->gtop[GTAGS];
		break;
	case PARSER_REF_SYM:
		gtop = pdata->gtop[GRTAGS];
		if (gtop == NULL)
			return;
		break;
	default:
		return;
	}
	gtags_put_using(gtop, tag, lno, pdata->fid, line_image);
}

int project_simple_add(void *thiz, const char *file)
{
    ProjectContextDefault *pdcontext = (ProjectContextDefault *)thiz;

    if (NULL == pdcontext->super.parser || NULL == pdcontext->super.path ||
            NULL == file) {
        LOGE("Invalid argument");
        return -1;
    }

    const char *fid = wpath_put(pdcontext->super.path, file);
    if (NULL == fid) {
        LOGE("Cannot put path into project: %s", file);
        return -1;
    }
    pdcontext->data.fid = fid;
    int res = wparser_parse(pdcontext->super.parser,
            file, project_parser_cb, pdcontext);
    gtags_flush(pdcontext->data.gtop[GTAGS], fid);
    gtags_flush(pdcontext->data.gtop[GRTAGS], fid);

    return res;
}

int project_simple_del_set(void *thiz, IDSET *delset)
{
    ProjectContextDefault *pdcontext = (ProjectContextDefault *)thiz;

    // get GTAGS and GRTAGS associate with fid in delset
    gtags_delete(pdcontext->data.gtop[GTAGS], delset);
    gtags_delete(pdcontext->data.gtop[GRTAGS], delset);

    // delete files in GPATH
    int i = END_OF_ID;
    for (i = idset_first(delset); i != END_OF_ID; i = idset_next(delset)) {
        int res = wpath_deleteByID(pdcontext->super.path, i);
        if (0 != res)
            LOGE("Cannot delete file id: %d", i);
    }
    
    return 0;
}

