# The layout themes' video

A short video of the example layout themes (Canvas, Hillside, Turntable) running on a handheld's two screens, with
what the theme engine does beside each one: its headline, a few points and a piece of the theme's own JSON, typed in.

    make ui && ui/tests/fixture.sh /tmp/fx     # banana-ui and its test library
    python3 tools/promo/screens.py              # each theme's screens at 30 fps (1024 x 768, the RG DS Plus's)
    python3 tools/promo/video.py out.mp4        # the video: 1920 x 1080, 30 fps, about 30 s

screens.py writes to screens/ next to it, video.py reads from there (Pillow and ffmpeg).
