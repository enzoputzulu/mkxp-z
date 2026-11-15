# Game shaders (fork for "Au nom de demain")

This branch (`and-shaders`) is mkxp-z at revision c9378cf, the build shipped with
Pokémon Essentials v21.1, plus a small API that lets the game run its own GLSL
fragment shaders. Nothing else changes: without a shader set, rendering is identical.

```ruby
s = Shader.new(File.read("Shaders/night.frag"))   # GLSL ES 1.00 fragment stage
s.set_float("strength", 0.5)                      # float / vec2 / vec3 / vec4 (1 to 4 values)
s.set_array("lights", 4, [x, y, radius, power])   # uniform vec4 lights[N], comps 1 to 4
s.set_texture("noise", bitmap)                    # extra sampler2D (nil removes it)
viewport.shader = s                               # post-process what the viewport drew
sprite.shader = s                                 # draw one sprite through the shader
Shader::API                                       # => 1
```

The fragment stage receives `uniform sampler2D texture`, `varying vec2 v_texCoord`
(normalized), `varying vec2 v_pos` (pixels, y down) and, when declared,
`uniform vec2 size` (texture size in pixels), `uniform float opacity`,
`uniform vec4 tone` and `uniform vec4 color` (sprite values, normalized).

`v_pos` is a pixel of the drawn texture: for a viewport shader, the screen
(the viewport rectangle in screen pixels); for a sprite shader, the sprite's
bitmap (its source rectangle, before position, zoom, angle and mirror).

High-res mode (`enableHires` with `framebufferScalingFactor`): the screen
texture is larger than the game screen, and a bitmap drawn by the game has a
high-res twin. `v_pos` and `size` stay in game pixels in both kinds of
shaders, so a shader written for game pixels works unchanged. `gl_FragCoord`
counts screen texture pixels: declare `uniform float fragScale` and use
`gl_FragCoord.xy * fragScale` for game pixels (1 without high-res mode). An
extra texture with a high-res twin is read from the twin, which holds what
was drawn on it.

Extra textures (`set_texture`) are read without filtering or repeat (nearest,
clamped); a removed or disposed bitmap reads as black. A mega bitmap (larger
than a GL texture) is refused.

A sprite drawn through a shader skips the engine's own sprite effects that are
not passed as uniforms: `bush_depth`, `bush_opacity`, `invert` and `pattern`.
A viewport shader draws with blending off: its output alpha is written as is
(keep `src.a`). `set_array` with fewer values than before leaves the rest of the
uniform array as it was: bound loops by a count uniform.

GLSL: no `#version` (desktop GLSL 1.10, GLSL ES 1.00 on ES). On desktop the
engine defines `highp`, `mediump` and `lowp` as empty, so a `precision`
statement must be guarded by `#ifdef GLSLES`, the define the engine sets on ES.

A viewport shader runs after the viewport's children are drawn and before its
tone, color and flash. It reuses the ping-pong path of the gray tone, clipped to
the viewport rectangle, so windows and sprites drawn later are not affected.

Compile errors raise an `MKXPError` with the GLSL log.

The Windows build comes from `.github/workflows/autobuild.yml` (Windows job only),
on every push to `and-shaders` or by hand.
