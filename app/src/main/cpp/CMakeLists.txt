cmake_minimum_required(VERSION 3.22.1)

project("il2Fusion")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        native_hook
        SHARED
        native_hook.cpp
        config/json_extract.cpp
        il2CppDumper/hack.cpp
        il2CppDumper/il2cpp_dump.cpp
        il2CppDumper/xdl/xdl.c
        il2CppDumper/xdl/xdl_iterate.c
        il2CppDumper/xdl/xdl_linker.c
        il2CppDumper/xdl/xdl_lzma.c
        il2CppDumper/xdl/xdl_util.c
        plugins/dump/dump.cpp
        plugins/cocos/cocos_font_replacement.cpp
        plugins/cocos/cocos_lua_interceptor.cpp
        plugins/cocos/cocos_runtime.cpp
        plugins/cocos/cocos_runtime_state.cpp
        plugins/cocos/cocos_text_burst_delay_guard.cpp
        plugins/cocos/cocos_text_decoder.cpp
        plugins/cocos/cocos_text_capture.cpp
        plugins/cocos/cocos_text_symbols.cpp
        plugins/textExtractor/textExtractor.cpp
        plugins/textExtractor/unity_translation_store.cpp
        utils/db.cpp
        utils/elf_symbols.cpp
        utils/hook_backend.cpp
        utils/safe_memory.cpp
        utils/sqlite3.c
        utils/text_utils.cpp
        utils/utils.cpp
        third_party/And64InlineHook/And64InlineHook.cpp
)

target_link_options(
        native_hook PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
)

target_include_directories(
        native_hook PRIVATE
        ${CMAKE_SYSROOT}/usr/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/il2CppDumper
        ${CMAKE_CURRENT_SOURCE_DIR}/il2CppDumper/xdl/include
)

add_library(DOBBY_LIB STATIC IMPORTED)

set_target_properties(DOBBY_LIB PROPERTIES IMPORTED_LOCATION
${CMAKE_CURRENT_SOURCE_DIR}/libs/${CMAKE_ANDROID_ARCH_ABI}/libdobby.a
)

target_link_libraries(
        native_hook PRIVATE
        DOBBY_LIB
        log
)
