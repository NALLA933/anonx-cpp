# MoldLinker.cmake - Inject modern ultra-fast linkers (mold / lld) if present

if(NOT WIN32 AND NOT APPLE)
    find_program(MOLD_PATH mold)
    find_program(LLD_PATH ld.lld)

    if(MOLD_PATH)
        message(STATUS "High-performance linker detected: mold (${MOLD_PATH})")
        add_link_options("-fuse-ld=mold")
    elseif(LLD_PATH)
        message(STATUS "High-performance linker detected: lld (${LLD_PATH})")
        add_link_options("-fuse-ld=lld")
    else()
        message(STATUS "Using default system linker (consider installing mold or lld for 10x faster linking)")
    endif()
endif()
