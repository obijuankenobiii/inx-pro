# Downloadable font packs

The device Font Manager uses the static ZIP catalog in the [`inx-store`](https://github.com/obijuankenobiii/inx-store) repository. There is one ZIP per family under `font/`; the firmware does not query the GitHub API.

Each pack is at or below 5 MB and includes the source outline files under one top-level family folder:

```text
MyFont.zip
  MyFont/Regular.ttf
  MyFont/Bold.ttf
  MyFont/Italic.ttf
  MyFont/BoldItalic.ttf
```

`Regular.ttf` or `Regular.otf` is required. The device extracts the files into `/fonts/<family>/` and rasterizes them at the requested reader size. This local directory is not the source catalog; it is retained only as documentation for the device-side font layout.
