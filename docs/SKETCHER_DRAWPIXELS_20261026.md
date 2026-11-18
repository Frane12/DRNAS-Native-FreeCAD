# Sketcher constraint icons: raster sampler and blending

Branch: `diagnostics/sketcher-raster-20261026`, based on the real icon capture
branch at `1d467562a38c5a19346cdc93d34017a2aba7cc74`.

The phone showed correct H/V glyphs at BitmapFactory, composed QImage and CPU
Coin SoSFImage checkpoints, while the same scene showed opaque black squares.

A host test using pinned GL4ES `a025615` and actual Mesa GLES reproduced it:

| Implementation | Transparent part | Opaque red glyph |
| --- | --- | --- |
| Original GL4ES | Black | Black |
| Materialize raster sampler only | Black | Red |
| Raster sampler + isolated DrawPixels blending | Background preserved | Red |

`raster_to_texture` configures nearest filtering through GL4ES's deferred
sampler API, then bypasses the normal texture draw path. The native texture
retains `GL_NEAREST_MIPMAP_LINEAR` (0x2702), with only level 0 defined, so texture
sampling returns opaque black. `realize_1texture` materializes the raster's
sampler before the previous texture is restored. In the independent test,
native texture FBO readback was red even before this fix: upload was correct;
sampling was incomplete.

The GLES2 blitter also unconditionally disables blending. A dedicated
`BLIT_DRAWPIXELS` mode retains the tracked blending and applies its factors and
equations directly to GLES, restoring native blending afterward. Ordinary
texture/framebuffer and bitmap blit modes retain their upstream paths.

## Validation

- Compiled the real pinned GL4ES C sources locally, including the existing
  Qt state patch and the new DrawPixels patch.
- Real surfaceless Mesa GLES 2.0 and GLES 3.2 pixel tests pass for fully
  transparent, partially transparent and opaque texels, blend-disabled
  behavior, repeated uploads and preservation of logical blend enable state.
- The harness reports native GL errors instead of treating pixel success as
  an error-free renderer. A first-draw 0x502 also occurs with upstream GL4ES;
  this investigation does not claim to fix every upstream GL diagnostic.
- Native type/Shiboken regression: 3 tests pass; shell syntax checked.
- Android compilation, APK packaging and physical-phone result are separate
  milestones and still require their own verification.

Reproduce with the CMake options `NOX11=ON`, `NOEGL=ON`,
`NO_INIT_CONSTRUCTOR=ON`, `DEFAULT_ES=2`, then run:

```sh
MESA_GLES_VERSION_OVERRIDE=2.0 python3 full-gui/qt-activity/tests/drawpixels-mesa.py /absolute/path/libGL.so.1 --assert-fixed
```

Native logs are bounded to the first 16 raster uploads/blits:
`DRAWPIXELS_RGBA` records converted alpha/color counts and `DRAWPIXELS_BLIT`
records binding, program and blending. The APK gate requires the exported
`drnas_gl4es_drawpixels_blend_v1` marker; GL cache identity includes both patches.

On the phone, open a sketch, draw a rectangle, and check the horizontal and
vertical constraint glyphs after closing DBG. If they remain black, collect
these new markers from that session. Also check ordinary solid rendering and
Open/Save on the separate diagnostic APK before promoting a baseline.
