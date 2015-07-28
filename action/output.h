/*
 * =====================================================================================
 *
 *       Filename:  output.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/28/2015 11:58:22 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _CXM_OUTPUT_H_
#define _CXM_OUTPUT_H_

#include <stdio.h>
#include "pathconvert.h"

typedef void (*output_path)(void *thiz, const char *path);
typedef void (*output_tag)(void *thiz, const char *tagname,
        const char *path, int lineno, const char *image, const char *fid);

typedef struct Output {
    output_path outPath;
    output_tag outTag;

    CONVERT *cv;
} *POutput;

POutput output_open(int type, int format, const char *root, const char *cwd,
    const char *dbpath, FILE *op, int db);

void output_close(POutput *ppout);

void output_put_path(POutput pout, const char *path);
void output_put_tag(POutput pout, const char *tagname,
        const char *path, int lineno, const char *image, const char *fid);

#endif

