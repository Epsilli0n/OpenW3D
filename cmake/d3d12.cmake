# Native Direct3D 12 renderer support.
#
# Defines:
#   W3D_RENDERER_D3D12      option, ON by default for MSVC Windows builds.
#   d3d12lib                INTERFACE target linking d3d12, dxgi and dxguid.
#   w3d_compile_hlsl()      compiles an HLSL entry point to a header holding the
#                           DXIL bytecode as a byte array, using the Windows SDK dxc.

include(CMakeDependentOption)

cmake_dependent_option(W3D_RENDERER_D3D12 "Build the native Direct3D 12 renderer." ON "WIN32;MSVC" OFF)
add_feature_info(D3D12Renderer W3D_RENDERER_D3D12 "Build the native Direct3D 12 renderer")

if(NOT W3D_RENDERER_D3D12)
    return()
endif()

add_library(d3d12lib INTERFACE)
target_link_libraries(d3d12lib INTERFACE d3d12 dxgi dxguid)
target_compile_definitions(d3d12lib INTERFACE W3D_RENDERER_D3D12=1)

# Locate dxc.exe. vcvars puts the SDK bin directory on PATH; the Visual Studio
# generators do not, so fall back to the newest Windows SDK that ships one.
set(_w3d_dxc_hints "$ENV{WindowsSdkVerBinPath}x64" "$ENV{WindowsSdkBinPath}x64")
if(CMAKE_WINDOWS_KITS_10_DIR)
    file(GLOB _w3d_sdk_bins LIST_DIRECTORIES true "${CMAKE_WINDOWS_KITS_10_DIR}/bin/10.*")
else()
    file(GLOB _w3d_sdk_bins LIST_DIRECTORIES true "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*")
endif()
list(SORT _w3d_sdk_bins COMPARE NATURAL ORDER DESCENDING)
foreach(_dir IN LISTS _w3d_sdk_bins)
    list(APPEND _w3d_dxc_hints "${_dir}/x64")
endforeach()

find_program(W3D_DXC_EXECUTABLE NAMES dxc HINTS ${_w3d_dxc_hints} REQUIRED)
message(STATUS "D3D12 shader compiler: ${W3D_DXC_EXECUTABLE}")

# w3d_compile_hlsl(<target> SOURCE <file.hlsl> ENTRY <name> PROFILE <vs_6_0|ps_6_0|...>
#                  VARIABLE <c identifier> [DEFINES A=1 B ...] [DEPENDS extra files])
#
# Generates ${CMAKE_CURRENT_BINARY_DIR}/shaders/<VARIABLE>.h and adds it to <target>,
# along with an include path so sources can #include "<VARIABLE>.h".
function(w3d_compile_hlsl target)
    cmake_parse_arguments(ARG "" "SOURCE;ENTRY;PROFILE;VARIABLE" "DEFINES;DEPENDS" ${ARGN})
    foreach(_required SOURCE ENTRY PROFILE VARIABLE)
        if(NOT ARG_${_required})
            message(FATAL_ERROR "w3d_compile_hlsl: ${_required} is required")
        endif()
    endforeach()

    get_filename_component(_source "${ARG_SOURCE}" ABSOLUTE)
    get_filename_component(_source_dir "${_source}" DIRECTORY)
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(_header "${_out_dir}/${ARG_VARIABLE}.h")

    set(_defines "")
    foreach(_define IN LISTS ARG_DEFINES)
        list(APPEND _defines "-D${_define}")
    endforeach()

    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_dir}"
        COMMAND "${W3D_DXC_EXECUTABLE}" -nologo -T ${ARG_PROFILE} -E ${ARG_ENTRY}
                -O3 -Qstrip_debug -Qstrip_reflect -WX
                -I "${_source_dir}" ${_defines}
                -Vn ${ARG_VARIABLE} -Fh "${_header}" "${_source}"
        # Not MAIN_DEPENDENCY: one .hlsl file usually feeds several entry points.
        DEPENDS "${_source}" ${ARG_DEPENDS}
        COMMENT "Compiling HLSL ${ARG_VARIABLE} (${ARG_PROFILE})"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${_header}")
    target_include_directories(${target} PRIVATE "${_out_dir}")
endfunction()
