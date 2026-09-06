# Official complete release archive (includes generated/framework sources).
# Source and SHA256 verified against the Mbed-TLS release assets, 2026-09-06.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(ENABLE_PROGRAMS OFF CACHE BOOL "Do not build Mbed TLS sample applications" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Do not build Mbed TLS upstream tests" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "Link private TLS dependency statically" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "Build portable TLS static libraries" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Keep upstream warnings separate from game checks" FORCE)
FetchContent_Declare(four_winds_mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
FetchContent_MakeAvailable(four_winds_mbedtls)
