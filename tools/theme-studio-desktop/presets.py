"""What the palette adds (ready-made elements) and the starting points for a new theme."""
import copy
import os

# (group, label, tip, element)
PALETTE = [
    ('Basic', 'Rectangle', 'A filled box', {'type': 'rect', 'w': 160, 'h': 90, 'fill': '#2a3045', 'radius': 12}),
    ('Basic', 'Text', 'A line of text; {hooks} in braces', {'type': 'text', 'text': 'Text', 'size': 20, 'color': '#ffffff', 'font': 'inter'}),
    ('Basic', 'Image', 'A picture from the theme folder or a hook', {'type': 'image', 'src': '{sys.icon}', 'w': 96, 'h': 96, 'fit': 'pixelContain'}),
    ('Basic', 'Group', 'A box that holds other elements', {'type': 'group', 'w': 200, 'h': 120, 'children': []}),
    ('Basic', 'Background', 'Fills the whole screen with a gradient',
     {'type': 'rect', 'x': 0, 'y': 0, 'w': '100%', 'h': '100%', 'gradient': {'angle': 180, 'stops': ['#1b2030', '#0b0d14']}}),
    ('Basic', 'Circle', 'A round shape', {'type': 'rect', 'w': 80, 'h': 80, 'radius': '50%', 'fill': '=accent'}),
    ('Layout', 'Row', 'Places its children side by side',
     {'type': 'group', 'layout': 'row', 'gap': 8, 'padding': 8, 'align': 'center', 'fill': '#ffffff22', 'radius': 10,
      'children': [{'type': 'text', 'text': 'One', 'size': 16}, {'type': 'text', 'text': 'Two', 'size': 16}]}),
    ('Layout', 'Column', 'Places its children one below another',
     {'type': 'group', 'layout': 'column', 'gap': 6, 'padding': 10, 'fill': '#ffffff22', 'radius': 10,
      'children': [{'type': 'text', 'text': 'Title', 'size': 20, 'font': 'inter-bold'}, {'type': 'text', 'text': 'Subtitle', 'size': 14}]}),
    ('Layout', 'Pill', 'A chip as wide as its text',
     {'type': 'group', 'layout': 'row', 'padding': 8, 'fill': '=accent', 'radius': '50%',
      'children': [{'type': 'text', 'text': 'Label', 'size': 14, 'color': '#10131c', 'font': 'inter-semibold'}]}),
    ('Lists', 'Systems carousel', 'The systems, sliding sideways with the selection in the middle',
     {'type': 'list', 'source': 'systems', 'direction': 'row', 'x': 0, 'y': 140, 'w': '100%', 'h': 200, 'itemW': 160, 'itemH': 200, 'gap': 16,
      'item': {'type': 'group', 'w': 160, 'h': 200, 'children': [
          {'type': 'rect', 'fill': "=sel ? item.color : '#1b2030'", 'radius': 18, 'border': '=sel ? 3 : 0', 'borderColor': '=accent'},
          {'type': 'image', 'src': '{item.icon}', 'x': 80, 'y': 80, 'w': 64, 'h': 64, 'anchor': 'center', 'fit': 'pixelContain'},
          {'type': 'text', 'x': 0, 'y': 150, 'w': 160, 'align': 'center', 'text': '{item.name}', 'size': 14, 'color': '#ffffff'}]}}),
    ('Lists', 'Systems wheel', 'A free list: each item places itself from its distance to the selection',
     {'type': 'list', 'source': 'systems', 'direction': 'free', 'range': 4,
      'item': {'type': 'image', 'src': '{item.icon}', 'w': 96, 'h': 96, 'fit': 'pixelContain', 'anchor': 'center',
               'x': '=320 + pos * 150', 'y': '=240 + abs(pos) * 30', 'scale': '=1 - min(abs(pos), 3) * 0.2',
               'opacity': '=1 - min(abs(pos), 3) * 0.25'}}),
    ('Lists', 'Games list', 'The library as a list down the screen',
     {'type': 'list', 'source': 'games', 'direction': 'column', 'x': 20, 'y': 70, 'w': 600, 'h': 340, 'itemH': 48, 'gap': 6, 'scrollMode': 'edge',
      'item': {'type': 'group', 'h': 48, 'children': [
          {'type': 'rect', 'fill': "=sel ? accent : '#1b2030'", 'radius': 10},
          {'type': 'text', 'x': 16, 'h': 48, 'valign': 'middle', 'w': 560, 'text': '{item.title}', 'size': 18,
           'color': "=sel ? '#10131c' : '#eef1ff'", 'font': 'inter-medium'}]}}),
    ('Lists', 'Games grid', 'The library as a grid of box art',
     {'type': 'list', 'source': 'games', 'direction': 'grid', 'columns': 4, 'x': 20, 'y': 60, 'w': 600, 'h': 400,
      'itemW': 140, 'itemH': 130, 'gap': 12, 'scrollMode': 'edge',
      'item': {'type': 'group', 'w': 140, 'h': 130, 'children': [
          {'type': 'image', 'src': '{item.art}', 'w': 140, 'h': 130, 'fit': 'cover', 'radius': 10, 'placeholder': '#2a3045'},
          {'type': 'rect', 'w': 140, 'h': 130, 'radius': 10, 'border': '=sel ? 4 : 0', 'borderColor': '=accent'}]}}),
    ('Lists', 'Button hints', 'The buttons and what they do, as chips',
     {'type': 'list', 'source': 'hints', 'direction': 'row', 'flow': True, 'scrollMode': 'none', 'x': 20, 'bottom': 12, 'w': 600, 'h': 26, 'gap': 16,
      'item': {'type': 'group', 'layout': 'row', 'gap': 6, 'align': 'center', 'h': 26, 'children': [
          {'type': 'group', 'layout': 'row', 'padding': 5, 'h': 22, 'fill': '#eef1ff', 'radius': 11,
           'children': [{'type': 'text', 'y': -2, 'text': '{item.key}', 'font': 'inter-bold', 'size': 12, 'color': '#10131c'}]},
          {'type': 'text', 'text': '{item.label}', 'size': 13, 'color': '#9aa3c0', 'y': 3}]}}),
    ('Lists', 'Page dots', 'A dot per system, the selected one bright',
     {'type': 'list', 'source': 'systems', 'direction': 'row', 'scrollMode': 'none', 'x': 260, 'y': 440, 'w': 120, 'h': 10, 'itemW': 10, 'itemH': 10, 'gap': 6,
      'item': {'type': 'rect', 'w': 10, 'h': 10, 'radius': '50%', 'fill': "=sel ? accent : '#ffffff40'"}}),
    ('Status', 'Clock', 'The time', {'type': 'text', 'right': 20, 'y': 10, 'w': 100, 'align': 'right', 'text': '{clock}', 'size': 16, 'font': 'inter-semibold', 'color': '#ffffff'}),
    ('Status', 'Battery', 'The battery as a bar',
     {'type': 'group', 'right': 130, 'y': 12, 'w': 28, 'h': 14, 'visible': '=battery >= 0', 'children': [
         {'type': 'rect', 'w': 26, 'h': 14, 'radius': 3, 'border': 1.5, 'borderColor': '#ffffffaa'},
         {'type': 'rect', 'x': 2, 'y': 2, 'w': '=22 * battery / 100', 'h': 10, 'radius': 1.5, 'fill': "=battery < 20 ? '#ff5a5a' : '#ffffff'"}]}),
    ('Status', 'Wi-Fi dot', 'Bright when online', {'type': 'rect', 'right': 170, 'y': 15, 'w': 8, 'h': 8, 'radius': '50%', 'fill': "=online ? '#4ade80' : '#ffffff30'"}),
    ('Status', 'System name', 'The selected system', {'type': 'text', 'x': 0, 'w': '100%', 'y': 60, 'align': 'center', 'text': '{sys.name}', 'size': 30, 'font': 'inter-bold', 'color': '#ffffff'}),
    ('Status', 'Game title', 'The selected game (library)', {'type': 'text', 'x': 20, 'y': 20, 'w': 600, 'text': '{game.title}', 'size': 24, 'font': 'inter-bold', 'color': '#ffffff'}),
    ('Status', 'Box art', 'The selected game\'s picture', {'type': 'image', 'src': '{game.art}', 'w': 200, 'h': 180, 'fit': 'contain', 'radius': 8, 'placeholder': '#2a3045'}),
    ('Status', 'Launch bar', 'Fills while a game starts (launch view)',
     {'type': 'group', 'x': 120, 'y': 400, 'w': 400, 'h': 8, 'children': [
         {'type': 'rect', 'radius': 4, 'fill': '#ffffff22'},
         {'type': 'rect', 'w': '=400 * launch.progress', 'radius': 4, 'fill': '=accent'}]}),
]


def blank():
    """A small layout theme that already works: a background, a carousel, the name and hints"""
    return {
        'name': 'My theme',
        'author': '',
        'base': 'dark',
        'accent': '#f2c94c',
        'font': 'inter',
        'vars': {'bg': '#10131c', 'card': '#1b2030', 'ink': '#eef1ff', 'soft': '#9aa3c0'},
        'components': {},
        'behavior': {'launchMs': 800},
        'screens': {
            'home': {
                'top': [
                    {'type': 'rect', 'gradient': {'angle': 180, 'stops': ['=mix(sys.color, bg, 0.5)', '=bg']}},
                    {'type': 'image', 'src': '{sys.icon}', 'x': 320, 'y': 200, 'w': 128, 'h': 128, 'anchor': 'center', 'fit': 'pixelContain'},
                    {'type': 'text', 'x': 0, 'w': '100%', 'y': 300, 'align': 'center', 'text': '{sys.name}', 'font': 'inter-bold', 'size': 32, 'color': '=ink'},
                ],
                'bottom': [
                    {'type': 'rect', 'fill': '=bg'},
                    copy.deepcopy(PALETTE[9][3]),
                    copy.deepcopy(PALETTE[13][3]),
                ],
            },
            'library': {
                'top': [
                    {'type': 'rect', 'fill': '=bg'},
                    {'type': 'image', 'src': '{game.art}', 'x': 320, 'y': 220, 'w': 300, 'h': 260, 'anchor': 'center', 'fit': 'contain', 'radius': 10},
                    {'type': 'text', 'x': 0, 'w': '100%', 'y': 400, 'align': 'center', 'text': '{game.title}', 'font': 'inter-bold', 'size': 24, 'color': '=ink'},
                ],
                'bottom': [
                    {'type': 'rect', 'fill': '=bg'},
                    copy.deepcopy(PALETTE[11][3]),
                    copy.deepcopy(PALETTE[13][3]),
                ],
            },
        },
    }


def templates(repo):
    """(label, path or None for the blank one). The label is what the "New from a template" list shows, so it
    says what you get rather than what the file is called."""
    r = [('Blank — a working start', None)]
    if repo:
        for label, rel in (('Canvas — the example layout theme (a good one to learn from)', 'system/rootfs/usr/share/banana/themes/canvas.json'),
                           ('Hillside — a 2000s desktop, all shapes and no pictures', 'docs/themes/hillside.json'),
                           ('Turntable — a dark launcher with tiles that spin', 'docs/themes/turntable.json'),
                           ('Classic — a bright handheld menu with a status bar', 'docs/themes/classic.json')):
            p = os.path.join(repo, rel)
            if os.path.exists(p):
                r.append((label, p))
    return r
