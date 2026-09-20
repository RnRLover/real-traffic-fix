local pluginSdkDir = os.getenv("PLUGIN_SDK_DIR")
if not pluginSdkDir or pluginSdkDir == "" then
    error("Set PLUGIN_SDK_DIR to the plugin-sdk root folder.")
end

workspace "RealTrafficFix-ThreePlatform"
    configurations { "Release", "Debug" }
    architecture "x86"
    location "project_files"
    startproject "GTASARealTrafficFix"

local function commonSettings()
    kind "SharedLib"
    language "C++"
    cppdialect "C++latest"
    systemversion "latest"
    characterset "MBCS"
    staticruntime "on"
    targetextension ".asi"
    flags { "NoImportLib", "MultiProcessorCompile" }
    objdir "output/obj/%{prj.name}/%{cfg.buildcfg}"
    targetdir "output/asi/%{prj.name}/%{cfg.buildcfg}"
    linkoptions { "/SAFESEH:NO" }
    buildoptions { "/sdl-" }
    defines {
        "_CRT_SECURE_NO_WARNINGS",
        "_CRT_NON_CONFORMING_SWPRINTFS",
        "_USE_MATH_DEFINES",
        "RW"
    }
    disablewarnings { "4073", "4244", "4305", "4800", "4838", "4996", "26812", "26495" }
    files {
        "source/Main.cpp",
        "source/common/**.h",
        "source/common/**.cpp"
    }
    includedirs {
        "source",
        "source/common",
        pluginSdkDir,
        pluginSdkDir .. "/shared",
        pluginSdkDir .. "/shared/game",
        pluginSdkDir .. "/shared/dxsdk",
        pluginSdkDir .. "/safetyhook"
    }
    libdirs {
        pluginSdkDir .. "/output/lib",
        pluginSdkDir .. "/shared/dxsdk"
    }
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        symbols "Off"
    filter "configurations:Debug"
        defines { "DEBUG" }
        optimize "Off"
        symbols "On"
    filter {}
end

project "GTA3RealTrafficFix"
    commonSettings()
    targetname "GTA3RealTrafficFix"
    defines { "GTA3", "PLUGIN_SGV_10EN" }
    files { "source/platform/GTA3Platform.cpp", "GTA_III/resources/GTA3RealTrafficFix.ini" }
    includedirs {
        pluginSdkDir .. "/plugin_III",
        pluginSdkDir .. "/plugin_III/game_III",
        pluginSdkDir .. "/plugin_III/game_III/enums",
        pluginSdkDir .. "/plugin_III/game_III/rw"
    }
    links { "d3d8", "d3dx8" }
    filter "configurations:Release"
        links { "plugin_iii" }
    filter "configurations:Debug"
        links { "plugin_iii_d" }
    filter {}
    postbuildcommands {
        "{COPYFILE} \"%{wks.location}/../GTA_III/resources/GTA3RealTrafficFix.ini\" \"%{cfg.targetdir}/GTA3RealTrafficFix.ini\""
    }

project "GTAVCRealTrafficFix"
    commonSettings()
    targetname "GTAVCRealTrafficFix"
    defines { "GTAVC", "PLUGIN_SGV_10EN" }
    files { "source/platform/VCPlatform.cpp", "GTA_Vice_City/resources/GTAVCRealTrafficFix.ini" }
    includedirs {
        pluginSdkDir .. "/plugin_vc",
        pluginSdkDir .. "/plugin_vc/game_vc",
        pluginSdkDir .. "/plugin_vc/game_vc/enums",
        pluginSdkDir .. "/plugin_vc/game_vc/rw"
    }
    links { "d3d8", "d3dx8" }
    filter "configurations:Release"
        links { "plugin_vc" }
    filter "configurations:Debug"
        links { "plugin_vc_d" }
    filter {}
    postbuildcommands {
        "{COPYFILE} \"%{wks.location}/../GTA_Vice_City/resources/GTAVCRealTrafficFix.ini\" \"%{cfg.targetdir}/GTAVCRealTrafficFix.ini\""
    }

project "GTASARealTrafficFix"
    commonSettings()
    targetname "GTASARealTrafficFix"
    defines { "GTASA", "PLUGIN_SGV_10US" }
    files { "source/platform/SAPlatform.cpp", "GTA_San_Andreas/resources/GTASARealTrafficFix.ini" }
    includedirs {
        pluginSdkDir .. "/plugin_sa",
        pluginSdkDir .. "/plugin_sa/game_sa",
        pluginSdkDir .. "/plugin_sa/game_sa/enums",
        pluginSdkDir .. "/plugin_sa/game_sa/rw"
    }
    links { "d3d9", "d3dx9" }
    filter "configurations:Release"
        links { "plugin" }
    filter "configurations:Debug"
        links { "plugin_d" }
    filter {}
    postbuildcommands {
        "{COPYFILE} \"%{wks.location}/../GTA_San_Andreas/resources/GTASARealTrafficFix.ini\" \"%{cfg.targetdir}/GTASARealTrafficFix.ini\""
    }
