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

A viewport shader runs after the viewport's children are drawn and before its
tone, color and flash. It reuses the ping-pong path of the gray tone, clipped to
the viewport rectangle, so windows and sprites drawn later are not affected.

Compile errors raise an `MKXPError` with the GLSL log.

The Windows build comes from `.github/workflows/autobuild.yml` (Windows job only),
on every push to `and-shaders` or by hand.
