# The layout themes' video

A 3D model of the RG DS Plus (three.js, built in code from Anbernic's dimensioned drawing: 167 x 96 x 24 mm closed,
181 mm opened flat, two 4.3" 4:3 screens in dark panels, the d-pad over the one stick and two small buttons under it
on the left, ABXY and Start/Select on the right, the lid's barrel between the base's two knuckles, the back edge's
microSD, USB-C, USB-C, 3.5 mm jack and microSD, stacked shoulders; Matte Black), filmed in a short scripted video: the lid opens, the screens come on, and they run the example layout themes
(canvas, hillside, turntable, pocket) one after another, switching with a glitch. Bloom, depth of field, chromatic
aberration, film grain, a vignette, a colour grade and letterbox bars are done in post-processing.

    make ui && ui/tests/fixture.sh /tmp/fx                 # banana-ui and its test library
    python3 tools/promo/screens.py                          # the themes' screens, 30 fps (paths at the top)
    cd tools/promo && npm install
    python3 -m http.server 8765 --bind 127.0.0.1 &          # (the page loads three.js as modules)
    node render.mjs 1600 0 1020 1 frames                    # width, first and last frame, step, folder
    ffmpeg -framerate 30 -i frames/f%05d.png -c:v libx264 -pix_fmt yuv420p -crf 17 video.mp4

screens.py writes to screens/ next to scene.html. The colour is a parameter: scene.html?color=%23e86a7a (Coral
Pink), %2327489a (Metallic Blue); Matte Black by default. pose.mjs renders stills of the model from any angle (for checking it against photos). It renders with Chromium's software WebGL (SwiftShader):
about 5 s a frame at 1600 x 900 on four cores.
