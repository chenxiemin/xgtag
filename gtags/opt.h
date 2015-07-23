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
            int Oflag;					/* use objdir */
            const char *file_list;
            const char *single_update;
            const char *gtagsconf;
            const char *gtagslabel;
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

