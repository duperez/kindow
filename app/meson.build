project('kindow-client', 'c', version: '0.1.0', default_options: ['c_std=gnu11'], meson_version: '>=1.1')

gtk_dep = dependency('gtk+-2.0')
vnc_dep = dependency('libvncclient')

sources = files(
    'src/main.c',
    'src/ui.c',
    'src/session.c',
    'src/kindle_platform.c',
    'src/vnc_client.c',
    'src/pixel_convert.c',
)

executable('kindow-client', sources, dependencies: [gtk_dep, vnc_dep])

# pixel_convert.c é lógica pura (sem GTK, sem libvncclient) extraída de vnc_client.c
# especificamente pra ser testável como unidade — ver app/tests/test_pixel_convert.c.
test_pixel_convert = executable(
    'test-pixel-convert',
    files('src/pixel_convert.c', 'tests/test_pixel_convert.c'),
    include_directories: include_directories('src'),
)
test('pixel_convert', test_pixel_convert)
