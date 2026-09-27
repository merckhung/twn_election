"""Compile GLSL shaders to SPIR-V and embed them into a C++ library."""

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@shader_compiler//:flavor.bzl", "FLAVOR")

def spirv_library(name, srcs, hdrs = [], namespace = "shaders", **kwargs):
    """Compiles each GLSL file in `srcs` to SPIR-V and embeds the result.

    `hdrs` are GLSL files pulled in with #include (GL_GOOGLE_include_directive).
    Generates `<name>.h` declaring `std::span<const uint32_t> <namespace>::<id>()`
    for each shader, where id is the file name with '.' replaced by '_'.
    """
    spvs = []
    for src in srcs:
        out = src.split("/")[-1] + ".spv"
        if FLAVOR == "glslc":
            cmd = "$(location @shader_compiler//:compiler) -O --target-env=vulkan1.1 -o $@ $(location %s)" % src
        else:
            cmd = "$(location @shader_compiler//:compiler) -V --target-env vulkan1.1 -o $@ $(location %s)" % src
        native.genrule(
            name = name + "_" + out.replace(".", "_"),
            srcs = [src] + hdrs,
            outs = [out],
            cmd = cmd,
            tools = ["@shader_compiler//:compiler"],
        )
        spvs.append(out)

    native.genrule(
        name = name + "_embed",
        srcs = spvs,
        outs = [name + ".h", name + ".cc"],
        cmd = "$(location //tools/embed:embed_spirv) --namespace={ns} --header=$(location {h}) --source=$(location {cc}) --include={inc} $(SRCS)".format(
            ns = namespace,
            h = name + ".h",
            cc = name + ".cc",
            inc = native.package_name() + "/" + name + ".h",
        ),
        tools = ["//tools/embed:embed_spirv"],
    )

    cc_library(
        name = name,
        hdrs = [name + ".h"],
        srcs = [name + ".cc"],
        **kwargs
    )
