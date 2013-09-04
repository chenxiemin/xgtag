/*
 * =====================================================================================
 *
 *       Filename:  c_def.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  05/09/12 17:28:19
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _C_DEF_H_
#define _C_DEF_H_

#define START_VARIABLE	1001
#define START_WORD	2001
#define START_SHARP	3001
#define START_YACC	4001
#define IS_RESERVED_WORD(a)	((a) >= START_WORD)
#define IS_RESERVED_VARIABLE(a)	((a) >= START_VARIABLE && (a) < START_WORD)
#define IS_RESERVED_SHARP(a)	((a) >= START_SHARP && (a) < START_YACC)
#define IS_RESERVED_YACC(a)	((a) >= START_YACC)

#define C___P	2001
#define C___ATTRIBUTE__	2002
#define C___EXTENSION__	2003
#define C_ASM	2004
#define C_CONST	2005
#define C_INLINE	2006
#define C_RESTRICT	2007
#define C_SIGNED	2008
#define C_VOLATILE	2009
#define C__ALIGNAS	2010
#define C__ALIGNOF	2011
#define C__ATOMIC	2012
#define C__BOOL	2013
#define C__COMPLEX	2014
#define C__GENERIC	2015
#define C__IMAGINARY	2016
#define C__NORETURN	2017
#define C__STATIC_ASSERT	2018
#define C__THREAD_LOCAL	2019
#define C_AUTO	2020
#define C_BREAK	2021
#define C_CASE	2022
#define C_CHAR	2023
#define C_CONTINUE	2024
#define C_DEFAULT	2025
#define C_DO	2026
#define C_DOUBLE	2027
#define C_ELSE	2028
#define C_ENUM	2029
#define C_EXTERN	2030
#define C_FLOAT	2031
#define C_FOR	2032
#define C_GOTO	2033
#define C_IF	2034
#define C_INT	2035
#define C_LONG	2036
#define C_REGISTER	2037
#define C_RETURN	2038
#define C_SHORT	2039
#define C_SIZEOF	2040
#define C_STATIC	2041
#define C_STRUCT	2042
#define C_SWITCH	2043
#define C_TYPEDEF	2044
#define C_UNION	2045
#define C_UNSIGNED	2046
#define C_VOID	2047
#define C_WHILE	2048
#define SHARP_SHARP	3001
#define SHARP_ASSERT	3002
#define SHARP_DEFINE	3003
#define SHARP_ELIF	3004
#define SHARP_ELSE	3005
#define SHARP_ENDIF	3006
#define SHARP_ERROR	3007
#define SHARP_IDENT	3008
#define SHARP_IF	3009
#define SHARP_IFDEF	3010
#define SHARP_IFNDEF	3011
#define SHARP_IMPORT	3012
#define SHARP_INCLUDE	3013
#define SHARP_INCLUDE_NEXT	3014
#define SHARP_LINE	3015
#define SHARP_PRAGMA	3016
#define SHARP_SCCS	3017
#define SHARP_UNASSERT	3018
#define SHARP_UNDEF	3019
#define SHARP_WARNING	3020
#define YACC_SEP	4001
#define YACC_DEBUG	4002
#define YACC_DEFAULT_PREC	4003
#define YACC_DEFINE	4004
#define YACC_DEFINES	4005
#define YACC_DESTRUCTOR	4006
#define YACC_DPREC	4007
#define YACC_ERROR_VERBOSE	4008
#define YACC_EXPECT	4009
#define YACC_EXPECT_RR	4010
#define YACC_FILE_PREFIX	4011
#define YACC_GLR_PARSER	4012
#define YACC_INITIAL_ACTION	4013
#define YACC_LEFT	4014
#define YACC_LEX_PARAM	4015
#define YACC_LOCATIONS	4016
#define YACC_MERGE	4017
#define YACC_NAME_PREFIX	4018
#define YACC_NO_DEFAULT_PREC	4019
#define YACC_NO_LINES	4020
#define YACC_NONASSOC	4021
#define YACC_NONDETERMINISTIC_PARSER	4022
#define YACC_NTERM	4023
#define YACC_OUTPUT	4024
#define YACC_PARSE_PARAM	4025
#define YACC_PREC	4026
#define YACC_PRINTER	4027
#define YACC_PURE_PARSER	4028
#define YACC_REQUIRE	4029
#define YACC_RIGHT	4030
#define YACC_SKELETON	4031
#define YACC_START	4032
#define YACC_TOKEN	4033
#define YACC_TOKEN_TABLE	4034
#define YACC_TYPE	4035
#define YACC_UNION	4036
#define YACC_VERBOSE	4037
#define YACC_YACC	4038
#define YACC_BEGIN	4039
#define YACC_END	4040

#endif

