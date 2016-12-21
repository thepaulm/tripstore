Import('env')

#
# Variables for everybody
#

build = ARGUMENTS.get('build', 'debug')

if build == 'debug':
    env.Append(CCFLAGS = '-g')

env.Append(CCFLAGS = '-O2')

#
#
# Environment for building the sqlite.a
#
libenv = env.Clone()
libenv.Append(CCFLAGS = '-DSQLITE_ENABLE_RTREE=1')

sqlite_src = ['sqlite3.c']
sqlite_lib = libenv.Library(target='libsqlite3', source=sqlite_src)

#
# Common sources
#
common_src = [
              'sockets.c',
              'msgs.c',
             ]
common_obj = map(env.Object, common_src)

#
# Sources and libs for the tripstore binary
#
src = [
       'tripstore.c',
       'sqls.c',
       ]

libs = [
        sqlite_lib,
        'pthread',
        'dl',
       ]

#
# Sources and libs for the tripgen binary
#
gensrc = [
      'tripgen.c',
         ]

genlibs = [
          'pthread',
          ]

#
# Definition for building the binary
#
Default(env.Program(target='tripstore', source=src + common_obj, LIBS=libs))
Default(env.Program(target='tripgen', source=gensrc + common_obj, LIBS=genlibs))

