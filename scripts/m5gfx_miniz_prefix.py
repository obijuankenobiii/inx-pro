"""Renames M5GFX's bundled miniz compressor symbols so they cannot clash with lib/miniz.

M5GFX (pulled in only by the t5s3 env, for freeink-sdk's LgfxEpdDriver) ships its own
miniz v1.15 as lgfx/utility/lgfx_miniz.c. Upstream prefixed most of it with `lgfx_`, but
not the `tdefl_*` deflate functions, so linking it next to lib/miniz fails with
"multiple definition of `tdefl_compress'" and friends.

Neither copy can simply be dropped: lib/miniz is what the firmware uses, and M5GFX needs
`tdefl_write_image_to_png_file_in_memory_ex_with_cb`, which only its copy has. Instead,
every M5GFX source is compiled with defines that rename its non-static tdefl functions
to `lgfx_tdefl_*`. All M5GFX translation units see the same renames, so the library stays
self-consistent, and lib/miniz keeps the plain names.
"""

Import("env")

# Non-static functions defined in lgfx_miniz.c. Types (tdefl_compressor, tdefl_status, ...)
# are not symbols and are left alone.
RENAMED = (
    "tdefl_compress",
    "tdefl_compress_buffer",
    "tdefl_compress_mem_to_heap",
    "tdefl_compress_mem_to_mem",
    "tdefl_compress_mem_to_output",
    "tdefl_create_comp_flags_from_zip_params",
    "tdefl_get_adler32",
    "tdefl_get_prev_return_status",
    "tdefl_init",
    "tdefl_write_image_to_png_file_in_memory",
    "tdefl_write_image_to_png_file_in_memory_ex",
    "tdefl_write_image_to_png_file_in_memory_ex_with_cb",
)


def prefix_m5gfx_miniz(env, node):
    if "M5GFX" not in node.get_abspath():
        return node
    return env.Object(node, CPPDEFINES=list(env["CPPDEFINES"]) + [(name, "lgfx_" + name) for name in RENAMED])


env.AddBuildMiddleware(prefix_m5gfx_miniz)
