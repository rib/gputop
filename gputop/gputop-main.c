/*
 * GPU Top
 *
 * Copyright (C) 2013,2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE

#include <config.h>

#include <stdio.h>
#include <libgen.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>


static void
usage(void)
{
    printf("Usage: gputop [options] <program> [program args...]\n"
           "\n"
           "     --disable-ioctl-intercept     Disable per-context monitoring by intercepting\n"
           "                                   DRM_CONTEXT ioctl's\n\n"
           "                                   without executing the program\n\n"
           "     --dry-run                     Print the environment variables\n"
           "                                   without executing the program\n\n"
           "     --fake                        Run gputop using fake metrics\n\n");
#ifdef SUPPORT_GL
    printf("     --libgl=<libgl_filename>      Explicitly specify the real libGL\n"
           "                                   library to intercept\n\n"
           "     --libegl=<libegl_filename>    Explicitly specify the real libEGL\n"
           "                                   library to intercept\n\n"
           "     --debug-gl-context            Create a debug context and report\n"
           "                                   KHR_debug perf issues\n\n"
           "     --enable-gl-scissor-test      Enable 1x1 scissor test\n"
           "                                   glScissor(0, 0, 1, 1);\n");
#endif
    printf("     --ncurses                     Enable ncurses view of metrics\n");
    printf("                                   (deprecated)\n\n");
    printf(" -h, --help                        Display this help\n\n"
           "\n"
           " Note: gputop is only a wrapper for setting environment variables\n"
           " including LD_LIBRARY_PATH to interpose OpenGL. For only viewing\n"
           " system-wide metrics (when no program is specified) gputop-system\n"
           " is run as a dummy 'GL' application.\n"
           "\n"
           " Environment:\n"
           "\n"
           "     GPUTOP_MODE={remote,ncurses}  The mode of accessing metrics\n"
           "                                   (defaults to remote)\n\n"
#ifdef SUPPORT_GL
           "     LD_PRELOAD=<prefix>/lib/wrappers/libfakeGL.so:<prefix>/lib/libgputop.so\n"
           "                                   The gputop libGL.so and syscall\n"
           "                                   interposer\n\n"
           "     GPUTOP_GL_LIBRARY=<libGL.so>  Path to real libGL.so to chain\n"
           "                                   up to from interposer\n\n"
           "     GPUTOP_GL_DEBUG_CONTEXT=1     Force GL contexts to be debug\n"
           "                                   contexts and report KHR_debug\n"
           "                                   perf issues\n"
#else
           "     LD_PRELOAD=<prefix>/lib/libgputop.so\n"
           "                                   The gputop syscall interposer\n\n"
#endif
           "\n"
           ""
           );

    exit(1);
}

#ifdef SUPPORT_GL
static bool
resolve_lib_path_for_env(const char *lib, const char *sym_name, const char *env)
{
    void *lib_handle;
    void *sym;
    Dl_info sym_info;

    lib_handle = dlopen(lib, RTLD_LAZY);
    if (!lib_handle) {
        fprintf(stderr, "gputop: Failed to dlopen \"%s\" while trying to resolve a default library path: %s\n",
                lib, dlerror());
        return false;
    }

    sym = dlsym(lib_handle, sym_name);
    if (!sym) {
        fprintf(stderr, "gputop: Failed to lookup \"%s\" symbol while trying to resolve a default %s path: %s\n",
                sym_name, lib, dlerror());
        exit(1);
    }

    if (dladdr(sym, &sym_info) == 0) {
        fprintf(stderr, "gputop: Failed to map %s symbol to a filename while trying to resolve a default %s path: %s\n",
                sym_name, lib, dlerror());
        exit(1);
    }

    setenv(env, sym_info.dli_fname, true);

    if (dlclose(lib_handle) != 0) {
        fprintf(stderr, "gputop: Failed to close %s handle after resolving default library path: %s\n",
                lib, dlerror());
        exit(1);
    }

    return true;
}
#endif


static const char *
get_bin_dir(void)
{
    static const char *bin_dir = NULL;
    pid_t pid = getpid();
    char *exe_link = NULL;
    char exe_path[1024];

    if (bin_dir)
        return bin_dir;

    if (asprintf(&exe_link, "/proc/%d/exe", (int)pid) < 0) {
        perror(NULL);
        exit(1);
    }

    if (readlink(exe_link, exe_path, sizeof(exe_path)) < 0) {
        perror("Failed to resolve path of executable");
        exit(1);
    }

    free(exe_link);
    exe_link = NULL;

    bin_dir = strdup(dirname(exe_path));

    return bin_dir;
}

/* To help with running gputop from a build directory we search in a number of
 * different locations for different helper libraries to try and avoid muddling
 * a mixture of installed and build directory binaries.
 *
 * This also makes gputop binaries relocatable.
 */
static const char * const *
get_lib_dirs(void)
{
    static char *lib_dirs[4] = { 0 };
    const char *bin_dir = get_bin_dir();
    const char *prefix;
    char dir[1024];

    if (lib_dirs[0] != NULL)
        return (const char * const *)lib_dirs;

    if (asprintf(&lib_dirs[0], "%s/.libs", bin_dir) < 0) {
        perror("Failed to resolve libdir");
        exit(1);
    }

    strncpy(dir, bin_dir, sizeof(dir));
    prefix = dirname(dir);

    if (asprintf(&lib_dirs[1], "%s/lib", prefix) < 0) {
        perror("Failed to resolve libdir");
        exit(1);
    }
    if (asprintf(&lib_dirs[2], "%s/lib/wrappers", prefix) < 0) {
        perror("Failed to resolve libdir");
        exit(1);
    }

    return (const char * const *)lib_dirs;
}

static char *
resolve_gputop_lib(const char *lib)
{
    const char * const *lib_dirs = get_lib_dirs();

    for (int i = 0; lib_dirs[i]; i++) {
        char *path = NULL;
        struct stat sb;

        if (asprintf(&path, "%s/%s", lib_dirs[i], lib) < 0) {
            perror("Failed to format path to libary");
            exit(1);
        }

        if (stat(path, &sb) == 0)
            return path;

        free(path);
    }

    fprintf(stderr, "Failed to find %s", lib);
    exit(1);

    return NULL;
}

static void
env_resolve_append_lib_path(const char *var, const char *lib)
{
    char *path = resolve_gputop_lib(lib);
    const char *cur = getenv(var);

    fprintf(stderr, "Using %s at %s\n", lib, path);

    if (cur) {
        char *val;
        asprintf(&val, "%s:%s", cur, path);
        setenv(var, val, true);
        free(val);
    } else
        setenv(var, path, true);
}

static void
print_gputop_env_vars(void)
{
    if (getenv("GPUTOP_FAKE_MODE"))
        fprintf(stderr, "GPUTOP_FAKE_MODE=%s \\\n", getenv("GPUTOP_FAKE_MODE"));

#ifdef SUPPORT_GL
    if (getenv("GPUTOP_GL_LIBRARY"))
        fprintf(stderr, "GPUTOP_GL_LIBRARY=%s \\\n", getenv("GPUTOP_GL_LIBRARY"));

    if (getenv("GPUTOP_EGL_LIBRARY"))
            fprintf(stderr, "GPUTOP_EGL_LIBRARY=%s \\\n", getenv("GPUTOP_EGL_LIBRARY"));

    if (getenv("GPUTOP_GL_DEBUG_CONTEXT"))
            fprintf(stderr, "GPUTOP_GL_DEBUG_CONTEXT=%s \\\n", getenv("GPUTOP_GL_DEBUG_CONTEXT"));

    if (getenv("GPUTOP_GL_SCISSOR_TEST"))
        fprintf(stderr, "GPUTOP_GL_SCISSOR_TEST=%s \\\n", getenv("GPUTOP_GL_SCISSOR_TEST"));
#endif

    if (getenv("GPUTOP_MODE"))
        fprintf(stderr, "GPUTOP_MODE=%s \\\n", getenv("GPUTOP_MODE"));
    if (getenv("GPUTOP_WEB_ROOT"))
        fprintf(stderr, "GPUTOP_WEB_ROOT=%s \\\n", getenv("GPUTOP_WEB_ROOT"));
}

static char *
get_gputop_system_path(void)
{
    const char *bin_dir = get_bin_dir();
    const char *options[] = {
        ".libs/lt-gputop-system",
        ".libs/gputop-system",
        "gputop-system",
        NULL
    };

    for (int i = 0; options[i]; i++) {
        struct stat sb;
        char *path = NULL;

        if (asprintf(&path, "%s/%s", bin_dir, options[i]) < 0) {
            perror("Failed to format gputop-system path");
            exit(1);
        }

        if (stat(path, &sb) == 0)
            return path;

        free(path);
    }

    fprintf(stderr, "Failed to find gputop-system program\n");
    exit(1);
}

/* See if gputop is being run from a source/build directory and set
 * the GPUTOP_WEB_ROOT environment accordingly if so, to avoid
 * neeing to run make install during development/testing...
 */
static void
setup_web_root_env(void)
{
    const char *bin_dir = get_bin_dir();
    char *index_path = NULL;
    struct stat sb;

    if (getenv("GPUTOP_WEB_ROOT"))
        return;

    if (asprintf(&index_path, "%s/gputop.js", bin_dir) < 0) {
        fprintf(stderr, "Failed to format index.html path\n");
        exit(1);
    }

    if (stat(index_path, &sb) == 0)
        setenv("GPUTOP_WEB_ROOT", bin_dir, 1);

    free(index_path);
}

int
main (int argc, char **argv)
{
    int opt;
    bool dry_run = false;
    bool disable_ioctl = false;

#define LIB_GL_OPT              (CHAR_MAX + 1)
#define LIB_EGL_OPT             (CHAR_MAX + 2)
#define DEBUG_CTX_OPT           (CHAR_MAX + 3)
#define NCURSES_OPT             (CHAR_MAX + 4)
#define DRY_RUN_OPT             (CHAR_MAX + 5)
#define DISABLE_IOCTL_OPT       (CHAR_MAX + 6)
#define FAKE_OPT                (CHAR_MAX + 7)
#define GPUTOP_SCISSOR_TEST     (CHAR_MAX + 8)
#define PORT_OPT                (CHAR_MAX + 9)

    /* The initial '+' means that getopt will stop looking for
     * options after the first non-option argument. */
    const char *short_options="+h";
    const struct option long_options[] = {
        {"help",            no_argument,        0, 'h'},
        {"dry-run",         no_argument,        0, DRY_RUN_OPT},
        {"fake",            no_argument,        0, FAKE_OPT},
        {"disable-ioctl-intercept", optional_argument,  0, DISABLE_IOCTL_OPT},
#ifdef SUPPORT_GL
        {"libgl",                   optional_argument,  0, LIB_GL_OPT},
        {"libegl",                  optional_argument,  0, LIB_EGL_OPT},
        {"debug-gl-context",        no_argument,        0, DEBUG_CTX_OPT},
        {"enable-gl-scissor-test",  optional_argument,  0, GPUTOP_SCISSOR_TEST},
#endif
        {"ncurses",         no_argument,        0, NCURSES_OPT},
        {"port",            required_argument,  0, PORT_OPT},
        {0, 0, 0, 0}
    };
    char *ld_preload_path;
    char *gputop_system_args[] = {
        get_gputop_system_path(),
        NULL
    };
    char **args = argv;
    int err;
    int i;


    while ((opt = getopt_long(argc, argv, short_options, long_options, NULL))
           != -1)
    {
        switch (opt) {
            case 'h':
                usage();
                return 0;
            case DRY_RUN_OPT:
                dry_run = true;
                break;
            case FAKE_OPT:
                setenv("GPUTOP_FAKE_MODE", "1", true);
                break;
            case DISABLE_IOCTL_OPT:
                disable_ioctl = true;
                break;
#ifdef SUPPORT_GL
            case LIB_GL_OPT:
                setenv("GPUTOP_GL_LIBRARY", optarg, true);
                break;
            case LIB_EGL_OPT:
                setenv("GPUTOP_EGL_LIBRARY", optarg, true);
                break;
            case DEBUG_CTX_OPT:
                setenv("GPUTOP_GL_DEBUG_CONTEXT", "1", true);
                break;
            case GPUTOP_SCISSOR_TEST:
                setenv("GPUTOP_GL_SCISSOR_TEST", "1", true);
                break;
#endif
            case NCURSES_OPT:
                setenv("GPUTOP_MODE", "ncurses", true);
                break;
            case PORT_OPT:
                setenv("GPUTOP_PORT", optarg, true);
                break;
            default:
                fprintf(stderr, "Internal error: "
                        "unexpected getopt value: %d\n", opt);
                exit (1);
        }
    }

    if (optind >= argc) {
        /* If no program is given then launch a dummy "test
         * application" so gputop can also be used for analysing
         * system wide metrics. */
        args = gputop_system_args;
        optind = 0;
        argc = 1;
    }

    if (!disable_ioctl)
        env_resolve_append_lib_path("LD_PRELOAD", "libgputop.so");

#ifdef SUPPORT_GL
    env_resolve_append_lib_path("LD_PRELOAD", "libfakeGL.so");

    if (!getenv("GPUTOP_GL_LIBRARY")) {
        bool found = resolve_lib_path_for_env("libGL.so.1", "glClear", "GPUTOP_GL_LIBRARY");
        if (!found) {
            fprintf(stderr, "Could not resolve a path for the system libGL.so library\n");
            exit(0);
        }
    }

    if (!getenv("GPUTOP_EGL_LIBRARY"))
        resolve_lib_path_for_env("libEGL.so.1", "eglGetDisplay", "GPUTOP_EGL_LIBRARY");
#endif

    setup_web_root_env();

    /*
     * Print out the environment that we are setting up...
     */

    if (dry_run)
        fprintf(stderr, "\nWould run:\n\n");
    else
        fprintf(stderr, "Running:\n\n");

    ld_preload_path = getenv("LD_PRELOAD");
    if (ld_preload_path)
        fprintf(stderr, "LD_PRELOAD=%s \\\n", ld_preload_path);

    print_gputop_env_vars();

    for (i = optind; i < argc; i++)
        fprintf(stderr, "%s ", args[i]);
    fprintf(stderr, "\n\n");

    fprintf(stderr, "The LD_PRELOAD makes running gdb awkward. Either attach with\n");
    fprintf(stderr, "gdb --pid=`pidof %s` or run like:\n\n", args[optind]);

    print_gputop_env_vars();
    if (ld_preload_path)
        fprintf(stderr, "gdb -ex \"set exec-wrapper env 'LD_PRELOAD=%s'\" --args \\\n", ld_preload_path);
    else
        fprintf(stderr, "gdb --args \\\n");

    for (i = optind; i < argc; i++)
        fprintf(stderr, "%s ", args[i]);
    fprintf(stderr, "\n\n");


    fprintf(stderr, "Valgrind can be run like:\n\n");

    if (ld_preload_path)
        fprintf(stderr, "LD_PRELOAD=%s \\\n", ld_preload_path);
    print_gputop_env_vars();
    fprintf(stderr, "valgrind --log-file=valgrind-log.txt \\\n");

    for (i = optind; i < argc; i++)
        fprintf(stderr, "%s ", args[i]);
    fprintf(stderr, "\n\n");


    if (!dry_run)
    {
        execvp(args[optind], &args[optind]);
        err = errno;

        fprintf(stderr, "gputop: Failed to run GL application: \n\n"
               "  ");
        for (i = optind; i < argc; i++)
               fprintf(stderr, "%s ", args[i]);
        fprintf(stderr, "\n\n%s\n", strerror(err));
    }

    return 0;
}
