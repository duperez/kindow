# Toolchain file de CMake pro alvo do Kindle jailbreakado (arm-kindlehf-linux-gnueabihf).
#
# Espelha o meson-crosscompile.txt já usado pelo projeto `kindle` (mesmo toolchain
# Koxtoolchain/KMC SDK, container Docker `kindle-toolchain`), traduzido pra CMake porque
# libvncclient usa CMake, não Meson. Ver docs/findings/libvncclient-api.md.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX /home/builder/x-tools/arm-kindlehf-linux-gnueabihf)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}/bin/arm-kindlehf-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}/bin/arm-kindlehf-linux-gnueabihf-g++)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}/bin/arm-kindlehf-linux-gnueabihf-ar CACHE FILEPATH "")
set(CMAKE_STRIP        ${TOOLCHAIN_PREFIX}/bin/arm-kindlehf-linux-gnueabihf-strip CACHE FILEPATH "")

set(CMAKE_SYSROOT ${TOOLCHAIN_PREFIX}/arm-kindlehf-linux-gnueabihf/sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

# Programas (ex: ferramentas de build) vêm do host; libs/headers/pacotes só do sysroot do alvo.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
