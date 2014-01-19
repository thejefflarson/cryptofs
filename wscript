#!/usr/bin/env python


def options(opt):
    configure(opt)


def configure(cnf):
    cnf.load('compiler_c compiler_cxx')
    cnf.load('ragel', tooldir='./tools/')


def build(bld):
    bld.stlib(
        features='c cstlib',
        source='./lib/tweetnacl.c',
        target='tweetnacl'
    )
    bld.program(
        features='c ragel',
        source=bld.path.ant_glob(['src/*.rl', 'src/*.cc']),
        includes='.',
        target='cryptofs',
        use='tweetnacl'
    )
