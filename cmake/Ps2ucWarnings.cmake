# Warning flags shared by every target in this repository.

function(ps2uc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(PS2UC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(PS2UC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Generated code is machine-written: one label per branch target, one helper
# call per instruction. Keep the warnings that can reveal a recompiler bug and
# drop the ones that only fire because of that style.
function(ps2uc_set_generated_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /utf-8 /bigobj /wd4102)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wno-unused-label -Wno-unused-but-set-variable)
    endif()
endfunction()
