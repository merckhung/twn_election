"""Module extension that locates a GLSL -> SPIR-V compiler on the host.

Vulkan shader compilers are not available from the BCR, so we look for
`glslangValidator` (package glslang-tools) or `glslc` (shaderc) on PATH.
Set GLSLANG_VALIDATOR=/path/to/glslangValidator to force a specific binary.
"""

def _shader_compiler_repo_impl(rctx):
    path = rctx.os.environ.get("GLSLANG_VALIDATOR")
    flavor = "glslang"
    if not path:
        found = rctx.which("glslangValidator")
        if found:
            path = str(found)
        else:
            found = rctx.which("glslc")
            if found:
                path = str(found)
                flavor = "glslc"
    if not path:
        fail("No GLSL compiler found. Install glslang-tools (glslangValidator) " +
             "or shaderc (glslc), or set GLSLANG_VALIDATOR.")
    rctx.symlink(path, "compiler")
    rctx.file("flavor.bzl", "FLAVOR = %r\n" % flavor)
    rctx.file("BUILD.bazel", """
exports_files(["compiler", "flavor.bzl"], visibility = ["//visibility:public"])
""")

shader_compiler_repo = repository_rule(
    implementation = _shader_compiler_repo_impl,
    environ = ["GLSLANG_VALIDATOR", "PATH"],
    local = True,
)

def _shader_tools_impl(module_ctx):
    shader_compiler_repo(name = "shader_compiler")
    return module_ctx.extension_metadata(reproducible = False)

shader_tools = module_extension(implementation = _shader_tools_impl)
