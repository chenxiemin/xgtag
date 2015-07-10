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

PProjectContext project_open(int type, const char *root, const char *db)
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
            db, root, GTAGS, GTAGS_CREATE, openflags);
    pcontext->data.gtop[GTAGS]->flags = GTAGS_EXTRACTMETHOD;

    // open grtags
	pcontext->data.gtop[GRTAGS] = gtags_open(
            db, root, GRTAGS, GTAGS_CREATE, openflags);
	pcontext->data.gtop[GRTAGS]->flags = pcontext->data.gtop[GTAGS]->flags;

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

int project_add(PProjectContext pcontext, const char *file, const char *fid)
{
    if (NULL == pcontext || NULL == pcontext->parser || NULL == file ||
            NULL == fid) {
        LOGE("Invalid argument");
        return -1;
    }

    ProjectContextDefault *pdcontext = (ProjectContextDefault *)pcontext;

    pdcontext->data.fid = fid;
    int res = wparser_parse(pcontext->parser, file, project_parser_cb, pcontext);
    gtags_flush(pdcontext->data.gtop[GTAGS], fid);
    gtags_flush(pdcontext->data.gtop[GRTAGS], fid);

    return res;
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

