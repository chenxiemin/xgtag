/*
 * =====================================================================================
 *
 *       Filename:  project_simple_opt.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  08/12/2015 09:44:31 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#include <assert.h>
#include <stdlib.h>

#include "project_simple_opt.h"
#include "opt.h"
#include "pathconvert.h"
#include "locatestring.h"
#include "compress.h"
#include "gpathop.h"
#include "die.h"
#include "format.h"

#define SORT_FILTER     1

ProjectContextSimple *project_simple_open(int type, const char *root,
        const char *db, WPATH_MODE_T mode)
{
    ProjectContextSimple *pcontext = (ProjectContextSimple *)
        malloc(sizeof(ProjectContextSimple));
    if (NULL == pcontext) {
        LOGE("Cannot create project context");
        return NULL;
    }
    memset(pcontext, 0, sizeof(ProjectContextSimple));
    LOGD("open project at root %s db %s: %p", root, db, pcontext);

	int openflags = O.c.cflag ? GTAGS_COMPACT : 0;
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
    pcontext->super.sel = project_simple_select;

    return pcontext;
}

void project_simple_close(ProjectContextSimple *pcontext)
{
    if (NULL == pcontext)
        return;
    LOGD("close project at: %p", pcontext);

    // close tags
	gtags_close(pcontext->data.gtop[GTAGS]);
	gtags_close(pcontext->data.gtop[GRTAGS]);

    // free context
    free(pcontext);
}

static void project_parser_cb(int type, const char *tag,
        int lno, const char *path, const char *line_image, void *arg)
{
    assert(NULL != tag && NULL != path && NULL != arg);

    ProjectContextSimple *pcontext = (ProjectContextSimple *)arg;

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
    ProjectContextSimple *pdcontext = (ProjectContextSimple *)thiz;

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
    ProjectContextSimple *pdcontext = (ProjectContextSimple *)thiz;

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

int project_simple_select_define(void *thiz, const char *pattern,
        GTOP *gtop, POutput pout)
{
    // search through tag file.
    // set search flag
	int flags = 0;
	STRBUF *sb = NULL;
    STRBUF *ib = NULL;
    if (O.s.nofilter & SORT_FILTER)
        flags |= GTOP_NOSORT;
    if (O.s.iflag) {
        if (!isregex(pattern)) {
            sb = strbuf_open(0);
            strbuf_putc(sb, '^');
            strbuf_puts(sb, pattern);
            strbuf_putc(sb, '$');
            pattern = strbuf_value(sb);
        }
        flags |= GTOP_IGNORECASE;
    }
    if (O.s.Gflag)
        flags |= GTOP_BASICREGEX;
    if (gtop->format & GTAGS_COMPACT)
        ib = strbuf_open(0);

    // iterator result
	GTP *gtp = NULL;
    for (gtp = gtags_first(gtop, pattern, flags); gtp; gtp = gtags_next(gtop))
    {
        // if do not under current folder
        if (O.s.lflag && !locatestring(gtp->path,
                    O.s.localprefix, MATCH_AT_FIRST))
            continue;

        // select path
        if (O.s.format == FORMAT_PATH) {
            output_put_path(pout, gtp->path);
            continue;
        }

        // Standard format:   a          b         c
        // tagline = <file id> <tag name> <line no> <line image>
        char *p = (char *)gtp->tagline;
        char namebuf[IDENTLEN];
        
        // get fid
        const char *fid = NULL;
        fid = p;
        while (*p != ' ')
            p++;
        *p++ = '\0';			/* a */

        // get tag name
        const char *tagname = NULL;
        tagname = p;
        while (*p != ' ')
            p++;
        *p++ = '\0';			/* b */
        if (gtop->format & GTAGS_COMPNAME) {
            strlimcpy(namebuf, uncompress(tagname, gtp->tag),
                    (int)sizeof(namebuf));
            tagname = namebuf;
        }

        // get lineno
        char *lno = p;
        while (' ' != *p && '\0' != *p)
            p++;
        *p++ = '\0';

        // get image
        const char *image;
        if ('\0' == *p) { // no tag image
            image = tagname;
        } else {
            image = p + 1;		/* c + 1 */
            if (gtop->format & GTAGS_COMPRESS)
                image = uncompress(image, gtp->tag);
        }

        // select tag
        if (GRTAGS == gtop->db) {
            int lnobase = 0;
            p = lno;
            while ('\0' != *p) {
                if (',' == *p)
                    *p = '\0';
                if ('\0' == *p) {
                    lnobase = atoi(lno) + lnobase;
                    output_put_tag(pout, tagname, gtp->path,
                            lnobase, image, fid);
                    lno = p + 1;
                }

                p++;
            }

            lnobase = atoi(lno) + lnobase;
            output_put_tag(pout, tagname, gtp->path, lnobase, image, fid);
        } else {
            output_put_tag(pout, tagname, gtp->path, gtp->lineno, image, fid);
        }
    }

    // cleanup
	if (sb)
		strbuf_close(sb);
	if (ib)
		strbuf_close(ib);
    return 0;
}

int project_simple_select_path(void *thiz, const char *pattern,
        GTOP *gtop, POutput pout)
{
    ProjectContextSimple *pdcontext = (ProjectContextSimple *)thiz;

    // compile regex
    regex_t preg;
    if (pattern) {
        int flags = 0;

        if (!O.s.Gflag)
            flags |= REG_EXTENDED;
        if (O.s.iflag || getconfb("icase_path"))
            flags |= REG_ICASE;
#ifdef _WIN32
        flags |= REG_ICASE;
#endif /* _WIN32 */

        // We assume '^aaa' as '^/aaa'.
        if (regcomp(&preg, pattern, flags) != 0)
            die("invalid regular expression.");
    }
    if (!O.s.localprefix)
        O.s.localprefix = "./";

	GFIND *gp = gfind_open(wpath_getDB(pdcontext->super.path),
            O.s.localprefix, GPATH_SOURCE);
    int lplen = strlen(O.s.localprefix);
    const char *path = NULL;
	while ((path = gfind_read(gp)) != NULL) {
		if (pattern) {
            // skip localprefix because end-user doesn't see it.
			int result = regexec(&preg, path + lplen, 0, 0, 0);

            if (0 != result)
                continue;
		}

		if (O.s.format == FORMAT_PATH)
            output_put_path(pout, path);
		else
            output_put_tag(pout, "path", path, 1, " ", gp->dbop->lastdat);
	}

	gfind_close(gp);
	if (pattern)
		regfree(&preg);
}

int project_simple_select(void *thiz, const char *pattern,
        SEL_TYPE_T query, int db, POutput pout)
{
    ProjectContextSimple *pdc = (ProjectContextSimple *)thiz;
    if (NULL == pdc->data.gtop[db]) {
        LOGE("Invalid parameter");
        return -2;
    }

    GTOP *gtop = pdc->data.gtop[db];
    switch (query)
    {
    case SEL_TYPE_DEFINE:
        return project_simple_select_define(thiz, pattern, gtop, pout);
        break;
    case SEL_TYPE_PATH:
        return project_simple_select_path(thiz, pattern, gtop, pout);
        break;
    default:
        LOGE("Unknwon select type: %d", query);
        return -1;
    }
}

