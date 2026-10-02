add_library(c1quirc STATIC
  ${C1_TOX_ROOT}/.deps/quirc/lib/quirc.c
  ${C1_TOX_ROOT}/.deps/quirc/lib/decode.c
  ${C1_TOX_ROOT}/.deps/quirc/lib/identify.c
  ${C1_TOX_ROOT}/.deps/quirc/lib/version_db.c)
target_include_directories(c1quirc PUBLIC ${C1_TOX_ROOT}/.deps/quirc/lib)
add_library(c1toxqr STATIC ${C1_TOX_ROOT}/tox/src/qr.cpp ${C1_TOX_ROOT}/tox/src/scanner.cpp ${C1_TOX_ROOT}/tox/src/focus.cpp)
target_include_directories(c1toxqr PUBLIC ${C1_TOX_ROOT}/tox/src)
target_link_libraries(c1toxqr PUBLIC c1quirc lvgl pthread m)
