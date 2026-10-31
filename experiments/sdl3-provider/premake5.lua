local sdk = os.getenv("PLUGIN_SDK_DIR") or "../../../plugin-sdk"
local sdl = "deps/SDL3-3.4.18"
local lua = "deps/misi/MISI/lua-5.3.4/src"

workspace "ginput-sdl3-experiment"
    configurations { "Release" }
    architecture "x86"
    location "build/projects"
    system "windows"
    systemversion "latest"
    language "C++"
    cppdialect "C++17"
    staticruntime "On"
    symbols "On"
    optimize "Speed"
    targetdir "dist"
    objdir "build/obj/%{prj.name}"
    includedirs { "include" }
    defines { "_CRT_SECURE_NO_WARNINGS", "NOMINMAX" }

project "GInputSDL3"
    kind "SharedLib"
    targetextension ".asi"
    defines { "GINPUT_SDL3_BUILD" }
    includedirs { sdl .. "/include" }
    libdirs { sdl .. "/lib/x86" }
    links { "SDL3" }
    files { "src/Provider.cpp", "include/GInputSDL3.h" }

project "GInputSDL3Hooks"
    kind "SharedLib"
    cppdialect "C++latest"
    characterset "MBCS"
    includedirs { "build/classic", sdk .. "/shared", sdk .. "/shared/game", sdk .. "/plugin_II", sdk .. "/plugin_II/game_II" }
    libdirs { sdk .. "/output/lib" }
    defines { "GTA2", "PLUGIN_SGV_96EN", "_USE_MATH_DEFINES", "_CRT_NON_CONFORMING_SWPRINTFS" }
    buildoptions { "/permissive" }
    linkoptions { "/SAFESEH:NO" }
    links { "GInputSDL3", "plugin_ii" }
    files { "build/classic/**.cpp", "build/classic/**.h", "src/GameVibration.cpp", "src/VibrationEffects.h" }

project "MisiLua"
    kind "StaticLib"
    language "C"
    files { lua .. "/*.c", lua .. "/*.h" }
    removefiles { lua .. "/lua.c", lua .. "/luac.c" }
    warnings "Off"

project "MISI"
    kind "SharedLib"
    cppdialect "C++14"
    targetextension ".ASI"
    defines { "WIN32", "NDEBUG", "MISI_EXPORTS", "_WINDOWS", "_USRDLL", "_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING" }
    includedirs { "build/misi", lua }
    buildoptions { "/permissive-" }
    links { "GInputSDL3", "MisiLua" }
    files { "build/misi/MISI.cpp", "src/MisiBridge.cpp", "deps/misi/MISI/appmethods.cpp", "deps/misi/MISI/misimethods.cpp" }

project "provider-tests"
    kind "ConsoleApp"
    includedirs { sdl .. "/include", lua }
    libdirs { sdl .. "/lib/x86" }
    links { "GInputSDL3", "SDL3", "MisiLua" }
    files { "tests/ProviderTests.cpp", "src/MisiBridge.cpp" }

project "vibration-tests"
    kind "ConsoleApp"
    includedirs { "src" }
    files { "tests/VibrationTests.cpp", "src/VibrationEffects.h" }
