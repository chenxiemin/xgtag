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

#define MAX_PATTERN_LENGTH 1024

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

        struct { // select options
            int nofilter;
            int iflag;				/* [option]		*/
            int Gflag;				/* [option]		*/
            int lflag;				/* [option]		*/
            int sflag;				/* [option]		*/
            int format;
            const char *localprefix;		/* local prefix		*/
            int nosource;				/* undocumented command */
            int Pflag;				/* command		*/
            int type;               /* path search type: relative / absolute */
            // const char *pattern;    /* search pattern, path or tag */
            char pattern[MAX_PATTERN_LENGTH + 1];    /* search pattern, path or tag */
            int eflag;				/* regular expression search */
        } s;
    };

};

extern struct Options O;

#endif

