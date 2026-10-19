/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "CreatureEraHp.h"
#include "AreaDefines.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStructure.h"
#include "Log.h"
#include "Map.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Tokenize.h"
#include <algorithm>

namespace
{
    constexpr char const* EraNames[CREATURE_ERA_MAX] = { "Vanilla", "TBC", "WotLK" };
    constexpr char const* ContentNames[CREATURE_ERA_CONTENT_MAX] = { "World", "Dungeon", "Raid" };

    // Indexed by CreatureEliteType.
    constexpr char const* RankNames[CREATURE_ERA_HP_RANKS] = { "Normal", "Elite", "RareElite", "WorldBoss", "Rare" };

    // Map 530 holds both the Blood Elf / Draenei starting zones and Outland.
    // Below this level a creature there is levelling content of the Vanilla era.
    constexpr uint8 OUTLAND_FIRST_TBC_LEVEL = 58;

    // Onyxia's Lair and Naxxramas are each used by two eras (mod-individual-progression's
    // 60-63 versions and the WotLK 80-83 ones), so the map's expansion cannot tell them apart.
    constexpr uint8 SHARED_RAID_LAST_VANILLA_LEVEL = 63;

    std::string_view Trim(std::string_view str)
    {
        size_t const first = str.find_first_not_of(" \t");
        if (first == std::string_view::npos)
            return {};

        size_t const last = str.find_last_not_of(" \t");
        return str.substr(first, last - first + 1);
    }
}

CreatureEraHp::CreatureEraHp()
{
    std::fill_n(&_rate[0][0][0][0], sizeof(_rate) / sizeof(float), 1.0f);
}

CreatureEraHp* CreatureEraHp::instance()
{
    static CreatureEraHp instance;
    return &instance;
}

void CreatureEraHp::LoadConfig()
{
    LoadRates();
    LoadEntryOverrides();
}

void CreatureEraHp::LoadRates()
{
    uint32 scaledRows = 0;

    for (uint8 era = 0; era < CREATURE_ERA_MAX; ++era)
        for (uint8 content = 0; content < CREATURE_ERA_CONTENT_MAX; ++content)
            for (uint8 rank = 0; rank < CREATURE_ERA_HP_RANKS; ++rank)
            {
                float* row = _rate[era][content][rank];
                std::fill_n(row, CREATURE_ERA_HP_BANDS, 1.0f);

                std::string const key = Acore::StringFormat("Rate.Creature.EraHP.{}.{}.{}",
                    EraNames[era], ContentNames[content], RankNames[rank]);
                std::string const value = sConfigMgr->GetOption<std::string>(key, "");
                if (value.empty())
                    continue;

                std::vector<std::string_view> const tokens = Acore::Tokenize(value, ',', true);
                if (tokens.size() != CREATURE_ERA_HP_BANDS)
                {
                    LOG_ERROR("server.loading", "{}: expected {} comma-separated values, got {}. Row left at 1.",
                        key, CREATURE_ERA_HP_BANDS, tokens.size());
                    continue;
                }

                float parsed[CREATURE_ERA_HP_BANDS];
                bool valid = true;
                for (uint8 band = 0; band < CREATURE_ERA_HP_BANDS && valid; ++band)
                {
                    Optional<float> const rate = Acore::StringTo<float>(Trim(tokens[band]));
                    if (!rate || *rate <= 0.0f)
                    {
                        LOG_ERROR("server.loading", "{}: value {} ('{}') is not a positive number. Row left at 1.",
                            key, band + 1, std::string(Trim(tokens[band])));
                        valid = false;
                    }
                    else
                        parsed[band] = *rate;
                }

                if (!valid)
                    continue;

                std::copy_n(parsed, CREATURE_ERA_HP_BANDS, row);

                if (std::any_of(row, row + CREATURE_ERA_HP_BANDS, [](float rate) { return rate != 1.0f; }))
                    ++scaledRows;
            }

    if (scaledRows)
        LOG_INFO("server.loading", "Creature era HP: {} of {} rows scale health.", scaledRows,
            CREATURE_ERA_MAX * CREATURE_ERA_CONTENT_MAX * CREATURE_ERA_HP_RANKS);
}

void CreatureEraHp::LoadEntryOverrides()
{
    _entryOverrides.clear();

    std::string const value = sConfigMgr->GetOption<std::string>("Rate.Creature.EraHP.EntryOverrides", "");
    if (value.empty())
        return;

    for (std::string_view token : Acore::Tokenize(value, ',', false))
    {
        std::vector<std::string_view> const pair = Acore::Tokenize(token, ':', false);
        Optional<uint32> const entry = pair.size() == 2 ? Acore::StringTo<uint32>(Trim(pair[0])) : std::nullopt;
        Optional<uint8> const era = pair.size() == 2 ? Acore::StringTo<uint8>(Trim(pair[1])) : std::nullopt;

        if (!entry || !era || *era >= CREATURE_ERA_MAX)
        {
            LOG_ERROR("server.loading", "Rate.Creature.EraHP.EntryOverrides: invalid '{}', expected 'entry:era' "
                "with era 0 (Vanilla), 1 (TBC) or 2 (WotLK).", std::string(Trim(token)));
            continue;
        }

        _entryOverrides[*entry] = *era;
    }

    if (!_entryOverrides.empty())
        LOG_INFO("server.loading", "Creature era HP: loaded {} entry override(s).", _entryOverrides.size());
}

uint8 CreatureEraHp::GetBand(uint8 level)
{
    if (level < 60)
        return level < 10 ? 0 : level / 10;    // 1-9, 10-19, ... 50-59

    if (level == 60)
        return 6;
    if (level <= 63)
        return 7;
    if (level <= 69)
        return 8;
    if (level == 70)
        return 9;
    if (level <= 73)
        return 10;
    if (level <= 79)
        return 11;
    if (level == 80)
        return 12;

    return 13;                                 // 81+
}

float CreatureEraHp::GetRate(Creature const* creature, uint8 level) const
{
    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if (!cInfo || creature->IsPet())
        return 1.0f;

    // Player pets, guardians and totems keep their stock health. The summoner is known from
    // the TempSummon constructor on, which is before the first SelectLevel().
    if (creature->HasUnitTypeMask(UNIT_MASK_MINION))
        if (TempSummon const* summon = creature->ToTempSummon())
            if (summon->GetSummonerGUID().IsPlayer())
                return 1.0f;

    Map const* map = creature->FindMap();
    MapEntry const* mapEntry = map ? map->GetEntry() : nullptr;
    if (!mapEntry || mapEntry->IsBattlegroundOrArena())
        return 1.0f;

    uint8 era;
    auto const overrideItr = _entryOverrides.find(creature->GetEntry());
    if (overrideItr != _entryOverrides.end())
        era = overrideItr->second;
    else if (mapEntry->MapID == MAP_OUTLAND)
        era = level < OUTLAND_FIRST_TBC_LEVEL ? CREATURE_ERA_VANILLA : CREATURE_ERA_TBC;
    else if (mapEntry->MapID == MAP_ONYXIAS_LAIR || mapEntry->MapID == MAP_NAXXRAMAS)
        era = level <= SHARED_RAID_LAST_VANILLA_LEVEL ? CREATURE_ERA_VANILLA : CREATURE_ERA_WOTLK;
    else
        era = mapEntry->Expansion() < CREATURE_ERA_MAX ? uint8(mapEntry->Expansion()) : uint8(CREATURE_ERA_WOTLK);

    uint8 const content = mapEntry->IsRaid() ? CREATURE_ERA_CONTENT_RAID
        : mapEntry->IsNonRaidDungeon() ? CREATURE_ERA_CONTENT_DUNGEON
        : CREATURE_ERA_CONTENT_WORLD;

    // CREATURE_UNKNOWN (5) uses the Elite row, like the default: branch of Creature::_GetHealthMod().
    uint8 const rank = cInfo->rank < CREATURE_ERA_HP_RANKS ? uint8(cInfo->rank) : uint8(CREATURE_ELITE_ELITE);

    return _rate[era][content][rank][GetBand(level)];
}
