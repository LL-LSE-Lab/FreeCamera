add_rules("mode.debug", "mode.release", "mode.releasedbg")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
add_repositories("groupmountain-repo https://github.com/GroupMountain/xmake-repo.git")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- GMLIB 26.51.0 SDK-static was regenerated from the LeviLamina 26.51.5 headers, so gmlib and
-- levilamina must stay in the same generation (26.51.x). levilamina 26.51.6 pulls
-- bedrockdata v26.51.1-server.7 -> prelink v0.8.6 transitively; never substitute those by hand.
add_requires(
    "levilamina 26.51.6",
    "levibuildscript 0.6.1",
    "gmlib 26.51.0"
)

target("FreeCamera")
    add_cxflags(
        "/EHa",
        "/utf-8",
        "/W4",
        "/w44265",
        "/w44289",
        "/w44296",
        "/w45263",
        "/w44738",
        "/w45204"
    )
    add_defines(
        "NOMINMAX", 
        "UNICODE", 
        "_HAS_CXX17",
        "_HAS_CXX20",
        "_HAS_CXX23"
    )
    add_files(
        "src/**.cpp",
        "src/**.rc"
    )
    add_includedirs("src")
    add_packages(
        "levilamina",
        "gmlib"
    )
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    set_exceptions("none")
    set_kind("shared")
    set_languages("cxx20")
    set_symbols("debug")