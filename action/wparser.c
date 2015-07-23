/*
 * =====================================================================================
 *
 *       Filename:  wparser.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/10/2015 10:57:01 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#include <stdlib.h>

#include "wparser.h"
#include "strbuf.h"
#include "die.h"

PParser wparser_open()
{
    char *langmap = NULL;
    char *gtags_parser = NULL;

    // get lang map
	STRBUF *langMap = strbuf_open(0);
	getconfs("langmap", langMap);

    // get gtags parser
    STRBUF *parser = strbuf_open(0);
	getconfs("gtags_parser", parser);

    parser_init(strbuf_value(langMap), strbuf_value(parser));

    strbuf_close(langMap);
    strbuf_close(parser);

#if 0
    // get parser
	if (getconfs("gtags_parser", sb))
		gtags_parser = check_strdup(strbuf_value(sb));
	if (vflag && gtags_parser)
		fprintf(stderr, " Using plug-in parser.\n");
    // init parser
	parser_init(langmap, gtags_parser);

    // free
    if (NULL != gtags_parser)
        free(gtags_parser);
    strbuf_close(sb);
#endif

    PParser tparser = (PParser)malloc(sizeof(struct Parser));
    LOGD("Create parser at addresss %p", tparser);
    return tparser;
}

void wparser_close(PParser *ppparser)
{
    if (NULL == ppparser || NULL == *ppparser)
        return;

    LOGD("free parser at addresss %p", *ppparser);
    free(*ppparser);
    *ppparser = NULL;

    parser_exit();
}

int wparser_parse(PParser pparser, const char *file,
        PARSER_CALLBACK cb, void *tag)
{
    if (NULL == pparser || NULL == file) {
        LOGE("Invalid parameter");
        return -1;
    }

    parse_file(file, 0, cb, tag);
    return 0;
}

