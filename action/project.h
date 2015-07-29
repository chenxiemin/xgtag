/*
 * =====================================================================================
 *
 *       Filename:  project.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/10/2015 10:18:52 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _PROJECT_H_
#define _PROJECT_H_

#include "strbuf.h"
#include "wparser.h"
#include "gtagsop.h"
#include "idset.h"
#include "wpath.h"
#include "output.h"

typedef enum
{
    SEL_TYPE_UNKNOWN
} SEL_TYPE_T;

// TODO reconstruct
struct put_func_data {
	GTOP *gtop[GTAGLIM];
	const char *fid;
};

typedef int (*add_project)(void *thiz, const char *file);
typedef int (*del_set_project)(void *thiz, IDSET *deleteFileIDSet);
typedef int (*upd_project)(void *thiz, const char *src);
typedef int (*sel_project)(void *thiz, const char *pattern,
        SEL_TYPE_T query, GTOP *gtop, POutput pout);

// a project can treate as a folder which contains GTAGS/GPATH/...
typedef struct ProjectContext
{
    add_project add; // add a file in to project
    add_project del; // delete a file from project
    del_set_project delset; // delete file list from project
    upd_project upd; // update a file from project
    sel_project sel; // query result from project

    PParser parser; // set before use project_add and so on
    PWPath path;
} *PProjectContext;

PProjectContext project_open(int type, const char *root,
        const char *db, WPATH_MODE_T mode);

void project_close(PProjectContext *pcontext);

// add a file into a project
int project_add(PProjectContext pcontext, const char *file);

// delete a file from project
int project_del(PProjectContext pcontext, const char *src);

// delete files list in deleteFileIDSet
int project_del_set(PProjectContext pcontext, IDSET *deleteFileIDSet);

// update one source file in project
int project_update(PProjectContext pcontext, const char *src);

int project_select(PProjectContext pcontext, const char *pattern,
        SEL_TYPE_T query, int db, POutput pout);

#endif

