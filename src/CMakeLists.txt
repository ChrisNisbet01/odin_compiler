set(app "odinc")

# Generate the grammar library using the macro
epc_generate_grammar(
    TARGET odin_grammar
    GDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/odin_grammar.gdl
    BOOTSTRAP_AST
)

add_executable(${app}
    main.c
    odin_grammar_ast_actions.c
)

target_compile_options(${app} PRIVATE -O3 -g -std=gnu11 -Wall -Wextra -Wswitch-enum -pedantic)

# Add LLVM support for linking
llvm_map_components_to_libnames(llvm_libs support core irreader X86 target mc asmparser asmprinter)

target_link_libraries(${app} PRIVATE
    odin_grammar
    easy_pc_shared
    ${llvm_libs}
    stdc++
)

target_include_directories(${app} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable LTO if supported
if(lto_supported)
    message(STATUS "LTO enabled for ${app} and odin_grammar.")
    set_target_properties(${app} odin_grammar PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()
