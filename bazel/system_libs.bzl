"""Module extension exposing host-installed GLFW 3 (@system_glfw//:glfw) and
libcurl (@system_curl//:curl).

GLFW's BCR module builds X11/Wayland client libraries from source; using the
distribution package (libglfw3-dev / brew install glfw) is lighter. The rule
uses pkg-config when available and falls back to common include locations.
"""

def _find_header_dir(rctx, cflags):
    candidates = [f[2:] for f in cflags if f.startswith("-I")]
    candidates += ["/usr/include", "/usr/local/include", "/opt/homebrew/include"]
    for d in candidates:
        if rctx.path(d + "/GLFW/glfw3.h").exists:
            return d
    return None

def _system_glfw_impl(rctx):
    cflags = []
    libs = ["-lglfw"]
    pkg_config = rctx.which("pkg-config")
    if pkg_config:
        res = rctx.execute([pkg_config, "--cflags", "--libs", "glfw3"])
        if res.return_code == 0:
            flags = res.stdout.strip().split(" ")
            cflags = [f for f in flags if f.startswith("-I")]
            libs = [f for f in flags if f.startswith("-l") or f.startswith("-L") or f.startswith("-Wl")]
    inc = _find_header_dir(rctx, cflags)
    if not inc:
        fail("GLFW 3 headers not found. Install libglfw3-dev (Debian/Ubuntu), " +
             "glfw-devel (Fedora) or `brew install glfw`.")
    rctx.symlink(inc + "/GLFW", "include/GLFW")
    rctx.file("BUILD.bazel", """
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "glfw",
    hdrs = glob(["include/GLFW/*.h"]),
    includes = ["include"],
    linkopts = {libs},
    visibility = ["//visibility:public"],
)
""".format(libs = repr(libs)))

system_glfw = repository_rule(
    implementation = _system_glfw_impl,
    environ = ["PKG_CONFIG_PATH"],
    local = True,
)

def _system_curl_impl(rctx):
    libs = ["-lcurl"]
    cflags = []
    for tool in ["pkg-config", "curl-config"]:
        path = rctx.which(tool)
        if not path:
            continue
        args = [path, "--cflags", "--libs", "libcurl"] if tool == "pkg-config" else [path, "--cflags", "--libs"]
        res = rctx.execute(args)
        if res.return_code == 0:
            flags = res.stdout.strip().replace("\n", " ").split(" ")
            cflags = [f for f in flags if f.startswith("-I")]
            libs = [f for f in flags if f.startswith("-l") or f.startswith("-L")]
            break
    candidates = [f[2:] for f in cflags] + ["/usr/include", "/usr/include/x86_64-linux-gnu",
                                            "/usr/include/aarch64-linux-gnu", "/usr/local/include",
                                            "/opt/homebrew/include", "/opt/homebrew/opt/curl/include"]
    inc = None
    for d in candidates:
        if rctx.path(d + "/curl/curl.h").exists:
            inc = d
            break
    if not inc:
        fail("libcurl headers not found. Install libcurl4-openssl-dev (Debian/Ubuntu), " +
             "libcurl-devel (Fedora) or `brew install curl`.")
    rctx.symlink(inc + "/curl", "include/curl")
    rctx.file("BUILD.bazel", """
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "curl",
    hdrs = glob(["include/curl/*.h"]),
    includes = ["include"],
    linkopts = {libs},
    visibility = ["//visibility:public"],
)
""".format(libs = repr(libs)))

system_curl = repository_rule(
    implementation = _system_curl_impl,
    environ = ["PKG_CONFIG_PATH"],
    local = True,
)

def _system_libs_impl(module_ctx):
    system_curl(name = "system_curl")
    system_glfw(name = "system_glfw")
    return module_ctx.extension_metadata(reproducible = False)

system_libs = module_extension(implementation = _system_libs_impl)
