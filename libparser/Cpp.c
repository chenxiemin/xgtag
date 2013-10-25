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

// Is current StatementInfo under the Enum body
#define isInEnum(pStatementInfo) (\
		NULL != (pStatementInfo) && NULL != (pStatementInfo)->scopeParent && \
		DECL_ENUM == (pStatementInfo)->scopeParent->declaration)
// Is current statementinfo under the global
#define isGlobal(pStatementInfo) (\
		NULL == (pStatementInfo) || \
		NULL == (pStatementInfo)->scopeParent || \
		(DECL_FUNCTION != (pStatementInfo)->scopeParent->declaration && \
		DECL_CLASS != (pStatementInfo)->scopeParent->declaration && \
		DECL_STRUCT != (pStatementInfo)->scopeParent->declaration && \
		DECL_UNION != (pStatementInfo)->scopeParent->declaration))
// do we under a Condition Directive of #else, #elif?
#define isConditionDirective(directiveInfo) \
    ((directiveInfo).isInConditionDirective > 0)
#define setConditionDirective(directiveInfo, state) do {\
		(directiveInfo).isInConditionDirective &= \
            ~(1 << ((directiveInfo).nestLevel - 1)); \
		(directiveInfo).isInConditionDirective |= \
            ((int)(state)) << ((directiveInfo).nestLevel - 1); } while(0)
#define LONGJMP(buf, type, linesave) do { strcpy((buf).file, __FILE__); \
    if (0 == (buf).line) (buf).line = __LINE__; (buf).lineSource = (linesave); \
    longjmp((buf.jmpbuffer), (type)); } while(0)


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

typedef struct StrbufListT {
	STRBUF *name;
	struct StrbufListT *next;
} StrbufList;

// One StatementInfo instance means that we under a { } Context
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
	StrbufList *localVariables; // saving local variable definations
	// nest statementinfo use same variable defination list
	// so when before nest statementinfo destoryed
	// child statementinfo use this to retrieve parent variable defines
	StrbufList *nestStart;
	BOOL isInTypedef; // does the StatementInfo occur typedef
	BOOL isInExtern; // does the StatementInfo occur extern
	BOOL isInStructureBody; // we under struct { } [ here ]

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

// jmpbuf for exception handle
static struct JumpBuffer
{
    jmp_buf jmpbuffer;
    int lineSource;
    char file[255];
    int line;
} jmpbuffer;

int skipToMatchLevel = 0; // the level of skipToMatch()
static StatementInfo *CurrentStatementInfo = NULL;
// do we under the assign state
BOOL isInAssign = FALSE;
// convert operator to string
const static char OperateName[][4] = { "==", "<=", ">=", "!=", "<<", ">>" };

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

static inline STRBUF *getTokenName(tokenInfo *token)
{
	assert(NULL != token && NULL != token->name);
	return token->name;
}

// reset a token
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

// set active token in StatementInfo
static inline void setToken(tokenInfo *currentToken, tokenType type,
		const char *buffer, int cc, int lno)
{
    assert(NULL != currentToken);

	currentToken->type = type;
	strbuf_reset(getTokenName(currentToken));
	if (type == TOKEN_NAME || type == TOKEN_KEYWORD)
		strbuf_puts(getTokenName(currentToken), buffer);
	currentToken->cc = cc;
	currentToken->lno = lno;
}

// get [prev] index token in StatementInfo
static inline tokenInfo *prevToken(StatementInfo *pInfo, int prev)
{
	assert(prev < SAVETOKENNUM);

	return &(pInfo->saveToken[(pInfo->tokenIndex +
                SAVETOKENNUM - prev) % SAVETOKENNUM]);
}

// reset StatementInfo
static inline void resetStatementInfo(StatementInfo *pInfo)
{
	pInfo->declaration = DECL_NONE;
	pInfo->isInTypedef = FALSE;
	pInfo->isInExtern = FALSE;
	pInfo->isInStructureBody = FALSE;
}

static inline tokenInfo *activeToken(StatementInfo *pInfo)
{
	return &(pInfo->saveToken[pInfo->tokenIndex]);
}

// foward a token in StatementInfo
static inline void advanceToken(StatementInfo *pInfo)
{
	pInfo->tokenIndex = (pInfo->tokenIndex + 1) % SAVETOKENNUM;
	resetToken(activeToken(pInfo));
}

// reward a token in StatementInfo
static inline void reverseToken(StatementInfo *si)
{
	resetToken(activeToken(si));
	si->tokenIndex = (si->tokenIndex + SAVETOKENNUM - 1) % SAVETOKENNUM;
}

// reset prevCount(s) tokenInfo in StatementInfo
static inline void resetStatementToken(StatementInfo *pInfo, int prevCount)
{
	assert(prevCount < SAVETOKENNUM);

	int i = 0;
	for (; i < prevCount + 1; i++)
		resetToken(prevToken(pInfo, i));
}

// nest check declaration in StatementInfo.scopeParent
static inline BOOL isInScope(StatementInfo *si, declType scope)
{
	if (NULL == si || NULL == si->scopeParent)
		return FALSE;
	else if (scope == si->scopeParent->declaration)
		return TRUE;
	else
		return isInScope(si->scopeParent, scope);
}

// help function to convert cc to tokenType
static inline tokenType getTokenType(int cc)
{
	tokenType type = TOKEN_NONE;
	if (cc == SYMBOL)
		type = TOKEN_NAME;
	else if (cc >= START_WORD && cc < START_SHARP)
		type = TOKEN_KEYWORD;
	else
		type = TOKEN_UNKNOWN;

	return type;
}

// if token could be treate as a variable type
static inline isDefinableKeyword(tokenType type)
{
	return type == TOKEN_KEYWORD || type == TOKEN_NAME;
}

static inline void putSymbol(const struct parser_param *param,
		int type, const char *tag, int lno, const char *line)
{
	param->put(type, tag, lno, curfile, line, param->arg);
}

// is identical char
static inline int isident(char c)
{
	// '~" for c++ destructor
	return isalnum(c) || c == '_' || c == '~';
}

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

// parser file to generate tags
static void createTags(const struct parser_param *param);
// malloc new StatementInfo
// ppCurrentStatementInfo will be changed
static StatementInfo *newStatementInfo(StatementInfo **ppCurrentStatementInfo);
// delete ppCurrentStatementInfo, then replace with its parent
static void delStatementInfo(StatementInfo **ppCurrentStatementInfo,
		const struct parser_param *param);
// delete all StatementInfo
static void delAllStatementInfo(StatementInfo **ppCurrentStatementInfo,
		const struct parser_param *param);
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
// process '{'
static void processBraceOpen(const struct parser_param *param);
// process '}'
static void processBraceClose(const struct parser_param *param);
// process ;,
static void processStatementToken(const struct parser_param *param);
// process class inheritance
static void processInherit(const struct parser_param *);
// skip until a statement end
static void skipStatement(const struct parser_param *, char endChar);
// read args after '('
static void readArgs(const struct parser_param *param,
		char *prev, char *prev2, tokenInfo *current);
// read token in a ()
static void readParenthesesToken(const struct parser_param *param, STRBUF *buffer);
// read function name of Operator function
static void readOperatorFunction(STRBUF *funcName);
// get tag name of a function point
static void getFunctionPointName(STRBUF *tokenName, STRBUF *functionPointName);
// for K&R function prototype, should be int func(foo, bar) int foo; float bar; { }
// return arguments count, 0 indicates ANSI
static int checkKnRFunction(tokenInfo *tokenArgs);
// parse local variable definations in token args
// then add in to list
static void parseVarsInTokenArgs(STRBUF *tokenArgs, StrbufList **ppHead);

static StrbufList *find(StrbufList *head, const char *value)
{
	while (NULL != head)
	{
		if (0 == strcmp(strbuf_value(head->name), value))
			break;
		head = head->next;
	}

	return head;
}

static StrbufList *tail(StrbufList *head)
{
	if (NULL == head)
		return NULL;

	while (NULL != head->next)
		head = head->next;

	return head;
}

static void append(StrbufList **ppHead, STRBUF *value)
{
	assert(NULL != value && NULL != ppHead);
	if (find(*ppHead, strbuf_value(value)))
		return;

	StrbufList *theTail = (StrbufList *)malloc(sizeof(StrbufList));
	if (NULL == theTail)
		die("Cannot malloc space for StrbufList");
	theTail->next = NULL;
	theTail->name = strbuf_open(0);
	strbuf_puts(theTail->name, strbuf_value(value));

	if (NULL == *ppHead)
		*ppHead = theTail;
	else
		tail(*ppHead)->next = theTail;
}

static StrbufList *deleteAfter(StrbufList *tail)
{
	if (NULL == tail)
		return NULL;

	while (NULL != tail)
	{
		StrbufList *del = tail;
		tail = tail->next;
		strbuf_close(del->name);
		free(del);
	}
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
	// set scope info
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
	// set local variable list
	if (NULL != parent)
		if (isInScope(parent, DECL_FUNCTION))
		{
			newInfo->localVariables = parent->localVariables;
			newInfo->nestStart = tail(parent->localVariables);
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
				putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev)),
						prev->lno, NULL);
			strbuf_close(getTokenName(prev));
		}

		// free local variables
		StrbufList *tail = (*ppCurrentStatementInfo)->nestStart;
		if (NULL != tail)
		{
			// retrieve parenet local variable definations if necessary
			deleteAfter(tail->next);
			tail->next = NULL;
		}
		else
		{
			StrbufList *vars = (*ppCurrentStatementInfo)->localVariables;
			if (NULL != vars)
			{
				deleteAfter(vars);
				(*ppCurrentStatementInfo)->localVariables = NULL;
			}
		}

		StatementInfo *parentInfo = (*ppCurrentStatementInfo)->parent;
		free(*ppCurrentStatementInfo);

		*ppCurrentStatementInfo = parentInfo;
	}
}

static void delAllStatementInfo(StatementInfo **ppCurrentStatementInfo,
        const struct parser_param *param)
{
	while (NULL != *ppCurrentStatementInfo)
		delStatementInfo(ppCurrentStatementInfo, param);
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
	int except = setjmp(jmpbuffer.jmpbuffer);
	if (0 == except)
		// parse file to create tags
		createTags(param);
	else if (EXCEPTION_EOF == except)
    {
		// exception handle
		warning("<%s %d>: Unexpected end of file %s at line %d", jmpbuffer.file,
                jmpbuffer.line, param->file, jmpbuffer.lineSource);
        memset(&jmpbuffer, 0, sizeof(jmpbuffer));
    }
	else
		warning("Unknown exception");

	// clear
	delAllStatementInfo(&CurrentStatementInfo, param);
	closetoken();
	// reset env
	skipToMatchLevel = 0;
	isInAssign = FALSE;
	memset(&directiveInfo, 0, sizeof(directiveInfo));
}

static void createTags(const struct parser_param *param)
{
	const char *interested = "{}=,;()~";
	int c = 0;
	int cc = 0;

	while ((cc = cppNextToken(param)) != EOF)
	{
		// save new token if is not brace
		setToken(activeToken(CurrentStatementInfo), getTokenType(cc),
				token, cc, lineno);

		switch (cc)
		{
		case CHAR_BRACE_OPEN:
			processBraceOpen(param);
			break;
		case CHAR_BRACE_CLOSE:
			processBraceClose(param);
            continue; // remaint token space for nestted bracek open
		case CHAR_PARENTH_OPEN:
			parseParentheses(param, CurrentStatementInfo);
			break;
		case CHAR_COMMA:
		case CHAR_SEMICOLON:
		{
			processStatementToken(param);
			// reset env
			if (CHAR_SEMICOLON == cc)
			{
				// reset all of the previous tokens
				resetStatementToken(CurrentStatementInfo, 2);
				resetStatementInfo(CurrentStatementInfo);
				break;
			}
			else
			{
				reverseToken(CurrentStatementInfo);
				continue;
			}
		}
		default:
			break;
		}

		// treate as ref symbol whatever
		tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
		if (SYMBOL == prev2->cc)
		{
			putSymbol(param, PARSER_REF_SYM,
					strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			resetToken(prev2);
		}
		advanceToken(CurrentStatementInfo);
	}
}

static void processBraceOpen(const struct parser_param *param)
{
	tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
	tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
	STRBUF *tokenArgsBuf = NULL;
	// save tag if necessary
	if (TOKEN_ARGS == prev->type && 
			(TOKEN_NAME == prev2->type || TOKEN_PAREN_NAME == prev2->type))
	{
		// save function defination if necessarey
		if (TOKEN_NAME == prev2->type)
		{
			putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev2)),
                    prev2->lno, strbuf_value(getTokenName(prev)));
			tokenArgsBuf = strbuf_open(0);
			strbuf_puts(tokenArgsBuf, strbuf_value(getTokenName(prev)));
		}
		else
		{
			// return type is a function point
			// save function point
			STRBUF *bufName = strbuf_open(0);
			getFunctionPointName(bufName, getTokenName(prev2));
			putSymbol(param, PARSER_DEF, strbuf_value(bufName), prev2->lno,
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
		putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
				prev->lno, NULL);
		CurrentStatementInfo->isInStructureBody = TRUE;
		resetStatementToken(CurrentStatementInfo, 2);
	}

	CurrentStatementInfo->isInExtern = FALSE;
	// new context
	newStatementInfo(&CurrentStatementInfo);
	
	// add function args list into statement info
	if (NULL != tokenArgsBuf)
	{
		parseVarsInTokenArgs(tokenArgsBuf,
				&(CurrentStatementInfo->localVariables));
		// free token args
		strbuf_close(tokenArgsBuf);
	}
}

static void processBraceClose(const struct parser_param *param)
{
	// set token rest if necessary
	tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
	if (SYMBOL == prev->cc && isInEnum(CurrentStatementInfo))
	{
		// save
		putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
                prev->lno, NULL);
		resetToken(prev);
	}

	// delete context
	delStatementInfo(&CurrentStatementInfo, param);
	if (NULL == CurrentStatementInfo)
		newStatementInfo(&CurrentStatementInfo);
}

static void processStatementToken(const struct parser_param *param)
{
	// save token if necessarey
	tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
	tokenInfo *prev2 = prevToken(CurrentStatementInfo, 2);
	if (isGlobal(CurrentStatementInfo))
		// global scope, save struct, variable define, etc
		if (TOKEN_NAME == prev->type &&
				CurrentStatementInfo->isInStructureBody)
			// struct variable define
			putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
                    prev->lno, NULL);
		else if (TOKEN_PAREN_NAME == prev2->type && TOKEN_ARGS == prev->type)
		{
			// save global function point
			STRBUF *bufName = strbuf_open(0);
			getFunctionPointName(bufName, getTokenName(prev2));
			putSymbol(param, PARSER_DEF, strbuf_value(bufName), prev->lno,
					strbuf_value(getTokenName(prev)));
			strbuf_close(bufName);
		}
		else if (TOKEN_NAME == prev->type &&
				(CPP_NAMESPACE == prev2->cc | CPP_USING == prev2->cc))
			// ignore namespace
			putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev)),
					prev->lno, NULL);
		else if (TOKEN_NAME == prev->type &&
				isDefinableKeyword(prev2->type) &&
				!CurrentStatementInfo->isInExtern)
			// save global variable
			putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
					prev->lno, NULL);
		else if (TOKEN_NAME == prev->type &&
				isInEnum(CurrentStatementInfo))
			// put enum defination
			putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
					prev->lno, NULL);
		else
		{
			// treate as ref
			if (TOKEN_NAME == prev->type)
				putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev)),
						prev->lno, NULL);
			if (TOKEN_NAME == prev2->type)
				putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev2)),
						prev2->lno, NULL);
		}
	else if (TOKEN_NAME == prev->type && isInEnum(CurrentStatementInfo))
		// save enum member
		putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
                prev->lno, NULL);
	else if (TOKEN_NAME == prev->type && CurrentStatementInfo->isInTypedef)
	{
		// save typedef
		if (TOKEN_NAME == prev->type)
			putSymbol(param, PARSER_DEF, strbuf_value(getTokenName(prev)),
					prev->lno, NULL);
		if (TOKEN_NAME == prev2->type)
			putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev2)),
					prev2->lno, NULL);
	}
	else
	{
		// TODO distingush . / -> / ::
		// save local variable defination for ref symbol filter
		if (isInScope(CurrentStatementInfo, DECL_FUNCTION))
		{
			if (TOKEN_NAME == prev->type && isDefinableKeyword(prev2->type) &&
					!CurrentStatementInfo->isInExtern)
			{
				// save local variable as necessary
				append(&(CurrentStatementInfo->localVariables),
                        getTokenName(prev));
				// add token type reference
				if (TOKEN_NAME == prev2->type)
					putSymbol(param, PARSER_REF_SYM,
                            strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			}
			else
			{
				if (TOKEN_NAME == prev->type && NULL ==
						find(CurrentStatementInfo->localVariables,
							strbuf_value(getTokenName(prev))))
					putSymbol(param, PARSER_REF_SYM,
                            strbuf_value(getTokenName(prev)), prev->lno, NULL);
				if (TOKEN_NAME == prev2->type && NULL ==
						find(CurrentStatementInfo->localVariables,
							strbuf_value(getTokenName(prev2))))
					putSymbol(param, PARSER_REF_SYM,
                            strbuf_value(getTokenName(prev2)), prev2->lno, NULL);
			}
		}
		else
		{
			// save ref if necessary
			if (TOKEN_NAME == prev->type)
				putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev)),
						prev->lno, NULL);
			if (TOKEN_NAME == prev2->type)
				putSymbol(param, PARSER_REF_SYM, strbuf_value(getTokenName(prev2)),
						prev2->lno, NULL);
		}
	}
}

static void skipToMatch(const struct parser_param *param,
		const char match1, const char match2)
{
#ifdef DEBUG
	skipStackPush(match1, lineno);
#endif
	skipToMatchLevel++;
    int lineSave = lineno;

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
		{
			// TODO check to filter function local variable reference
			if (NULL == find(CurrentStatementInfo->localVariables, token))
				putSymbol(param, PARSER_REF_SYM, token, lineno, sp);
		}
		else if (EOF == cc)
			// trigger eof exception
			LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
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
    int lineSave = lineno;
	while (1)
	{
		cc = nextTokenStripMacro(param);
		switch (cc)
		{
			case SYMBOL:
				// TODO check to filter function local variable reference
				if (NULL == find(CurrentStatementInfo->localVariables, token))
					putSymbol(param, PARSER_REF_SYM, token, lineno, sp);
				break;
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
				LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
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

    int lineSave = lineno;
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
            readArgs(param, strbuf_value(getTokenName(prev)),
                    strbuf_value(getTokenName(prev2)), 
                    activeToken(CurrentStatementInfo));
        else
            readArgs(param, strbuf_value(getTokenName(prev)),
                    NULL, activeToken(CurrentStatementInfo));
        // set declaration
        si->declaration = DECL_FUNCTION;
        
        if ((TOKEN_NAME == prev2->type || TOKEN_KEYWORD == prev2->type) &&
                TOKEN_NAME == prev->type && (NULL == strchr("{},=;", peekc(0))) &&
                !isInScope(CurrentStatementInfo, DECL_FUNCTION))
        {
            // skip K&R arguments prototype
            tokenInfo *active = activeToken(CurrentStatementInfo);
            int knr = checkKnRFunction(active);
            if (0 != knr)
                while (knr-- > 0)
                    skipStatement(param, CHAR_SEMICOLON);
        }
        // skip symbol after args
        int cc = 0;
        while (cc = nextTokenStripMacro(param))
        {
            if (SYMBOL == cc || IS_RESERVED_WORD(cc))
                putSymbol(param, PARSER_REF_SYM, token, lineno, sp);
            else
            {
                pushbacktoken();
                break;
            }
        }
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
				LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
			else if ('(' == cc)
				parenthesesLevel++;
			else if (')' == cc)
				parenthesesLevel--;
			else if (SYMBOL == cc)
				// TODO check to filter function local variable reference
				if (NULL == find(CurrentStatementInfo->localVariables, token))
					putSymbol(param, PARSER_REF_SYM, token, lineno, sp);
		}
	}
}

static void processInherit(const struct parser_param *param)
{
	int cc = -1;
    int lineSave = lineno;
	while (1)
	{
		cc = nextTokenStripMacro(param);
		switch (cc)
		{
			case SYMBOL:
			{
				putSymbol(param, PARSER_REF_SYM, token, lineno, NULL);
				break;
			}
			case CHAR_BRACE_OPEN:
			{
				pushbacktoken();
				return;
			}
			case EOF:
				LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
			default:
				break;
		}
	}
}

static void skipStatement(const struct parser_param *param, char endChar)
{
	int cc = -1;
    int lineSave = lineno;

	while (1)
	{
		cc = nextTokenStripMacro(param);

		// TODO check to filter function local variable reference
		if (SYMBOL == cc)
		{
			if (NULL == find(CurrentStatementInfo->localVariables, token))
				putSymbol(param, PARSER_REF_SYM, token, lineno, NULL);
		}
		else if (endChar == cc)
			return;
		else if (EOF == cc)
			LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
		else
			;// just ignore
	}
}

// Read args and save to StatementInfo
static void readArgs(const struct parser_param *param,
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
    int lineSave = lineno;
	strbuf_putc(buffer, CHAR_PARENTH_OPEN);

	while (parenthesesLevel > 0)
	{
		// never return a line break
		cc = nextTokenStripMacro(param);
		if (SYMBOL == cc)
		{
			strbuf_puts(buffer, token);
			strbuf_putc(buffer, ' ');
            // put symbol if necessary
			if (NULL == find(CurrentStatementInfo->localVariables, token))
				putSymbol(param, PARSER_REF_SYM, token, lineno, sp);
		}
		else if (IS_RESERVED_WORD(cc))
		{
            // put keyword
			strbuf_puts(buffer, token);
			strbuf_putc(buffer, ' ');
		}
		else if ('(' == cc)
		{
			parenthesesLevel++;
			strbuf_putc(buffer, (char)cc);
		}
		else if (')' == cc)
		{
			parenthesesLevel--;
			strbuf_unputc(buffer, ' ');
			strbuf_putc(buffer, (char)cc);
		}
		else if (CHAR_COMMA == cc)
		{
			// unput space before symbol inserted
			strbuf_unputc(buffer, ' ');
			strbuf_putc(buffer, (char)cc);
			strbuf_putc(buffer, ' ');
		}
		else if (isspace(cc))
			continue; // ignore
		else if (EOF == cc)
			LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
		else if (cc < 256) // remain last process
			strbuf_putc(buffer, (char)cc); // anyway char treate as symbol
		else
			message("ignore token %d when parse parentheses", cc);
	}
}

static void readOperatorFunction(STRBUF *funcName)
{
	assert(NULL != funcName);
	strbuf_puts(funcName, "operator");

	BOOL findOper = FALSE;
	int cc = -1;
    int lineSave = lineno;
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
			LONGJMP(jmpbuffer, EXCEPTION_EOF, lineSave);
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

static void getFunctionPointName(STRBUF *tokenName, STRBUF *functionPointName)
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
            {
                // check keyword
                if (cpp_reserved_word(strbuf_value(tokenName),
                            strlen(strbuf_value(tokenName))))
                {
                    strbuf_reset(tokenName);
                    isCopy = 0;
                }
                else
                    break;
            }
}

static int checkKnRFunction(tokenInfo *tokenArgs)
{
    if (NULL == tokenArgs || TOKEN_ARGS != tokenArgs->type)
        return 0;

    int knrCount = 0;
    const char *args = strbuf_value(tokenArgs->name);
    BOOL acceptIdent = FALSE;
    BOOL findParenth = FALSE;

    do
    {
        if (CHAR_PARENTH_OPEN == *args)
            findParenth = TRUE;
        if (!findParenth)
            continue;

        if (isident(*args))
            // accept ident by skipping
            if (!acceptIdent)
                while ('\0' != *(args + 1))
                {
                    if (!isident(*(args + 1)))
                    {
                        // todo check first reserved word
                        acceptIdent = TRUE;
                        break;
                    }
                    args++;
                }
            else
                return 0;
        else if (CHAR_COMMA == *args)
        {
            if (acceptIdent)
                knrCount++;
            acceptIdent = FALSE;
        }
        else if (isspace(*args))
            ; // just ignore
        else if (CHAR_PARENTH_CLOSE == *args)
        {
            if (acceptIdent)
                knrCount++;
            acceptIdent = FALSE;
        }
        else
            if (acceptIdent)
                return 0;
    } while ('\0' != *(++args));

    // last param
    if (acceptIdent)
        knrCount++;

    return knrCount;
}

static void parseVarsInTokenArgs(STRBUF *tokenArgs, StrbufList **ppHead)
{
	assert(NULL != tokenArgs && NULL != ppHead);
	const char *start = strbuf_value(tokenArgs);
	const char *end = start + strlen(start) - 1;
	BOOL readLast = TRUE;
	BOOL readLocal = FALSE;
	STRBUF *tmpLocal = strbuf_open(0);
	for (; end >= start; end--)
	{
		if (CHAR_PARENTH_CLOSE == *end && readLast)
		{
			readLocal = TRUE;
			readLast = FALSE;
			continue;
		}
		if (CHAR_COMMA == *end)
		{
			readLocal = TRUE;
			continue;
		}

		// read local symbol
		if (readLocal)
		{
			if (isident(*end))
				strbuf_putc(tmpLocal, *end);
			else
			{
				strbuf_reverse(tmpLocal);
				append(ppHead, tmpLocal);
				strbuf_reset(tmpLocal);
				readLocal = FALSE;
			}
		}
	}

	strbuf_close(tmpLocal);
}

static int cppNextToken(const struct parser_param *param)
{
	int cc = -1;
	BOOL needContinue = FALSE;
	while ((cc = nextTokenStripMacro(param)) != EOF)
	{
		switch (cc)
		{
        case '*':
            needContinue = TRUE;
            break;
		case '[':
			skipToMatch(param, '[', ']');
			needContinue = TRUE;
			break;
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
			isInAssign = FALSE;
			break;
		case CHAR_COLON:
		{
			// read class inherit if necessary
			tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			if ((DECL_CLASS == CurrentStatementInfo->declaration ||
                    DECL_STRUCT == CurrentStatementInfo->declaration) &&
                    SYMBOL == prev->cc)
				processInherit(param);
			else if (TOKEN_ARGS == prev->type)
				// skip initializer list
				processInherit(param);
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
			CurrentStatementInfo->declaration = DECL_UNION;
			break;
		case CPP_STRUCT:
			CurrentStatementInfo->declaration = DECL_STRUCT;
			break;
		case CPP_ENUM:
			CurrentStatementInfo->declaration = DECL_ENUM;
			break;
		case CPP_CLASS:
			CurrentStatementInfo->declaration = DECL_CLASS;
			break;
		case CPP_NAMESPACE:
			CurrentStatementInfo->declaration = DECL_NAMESPACE;
			break;
		case CPP_WCOLON:
        {
			// ignore namespace
            tokenInfo *prev = prevToken(CurrentStatementInfo, 1);
			if (prev->type == TOKEN_NAME)
            {
                // put symbol as reference
                putSymbol(param, PARSER_REF_SYM, strbuf_value(prev->name),
                        prev->lno, sp);
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
			CurrentStatementInfo->isInTypedef = TRUE;
			needContinue = TRUE;
			break;
		case CPP_EXTERN:
			CurrentStatementInfo->isInExtern = TRUE;
			needContinue = TRUE;
			break;
		case CPP_TYPENAME:
		case CPP_TEMPLATE:
		case CPP_VOLATILE:
		case CPP_CONST:
			// ignore
			needContinue = TRUE;
			break;
		case CPP_IF:
		case CPP_FOR:
		case CPP_SWITCH:
		case CPP_WHILE:
        case CPP___ATTRIBUTE__:
		{
			// ignore line break
			int c = 0;
			do
			{
				c = nextTokenStripMacro(param);
				if (!isspace(c))
					break;
			} while(1);

			if (CHAR_PARENTH_OPEN == c)
				skipToMatch(param, '(', ')');
			else
				pushbacktoken();
			break;
		}
		case CPP_CASE:
			skipStatement(param, CHAR_COLON);
			break;
		case CPP_RETURN:
			skipStatement(param, CHAR_SEMICOLON);
			break;
		case CPP_THROW:
        case CPP_NEW:
			needContinue = TRUE;
			break;
		case CPP_ASM:
		{
			// skip space
			int c = 0;
			do
			{
				c = nextTokenStripMacro(param);
				if (!isspace(c))
					break;
			} while(1);

			// we dont support asm yet
			if (CHAR_BRACE_OPEN == c)
				// ignore
				skipToMatch(param, '{', '}');
			else
				pushbacktoken();
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
	const char *interested = "{}=,;:()~[]<>*";
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
					putSymbol(param, PARSER_DEF, token, lineno, sp);
					directiveInfo.acceptSymbolAsDefine = FALSE;
				}
				else
					// treate symbol as ref in directive context
					putSymbol(param, PARSER_REF_SYM, token, lineno, sp);

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
				while ((c = nextToken(interested, cpp_reserved_word,
                                0)) != EOF && c != '\n')
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
				if (directiveInfo.shouldIgnoreMacro &&
                        !isConditionDirective(directiveInfo))
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

