#!/usr/bin/env python

from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext, Logs

top = '.'
out = 'build'


def check_compiler_flag(conf, flag, lang):
	return conf.check(fragment = 'int main() { float f = 4.0; char c = f; return c - 4; }\n', execute = 0, mandatory = 0, define_ret = 0, msg = 'Checking for compiler switch %s' % flag, cxxflags = conf.env[lang + 'FLAGS'] + [flag], okmsg = 'yes', errmsg = 'no')  # the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the code being compiled causes a warning


def check_compiler_flags_2(conf, cflags, ldflags, msg):
	return conf.check(fragment = 'int main() { float f = 4.0; char c = f; return c - 4; }\n', execute = 0, mandatory = 0, define_ret = 0, msg = msg, cxxflags = cflags, ldflags = ldflags, okmsg = 'yes', errmsg = 'no')  # the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the code being compiled causes a warning


def add_compiler_flags(conf, env, flags, lang, compiler, uselib = ''):
	for flag in flags:
		if type(flag) == type(()):
			flag_candidate = flag[0]
			flag_alternative = flag[1]
		else:
			flag_candidate = flag
			flag_alternative = None

		if uselib:
			flags_pattern = lang + 'FLAGS_' + uselib
		else:
			flags_pattern = lang + 'FLAGS'

		if check_compiler_flag(conf, flag_candidate, compiler):
			env.append_value(flags_pattern, [flag_candidate])
		elif flag_alternative:
			if check_compiler_flag(conf, flag_alternative, compiler):
				env.append_value(flags_pattern, [flag_alternative])



plugins = {'dumb' : True, 'gme' : True, 'openmpt' : False, 'uade' : False}



def options(opt):
	opt.add_option('--enable-debug', action = 'store_true', default = False, help = 'enable debug build [default: %default]')
	opt.add_option('--with-package-name', action = 'store', default = "gstmpg123 plug-in source release", help = 'specify package name to use in plugin [default: %default]')
	opt.add_option('--with-package-origin', action = 'store', default = "Unknown package origin", help = 'specify package origin URL to use in plugin [default: %default]')
	opt.add_option('--plugin-install-path', action = 'store', default = "${PREFIX}/lib/gstreamer-1.0", help = 'where to install the plugin for GStreamer 1.0 [default: %default]')
	opt.load('compiler_c')
	opt.load('compiler_cxx')
	for plugin in plugins.keys():
		default_enabled = plugins[plugin]
		if default_enabled:
			opt.add_option('--disable-' + plugin, dest = plugin + '_enabled', action = 'store_false', default = True, help = 'Do not build %s plugin' % plugin)
		else:
			opt.add_option('--enable-' + plugin,  dest = plugin + '_enabled', action = 'store_true', default = False, help = 'Build %s plugin' % plugin)


def configure(conf):
	import os


	conf.load('compiler_c')
	conf.load('compiler_cxx')


	if conf.options.enable_debug:
		conf.env['DEBUG_IS_ENABLED'] = 1

	# check and add compiler flags
	if conf.env['CFLAGS'] and conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], conf.env['LINKFLAGS'], "Testing compiler flags %s and linker flags %s" % (' '.join(conf.env['CFLAGS']), ' '.join(conf.env['LINKFLAGS'])))
	elif conf.env['CFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], '', "Testing compiler flags %s" % ' '.join(conf.env['CFLAGS']))
	elif conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, '', conf.env['LINKFLAGS'], "Testing linker flags %s" % ' '.join(conf.env['LINKFLAGS']))
	c_compiler_flags = ['-Wextra', '-Wall', '-std=c99', '-pedantic', '-fPIC', '-DPIC']
	cxx_compiler_flags = ['-Wextra', '-Wall',  '-pedantic', '-fPIC', '-DPIC']
	common_compiler_flags = []
	if conf.options.enable_debug:
		common_compiler_flags = ['-O0', '-g3', '-ggdb']
	else:
		common_compiler_flags = ['-O2', '-s']
	c_compiler_flags += common_compiler_flags
	cxx_compiler_flags += common_compiler_flags

	add_compiler_flags(conf, conf.env, c_compiler_flags, 'C', 'CC')
	add_compiler_flags(conf, conf.env, cxx_compiler_flags, 'CXX', 'CXX')

	# test for SSE
	sse_test_fragment = """
	  #include <xmmintrin.h>
	  __m128 testfunc(float *a, float *b) { return _mm_add_ps(_mm_loadu_ps(a), _mm_loadu_ps(b)); }

	  int main() {
	    float a = 1.0f, b = 2.0f;
	    testfunc(&a, &b);
	    return 0;
	  }
	"""
	conf.env['SSE_SUPPORTED'] = conf.check(fragment = sse_test_fragment, execute = 0, define_ret = 0, msg = 'Checking for SSE support', okmsg = 'yes', errmsg = 'no', mandatory = 0)	

	# test for alloca.h
	conf.env['WITH_ALLOCA'] = conf.check_cc(header_name = 'alloca.h', uselib_store = 'ALLOCA', mandatory = 0)

	# test for stdint.h
	conf.env['WITH_STDINT'] = conf.check_cc(header_name = 'stdint.h', uselib_store = 'STDINT', mandatory = 0)

	# test for GStreamer libraries
	conf.check_cfg(package = 'gstreamer-1.0 >= 1.0.0',       uselib_store = 'GSTREAMER',       args = '--cflags --libs', mandatory = 1)
	conf.check_cfg(package = 'gstreamer-base-1.0 >= 1.0.0',  uselib_store = 'GSTREAMER_BASE',  args = '--cflags --libs', mandatory = 1)
	conf.check_cfg(package = 'gstreamer-audio-1.0 >= 1.0.0', uselib_store = 'GSTREAMER_AUDIO', args = '--cflags --libs', mandatory = 1)
	conf.env['PLUGIN_INSTALL_PATH'] = os.path.expanduser(conf.options.plugin_install_path)
	conf.define('GST_PACKAGE_NAME', conf.options.with_package_name)
	conf.define('GST_PACKAGE_ORIGIN', conf.options.with_package_origin)
	conf.define('PACKAGE', "gstnonstreamaudio")
	conf.define('VERSION', "1.0")

	conf.env['ENABLED_PLUGINS'] = []
	conf.env['DISABLED_PLUGINS'] = {}

	conf.recurse('gst/umxparse')

	for plugin in plugins:
		if getattr(conf.options, plugin + '_enabled'):
			conf.recurse('ext/' + plugin)
		else:
			conf.env['DISABLED_PLUGINS'][plugin] = 'disabled by configuration'

	conf.write_config_header('config.h')

	Logs.pprint('NORMAL', '')
	if len(conf.env['ENABLED_PLUGINS']) > 0:
		Logs.pprint('NORMAL', 'The following plugins will be built:')
		for plugin in conf.env['ENABLED_PLUGINS']:
			Logs.pprint('NORMAL', '    %s' % plugin)
	if len(conf.env['DISABLED_PLUGINS']) > 0:
		Logs.pprint('NORMAL', 'The following plugins will NOT be built:')
		for plugin in conf.env['DISABLED_PLUGINS'].keys():
			reason = conf.env['DISABLED_PLUGINS'][plugin]
			Logs.pprint('NORMAL', '    %s : %s' % (plugin, reason))


def build(bld):
	nonstreamaudio_source = bld.srcnode.ant_glob('gst-libs/*.c')
	bld(
		features = ['c', 'cshlib'],
		includes = ['.', 'gst-libs'],
		uselib = 'GSTREAMER GSTREAMER_BASE GSTREAMER_AUDIO',
		target = 'gstnonstreamaudio',
		name = 'gstnonstreamaudio',
		source = nonstreamaudio_source
	)

	bld.recurse('gst/umxparse')

	for plugin in bld.env['ENABLED_PLUGINS']:
		bld.recurse('ext/' + plugin)

