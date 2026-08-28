project('kindow-client', 'c', version: '0.1.0', default_options: ['c_std=gnu11'], meson_version: '>=1.1')

gtk_dep = dependency('gtk+-2.0')
vnc_dep = dependency('libvncclient')

sources = files(
    'src/main.c',
    'src/ui.c',
    'src/keyboard.c',
    'src/session.c',
    'src/kindle_platform.c',
    'src/remote_control.c',
    'src/vnc_client.c',
    'src/pixel_convert.c',
    'src/connection_store.c',
    'src/strings.c',
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

# keyboard.c é lógica pura (sem GTK, sem libvncclient) desenhada especificamente pra ser
# testável como unidade — ver app/tests/test_keyboard.c.
test_keyboard = executable(
    'test-keyboard',
    files('src/keyboard.c', 'src/strings.c', 'tests/test_keyboard.c'),
    include_directories: include_directories('src'),
)
test('keyboard', test_keyboard)

# connection_store.c é lógica pura (sem GTK, sem libvncclient, sem I/O além do próprio
# arquivo que recebe como parâmetro) — ver app/tests/test_connection_store.c.
test_connection_store = executable(
    'test-connection-store',
    files('src/connection_store.c', 'tests/test_connection_store.c'),
    include_directories: include_directories('src'),
)
test('connection_store', test_connection_store)

# strings.c é o módulo puro de i18n da UI (duas línguas, tr() por id) — ver
# app/tests/test_strings.c.
test_strings = executable(
    'test-strings',
    files('src/strings.c', 'tests/test_strings.c'),
    include_directories: include_directories('src'),
)
test('strings', test_strings)
