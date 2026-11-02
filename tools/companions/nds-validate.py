#!/usr/bin/env python3
"""Validate the DS Pokemon companion's guide data (system/rootfs/usr/share/banana/companions/nds-pokemon/<game>.json).

  python3 nds-validate.py                 every nds-pokemon/*.json
  python3 nds-validate.py a.json ...      those files
  -q                                      only failures and the summary
  --stats                                 entry counts per file (and the places whose game id is still unknown)
  --json                                  one JSON line per file (for scripts)

Exit status 0 when every file is valid. Warnings never fail a file.
The companion idea is PokeDaisy's (Lidor Itzhari). The loader is ui/src/nds/guide.cpp; the same rules are in
docs/COMPANIONS.md, "DS Pokemon guide data".

THE SCHEMA (schema 1, kind "nds-pokemon-guide"; one file per game, named <game>.json)
=======================================================================================
Everything is OUR OWN WORDS, written from facts; nothing is copied from a game or a guide. Names, sprites, Pokedex text,
trainer teams, encounter tables and items are read from the player's own ROM at run time, so the guide only says
what the ROM cannot: the story order, level caps, what opens when, secrets, how to evolve things in THIS game.
Comments (// and /* */) and trailing commas are allowed (ui/src/json.cpp).

Top level
  schema        1
  kind          "nds-pokemon-guide"
  game          diamond | pearl | platinum | heartgold | soulsilver | black | white | black2 | white2
  code          the header's game code: ADAE APAE CPUE IPKE IPGE IRBO IRAO IREO IRDO (must fit game)
  title         the game's English name ("Pokemon Platinum")
  revision      this data file's revision (integer, bump when you change facts)
  sources       list of the sites/documents the file draws on (a line each); every entry also names its own
  places        { key: { "name": "Oreburgh City", "zone": 169 | null, "zones": [..] } }
                key: [a-z0-9-]+. name: the place's name as the game shows it (the loader finds the game's zone ids
                by this name in the ROM's location names when "zone" is null). zone: the game's own map/zone id when
                known; null = "id not known yet, resolve by name" (listed by --stats). zones: more ids (interiors).
  milestones    [Milestone] in story order
  steps         [Step]      the guide's "next goals and what to prepare" (tips)
  secrets       [Secret]    hidden items, events, tips tied to a place
  evolutions    [Evolution] how to evolve something in this game (trade evolutions' in-game alternatives, items, places, times)
  notes         [Note]      version exclusives, roamers' rules, grotto notes (Black 2 / White 2), gifts, legendaries, trades

Every entry (all lists): id [a-z0-9-]+ unique in the file; quality "reference-only" (written from web research) or
"live-verified" (checked on a running game; says so in "checked": "device, date, what"); source: ONE line of sources
("Bulbapedia: Roark; Serebii: Platinum gym leaders"), required, no newline, <= 160 chars; text fields <= 480 chars.

Milestone  (gyms, rivals, villain team, Elite Four, champion, post-game; in story order)
  id, name ("Gym 1: Roark"), kind gym|rival|team|elite4|champion|postgame|legendary|story
  place         place key of where it happens
  leader        the person's name (optional)
  theme         the type name (lowercase: normal fighting flying poison ground rock bug ghost steel fire water grass
                electric psychic ice dragon dark) for gyms and Elite Four members (optional)
  level_cap     the highest level among the foe's Pokemon (1-100): a level to compare the player's team with; for a
                story or legendary step with no trainer it is the level to expect (a legendary's own level); a kind
                story milestone may leave it out (0 in GuideData)
  foe           { "name": "Roark", "class": "Leader", "ace_level": 14 } optional: how the loader finds the trainer's
                data in the ROM (name + class text, and the team's top level when several share the name); a LIST of
                such objects when the fight has several possible or several opponents (Gen V gym 1's three brothers,
                the starter-dependent rival); the ids of all of them are collected. name/class are the ROM's own text: a
                player-named rival is stored under a placeholder ("Cedric" in Diamond/Pearl/Platinum, "Rival" in
                Black 2/White 2), Gen IV's Crasher Wake is "Wake", the Frontier Brains' ROM teams are level 5 (leave
                ace_level out); a fight with no trainer in the ROM (a wild legend) has no foe;
  trainer_ids   [int] optional: the ROM's own trainer ids when known (wins over "foe")
  done_when     "badge:N" (N 1-8, 1-16 in HeartGold/SoulSilver: the Nth badge bit is set, Johto first) | "flag:N" (the story flag N, when the RAM workstream
                has it) | "party" (the player has a Pokémon: the starter) | "later" (done once any later milestone is) |
                "none" (no rule: like "later", so an optional fight never holds the story's next goal)
  opens         [place key] the places that become reachable while this is the goal (new ones: the loader adds them
                up, so a milestone's reachable set is everything opened up to and including it)
  tips          the preparation and counters in a few sentences (our words)
  badge_note    optional: what the badge or the fight unlocks (HM use, obedience level)

Step      id, title, text, stage (a milestone id: shown while that is the goal), places [keys], done_when (as above), + common
Secret    id, kind hidden-item|gift|event|trade|tip|legendary|shiny, text, place (key), x, y (tiles, optional, both or
          neither), grid ("map": the place's own map grid, the default; "world": the overworld's global tile grid, as
          the Gen IV decompilations list hidden items), condition (optional: "after the 3rd badge", "needs Surf"), + common
Evolution species (national dex number), name (the species' English name; the validator checks it against the number),
          text, + common. Several per species are joined.
Note      id, kind exclusive|roamer|grotto|gift|legendary|trade|version|tip, title, text, place (optional key),
          species [national numbers] (optional, with "names" [the English names] checked against them), + common
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(HERE, "..", "..", "system", "rootfs", "usr", "share", "banana", "companions", "nds-pokemon")

# game -> (code, generation's dex size, number of badges' region is always 8)
GAMES = {
    "diamond": ("ADAE", 493), "pearl": ("APAE", 493), "platinum": ("CPUE", 493),
    "heartgold": ("IPKE", 493), "soulsilver": ("IPGE", 493),
    "black": ("IRBO", 649), "white": ("IRAO", 649), "black2": ("IREO", 649), "white2": ("IRDO", 649),
}
TYPES = {"normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost", "steel", "fire", "water",
         "grass", "electric", "psychic", "ice", "dragon", "dark"}
QUALITY = {"reference-only", "live-verified"}
MILESTONE_KINDS = {"gym", "rival", "team", "elite4", "champion", "postgame", "legendary", "story"}
SECRET_KINDS = {"hidden-item", "gift", "event", "trade", "tip", "legendary", "shiny"}
NOTE_KINDS = {"exclusive", "roamer", "grotto", "gift", "legendary", "trade", "version", "tip"}
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
DONE_RE = re.compile(r"^(none|later|party|badge:([1-9]|1[0-6])|flag:\d+)$")
MAX_TEXT, MAX_SOURCE = 480, 160
# the national dex names 1-649, for checking "name" against "species" (a mistyped number is the likeliest data error)
_NAMES = (
    "Bulbasaur|Ivysaur|Venusaur|Charmander|Charmeleon|Charizard|Squirtle|Wartortle|Blastoise|Caterpie|"
    "Metapod|Butterfree|Weedle|Kakuna|Beedrill|Pidgey|Pidgeotto|Pidgeot|Rattata|Raticate|Spearow|Fearow|"
    "Ekans|Arbok|Pikachu|Raichu|Sandshrew|Sandslash|Nidoran♀|Nidorina|Nidoqueen|Nidoran♂|Nidorino|"
    "Nidoking|Clefairy|Clefable|Vulpix|Ninetales|Jigglypuff|Wigglytuff|Zubat|Golbat|Oddish|Gloom|"
    "Vileplume|Paras|Parasect|Venonat|Venomoth|Diglett|Dugtrio|Meowth|Persian|Psyduck|Golduck|Mankey|"
    "Primeape|Growlithe|Arcanine|Poliwag|Poliwhirl|Poliwrath|Abra|Kadabra|Alakazam|Machop|Machoke|Machamp|"
    "Bellsprout|Weepinbell|Victreebel|Tentacool|Tentacruel|Geodude|Graveler|Golem|Ponyta|Rapidash|"
    "Slowpoke|Slowbro|Magnemite|Magneton|Farfetch’d|Doduo|Dodrio|Seel|Dewgong|Grimer|Muk|Shellder|"
    "Cloyster|Gastly|Haunter|Gengar|Onix|Drowzee|Hypno|Krabby|Kingler|Voltorb|Electrode|Exeggcute|"
    "Exeggutor|Cubone|Marowak|Hitmonlee|Hitmonchan|Lickitung|Koffing|Weezing|Rhyhorn|Rhydon|Chansey|"
    "Tangela|Kangaskhan|Horsea|Seadra|Goldeen|Seaking|Staryu|Starmie|Mr. Mime|Scyther|Jynx|Electabuzz|"
    "Magmar|Pinsir|Tauros|Magikarp|Gyarados|Lapras|Ditto|Eevee|Vaporeon|Jolteon|Flareon|Porygon|Omanyte|"
    "Omastar|Kabuto|Kabutops|Aerodactyl|Snorlax|Articuno|Zapdos|Moltres|Dratini|Dragonair|Dragonite|"
    "Mewtwo|Mew|Chikorita|Bayleef|Meganium|Cyndaquil|Quilava|Typhlosion|Totodile|Croconaw|Feraligatr|"
    "Sentret|Furret|Hoothoot|Noctowl|Ledyba|Ledian|Spinarak|Ariados|Crobat|Chinchou|Lanturn|Pichu|Cleffa|"
    "Igglybuff|Togepi|Togetic|Natu|Xatu|Mareep|Flaaffy|Ampharos|Bellossom|Marill|Azumarill|Sudowoodo|"
    "Politoed|Hoppip|Skiploom|Jumpluff|Aipom|Sunkern|Sunflora|Yanma|Wooper|Quagsire|Espeon|Umbreon|"
    "Murkrow|Slowking|Misdreavus|Unown|Wobbuffet|Girafarig|Pineco|Forretress|Dunsparce|Gligar|Steelix|"
    "Snubbull|Granbull|Qwilfish|Scizor|Shuckle|Heracross|Sneasel|Teddiursa|Ursaring|Slugma|Magcargo|"
    "Swinub|Piloswine|Corsola|Remoraid|Octillery|Delibird|Mantine|Skarmory|Houndour|Houndoom|Kingdra|"
    "Phanpy|Donphan|Porygon2|Stantler|Smeargle|Tyrogue|Hitmontop|Smoochum|Elekid|Magby|Miltank|Blissey|"
    "Raikou|Entei|Suicune|Larvitar|Pupitar|Tyranitar|Lugia|Ho-Oh|Celebi|Treecko|Grovyle|Sceptile|Torchic|"
    "Combusken|Blaziken|Mudkip|Marshtomp|Swampert|Poochyena|Mightyena|Zigzagoon|Linoone|Wurmple|Silcoon|"
    "Beautifly|Cascoon|Dustox|Lotad|Lombre|Ludicolo|Seedot|Nuzleaf|Shiftry|Taillow|Swellow|Wingull|"
    "Pelipper|Ralts|Kirlia|Gardevoir|Surskit|Masquerain|Shroomish|Breloom|Slakoth|Vigoroth|Slaking|"
    "Nincada|Ninjask|Shedinja|Whismur|Loudred|Exploud|Makuhita|Hariyama|Azurill|Nosepass|Skitty|Delcatty|"
    "Sableye|Mawile|Aron|Lairon|Aggron|Meditite|Medicham|Electrike|Manectric|Plusle|Minun|Volbeat|"
    "Illumise|Roselia|Gulpin|Swalot|Carvanha|Sharpedo|Wailmer|Wailord|Numel|Camerupt|Torkoal|Spoink|"
    "Grumpig|Spinda|Trapinch|Vibrava|Flygon|Cacnea|Cacturne|Swablu|Altaria|Zangoose|Seviper|Lunatone|"
    "Solrock|Barboach|Whiscash|Corphish|Crawdaunt|Baltoy|Claydol|Lileep|Cradily|Anorith|Armaldo|Feebas|"
    "Milotic|Castform|Kecleon|Shuppet|Banette|Duskull|Dusclops|Tropius|Chimecho|Absol|Wynaut|Snorunt|"
    "Glalie|Spheal|Sealeo|Walrein|Clamperl|Huntail|Gorebyss|Relicanth|Luvdisc|Bagon|Shelgon|Salamence|"
    "Beldum|Metang|Metagross|Regirock|Regice|Registeel|Latias|Latios|Kyogre|Groudon|Rayquaza|Jirachi|"
    "Deoxys|Turtwig|Grotle|Torterra|Chimchar|Monferno|Infernape|Piplup|Prinplup|Empoleon|Starly|Staravia|"
    "Staraptor|Bidoof|Bibarel|Kricketot|Kricketune|Shinx|Luxio|Luxray|Budew|Roserade|Cranidos|Rampardos|"
    "Shieldon|Bastiodon|Burmy|Wormadam|Mothim|Combee|Vespiquen|Pachirisu|Buizel|Floatzel|Cherubi|Cherrim|"
    "Shellos|Gastrodon|Ambipom|Drifloon|Drifblim|Buneary|Lopunny|Mismagius|Honchkrow|Glameow|Purugly|"
    "Chingling|Stunky|Skuntank|Bronzor|Bronzong|Bonsly|Mime Jr.|Happiny|Chatot|Spiritomb|Gible|Gabite|"
    "Garchomp|Munchlax|Riolu|Lucario|Hippopotas|Hippowdon|Skorupi|Drapion|Croagunk|Toxicroak|Carnivine|"
    "Finneon|Lumineon|Mantyke|Snover|Abomasnow|Weavile|Magnezone|Lickilicky|Rhyperior|Tangrowth|"
    "Electivire|Magmortar|Togekiss|Yanmega|Leafeon|Glaceon|Gliscor|Mamoswine|Porygon-Z|Gallade|Probopass|"
    "Dusknoir|Froslass|Rotom|Uxie|Mesprit|Azelf|Dialga|Palkia|Heatran|Regigigas|Giratina|Cresselia|Phione|"
    "Manaphy|Darkrai|Shaymin|Arceus|Victini|Snivy|Servine|Serperior|Tepig|Pignite|Emboar|Oshawott|Dewott|"
    "Samurott|Patrat|Watchog|Lillipup|Herdier|Stoutland|Purrloin|Liepard|Pansage|Simisage|Pansear|"
    "Simisear|Panpour|Simipour|Munna|Musharna|Pidove|Tranquill|Unfezant|Blitzle|Zebstrika|Roggenrola|"
    "Boldore|Gigalith|Woobat|Swoobat|Drilbur|Excadrill|Audino|Timburr|Gurdurr|Conkeldurr|Tympole|"
    "Palpitoad|Seismitoad|Throh|Sawk|Sewaddle|Swadloon|Leavanny|Venipede|Whirlipede|Scolipede|Cottonee|"
    "Whimsicott|Petilil|Lilligant|Basculin|Sandile|Krokorok|Krookodile|Darumaka|Darmanitan|Maractus|"
    "Dwebble|Crustle|Scraggy|Scrafty|Sigilyph|Yamask|Cofagrigus|Tirtouga|Carracosta|Archen|Archeops|"
    "Trubbish|Garbodor|Zorua|Zoroark|Minccino|Cinccino|Gothita|Gothorita|Gothitelle|Solosis|Duosion|"
    "Reuniclus|Ducklett|Swanna|Vanillite|Vanillish|Vanilluxe|Deerling|Sawsbuck|Emolga|Karrablast|"
    "Escavalier|Foongus|Amoonguss|Frillish|Jellicent|Alomomola|Joltik|Galvantula|Ferroseed|Ferrothorn|"
    "Klink|Klang|Klinklang|Tynamo|Eelektrik|Eelektross|Elgyem|Beheeyem|Litwick|Lampent|Chandelure|Axew|"
    "Fraxure|Haxorus|Cubchoo|Beartic|Cryogonal|Shelmet|Accelgor|Stunfisk|Mienfoo|Mienshao|Druddigon|"
    "Golett|Golurk|Pawniard|Bisharp|Bouffalant|Rufflet|Braviary|Vullaby|Mandibuzz|Heatmor|Durant|Deino|"
    "Zweilous|Hydreigon|Larvesta|Volcarona|Cobalion|Terrakion|Virizion|Tornadus|Thundurus|Reshiram|Zekrom|"
    "Landorus|Kyurem|Keldeo|Meloetta|Genesect|").rstrip("|").split("|")
assert len(_NAMES) == 649, len(_NAMES)


class Report:
    def __init__(self, path):
        self.path, self.errors, self.warnings = path, [], []

    def err(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def strip_json_comments(text):
    """the loader accepts // and /* */ comments and trailing commas (ui/src/json.cpp); so do we, outside strings"""
    out, i, n, in_str = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            out.append(c)
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        else:
            out.append(c)
        i += 1
    return re.sub(r",(\s*[\]}])", r"\1", "".join(out))


def check_text(r, where, d, key, required=True, limit=MAX_TEXT):
    v = d.get(key)
    if v is None:
        if required:
            r.err(where, "missing %r" % key)
        return None
    if not isinstance(v, str) or not v.strip():
        r.err(where, "%r must be a non-empty string" % key)
        return None
    if len(v) > limit:
        r.err(where, "%r is %d characters (max %d): shorten it, it is a hint, not a guide page" % (key, len(v), limit))
    if re.search(r"[\x00-\x08\x0b-\x1f]", v):
        r.err(where, "%r has control characters" % key)
    return v


def check_common(r, where, e, ids):
    """id, quality, source: on every entry"""
    i = e.get("id")
    if not isinstance(i, str) or not ID_RE.match(i):
        r.err(where, "id %r must be lowercase words with dashes" % (i,))
    elif i in ids:
        r.err(where, "duplicate id %r (also in %s)" % (i, ids[i]))
    else:
        ids[i] = where
    q = e.get("quality")
    if q not in QUALITY:
        r.err(where, "quality must be one of %s, not %r" % (sorted(QUALITY), q))
    elif q == "live-verified":
        check_text(r, where, e, "checked", True, 160)
    s = check_text(r, where, e, "source", True, MAX_SOURCE)
    if s and "\n" in s:
        r.err(where, "source must be ONE line")


def check_place_ref(r, where, e, key, places, required=False):
    v = e.get(key)
    if v is None:
        if required:
            r.err(where, "missing %r" % key)
        return
    if v not in places:
        r.err(where, "%s %r is not in places (add it there: key, name, zone or null)" % (key, v))


def check_species(r, where, num, name, top):
    if not is_int(num) or not (1 <= num <= top):
        r.err(where, "species %r must be a national dex number 1-%d" % (num, top))
        return
    if name is not None and name != _NAMES[num - 1]:
        r.err(where, "species %d is %s, not %r" % (num, _NAMES[num - 1], name))


def validate(path, stats=None):
    r = Report(path)
    base = os.path.basename(path)
    try:
        with open(path, encoding="utf-8") as f:
            raw = f.read()
        d = json.loads(strip_json_comments(raw))
    except (OSError, ValueError) as e:
        r.err(base, "cannot read/parse: %s" % e)
        return r
    if not isinstance(d, dict):
        r.err(base, "top level must be an object")
        return r
    if d.get("schema") != 1:
        r.err(base, "schema must be 1")
    if d.get("kind") != "nds-pokemon-guide":
        r.err(base, 'kind must be "nds-pokemon-guide"')
    game = d.get("game")
    if game not in GAMES:
        r.err(base, "game must be one of %s" % ", ".join(GAMES))
        return r
    code, top = GAMES[game]
    if d.get("code") != code:
        r.err(base, "code for %s must be %s, not %r" % (game, code, d.get("code")))
    if base != game + ".json":
        r.err(base, "file must be named %s.json" % game)
    check_text(r, base, d, "title", True, 80)
    if not is_int(d.get("revision")) or d["revision"] < 1:
        r.err(base, "revision must be an integer >= 1")
    src = d.get("sources")
    if not isinstance(src, list) or not src or not all(isinstance(s, str) and s.strip() for s in src):
        r.err(base, "sources must be a list of lines")

    # places
    places = d.get("places")
    if not isinstance(places, dict) or not places:
        r.err(base, "places must be an object of keys")
        places = {}
    pending = []
    names_seen = {}
    for k, p in places.items():
        w = "%s places.%s" % (base, k)
        if not ID_RE.match(k):
            r.err(w, "key must be lowercase words with dashes")
        if not isinstance(p, dict):
            r.err(w, "must be an object")
            continue
        n = check_text(r, w, p, "name", True, 60)
        if n:
            if n in names_seen:
                r.warn(w, "same name as %s" % names_seen[n])
            names_seen[n] = k
        z = p.get("zone")
        if z is None:
            pending.append(k)
        elif not is_int(z) or not (0 <= z <= 0xFFFF):
            r.err(w, "zone must be an integer 0-65535 or null")
        zs = p.get("zones", [])
        if not isinstance(zs, list) or not all(is_int(x) and 0 <= x <= 0xFFFF for x in zs):
            r.err(w, "zones must be a list of integers")

    used = set()
    ids = {}

    def refs(w, e, key):
        v = e.get(key, [])
        if not isinstance(v, list):
            r.err(w, "%r must be a list of place keys" % key)
            return []
        for x in v:
            if x not in places:
                r.err(w, "%s %r is not in places" % (key, x))
            else:
                used.add(x)
        return v

    # milestones
    ms = d.get("milestones")
    mids = []
    if not isinstance(ms, list) or not ms:
        r.err(base, "milestones must be a non-empty list")
        ms = []
    gyms = set()
    last_cap = {}
    for i, m in enumerate(ms):
        w = "%s milestones[%d]" % (base, i)
        if not isinstance(m, dict):
            r.err(w, "must be an object")
            continue
        w += " (%s)" % m.get("id")
        check_common(r, w, m, ids)
        mids.append(m.get("id"))
        check_text(r, w, m, "name", True, 80)
        if m.get("kind") not in MILESTONE_KINDS:
            r.err(w, "kind must be one of %s" % sorted(MILESTONE_KINDS))
        check_place_ref(r, w, m, "place", places, True)
        if m.get("place") in places:
            used.add(m["place"])
        if m.get("theme") is not None and m["theme"] not in TYPES:
            r.err(w, "theme %r is not a type name" % m.get("theme"))
        lc = m.get("level_cap")
        if lc is None and m.get("kind") == "story":
            pass	# a story step with no fight has no cap (levelCap 0 in GuideData)
        elif not is_int(lc) or not (1 <= lc <= 100):
            r.err(w, "level_cap must be 1-100 (only a kind story milestone may leave it out)")
        foe = m.get("foe")
        if foe is not None:
            for f in (foe if isinstance(foe, list) else [foe]):
                if not isinstance(f, dict) or not isinstance(f.get("name"), str) or \
                        not isinstance(f.get("class", ""), str) or not is_int(f.get("ace_level", 0)):
                    r.err(w, 'foe must be {"name": str, "class": str (optional), "ace_level": int (optional)} or a list of them')
                    break
        tid = m.get("trainer_ids", [])
        if not isinstance(tid, list) or not all(is_int(x) and 0 <= x <= 0xFFFF for x in tid):
            r.err(w, "trainer_ids must be a list of integers")
        dw = m.get("done_when", "none")
        if not isinstance(dw, str) or not DONE_RE.match(dw):
            r.err(w, "done_when must be none | later | party | badge:1-16 | flag:N")
        elif dw.startswith("badge:"):
            gyms.add(int(dw[6:]))
        refs(w, m, "opens")
        check_text(r, w, m, "tips", False)
        check_text(r, w, m, "badge_note", False)
        if m.get("kind") == "gym" and dw == "none":
            r.warn(w, "a gym should say badge:N")
        if is_int(lc) and m.get("kind") in ("gym", "elite4", "champion") and lc < last_cap.get(m.get("kind"), 0):
            r.warn(w, "level_cap %d is below the previous %s's %d" % (lc, m.get("kind"), last_cap[m["kind"]]))
        if is_int(lc):
            last_cap[m.get("kind")] = lc
    if len(set(mids)) != len(mids):
        pass  # reported by check_common
    want = set(range(1, 17 if game in ("heartgold", "soulsilver") else 9))
    if gyms and gyms != want:
        r.warn(base, "gym badges %s: expected %d-%d" % (sorted(gyms), min(want), max(want)))
    if not any(isinstance(m, dict) and m.get("kind") == "champion" for m in ms):
        r.warn(base, "no champion milestone")
    mset = set(mids)

    # steps
    steps = d.get("steps", [])
    if not isinstance(steps, list):
        r.err(base, "steps must be a list")
        steps = []
    for i, s in enumerate(steps):
        w = "%s steps[%d] (%s)" % (base, i, s.get("id") if isinstance(s, dict) else "?")
        if not isinstance(s, dict):
            r.err(w, "must be an object")
            continue
        check_common(r, w, s, ids)
        check_text(r, w, s, "title", True, 80)
        check_text(r, w, s, "text")
        if s.get("stage") is not None and s["stage"] not in mset:
            r.err(w, "stage %r is not a milestone id" % s["stage"])
        refs(w, s, "places")
        dw = s.get("done_when", "none")
        if not isinstance(dw, str) or not DONE_RE.match(dw):
            r.err(w, "done_when must be none | later | party | badge:1-16 | flag:N")

    # secrets
    secs = d.get("secrets", [])
    if not isinstance(secs, list):
        r.err(base, "secrets must be a list")
        secs = []
    for i, s in enumerate(secs):
        w = "%s secrets[%d] (%s)" % (base, i, s.get("id") if isinstance(s, dict) else "?")
        if not isinstance(s, dict):
            r.err(w, "must be an object")
            continue
        check_common(r, w, s, ids)
        if s.get("kind") not in SECRET_KINDS:
            r.err(w, "kind must be one of %s" % sorted(SECRET_KINDS))
        check_text(r, w, s, "text")
        check_place_ref(r, w, s, "place", places, True)
        if s.get("place") in places:
            used.add(s["place"])
        x, y = s.get("x"), s.get("y")
        if (x is None) != (y is None):
            r.err(w, "x and y go together")
        elif x is not None and not (is_int(x) and is_int(y) and 0 <= x < 1024 and 0 <= y < 1024):
            r.err(w, "x, y must be map tiles 0-1023")
        if s.get("grid", "map") not in ("map", "world"):
            r.err(w, 'grid must be "map" or "world"')
        if "grid" in s and x is None:
            r.err(w, "grid without x and y")
        check_text(r, w, s, "condition", False, 120)

    # evolutions
    evs = d.get("evolutions", [])
    if not isinstance(evs, list):
        r.err(base, "evolutions must be a list")
        evs = []
    for i, e in enumerate(evs):
        w = "%s evolutions[%d] (%s)" % (base, i, e.get("id") if isinstance(e, dict) else "?")
        if not isinstance(e, dict):
            r.err(w, "must be an object")
            continue
        check_common(r, w, e, ids)
        if e.get("name") is None:
            r.err(w, "missing name")
        check_species(r, w, e.get("species"), e.get("name"), top)
        check_text(r, w, e, "text")

    # notes
    notes = d.get("notes", [])
    if not isinstance(notes, list):
        r.err(base, "notes must be a list")
        notes = []
    for i, n in enumerate(notes):
        w = "%s notes[%d] (%s)" % (base, i, n.get("id") if isinstance(n, dict) else "?")
        if not isinstance(n, dict):
            r.err(w, "must be an object")
            continue
        check_common(r, w, n, ids)
        if n.get("kind") not in NOTE_KINDS:
            r.err(w, "kind must be one of %s" % sorted(NOTE_KINDS))
        check_text(r, w, n, "title", True, 80)
        check_text(r, w, n, "text")
        check_place_ref(r, w, n, "place", places)
        if n.get("place") in places:
            used.add(n["place"])
        sp, nm = n.get("species", []), n.get("names")
        if not isinstance(sp, list):
            r.err(w, "species must be a list of numbers")
        else:
            if sp and (not isinstance(nm, list) or len(nm) != len(sp)):
                r.err(w, "names must list the English name of each species")
                nm = [None] * len(sp)
            for j, num in enumerate(sp):
                check_species(r, w, num, nm[j] if nm else None, top)

    for k in places:
        if k not in used:
            r.warn("%s places.%s" % (base, k), "never used")
    if stats is not None:
        stats[base] = {"milestones": len(ms), "steps": len(steps), "secrets": len(secs), "evolutions": len(evs),
                       "notes": len(notes), "places": len(places), "places_without_zone_id": len(pending),
                       "pending": pending}
    return r


def main(argv):
    quiet = "-q" in argv
    as_json = "--json" in argv
    want_stats = "--stats" in argv
    files = [a for a in argv if not a.startswith("-")]
    if not files:
        files = sorted(os.path.join(DATA_DIR, f) for f in os.listdir(DATA_DIR) if f.endswith(".json")) \
            if os.path.isdir(DATA_DIR) else []
    if not files:
        print("no guide files found in %s" % DATA_DIR)
        return 1
    bad, stats = 0, {}
    for p in files:
        r = validate(p, stats)
        if as_json:
            print(json.dumps({"file": os.path.basename(p), "ok": not r.errors, "errors": r.errors, "warnings": r.warnings}))
        else:
            if r.errors or not quiet:
                print("%s %s" % ("FAIL" if r.errors else "ok  ", os.path.basename(p)))
            for e in r.errors:
                print("  error: " + e)
            if not quiet:
                for e in r.warnings:
                    print("  warn:  " + e)
        bad += bool(r.errors)
    if want_stats and not as_json:
        for f, s in sorted(stats.items()):
            print("%-16s ms %3d  steps %3d  secrets %3d  evo %3d  notes %3d  places %3d (%d without a zone id)" % (
                f, s["milestones"], s["steps"], s["secrets"], s["evolutions"], s["notes"], s["places"],
                s["places_without_zone_id"]))
    if not as_json:
        print("%d file(s), %d failed" % (len(files), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
