///////////////////////////////////////////////////////////////////////////////
//
/// \file       args.c
/// \brief      Argument parsing
///
/// \note       Filter-specific options parsing is in options.c.
//
//  Copyright (C) 2007 Lasse Collin
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"

#include "getopt.h"
#include <ctype.h>


enum tool_mode opt_mode = MODE_COMPRESS;
enum format_type opt_format = FORMAT_AUTO;

char *opt_suffix = NULL;

char *opt_files_name = NULL;
char opt_files_split = '\0';
FILE *opt_files_file = NULL;

bool opt_stdout = false;
bool opt_force = false;
bool opt_keep_original = false;
bool opt_preserve_name = false;

lzma_check opt_check = LZMA_CHECK_CRC64;
lzma_filter opt_filters[LZMA_BLOCK_FILTERS_MAX + 1];

// We don't modify or free() this, but we need to assign it in some
// non-const pointers.
const char *stdin_filename = "(stdin)";

static size_t preset_number = 7;
static bool preset_default = true;
static size_t filter_count = 0;

/// When compressing, which file format to use if --format=auto or no --format
/// at all has been specified. We need a variable because this depends on
/// with which name we are called. All names with "lz" in them makes us to
/// use the legacy .lzma format.
static enum format_type format_compress_auto = FORMAT_XZ;


enum {
	OPT_SUBBLOCK = INT_MIN,
	OPT_X86,
	OPT_POWERPC,
	OPT_IA64,
	OPT_ARM,
	OPT_ARMTHUMB,
	OPT_SPARC,
	OPT_DELTA,
	OPT_LZMA1,
	OPT_LZMA2,

	OPT_FILES,
	OPT_FILES0,
};


static const char short_opts[] = "cC:dfF:hlLkM:qrS:tT:vVz123456789";


static const struct option long_opts[] = {
	// gzip-like options
	{ "fast",               no_argument,       NULL,  '1' },
	{ "best",               no_argument,       NULL,  '9' },
	{ "memory",             required_argument, NULL,  'M' },
	{ "name",               no_argument,       NULL,  'N' },
	{ "suffix",             required_argument, NULL,  'S' },
	{ "threads",            required_argument, NULL,  'T' },
	{ "version",            no_argument,       NULL,  'V' },
	{ "stdout",             no_argument,       NULL,  'c' },
	{ "to-stdout",          no_argument,       NULL,  'c' },
	{ "decompress",         no_argument,       NULL,  'd' },
	{ "uncompress",         no_argument,       NULL,  'd' },
	{ "force",              no_argument,       NULL,  'f' },
	{ "help",               no_argument,       NULL,  'h' },
	{ "list",               no_argument,       NULL,  'l' },
	{ "info",               no_argument,       NULL,  'l' },
	{ "keep",               no_argument,       NULL,  'k' },
	{ "no-name",            no_argument,       NULL,  'n' },
	{ "quiet",              no_argument,       NULL,  'q' },
//	{ "recursive",          no_argument,       NULL,  'r' }, // TODO
	{ "test",               no_argument,       NULL,  't' },
	{ "verbose",            no_argument,       NULL,  'v' },
	{ "compress",           no_argument,       NULL,  'z' },

	// Filters
	{ "subblock",           optional_argument, NULL,   OPT_SUBBLOCK },
	{ "x86",                no_argument,       NULL,   OPT_X86 },
	{ "bcj",                no_argument,       NULL,   OPT_X86 },
	{ "powerpc",            no_argument,       NULL,   OPT_POWERPC },
	{ "ppc",                no_argument,       NULL,   OPT_POWERPC },
	{ "ia64",               no_argument,       NULL,   OPT_IA64 },
	{ "itanium",            no_argument,       NULL,   OPT_IA64 },
	{ "arm",                no_argument,       NULL,   OPT_ARM },
	{ "armthumb",           no_argument,       NULL,   OPT_ARMTHUMB },
	{ "sparc",              no_argument,       NULL,   OPT_SPARC },
	{ "delta",              optional_argument, NULL,   OPT_DELTA },
	{ "lzma1",              optional_argument, NULL,   OPT_LZMA1 },
	{ "lzma2",              optional_argument, NULL,   OPT_LZMA2 },

	// Other
	{ "format",             required_argument, NULL,   'F' },
	{ "check",              required_argument, NULL,   'C' },
	{ "files",              optional_argument, NULL,   OPT_FILES },
	{ "files0",             optional_argument, NULL,   OPT_FILES0 },

	{ NULL,                 0,                 NULL,   0 }
};


static void
add_filter(lzma_vli id, const char *opt_str)
{
	if (filter_count == LZMA_BLOCK_FILTERS_MAX) {
		errmsg(V_ERROR, _("Maximum number of filters is seven"));
		my_exit(ERROR);
	}

	opt_filters[filter_count].id = id;

	switch (id) {
	case LZMA_FILTER_SUBBLOCK:
		opt_filters[filter_count].options
				= parse_options_subblock(opt_str);
		break;

	case LZMA_FILTER_DELTA:
		opt_filters[filter_count].options
				= parse_options_delta(opt_str);
		break;

	case LZMA_FILTER_LZMA1:
	case LZMA_FILTER_LZMA2:
		opt_filters[filter_count].options
				= parse_options_lzma(opt_str);
		break;

	default:
		assert(opt_str == NULL);
		opt_filters[filter_count].options = NULL;
		break;
	}

	++filter_count;
	preset_default = false;
	return;
}


static void
parse_real(int argc, char **argv)
{
	int c;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL))
			!= -1) {
		switch (c) {
		// gzip-like options

		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			preset_number = c - '0';
			preset_default = false;
			break;

		// --memory
		case 'M':
			opt_memory = str_to_uint64("memory", optarg,
					1, SIZE_MAX);
			break;

		case 'N':
			opt_preserve_name = true;
			break;

		// --suffix
		case 'S':
			// Empty suffix and suffixes having a slash are
			// rejected. Such suffixes would break things later.
			if (optarg[0] == '\0' || strchr(optarg, '/') != NULL) {
				errmsg(V_ERROR, _("%s: Invalid filename "
						"suffix"), optarg);
				my_exit(ERROR);
			}

			free(opt_suffix);
			opt_suffix = xstrdup(optarg);
			break;

		case 'T':
			opt_threads = str_to_uint64("threads", optarg,
					1, SIZE_MAX);
			break;

		// --version
		case 'V':
			// This doesn't return.
			show_version();

		// --stdout
		case 'c':
			opt_stdout = true;
			break;

		// --decompress
		case 'd':
			opt_mode = MODE_DECOMPRESS;
			break;

		// --force
		case 'f':
			opt_force = true;
			break;

		// --help
		case 'h':
			// This doesn't return.
			show_help();

		// --list
		case 'l':
			opt_mode = MODE_LIST;
			break;

		// --keep
		case 'k':
			opt_keep_original = true;
			break;

		case 'n':
			opt_preserve_name = false;
			break;

		// --quiet
		case 'q':
			if (verbosity > V_SILENT)
				--verbosity;

			break;

		case 't':
			opt_mode = MODE_TEST;
			break;

		// --verbose
		case 'v':
			if (verbosity < V_DEBUG)
				++verbosity;

			break;

		case 'z':
			opt_mode = MODE_COMPRESS;
			break;

		// Filter setup

		case OPT_SUBBLOCK:
			add_filter(LZMA_FILTER_SUBBLOCK, optarg);
			break;

		case OPT_X86:
			add_filter(LZMA_FILTER_X86, NULL);
			break;

		case OPT_POWERPC:
			add_filter(LZMA_FILTER_POWERPC, NULL);
			break;

		case OPT_IA64:
			add_filter(LZMA_FILTER_IA64, NULL);
			break;

		case OPT_ARM:
			add_filter(LZMA_FILTER_ARM, NULL);
			break;

		case OPT_ARMTHUMB:
			add_filter(LZMA_FILTER_ARMTHUMB, NULL);
			break;

		case OPT_SPARC:
			add_filter(LZMA_FILTER_SPARC, NULL);
			break;

		case OPT_DELTA:
			add_filter(LZMA_FILTER_DELTA, optarg);
			break;

		case OPT_LZMA1:
			add_filter(LZMA_FILTER_LZMA1, optarg);
			break;

		case OPT_LZMA2:
			add_filter(LZMA_FILTER_LZMA2, optarg);
			break;

		// Other

		// --format
		case 'F': {
			// Just in case, support both "lzma" and "alone" since
			// the latter was used for forward compatibility in
			// LZMA Utils 4.32.x.
			static const struct {
				char str[8];
				enum format_type format;
			} types[] = {
				{ "auto",   FORMAT_AUTO },
				{ "xz",     FORMAT_XZ },
				{ "lzma",   FORMAT_LZMA },
				{ "alone",  FORMAT_LZMA },
				// { "gzip",   FORMAT_GZIP },
				// { "gz",     FORMAT_GZIP },
				{ "raw",    FORMAT_RAW },
			};

			size_t i = 0;
			while (strcmp(types[i].str, optarg) != 0) {
				if (++i == ARRAY_SIZE(types)) {
					errmsg(V_ERROR, _("%s: Unknown file "
							"format type"),
							optarg);
					my_exit(ERROR);
				}
			}

			opt_format = types[i].format;
			break;
		}

		// --check
		case 'C': {
			static const struct {
				char str[8];
				lzma_check check;
			} types[] = {
				{ "none",   LZMA_CHECK_NONE },
				{ "crc32",  LZMA_CHECK_CRC32 },
				{ "crc64",  LZMA_CHECK_CRC64 },
				{ "sha256", LZMA_CHECK_SHA256 },
			};

			size_t i = 0;
			while (strcmp(types[i].str, optarg) != 0) {
				if (++i == ARRAY_SIZE(types)) {
					errmsg(V_ERROR, _("%s: Unknown "
							"integrity check "
							"type"), optarg);
					my_exit(ERROR);
				}
			}

			opt_check = types[i].check;
			break;
		}

		case OPT_FILES:
			opt_files_split = '\n';

		// Fall through

		case OPT_FILES0:
			if (opt_files_name != NULL) {
				errmsg(V_ERROR, _("Only one file can be "
						"specified with `--files'"
						"or `--files0'."));
				my_exit(ERROR);
			}

			if (optarg == NULL) {
				opt_files_name = (char *)stdin_filename;
				opt_files_file = stdin;
			} else {
				opt_files_name = optarg;
				opt_files_file = fopen(optarg,
						c == OPT_FILES ? "r" : "rb");
				if (opt_files_file == NULL) {
					errmsg(V_ERROR, "%s: %s", optarg,
							strerror(errno));
					my_exit(ERROR);
				}
			}

			break;

		default:
			show_try_help();
			my_exit(ERROR);
		}
	}

	return;
}


static void
parse_environment(void)
{
	char *env = getenv("LZMA_OPT");
	if (env == NULL)
		return;

	env = xstrdup(env);

	// Calculate the number of arguments in env.
	unsigned int argc = 1;
	bool prev_was_space = true;
	for (size_t i = 0; env[i] != '\0'; ++i) {
		if (isspace(env[i])) {
			prev_was_space = true;
		} else if (prev_was_space) {
			prev_was_space = false;
			if (++argc > (unsigned int)(INT_MAX)) {
				errmsg(V_ERROR, _("The environment variable "
						"LZMA_OPT contains too many "
						"arguments"));
				my_exit(ERROR);
			}
		}
	}

	char **argv = xmalloc((argc + 1) * sizeof(char*));
	argv[0] = argv0;
	argv[argc] = NULL;

	argc = 1;
	prev_was_space = true;
	for (size_t i = 0; env[i] != '\0'; ++i) {
		if (isspace(env[i])) {
			prev_was_space = true;
		} else if (prev_was_space) {
			prev_was_space = false;
			argv[argc++] = env + i;
		}
	}

	parse_real((int)(argc), argv);

	free(env);

	return;
}


static void
set_compression_settings(void)
{
	static lzma_options_lzma opt_lzma;

	if (filter_count == 0) {
		if (lzma_lzma_preset(&opt_lzma, preset_number)) {
			errmsg(V_ERROR, _("Internal error (bug)"));
			my_exit(ERROR);
		}

		opt_filters[0].id = opt_format == FORMAT_LZMA
				? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2;
		opt_filters[0].options = &opt_lzma;
		filter_count = 1;
	}

	// Terminate the filter options array.
	opt_filters[filter_count].id = LZMA_VLI_UNKNOWN;

	// If we are using the LZMA_Alone format, allow exactly one filter
	// which has to be LZMA.
	if (opt_format == FORMAT_LZMA && (filter_count != 1
			|| opt_filters[0].id != LZMA_FILTER_LZMA1)) {
		errmsg(V_ERROR, _("With --format=lzma only the LZMA1 filter "
				"is supported"));
		my_exit(ERROR);
	}

	// TODO: liblzma probably needs an API to validate the filter chain.

	// If using --format=raw, we can be decoding.
	uint64_t memory_usage = opt_mode == MODE_COMPRESS
			? lzma_memusage_encoder(opt_filters)
			: lzma_memusage_decoder(opt_filters);

	// Don't go over the memory limits when the default
	// setting is used.
	if (preset_default) {
		while (memory_usage > opt_memory) {
			if (preset_number == 1) {
				errmsg(V_ERROR, _("Memory usage limit is too "
						"small for any internal "
						"filter preset"));
				my_exit(ERROR);
			}

			if (lzma_lzma_preset(&opt_lzma, --preset_number)) {
				errmsg(V_ERROR, _("Internal error (bug)"));
				my_exit(ERROR);
			}

			memory_usage = lzma_memusage_encoder(opt_filters);
		}

		// TODO: With --format=raw, we should print a warning since
		// the presets may change and thus the next version may not
		// be able to uncompress the raw stream with the same preset
		// number.

	} else {
		if (memory_usage > opt_memory) {
			errmsg(V_ERROR, _("Memory usage limit is too small "
					"for the given filter setup"));
			my_exit(ERROR);
		}
	}

	// Limit the number of worked threads so that memory usage
	// limit isn't exceeded.
	assert(memory_usage > 0);
	size_t thread_limit = opt_memory / memory_usage;
	if (thread_limit == 0)
		thread_limit = 1;

	if (opt_threads > thread_limit)
		opt_threads = thread_limit;

	return;
}


extern char **
parse_args(int argc, char **argv)
{
	// Check how we were called.
	{
		const char *name = str_filename(argv[0]);
		if (name != NULL) {
			// Default file format
			if (strstr(name, "lz") != NULL)
				format_compress_auto = FORMAT_LZMA;

			// Operation mode
			if (strstr(name, "cat") != NULL) {
				opt_mode = MODE_DECOMPRESS;
				opt_stdout = true;
			} else if (strstr(name, "un") != NULL) {
				opt_mode = MODE_DECOMPRESS;
			}
		}
	}

	// First the flags from environment
	parse_environment();

	// Then from the command line
	optind = 1;
	parse_real(argc, argv);

	// Never remove the source file when the destination is not on disk.
	// In test mode the data is written nowhere, but setting opt_stdout
	// will make the rest of the code behave well.
	if (opt_stdout || opt_mode == MODE_TEST) {
		opt_keep_original = true;
		opt_stdout = true;
	}

	if (opt_mode == MODE_COMPRESS && opt_format == FORMAT_AUTO)
		opt_format = format_compress_auto;

	if (opt_mode == MODE_COMPRESS || opt_format == FORMAT_RAW)
		set_compression_settings();

	// If no filenames are given, use stdin.
	if (argv[optind] == NULL && opt_files_name == NULL) {
		// We don't modify or free() the "-" constant.
		static char *argv_stdin[2] = { (char *)"-", NULL };
		return argv_stdin;
	}

	return argv + optind;
}
