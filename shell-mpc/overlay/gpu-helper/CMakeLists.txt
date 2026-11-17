# SynthyBoy - the GPU helper process of the MPC overlay (docs/MPC.md "Overlay (experimental)", "GPU drawing in a helper process"). Cross build for the device only: it needs Linux
# DRM headers and dlopen; the EGL / GBM / GLES2 libraries are NOT linked: gl_api.h opens the firmware's own libraries at run time (the build image has none, and the repository
# carries no device binary). Output: synthyboy-overlay-gpu (stripped copy: synthyboy-overlay-gpu.stripped).
add_executable(synthyboy_overlay_gpu main.cpp)
target_compile_features(synthyboy_overlay_gpu PRIVATE cxx_std_17)
target_compile_options(synthyboy_overlay_gpu PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions)
set_target_properties(synthyboy_overlay_gpu PROPERTIES OUTPUT_NAME "synthyboy-overlay-gpu")
target_link_libraries(synthyboy_overlay_gpu PRIVATE ${CMAKE_DL_LIBS})
target_link_options(synthyboy_overlay_gpu PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections -Wl,--as-needed)
add_custom_command(TARGET synthyboy_overlay_gpu POST_BUILD
    COMMAND ${CMAKE_STRIP} --strip-unneeded -o $<TARGET_FILE:synthyboy_overlay_gpu>.stripped $<TARGET_FILE:synthyboy_overlay_gpu>
    COMMENT "stripped copy of the GPU helper")

# SYNTHYBOY_MPC_DEV_LOG (shell-mpc/CMakeLists.txt) OFF: the helper writes no probe file and its output goes to /dev/null (shell-mpc/write_files.h).
if(DEFINED SYNTHYBOY_MPC_DEV_LOG AND NOT SYNTHYBOY_MPC_DEV_LOG)
    target_compile_definitions(synthyboy_overlay_gpu PRIVATE SYNTHYBOY_MPC_WRITE_FILES=0)
endif()

# The measuring scene (docs/MPC.md "Observed on the device", D plan trial 17): a debug build only, never the product. OFF by default.
option(SYNTHYBOY_MPC_OVERLAY_BENCH "MPC overlay GPU helper: draw a measuring scene of its own (/tmp/sb-bench.conf) and write /tmp/sb-bench-stats.txt (diagnostics only)" OFF)
if(SYNTHYBOY_MPC_OVERLAY_BENCH)
    target_compile_definitions(synthyboy_overlay_gpu PRIVATE SYNTHYBOY_OVERLAY_BENCH=1)
endif()
