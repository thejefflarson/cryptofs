#!/usr/bin/env python


def options(opt):
    opt.load('compiler_c')


def configure(cnf):
    cnf.load('compiler_c')
    cnf.check_cfg(package='fuse', args='--cflags --libs', uselib_store='FUSE')
    cnf.env.append_unique('CFLAGS', ['-g', '-static'])
    cnf.define('FUSE_USE_VERSION', 26)


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
        target='cryptofs',
        use='tweetnacl',
        uselib='FUSE'
    )
