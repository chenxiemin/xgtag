/*
 * =====================================================================================
 *
 *       Filename:  project_simple_opt.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  08/12/2015 09:44:05 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _PROJECT_SIMPLE_OPT_H_
#define _PROJECT_SIMPLE_OPT_H_

#include "project.h"

// TODO reconstruct
struct put_func_data {
	GTOP *gtop[GTAGLIM];
	const char *fid;
};

typedef struct
{
    struct ProjectContext super; // inherit super class
    struct put_func_data data;
} ProjectContextSimple;

ProjectContextSimple *project_simple_open(int type, const char *root,
        const char *db, WPATH_MODE_T mode);

void project_simple_close(ProjectContextSimple *pcontext);

// simple project operation functions
int project_simple_add(void *thiz, const char *file);
int project_simple_del_set(void *thiz, IDSET *delset);
int project_simple_select(void *thiz, const char *pattern,
        SEL_TYPE_T query, int db, POutput pout);

#endif

