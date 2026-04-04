target("graphiti-kuzu-writer")
    set_kind("static")
    add_files("src/**.cpp")
    add_includedirs("src", {public = true})
    -- Need KuzuGraphStore (parent class) and the writer client
    add_deps("graphiti-kuzu")
    -- Writer client lives in graphiti's src/remote/ — need include path
    add_includedirs("../../lib/graphiti/src", {private = true})
    add_packages("nlohmann_json")
    add_packages("ixwebsocket")
