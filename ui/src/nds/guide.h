#pragma once
// banana-ui: the DS Pokémon companion's guide data (model.h: GuideData). One JSON file per game in
// /usr/share/banana/companions/nds-pokemon/<game>.json, written by us from facts (schema: tools/companions/nds-validate.py
// header and docs/COMPANIONS.md). The loader is forgiving where the validator is strict: a broken entry is skipped (counted),
// a file that is not a guide of this game is refused.
//
// Zone ids: the files name places and may leave the game's own map/zone id out ("zone": null). The caller passes a
// GuideResolver (guideResolver(rom) below builds one from the ROM reader's RomData) that finds the ids by the place's
// name; what it finds nothing for stays in GuideData::unresolved and the milestone/secret just carries no id for it.
#include <functional>
#include <string>
#include <vector>
#include "model.h"

namespace banana::nds
{
	struct GuideResolver
	{
		// a place's zone ids by its name as the game shows it (empty: none known)
		std::function<std::vector<uint16_t>(const std::string& placeName)> zones;
		// the ROM's trainer ids for a named foe ("Roark", class "Leader"); aceLevel > 0 picks the team whose top level it is
		std::function<std::vector<uint16_t>(const std::string& name, const std::string& cls, int aceLevel)> trainers;
	};
	// from the ROM's data: zones by Location::name, trainers by Trainer::name / cls and the team's top level
	GuideResolver guideResolver(const RomData& rom);

	// the file name of a game's guide ("platinum.json"); empty for NONE
	std::string guideFileName(GameId id);
	const char* guideDir();		// /usr/share/banana/companions/nds-pokemon, or $BANANA_NDS_GUIDE for tests
	const char* gbaGuideDir();	// the GBA games' (Gen III): /usr/share/banana/companions/gba-pokemon, or $BANANA_GBA_GUIDE

	// load a file. Returns false (with the reason in *why) when it can't be read or isn't a guide of `expect` (GameId::NONE: any).
	// On success *why holds a note when entries were skipped ("2 entries skipped: milestones[3] ...") and is empty otherwise.
	bool loadGuideFile(const std::string& path, GuideData& out, GameId expect = GameId::NONE, std::string* why = nullptr,
		const GuideResolver* resolver = nullptr);
	// the guide for a recognised game from guideDir()
	bool loadGuide(const Game& game, GuideData& out, std::string* why = nullptr, const GuideResolver* resolver = nullptr);

	// the type a guide file names ("fire"), NO_TYPE for anything else
	Type typeFromName(const std::string& name);
}
