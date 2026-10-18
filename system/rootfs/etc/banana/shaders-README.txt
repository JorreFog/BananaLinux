DS SHADERS OF YOUR OWN
======================

Put a shader in this folder (banana/shaders on the card, over file sharing or with the card in a computer) as
<name>.frag, and it shows up in Settings > Nintendo DS > Shader as "<name> (card)", for SuperDrastic and BananaDS
alike. Pick it there and start a game. Banana's own shaders are listed first; one of yours with the same name as one
of Banana's is not listed (rename it).

The format is SuperDrastic's: one OpenGL ES 2 fragment shader that draws a DS screen onto a panel. It gets:

    varying vec2 v_texcoord;          where on the DS screen this pixel is (0..1)
    uniform sampler2D u_texture;      the DS screen (256 x 192, or 512 x 384 with 3D at 2x)
    uniform vec2 u_texture_size;      that size in pixels
    uniform vec2 u_output_size;       the panel's: 1024 x 768 on the RG DS Plus (exactly 4x), 640 x 480 on the RG DS

and writes gl_FragColor. Start from one of Banana's in /usr/lib/superdrastic/shaders (for example ds-integer.frag
or sharp-bilinear.frag): sharp-bilinear.frag shows how a RetroArch GLSL shader is adapted, with a few #defines that
map RetroArch's names (Texture, TextureSize, OutputSize, TEX0...) onto these, its vertex part left out.

A shader that doesn't compile shows the game without one; the reason is in banana/logs (superdrastic for
SuperDrastic, bananads for BananaDS). Heavy shaders cost frames at 2x 3D: try a game you know.
