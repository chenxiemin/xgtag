/*
 * Copyright (c) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009,
 *	2010
 *	Tama Communications Corporation
 *
 * This file is part of GNU GLOBAL.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <stdlib.h>
#include <assert.h>
#include <setjmp.h>

#include "internal.h"
#include "die.h"
#include "strbuf.h"
#include "strlimcpy.h"
#include "token.h"
#include "c_def.h"
#include "cpp_res.h"
#include "common.h"

#ifndef DEBUG
#define DEBUG
#endif

#define C_OPERATE 257
#define C_JUDGE (C_OPERATE + 0) // ==
#define C_LESSJUDGE (C_OPERATE + 1) // <=
#define C_BIGJUDGE (C_OPERATE + 2) // >=
#define C_NOTJUDGE (C_OPERATE + 3) // !=
#define C_LEFTSHIFT (C_OPERATE + 4) // <<
#define C_RIGHTSHIFT (C_OPERATE + 5) // >>
#define C_OPERATE_END 300

// max num to save token in StatementInfo
#define SAVETOKENNUM 3

#define EXCEPTION_EOF 1

#define isKeyword(token) (NULL != (token) && (token)->type == TOKEN_KEYWORD)
// Is current StatementInfo under the Enum body
#define isInEnum(pStatementInfo) (\
		NULL != (pStatementInfo) && NULL != (pStatementInfo)->scopeParent && \
		DECL_ENUM == (pStatementInfo)->scopeParent->declaration)
#define isType(pStatementInfo, type) ((type) == (pStatementInfo)->declaration)
// Is current statementinfo under the global
#define isGlobal(pStatementInfo) (\
		NULL == (pStatementInfo) || \
		NULL == (pStatementInfo)->scopeParent || \
		(DECL_FUNCTION != (pStatementInfo)->scopeParent->declaration && \
		DECL_CLASS != (pStatementInfo)->scopeParent->declaration && \
		DECL_STRUCT != (pStatementInfo)->scopeParent->declaration && \
		DECL_UNION != (pStatementInfo)->scopeParent->declaration))
// do we under a Condition Directive of #else, #elif?
#define isConditionDirective(directiveInfo) ((directiveInfo).isInConditionDirective > 0)
#define setConditionDirective(directiveInfo, state) (\
		(directiveInfo).isInConditionDirective &= ~(1 << ((directiveInfo).nestLevel - 1)), \
		(directiveInfo).isInConditionDirective |= ((int)(state)) << ((directiveInfo).nestLevel - 1))


typedef enum eTokenType {
    TOKEN_NONE = 0,      /* none */
    TOKEN_UNKNOWN,       /* unrecongnize token */
    TOKEN_ARGS,          /* a parenthetical pair and its contents */
    TOKEN_BRACE_CLOSE,
    TOKEN_BRACE_OPEN,
    TOKEN_COLON,         /* the colon character */
    TOKEN_COMMA,         /* the comma character */
    TOKEN_DOUBLE_COLON,  /* double colon indicates nested-name-specifier */
    TOKEN_KEYWORD,
    TOKEN_NAME,          /* an unknown name */
    TOKEN_PACKAGE,       /* a Java package name */
    TOKEN_PAREN_NAME,    /* a function point like (*fp) */
    TOKEN_SEMICOLON,     /* the semicolon character */
    TOKEN_SPEC,          /* a storage class specifier, qualifier, type, etc. */
    TOKEN_COUNT
} tokenType;

typedef struct {
	tokenType type;
	int cc; // type returned by parser
	// char name[MAXTOKEN]; // token name
	STRBUF *name;
	int lno;
} tokenInfo;

typedef enum eDeclaration {
    DECL_NONE,
    DECL_BASE,           /* base type (default) */
    DECL_CLASS,
    DECL_ENUM,
    DECL_EVENT,
    DECL_FUNCTION,
    DECL_IGNORE,         /* non-taggable "declaration" */
    DECL_INTERFACE,
    DECL_NAMESPACE,
    DECL_NOMANGLE,       /* C++ name demangling block */
    DECL_PACKAGE,
    DECL_PROGRAM,        /* Vera program */
    DECL_STRUCT,
    DECL_TASK,           /* Vera task */
    DECL_UNION,
    DECL_COUNT
} declType;

/*
 * One StatementInfo instance means that we under a { } Context
 */
typedef struct StatementInfoT {
	// the direct parent, but it may not contain scope info like
	// if () // this is a StatementInfo1
	// {
	// 	   // this is a StatementInfo2
	//     // if we in this, the StatementInfo2's direct parent
	//     // should be StatementInfo1
	// }
	struct StatementInfoT *parent;
	// the parent we under which scope
	// the scope only changes when parent is DECL_STRUCT, DECL_FUNCTION, etc...
	struct StatementInfoT *scopeParent; 
	declType declaration; // current declaration, enum / union / struct, etc
	int tokenIndex; // current token index
	tokenInfo saveToken[SAVETOKENNUM]; // cache token info
	BOOL isInTypedef; // does the StatementInfo occur typedef
	BOOL isInExtern; // does the StatementInfo occur extern

	// for debug
#ifdef DEBUG
	int lineno;
#endif
} StatementInfo;

struct DirectiveInfo {
	// do we under a directive, because condition directive could nest
	// so we use every bit of 32 bits in a int to represent a nest level
	int isInConditionDirective; 
	// now we are under the #define or #if, #endif macro
	// this means that cppNextToken() should process all of the tokens
	BOOL shouldIgnoreMacro;
	BOOL acceptSymbolAsDefine; // should we treate a symbol as define in directive?
	// int type; // which type of cc returned by nexttoken()
	int nestLevel; // nest level of condition macro
} directiveInfo = { 0 };

int skipToMatchLevel = 0; // the level of skipToMatch()
static StatementInfo *CurrentStatementInfo = NULL;
// do we under the assign state
BOOL isInAssign = FALSE;
// convert operator to string
const static char OperateName[][4] = { "==", "<=", ">=", "!=", "<<", ">>" };
// jmpbuf for exception handle
jmp_buf jmpbuffer;

#ifdef DEBUG
#define SKIP_STACK_NUM 1024
struct {
	int index;
	struct {
		int lineno;
		char skipChar;
	} stack[SKIP_STACK_NUM];
} skipStack = { 0 };

void skipStackPush(char skipChar, int lineno)
{
	skipStack.stack[skipStack.index].lineno = lineno;
	skipStack.stack[skipStack.index].skipChar = skipChar;
	skipStack.index++;
}

void skipStackPop()
{
	skipStack.index--;
}
#endif

// malloc new StatementInfo
// ppCurrentStatementInfo will be changed
static StatementInfo *newStatementInfo(StatementInfo **ppCurrentStatementInfo);
// delete ppCurrentStatementInfo, then replace with its parent
static void delStatementInfo(StatementInfo **ppCurrentStatementInfo,
		const struct parser_param *param);
// delete all StatementInfo
static void delAllStatementInfo(StatementInfo **ppCurrentStatementInfo,
		const struct parser_param *param);
// get active token in StatementInfo
static inline tokenInfo *activeToken(StatementInfo *pInfo);
// foward a token in StatementInfo
static inline void advanceToken(StatementInfo *pInfo);
// reward a token in StatementInfo
static inline void reverseToken(StatementInfo *si);
// get [prev] index token in StatementInfo
static inline tokenInfo *prevToken(StatementInfo *pInfo, int prev);
// set active token in StatementInfo
static inline void setToken(StatementInfo *si, tokenType type,
		const char *buffer, int cc, int lno);
// reset prevCount(s) tokenInfo in StatementInfo
static inline void resetStatementToken(StatementInfo *pInfo, int prevCount);
// reset StatementInfo
static inline void resetStatementInfo(StatementInfo *pInfo);
// reset a token
static inline void resetToken(tokenInfo *pToken);
static inline STRBUF *getTokenName(tokenInfo *token)
{
	assert(NULL != token && NULL != token->name);

	return token->name;
}
// nest check declaration in StatementInfo.scopeParent
static inline BOOL isInScope(StatementInfo *si, declType scope);
// help function to convert cc to tokenType
static inline tokenType getTokenType(int cc);

// wrap nexttoken()
// preprocess a primitive token
// this will never ever return a preprocess
// and also '\n' will never be returned
static int cppNextToken(const struct parser_param *param);
// wrap nexttoken() but with no Macro and Condition Directive
static int nextTokenStripMacro(const struct parser_param *param);
// wrap nexttoken, such as '==' will returned if two '=' continuously occured
static int nextToken(const char *interested,
		int (*reserved)(const char *, int), BOOL ignoreNewLine);
// skip '[]', '{}', etc...
// treate all symbols as reference
static void skipToMatch(const struct parser_param *param,
		const char match1, const char match2);
// process '='
static void parseInitializer(const struct parser_param *param, StatementInfo *si);
// process '('
static void parseParentheses(const struct parser_param *param, StatementInfo *si);
// process __attribute__
static void process_attribute(const struct parser_param *);
// process class inheritance
static void processInherit(const struct parser_param *);
// skip until a statement end
static void skipStatement(const struct parser_param *, char endChar);
// read args after '('
static void parseArgs(const struct parser_param *param,
		char *prev, char *prev2, tokenInfo *current);
// read token in a ()
static void readParenthesesToken(const struct parser_param *param, STRBUF *buffer);
// read function name of Operator function
static void readOperatorFunction(STRBUF *funcName);
// get tag name of a function point
static void getFunctionPointName(STRBUF *tokenName, const STRBUF *functionPointName);
// is identical char
static inline int isident(char c);

static inline void pushConditionDirective(struct DirectiveInfo *directiveInfo)
{
	directiveInfo->nestLevel++;
}

static inline void popConditionDirective(struct DirectiveInfo *directiveInfo)
{
	if (directiveInfo->nestLevel <= 0)
		return;

	setConditionDirective(*directiveInfo, FALSE);
	directiveInfo->nestLevel--;
}

static StatementInfo *newStatementInfo(StatementInfo **ppCurrentStatementInfo)
{
	StatementInfo *newInfo = NULL;
	newInfo = (StatementInfo *)malloc(sizeof(StatementInfo));
	if (NULL == newInfo)
		die("cannot alloc memory for StatementInfo");
	memset(newInfo, 0, sizeof(*newInfo));

	newInfo->parent = *ppCurrentStatementInfo;
	*ppCurrentStatementInfo = newInfo;
	int i = 0;
	for (; i < SAVETOKENNUM; i++)
		resetToken(&(newInfo->saveToken[i]));
	// set scope
	StatementInfo *parent = (*ppCurrentStatementInfo)->parent;
	if (NULL != parent)
		switch (parent->declaration)
		{
			case DECL_FUNCTION:
			case DECL_CLASS:
			case DECL_STRUCT:
			case DECL_UNION:
			case DECL_ENUM:
				// use direct parent as scope
				newInfo->scopeParent = parent;
				break;
			default:
				// use parent's scope
				newInfo->scopeParent = parent->scopeParent;
				break;
		}

#ifdef DEBUG
	newInfo->lineno = lineno;
#endif

	return newInfo;
}

static void delStatementInfo(StatementInfo **ppCurrentStatementInfo,
		const struct parser_param *param)
{
	assert(NULL != ppCurrentStatementInfo);

	if (NULL != *ppCurrentStatementInfo)
	{
		// save symbol
		int i = 0;
		for (; i < SAVETOKENNUM; i++)
		{
			tokenInfo *prev = prevToken(*ppCurrentStatementInfo, i);
			if (SYMBOL == prev->cc)
				PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev)),
						prev->lno, NULL);
		}

		StatementInfo *parentInfo = (*ppCurrentStatementInfo)->parent;
		free(*ppCurrentStatementInfo);

		*ppCurrentStatementInfo = parentInfo;
	}
}

static void delAllStatementInfo(StatementInfo **ppCurrentStatementInfo, const struct parser_param *param)
{
	while (NULL != *ppCurrentStatementInfo)
		delStatementInfo(ppCurrentStatementInfo, param);
}

static inline tokenInfo *activeToken(StatementInfo *pInfo)
{
	return &(pInfo->saveToken[pInfo->tokenIndex]);
}

static inline void advanceToken(StatementInfo *pInfo)
{
	pInfo->tokenIndex = (pInfo->tokenIndex + 1) % SAVETOKENNUM;
	resetToken(activeToken(pInfo));
}

static inline void reverseToken(StatementInfo *si)
{
	resetToken(activeToken(si));
	si->tokenIndex = (si->tokenIndex + SAVETOKENNUM - 1) % SAVETOKENNUM;
}

static inline tokenInfo *prevToken(StatementInfo *pInfo, int prev)
{
	assert(prev < SAVETOKENNUM);

	return &(pInfo->saveToken[(pInfo->tokenIndex + SAVETOKENNUM - prev) % SAVETOKENNUM]);
}

static inline void setToken(StatementInfo *si, tokenType type,
		const char *buffer, int cc, int lno)
{
	// get active token
	tokenInfo *currentToken = activeToken(si);
	currentToken->type = type;
	strbuf_reset(getTokenName(currentToken));
	if (type == TOKEN_NAME)
		strbuf_puts(getTokenName(currentToken), buffer);
	currentToken->cc = cc;
	currentToken->lno = lno;
}

static inline void resetStatementToken(StatementInfo *pInfo, int prevCount)
{
	assert(prevCount < SAVETOKENNUM);

	int i = 0;
	for (; i < prevCount + 1; i++)
		resetToken(prevToken(pInfo, i));
}

static inline void resetStatementInfo(StatementInfo *pInfo)
{
	pInfo->declaration = DECL_NONE;
	pInfo->isInTypedef = FALSE;
	pInfo->isInExtern = FALSE;
}

static inline void resetToken(tokenInfo *pToken)
{
	assert(NULL != pToken);

	pToken->type = 0;
	pToken->cc = -1;
	pToken->lno = 0;
	if (NULL == pToken->name)
		pToken->name = strbuf_open(0);
	strbuf_reset(pToken->name);
}

static inline BOOL isInScope(StatementInfo *si, declType scope)
{
	if (NULL == si || NULL == si->scopeParent)
		return FALSE;
	else if (scope == si->scopeParent->declaration)
		return TRUE;
	else
		return isInScope(si->scopeParent, scope);
}

static inline tokenType getTokenType(int cc)
{
	tokenType type = TOKEN_NONE;
	if (cc == SYMBOL)
		type = TOKEN_NAME;
	else if (cc >= START_WORD < START_SHARP)
		type = TOKEN_KEYWORD;
	else
		type = TOKEN_UNKNOWN;

	return type;
}

static const char *getReservedToken(int token)
{
	static struct keyword wordlist[] =
	{
		{""}, {""},
		{"::", CPP_WCOLON},
		{"asm", CPP_ASM},
		{""}, {""}, {""},
		{"do", CPP_DO},
		{"for", CPP_FOR},
		{""}, {""}, {""},
		{"##", SHARP_SHARP},
		{"int", CPP_INT},
		{""},
		{"const", CPP_CONST},
		{"struct", CPP_STRUCT},
		{"#assert", SHARP_ASSERT},
		{"continue", CPP_CONTINUE},
		{""},
		{"const_cast", CPP_CONST_CAST},
		{"static", CPP_STATIC},
		{"virtual", CPP_VIRTUAL},
		{"unsigned", CPP_UNSIGNED},
		{"goto", CPP_GOTO},
		{"__asm", CPP_ASM},
		{"static_cast", CPP_STATIC_CAST},
		{"__asm__", CPP_ASM},
		{"__signed", CPP_SIGNED},
		{"auto", CPP_AUTO},
		{"__signed__", CPP_SIGNED},
		{"__attribute", CPP___ATTRIBUTE__},
		{""},
		{"__attribute__", CPP___ATTRIBUTE__},
		{"#unassert", SHARP_UNASSERT},
		{"false", CPP_FALSE},
		{"#error", SHARP_ERROR},
		{"mutable", CPP_MUTABLE},
		{"try", CPP_TRY},
		{"case", CPP_CASE},
		{""},
		{"inline", CPP_INLINE},
		{""},
		{"volatile", CPP_VOLATILE},
		{"namespace", CPP_NAMESPACE},
		{"throw", CPP_THROW},
		{"export", CPP_EXPORT},
		{""},
		{"new", CPP_NEW},
		{""},
		{"#else", SHARP_ELSE},
		{"return", CPP_RETURN},
		{""},
		{"__P", CPP___P},
		{""}, {""},
		{"signed", CPP_SIGNED},
		{""},
		{"__thread", CPP___THREAD},
		{"char", CPP_CHAR},
		{"catch", CPP_CATCH},
		{"extern", CPP_EXTERN},
		{"__const", CPP_CONST},
		{"#include", SHARP_INCLUDE},
		{"__const__", CPP_CONST},
		{"#elif", SHARP_ELIF},
		{"#undef", SHARP_UNDEF},
		{"wchar_t", CPP_WCHAR_T},
		{"#include_next", SHARP_INCLUDE_NEXT},
		{""},
		{"using", CPP_USING},
		{"#ident", SHARP_IDENT},
		{""}, {""},
		{"protected", CPP_PROTECTED},
		{"union", CPP_UNION},
		{"delete", CPP_DELETE},
		{"#pragma", SHARP_PRAGMA},
		{"__inline", CPP_INLINE},
		{""},
		{"__inline__", CPP_INLINE},
		{"#endif", SHARP_ENDIF},
		{"#import", SHARP_IMPORT},
		{"register", CPP_REGISTER},
		{"long", CPP_LONG},
		{"#sccs", SHARP_SCCS},
		{"sizeof", CPP_SIZEOF},
		{""},
		{"#if", SHARP_IF},
		{""},
		{"class", CPP_CLASS},
		{""},
		{"#ifndef", SHARP_IFNDEF},
		{"template", CPP_TEMPLATE},
		{""}, {""}, {""},
		{"if", CPP_IF},
		{""},
		{"else", CPP_ELSE},
		{"__volatile", CPP_VOLATILE},
		{"friend", CPP_FRIEND},
		{"__volatile__", CPP_VOLATILE},
		{""},
		{"this", CPP_THIS},
		{"short", CPP_SHORT},
		{"reinterpret_cast", CPP_REINTERPRET_CAST},
		{"dynamic_cast", CPP_DYNAMIC_CAST},
		{"#warning", SHARP_WARNING},
		{""}, {""}, {""},
		{"default", CPP_DEFAULT},
		{"explicit", CPP_EXPLICIT},
		{"enum", CPP_ENUM},
		{"break", CPP_BREAK},
		{"double", CPP_DOUBLE},
		{""}, {""},
		{"void", CPP_VOID},
		{""},
		{"public", CPP_PUBLIC},
		{""}, {""},
		{"true", CPP_TRUE},
		{""},
		{"typeid", CPP_TYPEID},
		{"typedef", CPP_TYPEDEF},
		{"typename", CPP_TYPENAME},
		{""}, {""}, {""}, {""},
		{"__extension__", CPP___EXTENSION__},
		{""}, {""},
		{"#ifdef", SHARP_IFDEF},
		{""}, {""},
		{"bool", CPP_BOOL},
		{"#line", SHARP_LINE},
		{""}, {""}, {""}, {""},
		{"float", CPP_FLOAT},
		{""}, {""}, {""}, {""}, {""},
		{"switch", CPP_SWITCH},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""},
		{"private", CPP_PRIVATE},
		{"operator", CPP_OPERATOR},
		{""}, {""}, {""}, {""}, {""}, {""},
		{"while", CPP_WHILE},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
		{""}, {""}, {""},
		{"#define", SHARP_DEFINE},
		{NULL, 0}
	};

	int i = 0;
	for (i = 0; NULL != wordlist[i].name; i++)
		if (wordlist[i].token == token)
			return wordlist[i].name;

	return NULL;
}

static void createTags(const struct parser_param *param)
{
	const char *interested = "{}=,;()~";
	int c = 0;
	int cc = 0;

	while ((cc = cppNextToken(param)) != EOF)
	{
		// save new token
		setToken(CurrentStatementInfo, getTokenType(cc),
				token, cc, lineno);

		switch (cc)
		{
		case SYMBOL:
		{
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			if (TOKEN_ARGS == prev->type)
			{
				// skip a symbol after an TOKEN_ARGS
				PUT(PARSER_REF_SYM, token, lineno, sp);
				reverseToken(CurrentStatementInfo);
			}
			break;
		}
		case CHAR_BRACE_OPEN:
		{
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
			// save tag if necessary
			if (TOKEN_ARGS == prev->type && 
					(TOKEN_NAME == prev2->type || TOKEN_PAREN_NAME == prev2->type))
			{
				// save function defination if necessarey
				if (TOKEN_NAME == prev2->type)
				{
					PUT(PARSER_DEF, strbuf_value(getTokenName(prev2)), prev2->lno, 
							strbuf_value(getTokenName(prev)));
				}
				else
				{
					// return type is a function point
					// save function point
					STRBUF *bufName = strbuf_open(0);
					getFunctionPointName(bufName, getTokenName(prev2));
					PUT(PARSER_DEF, strbuf_value(bufName), prev2->lno,
							strbuf_value(getTokenName(prev)));
					strbuf_close(bufName);
				}
				resetStatementToken(CurrentStatementInfo, 2);
			}
			else if (TOKEN_NAME == prev->type &&
					!isInScope(CurrentStatementInfo, DECL_FUNCTION) &&
					(DECL_STRUCT == CurrentStatementInfo->declaration || 
					 DECL_CLASS == CurrentStatementInfo->declaration || 
						DECL_UNION == CurrentStatementInfo->declaration ||
						DECL_ENUM == CurrentStatementInfo->declaration ||
						DECL_NAMESPACE == CurrentStatementInfo->declaration))
			{
				// save struct / union / class / enum / namespace defination
				PUT(PARSER_DEF, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				resetStatementToken(CurrentStatementInfo, 2);
			}

			// we don't in extern anymore
			CurrentStatementInfo->isInExtern = FALSE;
			// new context
			newStatementInfo(&CurrentStatementInfo);
			break;
		}
		case CHAR_BRACE_CLOSE:
		{
			// set token rest if necessary
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			if (SYMBOL == prev->cc && isInEnum(CurrentStatementInfo))
			{
				// save
				PUT(PARSER_DEF, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				resetToken(prev);
			}
			
			// delete context
			delStatementInfo(&CurrentStatementInfo, param);
			if (NULL == CurrentStatementInfo)
			{
				newStatementInfo(&CurrentStatementInfo);
			}
			break;
		}
		case CHAR_PARENTH_OPEN:
		{
			parseParentheses(param, CurrentStatementInfo);
			break;
		}
		case CHAR_COMMA:
		case CHAR_SEMICOLON:
		{
			// save token if necessarey
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
			if (isGlobal(CurrentStatementInfo))
				if (TOKEN_NAME == prev->type &&
					(CPP_CLASS == prev2->cc ||
					CPP_STRUCT == prev2->cc ||
					CPP_UNION == prev2->cc ||
					CPP_ENUM == prev2->cc))
					// struct declaration
					PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				else if (TOKEN_PAREN_NAME == prev2->type && TOKEN_ARGS == prev->type)
				{
					// save global function point
					STRBUF *bufName = strbuf_open(0);
					getFunctionPointName(bufName, getTokenName(prev2));
					PUT(PARSER_DEF, strbuf_value(bufName), prev->lno,
							strbuf_value(getTokenName(prev)));
					strbuf_close(bufName);
				}
				else if (TOKEN_NAME == prev->type &&
						(CPP_NAMESPACE == prev2->cc | CPP_USING == prev2->cc))
					// ignore namespace
					PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				else if (TOKEN_NAME == prev->type && !CurrentStatementInfo->isInExtern)
					// save global variable
					PUT(PARSER_DEF, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				else
					// treate as ref
					if (TOKEN_NAME == prev->type)
						PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev)), prev->lno, NULL);
					if (TOKEN_NAME == prev2->type)
						PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			else if (TOKEN_NAME == prev->type && isInEnum(CurrentStatementInfo))
				// save the last enum member
				PUT(PARSER_DEF, strbuf_value(getTokenName(prev)), prev->lno, NULL);
			else if (TOKEN_NAME == prev->type && CurrentStatementInfo->isInTypedef)
				// typedef
				if (TOKEN_NAME == prev->type)
					PUT(PARSER_DEF, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				if (TOKEN_NAME == prev2->type)
					PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			else
			{
				// save ref if necessary
				if (TOKEN_NAME == prev->type)
					PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev)), prev->lno, NULL);
				if (TOKEN_NAME == prev2->type)
					PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			}

			// reset all of the previous tokens
			resetStatementToken(CurrentStatementInfo, 2);
			// reset env
			if (CHAR_SEMICOLON == cc)
				resetStatementInfo(CurrentStatementInfo);
			break;
		}
		case CPP_NEW:
			// ignore line break
			if ((c = nextToken(interested, cpp_reserved_word, 1)) == SYMBOL)
				PUT(PARSER_REF_SYM, token, lineno, sp);
			break;
		case CPP___ATTRIBUTE__:
			process_attribute(param);
			break;
		default:
			break;
		}

		// treate as ref symbol whatever
		tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
		if (SYMBOL == prev2->cc)
		{
			PUT(PARSER_REF_SYM, strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			resetToken(prev2);
		}
		// move token
		advanceToken(CurrentStatementInfo);
	}
}

// Cpp: read C++ file and pickup tag entries.
void Cpp(const struct parser_param *param)
{
	// init token processor
	if (!opentoken(param->file))
		die("'%s' cannot open.", param->file);
	cmode = 1; // allow macro
	crflag = 1; // require '\n' as a token
	cppmode = 1; // treat '::' as a token

	// new statementinfo to save context
	newStatementInfo(&CurrentStatementInfo);

	// init jmpbuf
	int except = setjmp(jmpbuffer);
	if (0 == except)
		// parse file to create tags
		createTags(param);
	else if (EXCEPTION_EOF == except)
		// exception handle
		warning("Unexpected end of file %s", param->file);
	else
		warning("Unknown exception");

	// clear
	delAllStatementInfo(&CurrentStatementInfo, param);
	closetoken();
}

static int cppNextToken(const struct parser_param *param)
{
	const char *interested = "{}=,;:()~[]<>";

	int cc = -1;
	BOOL needContinue = FALSE;
	while ((cc = nextTokenStripMacro(param)) != EOF)
	{
		switch (cc)
		{
		case '[':
		{
			skipToMatch(param, '[', ']');
			needContinue = TRUE;
			break;
		}
		case CHAR_ASSIGNMENT:
		{
			if (skipToMatchLevel == 0
					// && !isInEnum(CurrentStatementInfo)
			   )
			{
				if (!isInAssign)
				{
					isInAssign = TRUE;
				}
				else
				{
					// already in assign state, ignore
					needContinue = TRUE;
					break;
				}
				parseInitializer(param, CurrentStatementInfo);
			}
			needContinue = TRUE;
			break;
		}
		case CHAR_COMMA:
		case CHAR_SEMICOLON:
		{
			isInAssign = FALSE;
			break;
		}
		case CHAR_COLON:
		{
			// read class inherit if necessary
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			if ((DECL_CLASS == CurrentStatementInfo->declaration ||
						DECL_STRUCT == CurrentStatementInfo->declaration) && SYMBOL == prev->cc)
			{
				processInherit(param);
			}
			else if (TOKEN_ARGS == prev->type)
			{
				// skip initializer list
				processInherit(param);
			}
			needContinue = TRUE;
			break;
		}
		case CHAR_ANGEL_BRACKET_OPEN:
		{
			if (0 == skipToMatchLevel && !isInAssign)
			{
				// ignore template
				skipToMatch(param, '<', '>');
				needContinue = TRUE;
			}
			break;
		}
		case CPP_UNION:
		{
			CurrentStatementInfo->declaration = DECL_UNION;
			break;
		}
		case CPP_STRUCT:
		{
			CurrentStatementInfo->declaration = DECL_STRUCT;
			break;
		}
		case CPP_ENUM:
		{
			CurrentStatementInfo->declaration = DECL_ENUM;
			break;
		}
		case CPP_CLASS:
		{
			CurrentStatementInfo->declaration = DECL_CLASS;
			break;
		}
		case CPP_NAMESPACE:
		{
			CurrentStatementInfo->declaration = DECL_NAMESPACE;
			break;
		}
		case CPP_WCOLON:
		{
			// ignore namespace
			if (prevToken(CurrentStatementInfo, 1)->type == TOKEN_NAME)
			{
				reverseToken(CurrentStatementInfo);
			}
			needContinue = TRUE;
			break;
		}
		case CPP_OPERATOR:
		{
			STRBUF *funcName = strbuf_open(0);
			readOperatorFunction(funcName);
			// copy
			strcpy(token, strbuf_value(funcName));
			strbuf_close(funcName);
			cc = 0;
			break;
		}
		case CPP_TYPEDEF:
		{
			CurrentStatementInfo->isInTypedef = TRUE;
			needContinue = TRUE;
			break;
		}
		case CPP_EXTERN:
		{
			CurrentStatementInfo->isInExtern = TRUE;
			needContinue = TRUE;
			break;
		}
		case CPP_TYPENAME:
		case CPP_TEMPLATE:
		case CPP_VOLATILE:
		case CPP_CONST:
		{
			// ignore
			needContinue = TRUE;
			break;
		}
		case CPP_IF:
		case CPP_FOR:
		case CPP_SWITCH:
		case CPP_WHILE:
		{
			// ignore line break
			int c = nextToken(interested, cpp_reserved_word, 1);
			if (CHAR_PARENTH_OPEN == c)
			{
				skipToMatch(param, '(', ')');
			}
			else
			{
				pushbacktoken();
			}
			break;
		}
		case CPP_CASE:
		{
			skipStatement(param, CHAR_COLON);
			break;
		}
		case CPP_RETURN:
		{
			skipStatement(param, CHAR_SEMICOLON);
			break;
		}
		case CPP_THROW:
		{
			needContinue = TRUE;
			break;
		}
		case CPP_ASM:
		{
			// we dont support asm yet
			if (CHAR_BRACE_OPEN == peekc(0))
			{
				// skip first BRACE_OPEN, ignore line break
				nextToken(interested, cpp_reserved_word, 1);
				// ignore
				skipToMatch(param, '{', '}');
			}
			break;
		}
		default:
			break;
		}

		if (needContinue)
		{
			needContinue = FALSE;
			continue;
		}
		break;
	}

	return cc;
}

static int nextTokenStripMacro(const struct parser_param *param)
{
	const char *interested = "{}=,;:()~[]<>";
	int cc = -1;
	BOOL needContinue = FALSE;
	while ((cc = nextToken(interested, cpp_reserved_word, 0)) != EOF)
	{
		switch (cc)
		{
			case SHARP_DEFINE:
			case SHARP_UNDEF:
			{
				// mark directive state
				directiveInfo.shouldIgnoreMacro = TRUE;
				directiveInfo.acceptSymbolAsDefine = TRUE;
				break;
			}
			case SYMBOL:
			{
				if (!directiveInfo.shouldIgnoreMacro)
					break;

				if (directiveInfo.acceptSymbolAsDefine)
				{
					// only put first symbol as define for simplification
					PUT(PARSER_DEF, token, lineno, sp);
					directiveInfo.acceptSymbolAsDefine = FALSE;
				}
				else
					// treate as ref
					PUT(PARSER_REF_SYM, token, lineno, sp);

				break;
			}
			case SHARP_IMPORT:
			case SHARP_INCLUDE:
			case SHARP_INCLUDE_NEXT:
			case SHARP_ERROR:
			case SHARP_LINE:
			case SHARP_PRAGMA:
			case SHARP_WARNING:
			case SHARP_IDENT:
			case SHARP_SCCS:
			{
				int c = 0;
				while ((c = nextToken(interested, cpp_reserved_word, 0)) != EOF && c != '\n')
					;
				break;
			}
			case SHARP_IFDEF:
			case SHARP_IFNDEF:
			case SHARP_IF:
			case SHARP_ELIF:
			case SHARP_ELSE:
			case SHARP_ENDIF:
			{
				// mark the rest symbol must cannot be a DEFINE
				// usually, this mark will cancel by the CHAR_NEW_LINE occured
				directiveInfo.shouldIgnoreMacro = TRUE;
				directiveInfo.acceptSymbolAsDefine = FALSE;
				if (cc == SHARP_ELSE || cc == SHARP_ELIF)
					setConditionDirective(directiveInfo, TRUE);
				else if (cc == SHARP_ENDIF)
					setConditionDirective(directiveInfo, FALSE);

				// nest
				if (cc == SHARP_IF || cc == SHARP_IFDEF || cc == SHARP_IFNDEF)
					pushConditionDirective(&directiveInfo);
				else if (cc == SHARP_ENDIF)
					popConditionDirective(&directiveInfo);

				needContinue = TRUE;
				break;
			}
			case SHARP_SHARP:		/* ## */
				(void)nextToken(interested, cpp_reserved_word, 1);
				break;
			case CHAR_NEW_LINE:
			{
				if (directiveInfo.shouldIgnoreMacro && !isConditionDirective(directiveInfo))
				{
					directiveInfo.shouldIgnoreMacro = FALSE;
					directiveInfo.acceptSymbolAsDefine = TRUE;
				}
				needContinue = TRUE;

				break;
			}
			default:
			{
				break;
			}
		}

		if (needContinue || directiveInfo.shouldIgnoreMacro)
		{
			needContinue = FALSE;
			continue;
		}
		break;
	}

	return cc;
}

// process_attribute: skip attributes in __attribute__((...)).
static void process_attribute(const struct parser_param *param)
{
	int brace = 0;
	int c;
	/*
	 * Skip '...' in __attribute__((...))
	 * but pick up symbols in it.
	 */
	while ((c = nextToken("()", cpp_reserved_word, 0)) != EOF) {
		if (c == '(')
			brace++;
		else if (c == ')')
			brace--;
		else if (c == SYMBOL) {
			PUT(PARSER_REF_SYM, token, lineno, sp);
		}
		if (brace == 0)
			break;
	}
}

static void skipToMatch(const struct parser_param *param,
		const char match1, const char match2)
{
#ifdef DEBUG
	skipStackPush(match1, lineno);
#endif
	skipToMatchLevel++;

	int level = 1;
	int cc = -1;
	while (level > 0)
	{
		cc = nextTokenStripMacro(param);
		if (match1 == cc)
			level++;
		else if (match2 == cc)
			level--;
		else if (SYMBOL == cc)
			PUT(PARSER_REF_SYM, token, lineno, sp);
		else if (EOF == cc)
			// trigger eof exception
			longjmp(jmpbuffer, EXCEPTION_EOF);
		else
			; // ignore
	}

#ifdef DEBUG
	skipStackPop();
#endif

	skipToMatchLevel--;
	if (skipToMatchLevel < 0)
		skipToMatchLevel = 0;
}

static void parseInitializer(const struct parser_param *param, StatementInfo *si)
{
	assert(NULL != si && NULL != param);

	// treate all of the SYMBOLs as ref before a ';'
	int cc = -1;
	BOOL shouldBreak = FALSE;
	while (1)
	{
		cc = nextTokenStripMacro(param);
		switch (cc)
		{
			case SYMBOL:
			{
				PUT(PARSER_REF_SYM, token, lineno, sp);
				break;
			}
			case CHAR_COMMA:
			case CHAR_SEMICOLON:
			case CHAR_BRACE_CLOSE:
				shouldBreak = TRUE;
				pushbacktoken();
				break;
			case '[':
				skipToMatch(param, '[', ']');
				break;
			case '(':
				skipToMatch(param, '(', ')');
				break;
			case '{':
				skipToMatch(param, '{', '}');
				break;
			case EOF:
				longjmp(jmpbuffer, EXCEPTION_EOF);
			default:
				break;
		}

		if (shouldBreak)
		{
			break;
		}
	}
}

static void parseParentheses(const struct parser_param *param, StatementInfo *si)
{
	assert(NULL != param && NULL != si);

	tokenInfo *prev = prevToken(si, 1);
	tokenInfo *prev2 = prevToken(si, 2);
	if ((prev->type == TOKEN_NAME || prev->type == TOKEN_KEYWORD) &&
				'*' == peekc(0))
	{
		// a function point
		tokenInfo *current = activeToken(CurrentStatementInfo);
		readParenthesesToken(param, getTokenName(current));
		current->type = TOKEN_PAREN_NAME;
	}
	else if ((prev->type == TOKEN_NAME || TOKEN_PAREN_NAME == prev->type)
			//&& (prev2->type == TOKEN_NAME || TOKEN_KEYWORD == prev2->type)
			)
	{
		// read args
		if (TOKEN_NAME == prev2->type || TOKEN_KEYWORD == prev2->type)
			parseArgs(param, strbuf_value(getTokenName(prev)),
					strbuf_value(getTokenName(prev2)),
					activeToken(CurrentStatementInfo));
		else
			parseArgs(param, strbuf_value(getTokenName(prev)), NULL, activeToken(CurrentStatementInfo));
		// set declaration
		si->declaration = DECL_FUNCTION;
	}
	else
	{
		// treate all SYMBOL as ref
		const char *interested = "()";
		int parenthesesLevel = 1;
		int cc = 0;
		while (parenthesesLevel > 0)
		{
			cc = nextToken(interested, cpp_reserved_word, 0);
			if (EOF == cc)
				longjmp(jmpbuffer, EXCEPTION_EOF);
			else if ('(' == cc)
				parenthesesLevel++;
			else if (')' == cc)
				parenthesesLevel--;
			else if (SYMBOL == cc)
				PUT(PARSER_REF_SYM, token, lineno, sp);
		}
	}
}

static void processInherit(const struct parser_param *param)
{
	int cc = -1;
	while (1)
	{
		cc = nextTokenStripMacro(param);
		switch (cc)
		{
			case SYMBOL:
			{
				PUT(PARSER_REF_SYM, token, lineno, NULL);
				break;
			}
			case CHAR_BRACE_OPEN:
			{
				pushbacktoken();
				return;
			}
			case EOF:
				longjmp(jmpbuffer, EXCEPTION_EOF);
			default:
				break;
		}
	}
}

static void skipStatement(const struct parser_param *param, char endChar)
{
	int cc = -1;

	while (1)
	{
		cc = nextTokenStripMacro(param);
		if (SYMBOL == cc)
			PUT(PARSER_REF_SYM, token, lineno, NULL);
		else if (endChar == cc)
			return;
		else if (EOF == cc)
			longjmp(jmpbuffer, EXCEPTION_EOF);
		else
			;// just ignore
	}
}

// Read args and save to StatementInfo
static void parseArgs(const struct parser_param *param,
		char *prev, char *prev2, tokenInfo *current)
{
	assert(NULL != prev);

	STRBUF *curBuffer = getTokenName(current);
	if (NULL != prev2)
	{
		strbuf_puts(curBuffer, prev2);
		strbuf_putc(curBuffer, ' ');
	}
	strbuf_puts(curBuffer, prev);

	readParenthesesToken(param, curBuffer); 
	current->type = TOKEN_ARGS;
}

static void readParenthesesToken(const struct parser_param *param, STRBUF *buffer)
{
	int len = 0;
	int cc = 0;
	int parenthesesLevel = 1;
	strbuf_putc(buffer, CHAR_PARENTH_OPEN);

	while (parenthesesLevel > 0)
	{
		// never return a line break
		cc = nextTokenStripMacro(param);
		if (SYMBOL == cc)
		{
			strbuf_puts(buffer, token);
			strbuf_putc(buffer, ' ');
		}
		else if (IS_RESERVED_WORD(cc))
		{
			const char *tokenName = getReservedToken(cc);
			if (NULL != tokenName && strlen(tokenName) > 0)
			{
				strbuf_puts(buffer, tokenName);
				strbuf_putc(buffer, ' ');
			}
		}
		else if ('(' == cc)
		{
			parenthesesLevel++;
			strbuf_putc(buffer, (char)cc);
		}
		else if (')' == cc)
		{
			parenthesesLevel--;
			strbuf_putc(buffer, (char)cc);
		}
		else if (isspace(cc))
			// ignore
			continue;
		else if (EOF == cc)
			longjmp(jmpbuffer, EXCEPTION_EOF);
		else
			if (cc < 256)
				// treate as symbol
				strbuf_putc(buffer, (char)cc);
			else
			{
				message("ignore token %d when parse parentheses", cc);
				continue;
			}
	}
}

static void readOperatorFunction(STRBUF *funcName)
{
	assert(NULL != funcName);
	strbuf_puts(funcName, "operator");

	BOOL findOper = FALSE;
	int cc = -1;
	// never return a line break
	while (1)
	{
		cc = nextToken(NULL, NULL, 1);
		if (SYMBOL == cc)
		{
			strbuf_puts(funcName, token);
			strbuf_putc(funcName, ' ');
			findOper = TRUE;
			continue;
		}
		else if (EOF == cc)
			longjmp(jmpbuffer, EXCEPTION_EOF);
		else if (CHAR_PARENTH_OPEN == cc)
			if (findOper)
			{
				pushbacktoken();
				break;
			}
		findOper = TRUE;
		if (cc < 100)
			strbuf_putc(funcName, (char)cc);
		else if (cc >= C_OPERATE && cc < C_OPERATE_END)
			strbuf_puts(funcName, OperateName[cc - C_OPERATE]);
	}
}

static void getFunctionPointName(STRBUF *tokenName, const STRBUF *functionPointName)
{
	// save function point
	int isCopy = 0;
	const char *findStart = strbuf_value(functionPointName);
	while (*(findStart++) != '\0')
		if (isident(*findStart))
		{
			isCopy = 1;
			strbuf_putc(tokenName, *findStart);
		}
		else
			if (isCopy)
				break;
}

int nextToken(const char *interested,
		int (*reserved)(const char *, int), BOOL ignoreNewLine)
{
	int c = nexttoken(interested, reserved);

	switch (c)
	{
		case '=':
		{
			if (nextChar() == '=')
				c = C_JUDGE;
			else
				pushbackChar();
			break;
		}
		case '!':
		{
			if (nextChar() == '=')
				c = C_NOTJUDGE;
			else
				pushbackChar();
			break;
		}
		case '<':
		{
			int cc = nextChar();
			if ('<' == cc)
				c = C_LEFTSHIFT;
			else if ('=' == cc)
				c = C_LESSJUDGE;
			else
				pushbackChar();

			break;
		}
		case '>':
		{
			int cc = nextChar();
			if ('>' == cc)
				c = C_RIGHTSHIFT;
			else if ('=' == cc)
				c = C_BIGJUDGE;
			else
				pushbackChar();
			break;
		}
		default:
			break;
	}

	if (ignoreNewLine && CHAR_NEW_LINE == c)
		return nextToken(interested, reserved, ignoreNewLine);
	else
		return c;
}

static int isident(char c)
{
	// '~" for c++ destructor
	return isalnum(c) || c == '_' || c == '~';
}

