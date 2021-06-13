project('MistLog', 'c')

cc = meson.get_compiler('c')

sso_string_proj = subproject('sso_string')
sso_string = sso_string_proj.get_variable('sso_string_dep')

inc = include_directories([ 'include' ])
deps = [ sso_string ]
sources = [ './src/mist_log.c' ]

mist_log = static_library(
    'mist_log',
    sources,
    include_directories: inc,
    dependencies: deps,
    name_suffix: 'lib',
    name_prefix: ''
)

mist_log_shared = shared_library(
    'mist_log',
    sources,
    version: '0.0.1',
    include_directories: inc,
    dependencies: deps
)

mist_log_dep = declare_dependency(
    include_directories: inc,
    link_with: mist_log_shared
)

link_args = []

if cc.get_id() == 'msvc' and get_option('buildtype') == 'release'
    link_args += '/NODEFAULTLIB:MSVCRTD'
endif

if get_option('build_examples')
    subdir('examples')
endif