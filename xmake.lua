-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
includes("lib/commonlibsse-ng")

-- set project
set_project("DynamicGrip")
set_version("0.6.2")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- require packages
add_requires("simpleini")

-- targets
target("DynamicGrip")
    -- add dependencies to target
    add_deps("commonlibsse-ng")	
    add_packages("simpleini")

    -- add commonlibsse-ng plugin
    add_rules("commonlibsse-ng.plugin", {
        name = "DynamicGrip",
        author = "bosn",
        description = "SKSE64 plugin template using CommonLibSSE-NG"
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
