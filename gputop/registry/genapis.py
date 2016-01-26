#!/usr/bin/env python2
#
# Copyright (c) 2013-2014 The Khronos Group Inc.
# Copyright (c) 2014 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and/or associated documentation files (the
# "Materials"), to deal in the Materials without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Materials, and to
# permit persons to whom the Materials are furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Materials.
#
# THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.

import sys, time, pdb, string, cProfile
from reg import *
import argparse

# debug - start header generation in debugger
# profile - enable Python profiling
# timeit - time length of registry loading & header generation
debug   = False
profile = False
timeit  = False
# Default input / log files
errFilename = None
diagFilename = 'diag.txt'

class ShimGeneratorOptions(CGeneratorOptions):
    """Represents options during C header production from an API registry"""
    def __init__(self,
                 xmlfile = None,
                 filename = None,
                 apiname = None,
                 hooks = set(),
                 profile = None,
                 versions = '.*',
                 emitversions = '.*',
                 defaultExtensions = None,
                 addExtensions = None,
                 removeExtensions = None,
                 sortProcedure = regSortFeatures,
                 prefixText = ''):
        CGeneratorOptions.__init__(self, filename, apiname, profile,
                                   versions, emitversions, defaultExtensions)

        self.xmlfile         = xmlfile
        self.prefixText      = prefixText
        self.hooks           = hooks

class ShimOutputGenerator(COutputGenerator):
    """Generate specified API interfaces in a specific style, such as a C header"""
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        COutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.typeBody = ''
        self.enumBody = ''
        self.cmdBody = ''

    def makeProto(self, cmd, namePrefix = ''):
        proto = cmd.find('proto')
        params = cmd.findall('param')
        protostr = ''
        name = ''

        protostr += noneStr(proto.text)
        for elem in proto:
            text = noneStr(elem.text)
            tail = noneStr(elem.tail)
            if (elem.tag == 'name'):
                name = text
                protostr += namePrefix + text + tail
            else:
                protostr += text + tail

        n = len(params)
        protostr += ' ('
        if n > 0:
            for i in range(0,n):
                protostr += ''.join([t for t in params[i].itertext()])
                if (i < n - 1):
                    protostr += ', '
        else:
            protostr += 'void'
        protostr += ")"

        return protostr

    def makeHook(self, cmd):
        proto = cmd.find('proto')
        params = cmd.findall('param')
        hook = ''
        name = ''

        for elem in proto:
            if (elem.tag == 'name'):
                name = noneStr(elem.text)

        hook += self.makeProto(cmd, namePrefix = "gputop_") + ";\n";

        hook += self.makeProto(cmd)
        hook += "\n{\n";

        hook += "    "

        ret = proto.find('ptype')
        if ret != None:
            hook += "return "

        hook += "gputop_" + name + " ("

        n = len(params)
        if n > 0:
            for i in range(0,n):
                hook += params[i].find('name').text
                if (i < n - 1):
                    hook += ', '

        hook += ");\n"

        hook += "}\n\n"

        return hook

    def makeShim(self, cmd):
        """Generate shim function for <command> Element"""
        proto = cmd.find('proto')
        shim = ''
        name = ''

        for elem in proto:
            if (elem.tag == 'name'):
                name = noneStr(elem.text)

        if (name in self.genOpts.hooks):
            shim += self.makeHook(cmd)

        return shim
    #
    def newline(self):
        write('', file=self.outFile)
    #
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)

        # User-supplied prefix text, if any (list of strings)
        if (genOpts.prefixText):
            for s in genOpts.prefixText:
                write(s, file=self.outFile)

        #write(passthroughResolver, file=self.outFile)

    def endFile(self):
        self.newline()
        OutputGenerator.endFile(self)
    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # Accumulate function shims to print in endFeature()
        self.cmdBody = ''
        self.typeBody = ''
        self.featureExtraProtect = interface.get('protect')
    def endFeature(self):
        # Actually write the interface to the output file.
        if (self.emit):
            self.newline()

            if (self.featureExtraProtect != None):
                write('#ifdef', self.featureExtraProtect, file=self.outFile)

            #if (self.cmdBody != '' or self.typeBody != ''):
            if (self.cmdBody != ''):
                write('/* ', self.featureName, ' */', file=self.outFile)
                #write(self.typeBody, end='', file=self.outFile)
                write(self.cmdBody, end='', file=self.outFile)

            if (self.featureExtraProtect != None):
                write('#endif', file=self.outFile)

        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Type generation
    def genType(self, typeinfo, name):
        OutputGenerator.genType(self, typeinfo, name)
        #
        # Replace <apientry /> tags with an APIENTRY-style string
        # (from self.genOpts). Copy other text through unchanged.
        # If the resulting text is an empty string, don't emit it.
        typeElem = typeinfo.elem
        s = noneStr(typeElem.text)
        for elem in typeElem:
            if (elem.tag == 'apientry'):
                s += noneStr(elem.tail)
            else:
                s += noneStr(elem.text) + noneStr(elem.tail)
        if (len(s) > 0):
            self.typeBody += s + "\n"

    #
    # Enumerant generation
    def genEnum(self, enuminfo, name):
        OutputGenerator.genEnum(self, enuminfo, name)
    #
    # Command generation
    def genCmd(self, cmdinfo, name):
        OutputGenerator.genCmd(self, cmdinfo, name)

        self.cmdBody += self.makeShim(cmdinfo.elem)



if __name__ == '__main__':
    i = 1
    while (i < len(sys.argv)):
        arg = sys.argv[i]
        i = i + 1
        if (arg == '-debug'):
            write('Enabling debug (-debug)', file=sys.stderr)
            debug = True
        elif (arg == '-profile'):
            write('Enabling profiling (-profile)', file=sys.stderr)
            profile = True
        elif (arg == '-time'):
            write('Enabling timing (-time)', file=sys.stderr)
            timeit = True
        elif (arg[0:1] == '-'):
            write('Unrecognized argument:', arg, file=sys.stderr)
            exit(1)

parser = argparse.ArgumentParser()
parser.add_argument("registry", help="Location of Khronos API XML files")
parser.add_argument("--debug", dest='debug', action='store_true', help="Enable debug")
parser.add_argument("--profile", dest='profile', action='store_true', help="Enable profile")
parser.add_argument("--timing", dest='timeit', action='store_true', help="Enable timing")

args = parser.parse_args()

# Simple timer functions
startTime = None
def startTimer():
    global startTime
    startTime = time.clock()
def endTimer(msg):
    global startTime
    endTime = time.clock()
    if (timeit):
        write(msg, endTime - startTime)
        startTime = None

# Turn a list of strings into a regexp string matching exactly those strings
def makeREstring(list):
    return '^(' + '|'.join(list) + ')$'

# Descriptive names for various regexp patterns used to select
# versions and extensions

allVersions     = allExtensions = '.*'
noVersions      = noExtensions = None
gl12andLaterPat = '1\.[2-9]|[234]\.[0-9]'
gles2onlyPat    = '2\.[0-9]'
gles2and30Pat   = '2\.[0-9]|3.0'
gles2and30and31Pat    = '2.[0-9]|3.[01]'
# Extensions in old glcorearb.h but not yet tagged accordingly in gl.xml
glCoreARBPat    = None
glx13andLaterPat = '1\.[3-9]'

prefixStrings = [
    '/* DON\'T EDIT THIS FILE IT WAS AUTOMATICALLY GENERATED BY genshims.py */',
    ''
]

# glext.h / glcorearb.h define calling conventions inline (no GL *platform.h)
# GLES 1/2/3 core .h have separate *platform.h files to define calling conventions
gles2PlatformStrings = [ '#include <GLES2/gl2platform.h>', '' ]
gles3PlatformStrings = [ '#include <GLES3/gl3platform.h>', '' ]
eglPlatformStrings   = [ '#include <EGL/eglplatform.h>', '' ]

# GLES 1/2 extension .h have small addition to calling convention headers
gles2ExtPlatformStrings = [
    '#ifndef GL_APIENTRYP',
    '#define GL_APIENTRYP GL_APIENTRY*',
    '#endif',
    ''
]

# Insert generation date in a comment for headers not having *GLEXT_VERSION macros
genDateCommentString = [
    format("/* Generated on date %s */" % time.strftime("%Y%m%d")),
    ''
]

# GL_GLEXT_VERSION is defined only in glext.h
glextVersionStrings = [
    format("#define GL_GLEXT_VERSION %s" % time.strftime("%Y%m%d")),
    ''
]
# GLX_GLXEXT_VERSION is defined only in glxext.h
glxextVersionStrings = [
    format("#define GLX_GLXEXT_VERSION %s" % time.strftime("%Y%m%d")),
    ''
]
# EGL_EGLEXT_VERSION is defined only in eglext.h
eglextVersionStrings = [
    format("#define EGL_EGLEXT_VERSION %s" % time.strftime("%Y%m%d")),
    ''
]

glapiHooks = {
    'glGetError',
    'glEnable',
    'glDisable',
    'glScissor',
    'glBindTexture',
    'glDebugMessageControl',
    'glDebugMessageCallback'
}

glxapiHooks = {
    'glXGetProcAddress',
    'glXGetProcAddressARB',
    'glXMakeCurrent',
    'glXMakeContextCurrent',
    'glXSwapBuffers',
    'glXCreateContext',
    'glXCreateNewContext',
    'glXCreateContextAttribsARB',
    'glXDestroyContext',
    'glXQueryExtension'
}

glxapiHeaders = [
    '#include <GL/glx.h>',
    ''
]

glapiHeaders = [
    '#include <GL/gl.h>',
    ''
]

eglapiHeaders = [
    '#include <EGL/egl.h>',
    ''
]

passthroughGLResolver = ['''
void *gputop_passthrough_gl_resolve(const char *name);
static void *
passthrough_resolve(const char *name)
{
    return gputop_passthrough_gl_resolve(name);
}
''']

passthroughGLXResolver = ['''
void *gputop_passthrough_glx_resolve(const char *name);
static void *
passthrough_resolve(const char *name)
{
    return gputop_passthrough_glx_resolve(name);
}
''']

passthroughEGLResolver = ['''
void *gputop_passthrough_egl_resolve(const char *name);
static void *
passthrough_resolve(const char *name)
{
    return gputop_passthrough_egl_resolve(name);
}
''']

buildList = [
    # GL API 1.2+ + extensions
    ShimGeneratorOptions(
        xmlfile           = 'gl.xml',
        filename          = 'glapi.c',
        apiname           = 'gl',
        hooks             = glapiHooks,
        profile           = 'compatibility',
        versions          = allVersions,
        emitversions      = allVersions,
        defaultExtensions = 'gl',
        addExtensions     = None,
        removeExtensions  = None,
        prefixText        = prefixStrings + glextVersionStrings + glapiHeaders + passthroughGLResolver),
    # GLX 1.* API
    ShimGeneratorOptions(
        xmlfile           = 'glx.xml',
        filename          = 'glxapi.c',
        apiname           = 'glx',
        hooks             = glxapiHooks,
        profile           = None,
        versions          = allVersions,
        emitversions      = allVersions,
        defaultExtensions = 'glx',
        addExtensions     = None,
        removeExtensions  = None,
        # add glXPlatformStrings?
        prefixText        = prefixStrings + genDateCommentString + glxapiHeaders + passthroughGLXResolver),
    # EGL API
    ShimGeneratorOptions(
        xmlfile           = 'egl.xml',
        filename          = 'eglapi.c',
        apiname           = 'egl',
        profile           = None,
        versions          = allVersions,
        emitversions      = allVersions,
        defaultExtensions = 'egl',
        addExtensions     = None,
        removeExtensions  = None,
        prefixText        = prefixStrings + eglPlatformStrings + genDateCommentString + eglapiHeaders + passthroughEGLResolver),

    # End of list
    None
]

# create error/warning & diagnostic files
if (errFilename):
    errWarn = open(errFilename,'w')
else:
    errWarn = sys.stderr
diag = open(diagFilename, 'w')

def genShims():
    generated = 0
    for genOpts in buildList:

        if (genOpts == None):
            break

        # Load & parse registry
        reg = Registry()

        startTimer()
        tree = etree.parse(args.registry + '/' + genOpts.xmlfile)
        endTimer('Time to make ElementTree =')

        startTimer()
        reg.loadElementTree(tree)
        endTimer('Time to parse ElementTree =')

        write('*** Building', genOpts.filename)
        generated = generated + 1
        startTimer()
        gen = ShimOutputGenerator(errFile=errWarn,
                                  warnFile=errWarn,
                                  diagFile=diag)
        reg.setGenerator(gen)
        reg.apiGen(genOpts)
        write('** Generated', genOpts.filename)
        endTimer('Time to generate ' + genOpts.filename + ' =')

if (debug):
    pdb.run('genShims()')
elif (profile):
    import cProfile, pstats
    cProfile.run('genShims()', 'profile.txt')
    p = pstats.Stats('profile.txt')
    p.strip_dirs().sort_stats('time').print_stats(50)
else:
    genShims()
