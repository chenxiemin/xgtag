/*
 * =====================================================================================
 *
 *       Filename:  opt.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  07/10/2015 10:43:16 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _OPT_H_
#define _OPT_H_

struct Options
{
    union {
        struct { // create tag options
            int cflag;					/* compact format */

            int iflag;					/* incremental update */
            const char *file_list;
            char *single_update;
        } c;
        struct { // dump options
            const char *dump_target;
            int show_config;
            const char *config_name;
        } d;
    };
};

extern struct Options O;

#endif

