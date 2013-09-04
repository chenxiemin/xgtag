/*
 * =====================================================================================
 *
 *       Filename:  cpp_def.h
 *
 *    Description:  This file contain some cpp defination
 *
 *        Version:  1.0
 *        Created:  15/09/12 15:59:39
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xieminchen (mtk), hs_xieminchen@mediatek.com
 *        Company:  Mediatek
 *
 * =====================================================================================
 */

#ifndef _CPP_DEF_H_
#define _CPP_DEF_H_

#define START_VARIABLE	1001
#define START_WORD	2001
#define START_SHARP	3001
#define START_YACC	4001
#define IS_RESERVED_WORD(a)	((a) >= START_WORD)
#define IS_RESERVED_VARIABLE(a)	((a) >= START_VARIABLE && (a) < START_WORD)
#define IS_RESERVED_SHARP(a)	((a) >= START_SHARP && (a) < START_YACC)
#define IS_RESERVED_YACC(a)	((a) >= START_YACC)

#define CHAR_BRACE_OPEN '{'
#define CHAR_BRACE_CLOSE '}'
#define CHAR_ASSIGNMENT '='
#define CHAR_SEMICOLON ';'
#define CHAR_COMMA ','
#define CHAR_COLON ':'
#define CHAR_PARENTH_OPEN '('
#define CHAR_PARENTH_CLOSE ')'
#define CHAR_ANGEL_BRACKET_OPEN '<'
#define CHAR_ANGEL_BRACKET_CLOSE '>'
#define CHAR_NEW_LINE '\n'

#define CPP_WCOLON	2001
#define CPP___P	2002
#define CPP___ATTRIBUTE__	2003
#define CPP___EXTENSION__	2004
#define CPP___THREAD	2005
#define CPP_ASM	2006
#define CPP_CONST	2007
#define CPP_INLINE	2008
#define CPP_SIGNED	2009
#define CPP_VOLATILE	2010
#define CPP_AUTO	2011
#define CPP_BOOL	2012
#define CPP_BREAK	2013
#define CPP_CASE	2014
#define CPP_CATCH	2015
#define CPP_CHAR	2016
#define CPP_CLASS	2017
#define CPP_CONST_CAST	2018
#define CPP_CONTINUE	2019
#define CPP_DEFAULT	2020
#define CPP_DELETE	2021
#define CPP_DO	2022
#define CPP_DOUBLE	2023
#define CPP_DYNAMIC_CAST	2024
#define CPP_ELSE	2025
#define CPP_ENUM	2026
#define CPP_EXPLICIT	2027
#define CPP_EXPORT	2028
#define CPP_EXTERN	2029
#define CPP_FALSE	2030
#define CPP_FLOAT	2031
#define CPP_FOR	2032
#define CPP_FRIEND	2033
#define CPP_GOTO	2034
#define CPP_IF	2035
#define CPP_INT	2036
#define CPP_LONG	2037
#define CPP_MUTABLE	2038
#define CPP_NAMESPACE	2039
#define CPP_NEW	2040
#define CPP_OPERATOR	2041
#define CPP_PRIVATE	2042
#define CPP_PROTECTED	2043
#define CPP_PUBLIC	2044
#define CPP_REGISTER	2045
#define CPP_REINTERPRET_CAST	2046
#define CPP_RETURN	2047
#define CPP_SHORT	2048
#define CPP_SIZEOF	2049
#define CPP_STATIC	2050
#define CPP_STATIC_CAST	2051
#define CPP_STRUCT	2052
#define CPP_SWITCH	2053
#define CPP_TEMPLATE	2054
#define CPP_THIS	2055
#define CPP_THROW	2056
#define CPP_TRUE	2057
#define CPP_TRY	2058
#define CPP_TYPEDEF	2059
#define CPP_TYPENAME	2060
#define CPP_TYPEID	2061
#define CPP_UNION	2062
#define CPP_UNSIGNED	2063
#define CPP_USING	2064
#define CPP_VIRTUAL	2065
#define CPP_VOID	2066
#define CPP_WCHAR_T	2067
#define CPP_WHILE	2068
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
struct keyword { char *name; int token; };

#define TOTAL_KEYWORDS 99
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 16
#define MIN_HASH_VALUE 2
#define MAX_HASH_VALUE 242
/* maximum key range = 241, duplicates = 0 */

#endif

