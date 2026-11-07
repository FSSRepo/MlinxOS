cmake_minimum_required(VERSION 3.10)
project(node_lite C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# QuickJS with its libc (std/os modules) built into the library; the CLI and the examples are not needed.
set(QJS_BUILD_LIBC ON CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# shared with linweb: examples/external/quickjs
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../external/quickjs ${CMAKE_CURRENT_BINARY_DIR}/external/quickjs EXCLUDE_FROM_ALL)

# mbedTLS (library only, default configuration) for https:// in fetch
file(GLOB MBEDTLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../external/mbedtls/library/*.c)
add_library(mbedtls_lite STATIC ${MBEDTLS_SOURCES})
target_include_directories(mbedtls_lite PUBLIC ../external/mbedtls/include PRIVATE ../external/mbedtls/library)
target_compile_options(mbedtls_lite PRIVATE -w)

# Files embedded into the binary as C arrays: node-lite's JavaScript half and Mozilla's root certificates.
function(embed_file input symbol output)
    file(READ ${input} content HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," array "${content}")
    file(WRITE ${output}
        "const unsigned char ${symbol}[] = {${array}0x00};\n"
        "const unsigned long ${symbol}_len = sizeof(${symbol}) - 1;\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input})
endfunction()

embed_file(${CMAKE_CURRENT_SOURCE_DIR}/lib/node-lite.js node_lite_js ${CMAKE_CURRENT_BINARY_DIR}/node_lite_js.c)

# keep only the certificates from the bundle (the file also carries a lot of commentary)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../external/ca/cacert.pem CA_BUNDLE)
string(REGEX MATCHALL "-----BEGIN CERTIFICATE-----[^-]*-----END CERTIFICATE-----" CA_CERTS "${CA_BUNDLE}")
string(REPLACE ";" "\n" CA_PEM "${CA_CERTS}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ca-certificates.pem "${CA_PEM}\n")
embed_file(${CMAKE_CURRENT_BINARY_DIR}/ca-certificates.pem node_lite_ca_pem ${CMAKE_CURRENT_BINARY_DIR}/node_lite_ca.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../external/ca/cacert.pem)

add_executable(node-lite
    src/main.c
    src/net_native.c
    src/tls_native.c
    ${CMAKE_CURRENT_BINARY_DIR}/node_lite_js.c
    ${CMAKE_CURRENT_BINARY_DIR}/node_lite_ca.c
)
target_include_directories(node-lite PRIVATE src)
target_compile_definitions(node-lite PRIVATE _GNU_SOURCE)
target_compile_options(node-lite PRIVATE -Wall -Wextra)
target_link_libraries(node-lite PRIVATE qjs mbedtls_lite)

install(TARGETS node-lite RUNTIME DESTINATION bin)
install(DIRECTORY example DESTINATION share/node-lite)
