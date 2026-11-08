// Which OpenXR API layers sit between the mod and the runtime (src/common/openxr_layer_report_logic.h):
// manifest fields, where a layer's DLL is, whether it was loaded, and the known-problem table.
#include <cstdio>
#include <cstdlib>
#include "../src/common/openxr_layer_report_logic.h"

using namespace openxr_layer_report;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    // A manifest as the loader specification writes one.
    const char* manifest = R"({
        "file_format_version" : "1.0.0",
        "api_layer": {
            "name": "XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility",
            "library_path" : ".\\openxr-oculus-compatibility.dll",
            "api_version": "1.0",
            "implementation_version": "1",
            "description" : "Says \"hello\" \u0041",
            "disable_environment": "DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY"
        }
    })";
    Check(JsonStringField(manifest, "name") == "XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility", "name");
    Check(JsonStringField(manifest, "library_path") == ".\\openxr-oculus-compatibility.dll", "library path, escaped");
    Check(JsonStringField(manifest, "description") == "Says \"hello\" A", "quotes and \\u escapes");
    Check(JsonStringField(manifest, "disable_environment") == "DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY",
          "disable variable");
    Check(JsonStringField(manifest, "enable_environment").empty(), "absent key");
    Check(JsonStringField(R"({"name": 5})", "name").empty(), "a value that is not a string");
    Check(JsonStringField(R"({"name": "cut)", "name").empty(), "an unterminated string");
    Check(JsonStringField(R"({"other_name": "x", "name": "y"})", "name") == "y", "a key that only ends like ours");
    Check(JsonStringField(R"({"desc": "name", "name": "y"})", "name") == "y", "the key's text inside a value");

    // Where the DLL is.
    const char* where = "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\vd-layer.json";
    Check(ResolveLibraryPath(where, ".\\openxr-oculus-compatibility.dll") ==
              "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\openxr-oculus-compatibility.dll",
          "relative to the manifest");
    Check(ResolveLibraryPath(where, "bin/layer.dll") == "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\bin\\layer.dll",
          "a forward-slash relative path");
    Check(ResolveLibraryPath(where, "D:\\Layers\\x.dll") == "D:\\Layers\\x.dll", "absolute");
    Check(ResolveLibraryPath(where, "x.dll") == "x.dll", "a bare name stays bare");
    Check(Leaf("C:\\a\\b.dll") == "b.dll" && Leaf("b.dll") == "b.dll" && Leaf("C:/a/b.dll") == "b.dll", "leaf");

    // Was it loaded.
    Check(ModuleIsLibrary("c:\\program files\\virtual desktop streamer\\openxr\\OPENXR-OCULUS-COMPATIBILITY.DLL",
                          "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\openxr-oculus-compatibility.dll"),
          "the same path, another case");
    Check(!ModuleIsLibrary("D:\\Other\\openxr-oculus-compatibility.dll",
                           "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\openxr-oculus-compatibility.dll"),
          "the same name in another folder is not the layer");
    Check(ModuleIsLibrary("C:\\Windows\\System32\\x.dll", "x.dll"), "a bare name matches by leaf");
    Check(!ModuleIsLibrary("C:\\a\\x.dll", ""), "no library");

    // The verdict.
    Check(Classify(0, true, false, false, false, true) == State::Loaded, "loaded");
    Check(Classify(1, true, false, false, false, true) == State::Loaded, "a loaded DLL outranks the registry");
    Check(Classify(1, true, false, false, false, false) == State::RegistryOff, "registry off");
    Check(Classify(0, false, false, false, false, false) == State::NoManifest, "no manifest");
    Check(Classify(0, true, true, false, false, false) == State::DisabledByEnv, "disable variable set");
    Check(Classify(0, true, false, true, false, false) == State::NotEnabledByEnv, "enable variable missing");
    Check(Classify(0, true, false, true, true, false) == State::NotLoaded, "enabled, yet not loaded");
    Check(Classify(0, true, false, false, false, false) == State::NotLoaded, "nothing stopped it, yet not loaded");

    // Known problems.
    Check(KnownProblemAdvice("XR_APILAYER_OFXRBridge", "") != nullptr, "OFXR by its layer name, XR_APILAYER_OFXRBridge");
    Check(KnownProblemAdvice("XR_APILAYER_something", "C:\\Tools\\OFXR\\ofxr_layer.dll") != nullptr, "OFXR by DLL name");
    Check(KnownProblemAdvice("XR_APILAYER_something", "C:\\OFXR\\layer.dll") == nullptr,
          "only the DLL's own name counts, not its folder");
    Check(KnownProblemAdvice("XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility", "openxr-oculus-compatibility.dll") == nullptr,
          "Virtual Desktop's layer is not a known problem");

    std::printf("openxr_layer_report: %u checks passed\n", g_checks);
    return 0;
}
