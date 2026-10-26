# The layout themes' video and stills

**The video** (video.py): a trailer for the layout themes (coming in 0.5.0 beta): the example themes made from mockups (Hillside, Turntable, Classic) on two screens,
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

**The distro video** (distro.py): about two minutes on BananaOS itself, following the storyboard: the website's RG DS
Plus model (rgds_plus.min.glb from main's web/preview) rendered by distro3d.html / distro3d.mjs, with banana-ui's own
frames (headless) and the website's screenshots laid onto its screens, the flat two-screen panels for the menus, and
the website's capture (site-capture.mjs) on a monitor. Cut to the music's beat (128 a minute; MUSIC_AT 22.384 puts
Limoncello's drop at 0:06).

    WORK=/tmp/distro python3 tools/promo/distro.py prepare      # web/preview from main, three.js (npm)
    WORK=/tmp/distro python3 tools/promo/distro.py screens      # banana-ui's frames (30 fps)
    WORK=/tmp/distro python3 tools/promo/distro.py 3d           # the handheld's shots (software WebGL: slow)
    WORK=/tmp/distro SITE=<capture> MUSIC=song.mp3 python3 tools/promo/distro.py render out.mp4

**The 0.5.0 trailer** (release.py): what is new in the first beta, one feature a scene, cut to Wasabi's beat
(wasabi-beats.json, from librosa's beat tracker: the drop brings the first feature, the end card lands where the
song comes back in). BananaOS's look in 3D: the menu's pixel icons and the logo as lit voxels, the example themes'
screens, the Banana Theme Studio and the cards on tilted panels, a floor grid and pixel dust in depth, a pixel-block
wipe between scenes, bloom and grain, the glow pulsing with the beat. No dates anywhere: "Coming very soon in 0.5.0
beta". The GBA companions scene plays the RG DS Plus recordings from the media/companionvideo branch
(COMPANION: the folder holding companionvideo.mp4 and pokemoncompanionvideo.mp4). COMPANIONS sets how many games
have a GBA companion (left out when not given). Renders on every core.

    NO_DATE=1 python3 tools/promo/screens.py hillside turntable classic   # the clocks without dates
    git show origin/media/companionvideo:media/companionvideo.mp4 > rec/companionvideo.mp4   # and pokemoncompanionvideo.mp4
    STUDIO=dir-with-1.webp-and-2.webp COMPANION=rec MUSIC=Wasabi.mp3 python3 tools/promo/release.py out.mp4
