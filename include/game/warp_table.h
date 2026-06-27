// game/warp_table.h -- curated warp destinations (area / map / stage / room / spawn).

#pragma once

#include "game/types.h"

typedef struct WarpEntry {
    const char* area;    // region / area label
    const char* name;    // friendly map name
    const char* stage;   // stage file code (mNextStage name)
    u8          room;    // default room number
    s16         spawn;   // default spawn point
} WarpEntry;

static const WarpEntry kWarpTable[] = {
    // --- Hyrule Field ---
    { "Hyrule Field", "Hyrule Field", "F_SP121", 0, 0 },
    // --- Ordon ---
    { "Ordon", "Ordon Village", "F_SP103", 0, 0 },
    { "Ordon", "Outside Link's House", "F_SP103", 1, 0 },
    { "Ordon", "Ordon Ranch", "F_SP00", 0, 0 },
    { "Ordon", "Ordon Spring", "F_SP104", 1, 0 },
    { "Ordon", "Bo's House", "R_SP01", 0, 0 },
    { "Ordon", "Sera's Sundries", "R_SP01", 1, 0 },
    { "Ordon", "Jaggle's House", "R_SP01", 2, 0 },
    { "Ordon", "Link's House", "R_SP01", 4, 0 },
    { "Ordon", "Rusl's House", "R_SP01", 5, 0 },
    // --- Faron ---
    { "Faron", "South Faron Woods", "F_SP108", 0, 0 },
    { "Faron", "North Faron Woods", "F_SP108", 6, 0 },
    { "Faron", "Lost Woods", "F_SP117", 3, 0 },
    { "Faron", "Sacred Grove", "F_SP117", 1, 1 },
    { "Faron", "Temple of Time (Past)", "F_SP117", 2, 0 },
    { "Faron", "Faron Woods Cave", "D_SB10", 0, 0 },
    { "Faron", "Coro's House", "R_SP108", 0, 0 },
    // --- Eldin ---
    { "Eldin", "Kakariko Village", "F_SP109", 0, 0 },
    { "Eldin", "Death Mountain Trail", "F_SP110", 0, 0 },
    { "Eldin", "Kakariko Graveyard", "F_SP111", 0, 0 },
    { "Eldin", "Hidden Village", "F_SP128", 0, 0 },
    { "Eldin", "Renado's Sanctuary", "R_SP109", 0, 0 },
    { "Eldin", "Sanctuary Basement", "R_SP209", 7, 0 },
    { "Eldin", "Barnes' Bombs", "R_SP109", 1, 0 },
    { "Eldin", "Elde Inn", "R_SP109", 2, 0 },
    { "Eldin", "Malo Mart", "R_SP109", 3, 0 },
    { "Eldin", "Lookout Tower", "R_SP109", 4, 0 },
    { "Eldin", "Bomb Warehouse", "R_SP109", 5, 0 },
    { "Eldin", "Abandoned House", "R_SP109", 6, 0 },
    { "Eldin", "Goron Elder's Hall", "R_SP110", 0, 0 },
    // --- Lanayru ---
    { "Lanayru", "Outside Castle Town - West", "F_SP122", 8, 0 },
    { "Lanayru", "Outside Castle Town - South", "F_SP122", 16, 0 },
    { "Lanayru", "Outside Castle Town - East", "F_SP122", 17, 0 },
    { "Lanayru", "Castle Town", "F_SP116", 0, 0 },
    { "Lanayru", "Zora's River", "F_SP112", 1, 0 },
    { "Lanayru", "Zora's Domain", "F_SP113", 0, 0 },
    { "Lanayru", "Lake Hylia", "F_SP115", 0, 0 },
    { "Lanayru", "Lanayru Spring", "F_SP115", 1, 0 },
    { "Lanayru", "Upper Zora's River", "F_SP126", 0, 0 },
    { "Lanayru", "Fishing Pond", "F_SP127", 0, 0 },
    { "Lanayru", "Castle Town Sewers", "R_SP107", 0, 0 },
    { "Lanayru", "Telma's Bar / Secret Passage", "R_SP116", 5, 0 },
    { "Lanayru", "Hena's Cabin", "R_SP127", 0, 0 },
    { "Lanayru", "Impaz's House", "R_SP128", 0, 0 },
    { "Lanayru", "Malo Mart", "R_SP160", 0, 0 },
    { "Lanayru", "Fanadi's Palace", "R_SP160", 1, 0 },
    { "Lanayru", "Medical Clinic", "R_SP160", 2, 0 },
    { "Lanayru", "Agitha's Castle", "R_SP160", 3, 0 },
    { "Lanayru", "Goron Shop", "R_SP160", 4, 0 },
    { "Lanayru", "Jovani's House", "R_SP160", 5, 0 },
    { "Lanayru", "STAR Tent", "R_SP161", 7, 0 },
    // --- Gerudo Desert ---
    { "Gerudo Desert", "Bulblin Camp", "F_SP118", 0, 0 },
    { "Gerudo Desert", "Bulblin Camp Beta Room", "F_SP118", 2, 0 },
    { "Gerudo Desert", "Gerudo Desert", "F_SP124", 0, 0 },
    { "Gerudo Desert", "Mirror Chamber", "F_SP125", 4, 0 },
    // --- Snowpeak ---
    { "Snowpeak", "Snowpeak Mountain", "F_SP114", 0, 0 },
    // --- Forest Temple ---
    { "Forest Temple", "Forest Temple", "D_MN05", 0, 0 },
    { "Forest Temple", "Diababa Arena", "D_MN05A", 50, 0 },
    { "Forest Temple", "Ook Arena", "D_MN05B", 51, 0 },
    // --- Goron Mines ---
    { "Goron Mines", "Goron Mines", "D_MN04", 1, 0 },
    { "Goron Mines", "Fyrus Arena", "D_MN04A", 50, 0 },
    { "Goron Mines", "Dangoro Arena", "D_MN04B", 51, 0 },
    // --- Lakebed Temple ---
    { "Lakebed Temple", "Lakebed Temple", "D_MN01", 0, 0 },
    { "Lakebed Temple", "Morpheel Arena", "D_MN01A", 50, 0 },
    { "Lakebed Temple", "Deku Toad Arena", "D_MN01B", 51, 0 },
    // --- Arbiter's Grounds ---
    { "Arbiter's Grounds", "Arbiter's Grounds", "D_MN10", 0, 0 },
    { "Arbiter's Grounds", "Stallord Arena", "D_MN10A", 50, 0 },
    { "Arbiter's Grounds", "Death Sword Arena", "D_MN10B", 51, 0 },
    // --- Snowpeak Ruins ---
    { "Snowpeak Ruins", "Snowpeak Ruins", "D_MN11", 0, 0 },
    { "Snowpeak Ruins", "Blizzeta Arena", "D_MN11A", 50, 0 },
    { "Snowpeak Ruins", "Darkhammer Arena", "D_MN11B", 51, 0 },
    { "Snowpeak Ruins", "Darkhammer Beta Arena", "D_MN11B", 49, 0 },
    // --- Temple of Time ---
    { "Temple of Time", "Temple of Time", "D_MN06", 0, 0 },
    { "Temple of Time", "Armogohma Arena", "D_MN06A", 50, 0 },
    { "Temple of Time", "Darknut Arena", "D_MN06B", 51, 0 },
    // --- City in the Sky ---
    { "City in the Sky", "City in the Sky", "D_MN07", 0, 0 },
    { "City in the Sky", "Argorok Arena", "D_MN07A", 50, 0 },
    { "City in the Sky", "Aeralfos Arena", "D_MN07B", 51, 0 },
    // --- Palace of Twilight ---
    { "Palace of Twilight", "Palace of Twilight", "D_MN08", 0, 0 },
    { "Palace of Twilight", "Palace of Twilight Throne Room", "D_MN08A", 10, 0 },
    { "Palace of Twilight", "Phantom Zant Arena 1", "D_MN08B", 51, 0 },
    { "Palace of Twilight", "Phantom Zant Arena 2", "D_MN08C", 52, 0 },
    { "Palace of Twilight", "Zant Arenas", "D_MN08D", 50, 0 },
    // --- Hyrule Castle ---
    { "Hyrule Castle", "Hyrule Castle", "D_MN09", 1, 0 },
    { "Hyrule Castle", "Hyrule Castle Throne Room", "D_MN09A", 50, 0 },
    { "Hyrule Castle", "Horseback Ganondorf Arena", "D_MN09B", 0, 0 },
    { "Hyrule Castle", "Dark Lord Ganondorf Arena", "D_MN09C", 0, 0 },
    // --- Mini-Dungeons and Grottos ---
    { "Mini-Dungeons and Grottos", "Ice Cavern", "D_SB00", 0, 0 },
    { "Mini-Dungeons and Grottos", "Cave Of Ordeals", "D_SB01", 0, 0 },
    { "Mini-Dungeons and Grottos", "Kakariko Gorge Cavern", "D_SB02", 0, 0 },
    { "Mini-Dungeons and Grottos", "Lake Hylia Cavern", "D_SB03", 0, 0 },
    { "Mini-Dungeons and Grottos", "Goron Stockcave", "D_SB04", 10, 0 },
    { "Mini-Dungeons and Grottos", "Grotto 1", "D_SB05", 0, 0 },
    { "Mini-Dungeons and Grottos", "Grotto 2", "D_SB06", 1, 0 },
    { "Mini-Dungeons and Grottos", "Grotto 3", "D_SB07", 2, 0 },
    { "Mini-Dungeons and Grottos", "Grotto 4", "D_SB08", 3, 0 },
    { "Mini-Dungeons and Grottos", "Grotto 5", "D_SB09", 4, 0 },
    // --- Misc ---
    { "Misc", "Title Screen / King Bulblin 1", "F_SP102", 0, 0 },
    { "Misc", "King Bulblin 2", "F_SP123", 13, 0 },
    { "Misc", "Wolf Howling Cutscene Map", "F_SP200", 0, 0 },
    { "Misc", "Cutscene: Light Arrow Area", "R_SP300", 0, 0 },
    { "Misc", "Cutscene: Hyrule Castle Throne Room", "R_SP301", 0, 0 },
    { "Misc", "Title screen movie map", "S_MV000", 0, 0 },
};

static const int kWarpTableCount = (int)(sizeof(kWarpTable) / sizeof(kWarpTable[0]));
