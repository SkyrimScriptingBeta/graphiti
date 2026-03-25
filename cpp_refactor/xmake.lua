set_project("graphiti")
set_version("0.1.0")
set_languages("c++23")

add_repositories("SkyrimScriptingBeta https://github.com/SkyrimScriptingBeta/Packages.git")

add_requires("kuzu")
add_requires("nlohmann_json")
add_requires("catch2")

-- HTTP dependencies (Phase 1b: API Clients)
add_requires("cpp-httplib")
add_requires("openssl3")

-- YAML parsing for custom type definitions
add_requires("yaml-cpp")

-- SQLite for the optional graphiti-sqlite-logger target
add_requires("sqlite3-fts5-vec")

-- ONNX runtime for local embeddings (optional, used by stress_9_local_onnx)
add_requires("onnxruntime-wasm-compatible", {optional = true})

add_requires("ixwebsocket")
add_requires("cli11")

target("graphiti")
    set_kind("static")
    add_files("src/**.cpp")
    remove_files("src/server/**.cpp")
    remove_files("src/logger/**.cpp")
    add_includedirs("include", {public = true})
    add_headerfiles("include/(**.h)")
    add_includedirs("src", {private = true})
    add_packages("kuzu", {public = true})
    add_packages("nlohmann_json", {public = true})
    add_packages("cpp-httplib")
    add_packages("openssl3")
    add_packages("yaml-cpp")
    add_packages("ixwebsocket", {public = true})
    if is_plat("windows") then
        add_syslinks("bcrypt")
    end

target("graphiti-sqlite-logger")
    set_kind("static")
    add_files("src/logger/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("graphiti")
    add_packages("sqlite3-fts5-vec", {public = true})

target("graphiti-kuzu-writer-server")
    set_kind("binary")
    set_default(false)
    add_files("src/server/kuzu_writer_server.cpp")
    add_includedirs("src")
    add_deps("graphiti")
    add_packages("ixwebsocket")
    add_packages("nlohmann_json")
    add_packages("kuzu")
    add_packages("cli11")

target("graphiti_unit_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/unit/**.cpp")
    add_includedirs("src")
    add_deps("graphiti")
    add_packages("catch2")
    add_packages("nlohmann_json")
    add_packages("kuzu")

target("graphiti_integration_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/integration/**.cpp")
    add_includedirs("src")
    add_deps("graphiti")
    add_packages("catch2")
    add_packages("nlohmann_json")
    add_packages("kuzu")

-- Agent attribution example programs (run in order: 1, 2, 3)
for _, name in ipairs({
    "1_ingest_two_agents",
    "2_search_with_agent_filter",
    "3_inspect_graph_data",
}) do
    target("example_" .. name)
        set_kind("binary")
        set_default(false)
        add_files("examples/agent_attribution/" .. name .. ".cpp")
        add_includedirs("src")
        add_deps("graphiti")
        add_packages("nlohmann_json")
        add_packages("kuzu")
end

-- Stress tests / adversarial QA (try to break things)
for _, name in ipairs({
    "1_cypher_injection",
    "2_edge_cases",
    "3_concurrent_hammer",
    "4_chaos_pipeline",
    "5_error_recovery",
    "6_community_stress",
    "7_saga_stress",
    "8_custom_types",
}) do
    target("stress_" .. name)
        set_kind("binary")
        set_default(false)
        add_files("examples/stress_test/" .. name .. ".cpp")
        add_includedirs("src")
        add_deps("graphiti")
        add_packages("nlohmann_json")
        add_packages("kuzu")
end

-- ONNX local embeddings stress tests (requires onnxruntime)
for _, name in ipairs({
    "9_local_onnx",
    "10_full_local",
}) do
    target("stress_" .. name)
        set_kind("binary")
        set_default(false)
        add_files("examples/stress_test/" .. name .. ".cpp")
        add_deps("graphiti")
        add_packages("nlohmann_json")
        add_packages("kuzu")
        add_packages("onnxruntime-wasm-compatible")
end
