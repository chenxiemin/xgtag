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
static void dumpCommandDo();
static void readOptions(int argc, char **argv);
static void usage(void);
static void help(void);
int incremental(const char *, const char *);
void updateTags(const char *, const char *, IDSET *, STRBUF *, int);
void createTags(const char *, const char *);
int printConfig(const char *);


int statistics = STATISTICS_STYLE_NONE;

char dbpath[MAXPATHLEN];
char cwd[MAXPATHLEN];

#define GTAGSFILES "gtags.files"

static struct option const long_options[] = {
	/*
	 * These options have long name and short name.
	 * We throw them to the processing of short options.
	 *
	 * Though the -o(--omit-gsyms) was removed, this code
	 * is left for compatibility.
	 */
	{"compact", no_argument, NULL, 'c'},
	{"dump", required_argument, NULL, 'd'},
	{"file", required_argument, NULL, 'f'},
	// {"idutils", no_argument, NULL, 'I'},
	{"incremental", no_argument, NULL, 'i'},
	{"max-args", required_argument, NULL, 'n'},
	{"omit-gsyms", no_argument, NULL, 'o'},		/* removed */
	{"objdir", no_argument, NULL, 'O'},
	{"quiet", no_argument, NULL, 'q'},
	{"verbose", no_argument, NULL, 'v'},
	{"warning", no_argument, NULL, 'w'},

	/*
	 * The following are long name only.
	 */
#define OPT_CONFIG		128
#define OPT_GTAGSCONF		129
#define OPT_GTAGSLABEL		130
// #define OPT_PATH		131
#define OPT_SINGLE_UPDATE	132
#define OPT_ENCODE_PATH		133
#define OPT_ACCEPT_DOTFILES	134
	/* flag value */
	{"accept-dotfiles", no_argument, NULL, OPT_ACCEPT_DOTFILES},
	{"statistics", no_argument, &statistics, STATISTICS_STYLE_TABLE},

	/* accept value */
	{"config", optional_argument, NULL, OPT_CONFIG},
	{"encode-path", required_argument, NULL, OPT_ENCODE_PATH},
	{"gtagsconf", required_argument, NULL, OPT_GTAGSCONF},
	{"gtagslabel", required_argument, NULL, OPT_GTAGSLABEL},
	// {"path", required_argument, NULL, OPT_PATH},
	{"single-update", required_argument, NULL, OPT_SINGLE_UPDATE},
	{ 0 }
};

// static const char *langmap = DEFAULTLANGMAP;
// static const char *gtags_parser;

int main(int argc, char **argv)
{
    // phase 1
	// Start statistics.
	init_statistics();
    // read options
    parseOptions(argc, argv);
    // load configuration file.
    openconf();

    // phase 2
    // init parser
    GlobalParser = wparser_open();
    if (NULL == GlobalParser) {
        LOGE("Cannot open parser");
        return -1;
    }
    // decide mode
    WPATH_MODE_T mode = WPATH_MODE_CREATE;
    if (O.c.iflag)
        mode = WPATH_MODE_MODIFY;
    // init wpath
    GlobalPath = wpath_open(dbpath, cwd, mode);
    if (NULL == GlobalPath) {
        LOGE("Cannot open wpath");
        return -1;
    }
    // init project
    GlobalProject = project_open(0, dbpath, cwd, mode);
    if (NULL == GlobalProject)
        die("Cannot open project: %s %s", dbpath, cwd);
    GlobalProject->parser = GlobalParser;
    GlobalProject->path = GlobalPath;

    // make tag processing
    if (O.c.iflag) {
        LOGD("Incremental updating for tag: %s", dbpath);
        if (O.c.single_update) {
            // get source type
            WPATH_SOURCE_TYPE_T type = wpath_getSourceType(O.c.single_update);
            if (WPATH_SOURCE_TYPE_SOURCE == type)
                project_update(GlobalProject, O.c.single_update);
            else
                LOGD("Ignore source type %d: %s", type, O.c.single_update);
        } else {
            incremental(dbpath, cwd); // single update a file
        }
    } else {
        createTags(dbpath, cwd); // create GTAGS and GRTAGS
    }
	
    LOGD("[%s] Done.\n", now());

    // clean up
    // clean up phase 1
    wpath_close(&GlobalPath);
    project_close(&GlobalProject);
    wparser_close(&GlobalParser);

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

static void dumpCommandParser(int argc, char **argv);

static void parseOptions(int argc, char **argv)
{
    static CommandParseList CommandList[] = {
        { "d", "dump", dumpCommandParser, dumpCommandDo }
    };

    optind = 1;
    parseGlobalOptions(argc, argv);

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

    readOptions(argc, argv);
}

static void parseGlobalOptions(int argc, char **argv)
{
#define GLOBAL_OPTION_VERSION 150
#define GLOBAL_OPTION_HELP 151
    static struct option global_long_options[] = {
        { "version", no_argument, NULL, GLOBAL_OPTION_VERSION },
        { "help", no_argument, NULL, GLOBAL_OPTION_HELP },
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
        case 'h':
        case GLOBAL_OPTION_HELP:
            help();
            break;
        default:
            break;
        }
    }
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
        }
    }

    if (1 != optind) {
        argc -= optind;
        argv += optind;
    }
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

static void readOptions(int argc, char **argv)
{
	int option_index = 0;
	int optchar;
	while ((optchar = getopt_long(argc, argv, "cd:f:in:oOqvwse", long_options, &option_index)) != EOF) {
		switch (optchar) {
		case 0:
			/* already flags set */
			break;
		case OPT_GTAGSCONF:
			O.c.gtagsconf = optarg;
			break;
		case OPT_GTAGSLABEL:
			O.c.gtagslabel = optarg;
			break;
		case OPT_SINGLE_UPDATE:
			O.c.iflag++;
			O.c.single_update = optarg;
			break;
		case OPT_ENCODE_PATH:
			if (strlen(optarg) > 255)
				die("too many encode chars.");
			if (strchr(optarg, '/') || strchr(optarg, '.'))
				die("cannot encode '/' and '.' in the path.");
			set_encode_chars((unsigned char *)optarg);
			break;
		case OPT_ACCEPT_DOTFILES:
			set_accept_dotfiles();
			break;
		case 'c':
			O.c.cflag++;
			break;
		case 'f':
			O.c.file_list = optarg;
			break;
		case 'i':
			O.c.iflag++;
			break;
        /* // no use anymore
		case 'I':
			Iflag++;
			break;
        */
		case 'o':
			/*
			 * Though the -o(--omit-gsyms) was removed, this code
			 * is left for compatibility.
			 */
			break;
		case 'O':
			O.c.Oflag++;
			break;
		case 'q':
			setquiet();
			break;
		case 'v':
			setverbose();
			break;
		default:
			usage();
			break;
		}
	}

	if (O.c.gtagsconf) {
		char path[MAXPATHLEN];

		if (realpath(O.c.gtagsconf, path) == NULL)
			die("%s not found.", O.c.gtagsconf);
		set_env("GTAGSCONF", path);
	}
	if (O.c.gtagslabel) {
		set_env("GTAGSLABEL", O.c.gtagslabel);
	}

	argc -= optind;
    argv += optind;

	/* If dbpath is specified, -O(--objdir) option is ignored. */
	if (argc > 0)
		O.c.Oflag = 0;

	/*
	 * If 'gtags.files' exists, use it as a file list.
	 * If the file_list other than "-" is given, it must be readable file.
	 */
	if (O.c.file_list == NULL && test("f", GTAGSFILES))
		O.c.file_list = GTAGSFILES;
	if (O.c.file_list && strcmp(O.c.file_list, "-")) {
		if (test("d", O.c.file_list))
			die("'%s' is a directory.", O.c.file_list);
		else if (!test("f", O.c.file_list))
			die("'%s' not found.", O.c.file_list);
		else if (!test("r", O.c.file_list))
			die("'%s' is not readable.", O.c.file_list);
	}
	if (!getcwd(cwd, MAXPATHLEN))
		die("cannot get current directory.");
	canonpath(cwd);
	/*
	 * Regularize the path name for single updating (--single-update).
	 */
	if (O.c.single_update) {
		static char regular_path_name[MAXPATHLEN];
		const char *p = O.c.single_update;
		
		if (!test("f", p))
			die("'%s' not found.", p);
		if (isabspath(p)) {
			char *q = locatestring(p, cwd, MATCH_AT_FIRST);

			if (q && *q == '/')
				snprintf(regular_path_name, MAXPATHLEN, "./%s", q + 1);
			else
				die("path '%s' is out of the project.", p);

		} else {
			if (p[0] == '.' && p[1] == '/')
				snprintf(regular_path_name, MAXPATHLEN, "%s", p);
			else
				snprintf(regular_path_name, MAXPATHLEN, "./%s", p);
		}
		O.c.single_update = regular_path_name;
	}
	/*
	 * Decide directory (dbpath) in which gtags make tag files.
	 *
	 * Gtags create tag files at current directory by default.
	 * If dbpath is specified as an argument then use it.
	 * If the -i option specified and both GTAGS and GRTAGS exists
	 * at one of the candidate directories then gtags use existing
	 * tag files.
	 */
	if (O.c.iflag) {
		if (argc > 0)
			realpath(*argv, dbpath);
		else if (!gtagsexist(cwd, dbpath, MAXPATHLEN, 1))
			strlimcpy(dbpath, cwd, sizeof(dbpath));
	} else {
		if (argc > 0)
			realpath(*argv, dbpath);
		else if (O.c.Oflag) {
			char *objdir = getobjdir(cwd, 1);

			if (objdir == NULL)
				die("Objdir not found.");
			strlimcpy(dbpath, objdir, sizeof(dbpath));
		} else
			strlimcpy(dbpath, cwd, sizeof(dbpath));
	}
	if (O.c.iflag && (!test("f", makepath(dbpath, dbname(GTAGS), NULL)) ||
		!test("f", makepath(dbpath, dbname(GRTAGS), NULL)) ||
		!test("f", makepath(dbpath, dbname(GPATH), NULL)))) {
        LOGW("GTAGS, GRTAGS or GPATH not found. -i option ignored.");
		O.c.iflag = 0;
	}
	if (!test("d", dbpath))
		die("directory '%s' not found.", dbpath);
    LOGD("[%s] Gtags started.\n", now());
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

