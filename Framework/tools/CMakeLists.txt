set(RENDERER_DIAGNOSTICS_TOOL_FILES
        "RendererDiagnostics.cpp"
        "RendererDiagnosticsAnalyze.cpp"
        "RendererDiagnosticsCapture.cpp"
        "RendererDiagnosticsCapture.h"
        "RendererDiagnosticsCommands.h"
        "RendererDiagnosticsJson.h"
        "RendererDiagnosticsRun.cpp"
        "RendererDiagnosticsSelfTest.cpp")

add_executable(RendererDiagnostics ${RENDERER_DIAGNOSTICS_TOOL_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${RENDERER_DIAGNOSTICS_TOOL_FILES})
target_compile_options(RendererDiagnostics PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_features(RendererDiagnostics PRIVATE cxx_std_20)
target_link_libraries(RendererDiagnostics PRIVATE Framework)
set_target_properties(RendererDiagnostics PROPERTIES FOLDER "Tools")

add_executable(UnitySceneDump "UnitySceneDump.cpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES "UnitySceneDump.cpp")
target_compile_options(UnitySceneDump PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
target_compile_features(UnitySceneDump PRIVATE cxx_std_20)
target_link_libraries(UnitySceneDump PRIVATE Framework)
set_target_properties(UnitySceneDump PROPERTIES FOLDER "Tools")
