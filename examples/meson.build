link_with = [mist_log]

executable(
    'simple',
    ['simple.c'],
    link_with: link_with,
    link_args: link_args,
    include_directories: inc,
    dependencies: deps
)

if(host_machine.system() == 'windows')
    executable(
        'continuous',
        ['windows_continuous_example.c'],
        link_with: link_with,
        link_args: link_args,
        include_directories: inc,
        dependencies: deps
    )
endif