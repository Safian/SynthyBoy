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
