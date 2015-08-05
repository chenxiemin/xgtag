/*
 * Copyright (c) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2006, 2008,
 *	2009, 2010, 2012 Tama Communications Corporation
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
#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <utime.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#include "getopt.h"
#include "global.h"
#include "parser.h"
#include "const.h"
#include "opt.h"

#include "wparser.h"
#include "wpath.h"
#include "project.h"

PParser GlobalParser = NULL;
PProjectContext GlobalProject = NULL;
PWPath GlobalPath = NULL;

int main(int, char **);
static void parseOptions(int argc, char **argv);
static void parseGlobalOptions(int argc, char **argv);
static void usage(void);
static void help(void);
int incremental(const char *, const char *);
void updateTags(const char *, const char *, IDSET *, STRBUF *, int);
void createTags(const char *, const char *);
int printConfig(const char *);

int statistics = STATISTICS_STYLE_NONE;

char dbpath[MAXPATHLEN];
char cwd[MAXPATHLEN];

int main(int argc, char **argv)
{
    // phase 1
	// Start statistics.
	init_statistics();

    // load configuration file.
    openconf();

    // read options
    parseOptions(argc, argv);

    // cleanup phase2
	closeconf();

	return 0;
}

/*
 * incremental: incremental update
 *
 *	i)	dbpath	dbpath directory
 *	i)	root	root directory of source tree
 *	r)		0: not updated, 1: updated
 */
int incremental(const char *dbpath, const char *root)
{
    STRBUF *addlist = strbuf_open(0);
    const char *path;
    unsigned int id, limit;

    // The list of the path name which should be deleted from GPATH.
    IDSET *deleteset = idset_open(gpath_nextkey());
    // The list of the path name which exists in the current project.
    // A project is limited by the --file option.
    IDSET *findset = idset_open(gpath_nextkey());
    int total = 0;

    // Make add list and delete list for update.
    if (O.c.file_list)
        find_open_filelist(O.c.file_list, root);
    else
        find_open(NULL);
    while ((path = find_read()) != NULL) {
        // get file type
        WPATH_SOURCE_TYPE_T type = wpath_getSourceType(path);
        if (WPATH_SOURCE_TYPE_SOURCE != type) {
            LOGD("Ingnore source type %d: %s", type, path);
            continue;
        }

        // check file exist
        const char *fid = wpath_getID(GlobalPath, path);
        if (NULL != fid) {
            // add to findset
            idset_add(findset, atoi(fid));

            // already exist, check modified
            if (wpath_isModified(GlobalPath, path)) {
                // put file to delete list if already exist
                idset_add(deleteset, atoi(fid));
            } else {
                LOGD("Unchanged file since last scanning: %s", path);
                continue; // skip file pending to add list
            }
        }

        // if file not exist or modified, put to add list
        total++;
        strbuf_puts0(addlist, path);
    }
    find_close();

    // make delete list.
    limit = gpath_nextkey();
    for (id = 1; id < limit; id++) {
        char fid[MAXFIDLEN];
        int type;

        snprintf(fid, sizeof(fid), "%d", id);

        // This is a hole of GPATH. The hole increases if the deletion
        // and the addition are repeated.
        if ((path = gpath_fid2path(fid, &type)) == NULL)
            continue;

        // The file which does not exist in the findset is treated
        // assuming that it does not exist in the file system.
        if (!idset_contains(findset, id) || !test("f", path))
            idset_add(deleteset, id);
    }

    // update tag
    if (!idset_empty(deleteset) || strbuf_getlen(addlist) > 0)
        updateTags(dbpath, root, deleteset, addlist, total);

    strbuf_close(addlist);
    idset_close(deleteset);
    idset_close(findset);

    return 1;
}

/*
 * updateTags: update tag file.
 *
 *	i)	dbpath		directory in which tag file exist
 *	i)	root		root directory of source tree
 *	i)	deleteset	bit array of fid of deleted or modified files 
 *	i)	addlist		\0 separated list of added or modified files
 */
void updateTags(const char *dbpath, const char *root,
        IDSET *deleteset, STRBUF *addlist, int total)
{
	const char *path, *start, *end;

    LOGD("Updating '%s' and '%s'.\n", dbname(GTAGS), dbname(GRTAGS));
	
	// Delete tags from project
	if (!idset_empty(deleteset)) {
        int res = project_del_set(GlobalProject, deleteset);
        if (0 != res)
            LOGE("Cannot delete deleteset: %p", deleteset);
	}
	
	// Add tags to GTAGS and GRTAGS.
	start = strbuf_value(addlist);
	end = start + strbuf_getlen(addlist);

	int seqno = 0;
	for (path = start; path < end; path += strlen(path) + 1) {
        LOGD("[%d/%d] extracting tags of %s", ++seqno, total, path + 2);

        int res = project_add(GlobalProject, path);
        if (0 != res)
            LOGE("Cannot add file into project: %s", path);
	}
}

/*
 * createTags: create tags file
 *
 *	i)	dbpath	dbpath directory
 *	i)	root	root directory of source tree
 */
void createTags(const char *dbpath, const char *root)
{
	STATISTICS_TIME *tim;
	const char *path;
    char realPath[MAXPATHLEN + 1];
    char normalPath[MAXPATHLEN + 1];
    char rootSlash[MAXPATHLEN + 1];

    sprintf(rootSlash, "%s/", root);
	
	// Add tags to GTAGS and GRTAGS.
	if (O.c.file_list)
		find_open_filelist(O.c.file_list, root);
    else
		find_open(NULL);

	int seqno = 0;
	while ((path = find_read()) != NULL) {
        WPATH_SOURCE_TYPE_T type = wpath_getSourceType(path);
        if (WPATH_SOURCE_TYPE_SOURCE != type) {
            LOGD("Ignore source type %d: %s", type, path);
            continue;
        }

        LOGD(" [%d] extracting tags of %s", ++seqno, path + 2);

        project_add(GlobalProject, path);
	}
	
    // clean up
	find_close();
}

/*
 * printConfig: print configuration data.
 *
 *	i)	name	label of config data
 *	r)		exit code
 */
int printConfig(const char *name)
{
	int num;
	int exist = 1;

	if (getconfn(name, &num))
		fprintf(stdout, "%d\n", num);
	else if (getconfb(name))
		fprintf(stdout, "1\n");
	else {
		STRBUF *sb = strbuf_open(0);
		if (getconfs(name, sb))
			fprintf(stdout, "%s\n", strbuf_value(sb));
		else
			exist = 0;
		strbuf_close(sb);
	}
	return exist;
}

typedef void (*cmd_parser)(int argc, char **argv);
typedef void (*cmd_do)();

typedef struct
{
    const char *cmdShort;
    const char *cmdLong;
    cmd_parser parser;
    cmd_do docmd;
} CommandParseList;

static void createCommandParser(int argc, char **argv);
static void createCommandDo();
static void dumpCommandParser(int argc, char **argv);
static void searchCommandParser(int argc, char **argv);
static void dumpCommandDo();
static void searchCommandDo();

static void parseOptions(int argc, char **argv)
{
    static CommandParseList CommandList[] = {
        { "d", "dump", dumpCommandParser, dumpCommandDo },
        { "s", "search", searchCommandParser, searchCommandDo },
        { "c", "create", createCommandParser, createCommandDo }
    };

    // prepare path
	if (!getcwd(cwd, MAXPATHLEN))
		die("cannot get current directory.");
	canonpath(cwd);
    strlimcpy(dbpath, cwd, sizeof(dbpath));

    if (argc > 1) {
        int i = 0;
        int cmdSize = (int)sizeof(CommandList) / (int)sizeof(CommandParseList);
        for (i = 0; i < cmdSize; i++) {
            if (0 == strcmp(CommandList[i].cmdShort, argv[1]) ||
                    0 == strcmp(CommandList[i].cmdLong, argv[1])) {
                // move args
                argc--;
                argv++;

                // do parser and execute
                optind = 1;
                CommandList[i].parser(argc, argv);
                CommandList[i].docmd();

                exit(0);
            }
        }
    }

    // global options
    parseGlobalOptions(argc, argv);
}

static void parseGlobalOptions(int argc, char **argv)
{
#define GLOBAL_OPTION_VERSION 150
#define GLOBAL_OPTION_HELP 151
#define GLOBAL_OPTION_DEBUG 152
    static struct option global_long_options[] = {
        { "version", no_argument, NULL, GLOBAL_OPTION_VERSION },
        { "help", no_argument, NULL, GLOBAL_OPTION_HELP },
        { "debug", required_argument, NULL, GLOBAL_OPTION_DEBUG },
        NULL
    };

    char optchar = 0;
    int option_index = 0;
	while ((optchar = getopt_long(argc, argv, "vh",
                    global_long_options, &option_index)) != EOF) {
        switch ((unsigned char)optchar) {
        case 'v':
        case GLOBAL_OPTION_VERSION:
            version(NULL, 1);
            break;
        case GLOBAL_OPTION_DEBUG:
			if (0 == strcmp(optarg, "debug"))
                setverbose();
            else if (0 == strcmp(optarg, "info"))
                setdebug();
            else if (0 == strcmp(optarg, "error"))
                setquiet();
            else
                die("invalid argument for debug: %s", optarg);
            break;
        case 'h':
        case GLOBAL_OPTION_HELP:
            help();
            break;
        default:
            break;
        }
    }
}

static void createCommandParser(int argc, char **argv)
{
    char optchar = 0;
    int option_index = 0;
	while ((optchar = getopt_long(argc, argv, "u:",
                    NULL, &option_index)) != EOF) {
        switch ((unsigned char)optchar) {
        case 'u':
            O.c.iflag = 1;
            O.c.single_update = optarg;
            break;
        default:
            break;
        }
    }
    optind--;
    argc -= optind;
    argv += optind;
}

static void createCommandDo()
{
    // phase 2
    // init parser
    GlobalParser = wparser_open();
    if (NULL == GlobalParser) {
        LOGE("Cannot open parser");
        return;
    }
    // decide mode
    WPATH_MODE_T mode = WPATH_MODE_CREATE;
    if (O.c.iflag)
        mode = WPATH_MODE_MODIFY;
    // init wpath
    GlobalPath = wpath_open(dbpath, cwd, mode);
    if (NULL == GlobalPath) {
        LOGE("Cannot open wpath");
        return;
    }
    // init project
    GlobalProject = project_open(0, dbpath, cwd, mode);
    if (NULL == GlobalProject)
        die("Cannot open project: %s %s", dbpath, cwd);
    GlobalProject->parser = GlobalParser;
    GlobalProject->path = GlobalPath;

    if (O.c.iflag)
        incremental(dbpath, cwd);
    else
        createTags(dbpath, cwd);

    wpath_close(&GlobalPath);
    project_close(&GlobalProject);
    wparser_close(&GlobalParser);
}

static void dumpCommandParser(int argc, char **argv)
{
    if (argc < 2)
        usage();

	int option_index = 0;
    char optchar = '0';
	while ((optchar = getopt_long(argc, argv, "c",
                    NULL, &option_index)) != EOF) {
        switch (optchar) {
        case 'c':
			O.d.show_config = 1;
			if (optarg)
				O.d.config_name = optarg;
            break;
        default:
            break;
        }
    }

    optind--;
    argc -= optind;
    argv += optind;

    if (argc > 1)
        O.d.dump_target = argv[1];
}

static void dumpCommandDo()
{
    if (NULL != O.d.dump_target) {
        /*
         * Dump a tag file.
         */
        DBOP *dbop = NULL;
        const char *dat = 0;
        int is_gpath = 0;

        if (!test("f", O.d.dump_target))
            die("file '%s' not found.", O.d.dump_target);
        if ((dbop = dbop_open(O.d.dump_target,
                        0, 0, DBOP_RAW)) == NULL)
            die("file '%s' is not a tag file.", O.d.dump_target);
        /*
         * The file which has a NEXTKEY record is GPATH.
         */
        if (dbop_get(dbop, NEXTKEY))
            is_gpath = 1;
		for (dat = dbop_first(dbop, NULL, NULL, 0);
                dat != NULL; dat = dbop_next(dbop)) {
			const char *flag = is_gpath ? dbop_getflag(dbop) : "";

			if (*flag)
				printf("%s\t%s\t%s\n", dbop->lastkey, dat, flag);
			else
				printf("%s\t%s\n", dbop->lastkey, dat);
		}
		dbop_close(dbop);
    } else if (O.d.show_config) {
		if (O.d.config_name)
			printConfig(O.d.config_name);
		else
			printf("%s\n", getconfline());
    }
}

static void searchCommandParser(int argc, char **argv)
{
#define RESULT		128
    static struct option const search_long_options[] = {
        {"result", required_argument, NULL, RESULT},
        NULL
    };

    if (argc < 2)
        usage();

    // default format
    O.s.format = FORMAT_CTAGS;
    O.s.type = PATH_RELATIVE;

	int option_index = 0;
    char optchar = '0';
	while ((optchar = getopt_long(argc, argv, "ape",
                    search_long_options, &option_index)) != EOF) {
        switch ((unsigned char)optchar) {
        case 'a':
            O.s.type = PATH_ABSOLUTE;
            break;
        case 'e':
            O.s.eflag = 1;
            break;
        case 'p':
			O.s.Pflag++;
            O.s.format = FORMAT_PATH; // default path format
            break;
        case RESULT:
			if (!strcmp(optarg, "ctags-x"))
				O.s.format = FORMAT_CTAGS_X;
			else if (!strcmp(optarg, "ctags-xid"))
				O.s.format = FORMAT_CTAGS_XID;
			else if (!strcmp(optarg, "ctags"))
				O.s.format = FORMAT_CTAGS;
			else if (!strcmp(optarg, "ctags-mod"))
				O.s.format = FORMAT_CTAGS_MOD;
			else if (!strcmp(optarg, "path"))
				O.s.format = FORMAT_PATH;
			else if (!strcmp(optarg, "grep"))
				O.s.format = FORMAT_GREP;
			else if (!strcmp(optarg, "cscope"))
				O.s.format = FORMAT_CSCOPE;
			else
				die_with_code(2, "unknown format type");
			break;
        default:
            break;
        }
    }

    optind--;
    argc -= optind;
    argv += optind;

    if (argc != 2)
        die("invalid param");

    // create pattern
    if (O.s.eflag)
        snprintf(O.s.pattern, MAX_PATTERN_LENGTH, "%s", argv[1]);
    else
        snprintf(O.s.pattern, MAX_PATTERN_LENGTH, "%s$", argv[1]);
    LOGD("Use search pattern: %s", O.s.pattern);
}

static void searchCommandDo()
{
    // open context
    POutput pout = NULL;
    PProjectContext pcontext = NULL;
    PWPath wpath = NULL;

    do {
        pout = output_open(O.s.type, O.s.format,
                cwd, cwd, dbpath, stdout, GTAGS);
        if (NULL == pout) {
            LOGE("Cannot open output");
            break;
        }

        pcontext = project_open(0, cwd, dbpath, WPATH_MODE_READ);
        if (NULL == pcontext) {
            LOGE("Cannot create context");
            break;
        }

        wpath = wpath_open(dbpath, cwd, WPATH_MODE_READ);
        if (NULL == wpath) {
            LOGE("Cannot open wpath");
            break;
        }
        pcontext->path = wpath;

        // select
        int res = -1;
        if (O.s.Pflag)
            res = project_select(pcontext, O.s.pattern,
                    SEL_TYPE_PATH, GTAGS, pout);
        else
            res = project_select(pcontext, O.s.pattern,
                    SEL_TYPE_DEFINE, GTAGS, pout);
        if (0 != res)
            LOGE("Cannot select %s from project: %d", O.s.pattern, res);
    } while(0);

    // close context
    wpath_close(&wpath);
    project_close(&pcontext);
    output_close(&pout);
}

static void usage(void)
{
    fputs(usage_const, stderr);
	exit(2);
}

static void help(void)
{
	fputs(usage_const, stdout);
	fputs(help_const, stdout);
	exit(0);
}

