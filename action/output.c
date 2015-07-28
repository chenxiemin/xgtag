/*
 * =====================================================================================
 *
 *       Filename:  output.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/28/2015 11:58:58 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#include <stdlib.h>

#include "output.h"
#include "die.h"
#include "opt.h"


void output_convert_path(void *thiz, const char *path);
void output_convert_tag(void *thiz, const char *tagname,
        const char *path, int lineno, const char *image, const char *fid);

POutput output_open(int type, int format, const char *root, const char *cwd,
    const char *dbpath, FILE *op, int db)
{
	CONVERT *cv = convert_open(type, O.s.format, root, cwd, dbpath, stdout, db);
    if (NULL == cv) {
        LOGE("Cannot open convert");
        return NULL;
    }

    POutput pcontext = (POutput)malloc(sizeof(struct Output));
    if (NULL == pcontext) {
        LOGE("Cannot malloc output");
        convert_close(cv);
        return NULL;
    }

    pcontext->cv = cv;
    pcontext->outPath = output_convert_path;
    pcontext->outTag = output_convert_tag;

    return pcontext;
}

void output_close(POutput *ppout)
{
    if (NULL == ppout || NULL == *ppout)
        return;

    if (NULL != (*ppout)->cv)
        convert_close((*ppout)->cv);
    free(*ppout);
    *ppout = NULL;
}

void output_put_path(POutput pout, const char *path)
{
    if (NULL == pout || NULL == path) {
        LOGE("Invalid argument");
        return;
    }

    pout->outPath(pout, path);
}

void output_put_tag(POutput pout, const char *tagname,
        const char *path, int lineno, const char *image, const char *fid)
{
    if (NULL == pout || NULL == tagname) {
        LOGE("Invalid argument");
        return;
    }

    pout->outTag(pout, tagname, path, lineno, image, fid);
}

void output_convert_path(void *thiz, const char *path)
{
    CONVERT *cv = ((POutput)thiz)->cv;
    convert_put_path(cv, path);
}

void output_convert_tag(void *thiz, const char *tagname,
        const char *path, int lineno, const char *image, const char *fid)
{
    CONVERT *cv = ((POutput)thiz)->cv;
    convert_put_using(cv, tagname, path, lineno, image, fid);
}

