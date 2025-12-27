workspace "OpenGL-Drawing-C"
    configurations { "debug", "release" }
    startproject "OpenGL-Drawing-C"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

newoption {
    trigger = "wasm",
    description = "Choose whether or not to make build files for wasm",
}

project "OpenGL-Drawing-C"
    language "C"
    location "src"
    kind "ConsoleApp"

    includedirs {
        "src",
        "src/third_party"
    }

    files {
        "src/**.h",
        "src/**.c",
    }

    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    targetdir ("bin/" .. outputdir)
    targetprefix ""

    warnings "Extra"
    toolset "clang"

	if _OPTIONS["wasm"] then
        filter "action:ecc"
            -- This is just to make the clang language server happy
            defines "__EMSCRIPTEN__"
            includedirs "../emsdk/upstream/emscripten/system/include/"

        filter "options:wasm"
            buildoptions {
                "-fPIC",
            }

            linkoptions {
                "-sWASM=1",
                "-sASYNCIFY=1",
                "-sALLOW_MEMORY_GROWTH=1",
                "-sOFFSCREEN_FRAMEBUFFER=1",
                "-sMIN_WEBGL_VERSION=2",
                "-sMAIN_MODULE=2",
            }
            links { "m", "GL" }

            targetextension ".js"
    else
        architecture "x64"

        filter { "action:not vs*", "configurations:debug" }
            buildoptions { "-fsanitize=address" }
            linkoptions { "-fsanitize=address" }

        filter "system:linux"
            links {
                "m", "X11", "GL", "GLX",
            }

        filter { "system:windows", "action:*gmake*", "configurations:debug" }
            linkoptions { "-g" }

        filter "system:windows"
            systemversion "latest"

            links {
                "gdi32", "kernel32", "user32", "opengl32"
            }
    end            
        
    filter "configurations:debug"
        symbols "On"
        defines { "DEBUG" }

    filter "configurations:release"
        optimize "On"
        defines { "NDEBUG" }

    
