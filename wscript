#!/usr/bin/env python


def options(opt):
    opt.load('compiler_c')


def configure(cnf):
    cnf.load('compiler_c')
    cnf.check(features='c', lib='fuse')


def build(bld):
    bld.stlib(
        features='c cstlib',
        source='./lib/tweetnacl.c',
        target='tweetnacl'
    )
    bld.program(
        features='c',
        source=bld.path.ant_glob(['src/*.c']),
        includes='.',
        target='cryptofs fuse',
        use='tweetnacl'
    )
