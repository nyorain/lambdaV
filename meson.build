project(
	'lambdaV',
	'cpp',
	version: '0.1.0',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'cpp_std=c++17',
		'warning_level=3',
		'werror=true',
	],
)

dep_dlg = dependency('dlg', fallback: ['dlg', 'dlg_dep'])

executable('lambdav', [
		'compiler.cpp',
		'output.cpp',
	],
	dependencies: dep_dlg)
