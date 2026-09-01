# Downloadable font packs

The device Font Manager uses the static ZIP catalog in the [`inx-font`](https://github.com/obijuankenobiii/inx-font) repository. The catalog has separate `1bit` and `2bit` directories; the firmware does not query the GitHub API.

Each pack is at or below 5 MB and includes the compiled files directly in the archive or under one top-level family folder:

```text
MyFont.zip
  Regular_10.bin
  Regular_12.bin
  Bold_12.bin
  Italic_12.bin
  BoldItalic_12.bin
```

`Regular_<size>.bin` is required for each size. The device extracts the files into `/fonts/<family> <variant>/` and rescans the font list after installation. This local directory is not the source catalog; it is retained only as documentation for the device-side font layout.
