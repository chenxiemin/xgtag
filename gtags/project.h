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

typedef enum
{
    SEL_TYPE_UNKNOWN
} SEL_TYPE_T;

// TODO reconstruct
struct put_func_data {
	GTOP *gtop[GTAGLIM];
	const char *fid;
};

typedef int (*file_project)(STRBUF *file);
typedef int (*sel_project)(SEL_TYPE_T query, void *res);

// a project can treate as a folder which contains GTAGS/GPATH/...
typedef struct ProjectContext
{
    file_project add; // add a file in to project
    file_project del; // delete a file from project
    sel_project sel; // query result from project
    file_project upd; // update a file from project

    PParser parser; // set before use project_add and so on
} *PProjectContext;

PProjectContext project_open(int type, const char *root, const char *db);

void project_close(PProjectContext *pcontext);

// add a file into a project
int project_add(PProjectContext pcontext, const char *file, const char *fid);

#endif

