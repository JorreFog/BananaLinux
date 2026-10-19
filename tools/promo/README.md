# The layout themes' video and stills

**The video** (video.py): the example layout themes made from mockups (Hillside, Turntable, Classic) on two screens,
with what the theme engine does beside each one: its headline, a few points and a piece of the theme's JSON, typed
in; music optional.

**The stills** (scene.html, still.mjs): a 3D model of the RG DS Plus in Matte Black, built in three.js from
Anbernic's dimensioned drawing and photos (167 x 96 x 24 mm closed, 181 mm opened flat; the screens in glossy
frames, the ring speakers, the barrel between the base's knuckles, the d-pad with its ticks over the domed stick in
its collar and the two buttons under it, ABXY over Start and Select, the lights at the right edge, the shoulders side
by side, the back edge's ports), with a theme on its screens; bloom, depth of field and grain in post. pose.mjs
renders the model from any angle for checking it against photos.

    make ui && ui/tests/fixture.sh /tmp/fx              # banana-ui and its test library
    python3 tools/promo/screens.py                       # each theme's screens at 30 fps (1024 x 768)
    MUSIC=song.mp3 MUSIC_AT=20 python3 tools/promo/video.py out.mp4

    cd tools/promo && npm install && python3 -m http.server 8765 --bind 127.0.0.1 &
    node still.mjs 2560 '[["classic.png", "classic", 5.2, 150, [40, 125, 215], [0, 30, -10], 27, 0.00006, -6]]'
    #                  file, theme, its screens' time (s), lid angle, camera, target, fov, aperture, turn of the device

screens.py writes to screens/ next to it; video.py and scene.html read from there. The stills render with
Chromium's software WebGL (SwiftShader).
