# Bazel overlay for Skia (https://skia.org), used by the `skia` git_repository in
# MODULE.bazel. Builds the CPU raster backend, SkSL (needed by core),
# effects/path ops, and FreeType-based font managers (directory + empty).
#
# Source lists come from Skia's own exported gn/*.gni files via
# tools/gen_skia_srcs.py -> third_party/skia/skia_srcs.bzl.

load("@@//third_party/skia:skia_srcs.bzl", "SKIA_OPTS_ML3_SRCS", "SKIA_OPTS_ML4_SRCS", "SKIA_PUBLIC_HDRS", "SKIA_SRCS")
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])

exports_files(["LICENSE"])

SKIA_DEFINES = [
    "SK_R32_SHIFT=16",
    "SK_TYPEFACE_FACTORY_FREETYPE",
    "SK_FONTMGR_FREETYPE_DIRECTORY_AVAILABLE",
    "SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE",
    "SK_CODEC_DECODES_BMP",
    "SK_CODEC_DECODES_WBMP",
    "SK_DISABLE_TRACING",
] + select({
    "@rules_cc//cc/compiler:msvc-cl": [],
    "//conditions:default": [],
})

SKIA_COPTS = [
    "-std=c++20",
    "-fstrict-aliasing",
    "-fvisibility=hidden",
    "-ffp-contract=off",
    "-fno-rtti",
    "-Wno-attributes",
    "-Wno-psabi",
    "-w",  # third-party code: keep build logs readable
]

# All headers are visible to all Skia sources ("src/..." style includes).
_ALL_HDRS = glob(
    [
        "include/**/*.h",
        "src/**/*.h",
        "modules/skcms/**/*.h",
    ],
    exclude = [
        "src/gpu/**",
        "src/xps/**",
        "src/ports/**/*win*",
        "src/ports/**/*mac*",
        "src/ports/**/*android*",
    ],
) + glob(["src/gpu/**/*.h"])

_TEXTUAL = glob([
    "src/sksl/generated/*.sksl",
    "src/sksl/sksl_*.sksl",
    "src/**/*.inc",
    "modules/skcms/src/Transform_inl.h",
    "src/opts/*.h",
    "src/core/SkRasterPipelineOpContexts.h",
]) + ["src/partition_alloc/noop/raw_ptr.h"]

cc_library(
    name = "headers",
    hdrs = _ALL_HDRS,
    defines = SKIA_DEFINES,
    includes = ["."],
    textual_hdrs = _TEXTUAL,
    deps = ["@freetype"],
)

cc_library(
    name = "skcms",
    srcs = [
        "modules/skcms/skcms.cc",
        "modules/skcms/src/skcms_TransformBaseline.cc",
    ],
    copts = SKIA_COPTS,
    linkstatic = True,
    deps = [
        ":headers",
        ":skcms_hsw",
        ":skcms_skx",
    ],
)

cc_library(
    name = "skcms_hsw",
    srcs = ["modules/skcms/src/skcms_TransformHsw.cc"],
    copts = SKIA_COPTS + select({
        "@platforms//cpu:x86_64": ["-march=haswell"],
        "//conditions:default": [],
    }),
    deps = [":headers"],
    linkstatic = True,
)

cc_library(
    name = "skcms_skx",
    srcs = ["modules/skcms/src/skcms_TransformSkx.cc"],
    copts = SKIA_COPTS + select({
        "@platforms//cpu:x86_64": ["-march=skylake-avx512"],
        "//conditions:default": [],
    }),
    deps = [":headers"],
    linkstatic = True,
)

cc_library(
    name = "opts_ml3",
    srcs = SKIA_OPTS_ML3_SRCS,
    copts = SKIA_COPTS + select({
        "@platforms//cpu:x86_64": ["-march=x86-64-v3"],
        "//conditions:default": [],
    }),
    linkstatic = True,
    deps = [":headers"],
    alwayslink = True,
)

cc_library(
    name = "opts_ml4",
    srcs = SKIA_OPTS_ML4_SRCS,
    copts = SKIA_COPTS + select({
        "@platforms//cpu:x86_64": [
            "-march=x86-64-v4",
            "-mprefer-vector-width=512",
        ],
        "//conditions:default": [],
    }),
    linkstatic = True,
    deps = [":headers"],
    alwayslink = True,
)

cc_library(
    name = "skia",
    srcs = [s for s in SKIA_SRCS if not s.endswith(".h")],
    hdrs = SKIA_PUBLIC_HDRS,
    copts = SKIA_COPTS,
    linkopts = [
        "-ldl",
        "-lpthread",
    ],
    linkstatic = True,
    visibility = ["//visibility:public"],
    deps = [
        ":headers",
        ":skcms",
        "@freetype",
    ] + select({
        "@platforms//cpu:x86_64": [
            ":opts_ml3",
            ":opts_ml4",
        ],
        "//conditions:default": [],
    }),
)
