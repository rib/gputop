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
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


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
#ifdef SUPPORT_WEBUI
    printf("     --remote                      Enable remote web-based interface\n\n");
    printf("     --nodejs                      Enable remote nodejs-based interface\n\n");
#endif
    printf(" -h, --help                        Display this help\n\n"
           "\n"
           " Note: gputop is only a wrapper for setting environment variables\n"
           " including LD_LIBRARY_PATH to interpose OpenGL. For only viewing\n"
           " system-wide metrics (when no program is specified) gputop-system\n"
           " is run as a dummy 'GL' application.\n"
           "\n"
           " Environment:\n"
           "\n"
#ifdef SUPPORT_WEBUI
           "     GPUTOP_MODE={remote,ncurses,nodejs}  The mode of visualizing metrics\n"
           "                                          (defaults to ncurses)\n\n"
#endif
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

static void
env_append_path(const char *var, const char *path)
{
    const char *cur = getenv(var);

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

#ifdef SUPPORT_WEBUI
    if (getenv("GPUTOP_MODE"))
        fprintf(stderr, "GPUTOP_MODE=%s \\\n", getenv("GPUTOP_MODE"));
#endif
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
#define REMOTE_OPT              (CHAR_MAX + 4)
#define DRY_RUN_OPT             (CHAR_MAX + 5)
#define DISABLE_IOCTL_OPT       (CHAR_MAX + 6)
#define FAKE_OPT                (CHAR_MAX + 7)
#define GPUTOP_SCISSOR_TEST     (CHAR_MAX + 8)
#define NODEJS_OPT              (CHAR_MAX + 9)

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
#ifdef SUPPORT_WEBUI
        {"remote",          no_argument,        0, REMOTE_OPT},
        {"nodejs",          no_argument,        0, NODEJS_OPT},
#endif
        {0, 0, 0, 0}
    };
    char *ld_preload_path;
    char *gputop_system_args[] = {
        "gputop-system",
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
#ifdef SUPPORT_WEBUI
            case REMOTE_OPT:
                setenv("GPUTOP_MODE", "remote", true);
                break;
            case NODEJS_OPT:
                setenv("GPUTOP_MODE", "nodejs", true);
                break;
#endif
            default:
                fprintf (stderr, "Internal error: "
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
    }

    if (!disable_ioctl)
        env_append_path("LD_PRELOAD", GPUTOP_LIB_DIR "/libgputop.so");

#ifdef SUPPORT_GL
    env_append_path("LD_PRELOAD", GPUTOP_LIB_DIR "/wrappers/libfakeGL.so");

    if (!getenv("GPUTOP_GL_LIBRARY")) {
        bool found = resolve_lib_path_for_env("libGL.so.1", "glClear", "GPUTOP_GL_LIBRARY");
            if (!found)
            {
                fprintf(stderr, "Could not resolve a path for the system libGL.so library\n");
                exit(0);
            }
    }

    if (!getenv("GPUTOP_EGL_LIBRARY"))
        resolve_lib_path_for_env("libEGL.so.1", "eglGetDisplay", "GPUTOP_EGL_LIBRARY");
#endif


    /*
     * Print out the environment that we are setting up...
     */

    if (dry_run)
        fprintf(stderr, "Would run:\n\n");
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
