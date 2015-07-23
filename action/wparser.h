/*
 * =====================================================================================
 *
 *       Filename:  wparser.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/10/2015 10:55:37 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _W_PARSER_H_
#define _W_PARSER_H_

#include "parser.h"

typedef struct Parser
{
    void *dummy;
} *PParser;

PParser wparser_open();

void wparser_close(PParser *ppparser);

int wparser_parse(PParser pparser, const char *file,
        PARSER_CALLBACK cb, void *tag);

#endif

