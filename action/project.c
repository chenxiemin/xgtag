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
#include "format.h"
#include "pathconvert.h"
#include "locatestring.h"
#include "compress.h"
#include "gpathop.h"

#define SORT_FILTER     1

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
int project_simple_select(void *thiz, const char *pattern,
        SEL_TYPE_T query, GTOP *gtop, POutput pout);

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

int project_select(PProjectContext pcontext, const char *pattern,
        SEL_TYPE_T query, int db, POutput pout)
{
    if (NULL == pcontext || NULL == pout || NULL == pattern ||
            (GTAGS != db && GRTAGS != db)) {
        LOGE("Invalid argument");
        return -1;
    }
    ProjectContextDefault *pdc = (ProjectContextDefault *)pcontext;
    if (NULL == pdc->data.gtop[db]) {
        LOGE("Invalid parameter");
        return -2;
    }

    return pcontext->sel(pcontext, pattern, query, pdc->data.gtop[db], pout);
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
    if (O.s.format == FORMAT_PATH)
        flags |= GTOP_PATH;
    if (gtop->format & GTAGS_COMPACT)
        ib = strbuf_open(0);

    // iterator result
	GTP *gtp = NULL;
    for (gtp = gtags_first(gtop, pattern, flags); gtp; gtp = gtags_next(gtop)) {
        if (O.s.lflag && !locatestring(
                    gtp->path, O.s.localprefix, MATCH_AT_FIRST))
            continue;
        if (O.s.format == FORMAT_PATH) {
            output_put_path(pout, gtp->path);
        } else {
            // Standard format:   a          b         c
            // tagline = <file id> <tag name> <line no> <line image>
            char *p = (char *)gtp->tagline;
            char namebuf[IDENTLEN];
            const char *fid, *tagname, *image;

            fid = p;
            while (*p != ' ')
                p++;
            *p++ = '\0';			/* a */
            tagname = p;
            while (*p != ' ')
                p++;
            *p++ = '\0';			/* b */
            if (gtop->format & GTAGS_COMPNAME) {
                strlimcpy(namebuf, uncompress(tagname, gtp->tag),
                        (int)sizeof(namebuf));
                tagname = namebuf;
            }
            if (O.s.nosource) {
                image = " ";
            } else {
                while (*p != ' ')
                    p++;
                image = p + 1;		/* c + 1 */
                if (gtop->format & GTAGS_COMPRESS)
                    image = uncompress(image, gtp->tag);
            }
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
    ProjectContextDefault *pdcontext = (ProjectContextDefault *)thiz;

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
        SEL_TYPE_T query, GTOP *gtop, POutput pout)
{
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

