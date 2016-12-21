env = Environment()

env['BUILDERS']['CTags'] = Builder(action='ctags' + ' -R --exclude=build')
env.CTags(target='ctags', source = None)
env.CTags

SConscript('./SConscript', variant_dir='./build', duplicate=0, exports='env')
Export('env')
