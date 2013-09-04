/*
 * =====================================================================================
 *
 *       Filename:  common.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/02/13 17:45:06
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _UTIL_COMMON_H
#define _UTIL_COMMON_H

#define NEW(p, type) \
	((p) = ((type) *)check_malloc(sizeof(*(p))))
// this will calculate (p) twice
#define FREE(p) (free((p)), (p) = NULL)

typedef enum { FALSE, TRUE } BOOL;

#endif

