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
    int cflag;					/* compact format */
};

extern struct Options GlobalOptions;

#endif

