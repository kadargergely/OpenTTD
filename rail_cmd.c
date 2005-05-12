#include "stdafx.h"
#include "ttd.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "gfx.h"
#include "map.h"
#include "tile.h"
#include "vehicle.h"
#include "viewport.h"
#include "command.h"
#include "pathfind.h"
#include "town.h"
#include "sound.h"
#include "station.h"
#include "sprite.h"
#include "depot.h"
#include "waypoint.h"

extern uint16 _custom_sprites_base;

void ShowTrainDepotWindow(uint tile);

enum { /* These values are bitmasks for the map5 byte */
	RAIL_TYPE_NORMAL = 0,
	RAIL_TYPE_SIGNALS = 0x40,
	RAIL_TYPE_SPECIAL = 0x80, // If this bit is set, then it's not a regular track.
	RAIL_TYPE_DEPOT = 0xC0,
	RAIL_TYPE_MASK = 0xC0,

	RAIL_BIT_DIAG1 = 1,  // 0
	RAIL_BIT_DIAG2 = 2,  // 1
	RAIL_BIT_UPPER = 4,  // 2
	RAIL_BIT_LOWER = 8,  // 3
	RAIL_BIT_LEFT  = 16, // 4
	RAIL_BIT_RIGHT = 32, // 5
	RAIL_BIT_MASK = 0x3F,

	RAIL_DEPOT_TRACK_MASK = 1,
	RAIL_DEPOT_DIR = 3,

	RAIL_SUBTYPE_MASK     = 0x3C,
	RAIL_SUBTYPE_DEPOT    = 0x00,
	RAIL_SUBTYPE_WAYPOINT = 0x04
};

static inline bool IsRailDepot(byte m5)
{
	return
		(m5 & RAIL_TYPE_MASK) == RAIL_TYPE_DEPOT &&
		(m5 & RAIL_SUBTYPE_MASK) == RAIL_SUBTYPE_DEPOT;
}

/* Format of rail map5 byte.
 * 00 abcdef  => Normal rail
 * 01 abcdef  => Rail with signals
 * 10 ??????  => Unused
 * 11 ????dd  => Depot
 *
 * abcdef is a bitmask, which contains ones for all present tracks. Below the
 * value for each track is given.
 */

/*         4
 *     ---------
 *    |\       /|
 *    | \    1/ |
 *    |  \   /  |
 *    |   \ /   |
 *  16|    \    |32
 *    |   / \2  |
 *    |  /   \  |
 *    | /     \ |
 *    |/       \|
 *     ---------
 *         8
 */


// Constants for lower part of Map2 byte.
enum RailMap2Lower4 {
	RAIL_MAP2LO_GROUND_MASK = 0xF,
	RAIL_GROUND_BROWN = 0,
	RAIL_GROUND_GREEN = 1,
	RAIL_GROUND_FENCE_NW = 2,
	RAIL_GROUND_FENCE_SE = 3,
	RAIL_GROUND_FENCE_SENW = 4,
	RAIL_GROUND_FENCE_NE = 5,
	RAIL_GROUND_FENCE_SW = 6,
	RAIL_GROUND_FENCE_NESW = 7,
	RAIL_GROUND_FENCE_VERT1 = 8,
	RAIL_GROUND_FENCE_VERT2 = 9,
	RAIL_GROUND_FENCE_HORIZ1 = 10,
	RAIL_GROUND_FENCE_HORIZ2 = 11,
	RAIL_GROUND_ICE_DESERT = 12,
};


/* MAP2 byte:    abcd???? => Signal On? Same coding as map3lo
 * MAP3LO byte:  abcd???? => Signal Exists?
 *				 a and b are for diagonals, upper and left,
 *				 one for each direction. (ie a == NE->SW, b ==
 *				 SW->NE, or v.v., I don't know. b and c are
 *				 similar for lower and right.
 * MAP2 byte:    ????abcd => Type of ground.
 * MAP3LO byte:  ????abcd => Type of rail.
 * MAP5:         00abcdef => rail
 *               01abcdef => rail w/ signals
 *               10uuuuuu => unused
 *               11uuuudd => rail depot
 */

static bool CheckTrackCombination(uint map5, uint trackbits, uint flags)
{
	_error_message = STR_1001_IMPOSSIBLE_TRACK_COMBINATION;

	switch (map5 & RAIL_TYPE_MASK) {
		case RAIL_TYPE_NORMAL:
			if (map5 & trackbits) {
				_error_message = STR_1007_ALREADY_BUILT;
				return false;
			}

			if (flags & DC_NO_RAIL_OVERLAP) {
				// Computer players are not allowed to intersect pieces of rail.
				map5 |= trackbits;
				return
					map5 == (RAIL_BIT_UPPER | RAIL_BIT_LOWER) ||
					map5 == (RAIL_BIT_LEFT  | RAIL_BIT_RIGHT);
			} else {
				return true;
			}

		case RAIL_TYPE_SIGNALS:
			if (map5 & trackbits) {
				_error_message = STR_1007_ALREADY_BUILT;
				return false;
			}

			map5 |= trackbits;
			return
				map5 == (RAIL_TYPE_SIGNALS | RAIL_BIT_UPPER | RAIL_BIT_LOWER) ||
				map5 == (RAIL_TYPE_SIGNALS | RAIL_BIT_LEFT  | RAIL_BIT_RIGHT);

		default:
			return false;
	}
}


static const byte _valid_tileh_slopes[4][15] = {

// set of normal ones
{
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_RIGHT,
	RAIL_BIT_UPPER,
	RAIL_BIT_DIAG1,

	RAIL_BIT_LEFT,
	0,
	RAIL_BIT_DIAG2,
	RAIL_BIT_LOWER,

	RAIL_BIT_LOWER,
	RAIL_BIT_DIAG2,
	0,
	RAIL_BIT_LEFT,

	RAIL_BIT_DIAG1,
	RAIL_BIT_UPPER,
	RAIL_BIT_RIGHT,
},

// allowed rail for an evenly raised platform
{
	0,
	RAIL_BIT_LEFT,
	RAIL_BIT_LOWER,
	RAIL_BIT_DIAG2 | RAIL_BIT_LOWER | RAIL_BIT_LEFT,

	RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1 | RAIL_BIT_LOWER | RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,

	RAIL_BIT_UPPER,
	RAIL_BIT_DIAG1 | RAIL_BIT_UPPER | RAIL_BIT_LEFT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,

	RAIL_BIT_DIAG2 | RAIL_BIT_UPPER | RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
},

// allowed rail on coast tile
{
	0,
	RAIL_BIT_LEFT,
	RAIL_BIT_LOWER,
	RAIL_BIT_DIAG2|RAIL_BIT_LEFT|RAIL_BIT_LOWER,

	RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_RIGHT|RAIL_BIT_LOWER,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,

	RAIL_BIT_UPPER,
	RAIL_BIT_DIAG1|RAIL_BIT_LEFT|RAIL_BIT_UPPER,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,

	RAIL_BIT_DIAG2|RAIL_BIT_RIGHT|RAIL_BIT_UPPER,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	RAIL_BIT_DIAG1|RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LOWER|RAIL_BIT_LEFT|RAIL_BIT_RIGHT,
	},

	// valid railway crossings on slopes
	{
		1, 0, 0, // 0, 1, 2
		0, 0, 1, // 3, 4, 5
		0, 1, 0, // 6, 7, 8
		0, 1, 1, // 9, 10, 11
		0, 1, 1, // 12, 13, 14
	}
};

uint GetRailFoundation(uint tileh, uint bits)
{
	int i;

	if ((~_valid_tileh_slopes[0][tileh] & bits) == 0)
		return 0;

	if ((~_valid_tileh_slopes[1][tileh] & bits) == 0)
		return tileh;

	if ( ((i=0, tileh == 1) || (i+=2, tileh == 2) || (i+=2, tileh == 4) || (i+=2, tileh == 8)) && (bits == RAIL_BIT_DIAG1 || (i++, bits == RAIL_BIT_DIAG2)))
		return i + 15;

	return 0;
}

//
static uint32 CheckRailSlope(uint tileh, uint rail_bits, uint existing, TileIndex tile)
{
	// never allow building on top of steep tiles
	if (!(tileh & 0x10)) {
		rail_bits |= existing;

		// don't allow building on the lower side of a coast
		if (IsTileType(tile, MP_WATER) &&
				~_valid_tileh_slopes[2][tileh] & rail_bits) {
			return_cmd_error(STR_3807_CAN_T_BUILD_ON_WATER);
		}

		// no special foundation
		if ((~_valid_tileh_slopes[0][tileh] & rail_bits) == 0)
			return 0;

		if ((~_valid_tileh_slopes[1][tileh] & rail_bits) == 0 || ( // whole tile is leveled up
					(rail_bits == RAIL_BIT_DIAG1 || rail_bits == RAIL_BIT_DIAG2) &&
					(tileh == 1 || tileh == 2 || tileh == 4 || tileh == 8)
				)) { // partly up
			if (existing != 0) {
				return 0;
			} else if (!_patches.build_on_slopes ||
					(_is_ai_player && !_patches.ainew_active)) {
				return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
			} else {
				return _price.terraform;
			}
		}
	}
	return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
}

/* Validate functions for rail building */
static inline bool ValParamTrackOrientation(uint32 rail) {return rail <= 5;}

/** Build a single piece of rail
 * @param x,y coordinates on where to build
 * @param p1 railtype of being built piece (normal, mono, maglev)
 * @param p2 rail combination to build
 */
int32 CmdBuildSingleRail(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	TileIndex tile;
	uint tileh;
	uint m5;
	uint rail_bit;
	int32 cost = 0;
	int32 ret;

	if (!ValParamRailtype(p1) || !ValParamTrackOrientation(p2)) return CMD_ERROR;

	tile = TILE_FROM_XY(x, y);
	tileh = GetTileSlope(tile, NULL);
	m5 = _map5[tile];
	rail_bit = 1 << p2;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	switch (GetTileType(tile)) {
		case MP_TUNNELBRIDGE:
			if ((m5 & 0xC0) != 0xC0 || // not bridge middle part?
					(m5 & 0x01 ? 1 : 2) != rail_bit) { // wrong direction?
				// Get detailed error message
				return DoCommandByTile(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}

			switch (m5 & 0x38) { // what's under the bridge?
				case 0x00: // clear land
					ret = CheckRailSlope(tileh, rail_bit, 0, tile);
					if (CmdFailed(ret)) return ret;
					cost += ret;

					if (flags & DC_EXEC) {
						_map_owner[tile] = _current_player;
						_map3_lo[tile] &= ~0x0F;
						_map3_lo[tile] |= p1;
						_map5[tile] = (m5 & 0xC7) | 0x20; // railroad under bridge
					}
					break;

				case 0x20: // rail already there
					return_cmd_error(STR_1007_ALREADY_BUILT);

				default:
					// Get detailed error message
					return DoCommandByTile(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}
			break;

		case MP_RAILWAY:
			if (!CheckTrackCombination(m5, rail_bit, flags) ||
					!EnsureNoVehicle(tile)) {
				return CMD_ERROR;
			}
			if (m5 & RAIL_TYPE_SPECIAL ||
					_map_owner[tile] != _current_player ||
					(_map3_lo[tile] & 0xFU) != p1) {
				// Get detailed error message
				return DoCommandByTile(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}

			ret = CheckRailSlope(tileh, rail_bit, m5 & RAIL_BIT_MASK, tile);
			if (CmdFailed(ret)) return ret;
			cost += ret;

			if (flags & DC_EXEC) {
				_map2[tile] &= ~RAIL_MAP2LO_GROUND_MASK; // Bare land
				_map5[tile] = m5 | rail_bit;
			}
			break;

		case MP_STREET:
			if (!_valid_tileh_slopes[3][tileh]) // prevent certain slopes
				return_cmd_error(STR_1000_LAND_SLOPED_IN_WRONG_DIRECTION);
			if (!EnsureNoVehicle(tile)) return CMD_ERROR;

			if ((m5 & 0xF0) == 0 && ( // normal road?
						(rail_bit == 1 && m5 == 0x05) ||
						(rail_bit == 2 && m5 == 0x0A) // correct direction?
					)) {
				if (flags & DC_EXEC) {
					_map3_lo[tile] = _map_owner[tile];
					_map_owner[tile] = _current_player;
					_map3_hi[tile] = p1;
					_map5[tile] = 0x10 | (rail_bit == 1 ? 0x08 : 0x00); // level crossing
				}
				break;
			}

			if ((m5 & 0xF0) == 0x10 && (m5 & 0x08 ? 1 : 2) == rail_bit)
				return_cmd_error(STR_1007_ALREADY_BUILT);
			/* FALLTHROUGH */

		default:
			ret = CheckRailSlope(tileh, rail_bit, 0, tile);
			if (CmdFailed(ret)) return ret;
			cost += ret;

			ret = DoCommandByTile(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (CmdFailed(ret)) return ret;
			cost += ret;

			if (flags & DC_EXEC) {
				SetTileType(tile, MP_RAILWAY);
				_map_owner[tile] = _current_player;
				_map2[tile] = 0; // Bare land
				_map3_lo[tile] = p1; // No signals, rail type
				_map5[tile] = rail_bit;
			}
			break;
	}

	if (flags & DC_EXEC) {
		MarkTileDirtyByTile(tile);
		SetSignalsOnBothDir(tile, p2);
	}

	return cost + _price.build_rail;
}

static const byte _signals_table[] = {
	0x40, 0x40, 0x40, 0x10, 0x80, 0x20, 0, 0, // direction 1
	0x80, 0x80, 0x80, 0x20, 0x40, 0x10, 0, 0  // direction 2
};

static const byte _signals_table_other[] = {
	0x80, 0x80, 0x80, 0x20, 0x40, 0x10, 0, 0, // direction 1
	0x40, 0x40, 0x40, 0x10, 0x80, 0x20, 0, 0  // direction 2
};

static const byte _signals_table_both[] = {
	0xC0, 0xC0, 0xC0, 0x30, 0xC0, 0x30, 0, 0,	// both directions combined
	0xC0, 0xC0, 0xC0, 0x30, 0xC0, 0x30, 0, 0
};


/** Remove a single piece of track
 * @param x,y coordinates for removal of track
 * @param p1 unused
 * @param p2 rail orientation
 */
int32 CmdRemoveSingleRail(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	TileInfo ti;
	TileIndex tile;
	byte rail_bit = 1 << p2;

	if (!ValParamTrackOrientation(p2)) return CMD_ERROR;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	FindLandscapeHeight(&ti, x, y);

	tile = ti.tile;

	if (!((1<<ti.type) & ((1<<MP_TUNNELBRIDGE)|(1<<MP_STREET)|(1<<MP_RAILWAY))))
		return CMD_ERROR;

	if (_current_player != OWNER_WATER && !CheckTileOwnership(tile))
		return CMD_ERROR;

	// allow building rail under bridge
	if (ti.type != MP_TUNNELBRIDGE && !EnsureNoVehicle(tile))
		return CMD_ERROR;

	if (ti.type == MP_TUNNELBRIDGE) {
		if (!EnsureNoVehicleZ(tile, TilePixelHeight(tile)))
			return CMD_ERROR;

		if ((ti.map5 & 0xF8) != 0xE0)
			return CMD_ERROR;

		if ( ((ti.map5 & 1) ? 1 : 2) != rail_bit )
			return CMD_ERROR;

		if (!(flags & DC_EXEC))
			return _price.remove_rail;

		_map_owner[tile] = OWNER_NONE;
		_map5[tile] = ti.map5 & 0xC7;
	} else if (ti.type == MP_STREET) {
		byte m5;
		if (!(ti.map5 & 0xF0))
			return CMD_ERROR;

		if (ti.map5 & 0xE0)
			return CMD_ERROR;

		if (ti.map5 & 8) {
			m5 = 5;
			if (rail_bit != 1)
				return CMD_ERROR;
		} else {
			m5 = 10;
			if (rail_bit != 2)
				return CMD_ERROR;
		}

		if (!(flags & DC_EXEC))
			return _price.remove_rail;

		_map5[tile] = m5;
		_map_owner[tile] = _map3_lo[tile];
		_map2[tile] = 0;
	} else {
		assert(ti.type == MP_RAILWAY);

		if (ti.map5 & RAIL_TYPE_SPECIAL)
			return CMD_ERROR;

		if (!(ti.map5 & rail_bit))
			return CMD_ERROR;

		// don't allow remove if there are signals on the track
		if (ti.map5 & RAIL_TYPE_SIGNALS) {
			if (_map3_lo[tile] & _signals_table_both[p2])
				return CMD_ERROR;
		}

		if (!(flags & DC_EXEC))
			return _price.remove_rail;

		if ( (_map5[tile] ^= rail_bit) == 0) {
			DoClearSquare(tile);
			goto skip_mark_dirty;
		}
	}

	/* mark_dirty */
	MarkTileDirtyByTile(tile);

skip_mark_dirty:;

	SetSignalsOnBothDir(tile, (byte)p2);

	return _price.remove_rail;
}

static const struct {
	int8 xinc[16];
	int8 yinc[16];
} _railbit = {{
//  0   1   2   3   4   5
	-16,  0,-16,  0, 16,  0,    0,  0,
	 16,  0,  0, 16,  0,-16,    0,  0,
},{
	  0, 16,  0, 16,  0, 16,    0,  0,
	  0,-16,-16,  0,-16,  0,    0,  0,
}};

static int32 ValidateAutoDrag(byte *railbit, int x, int y, int ex, int ey)
{
	int dx, dy, trdx, trdy;

	if (!ValParamTrackOrientation(*railbit)) return CMD_ERROR;

	// calculate delta x,y from start to end tile
	dx = ex - x;
	dy = ey - y;

	// calculate delta x,y for the first direction
	trdx = _railbit.xinc[*railbit];
	trdy = _railbit.yinc[*railbit];

	if (*railbit & 0x6) {
		trdx += _railbit.xinc[*railbit ^ 1];
		trdy += _railbit.yinc[*railbit ^ 1];
	}

	// validate the direction
	while (((trdx <= 0) && (dx > 0)) || ((trdx >= 0) && (dx < 0)) ||
	       ((trdy <= 0) && (dy > 0)) || ((trdy >= 0) && (dy < 0))) {
		if (!HASBIT(*railbit, 3)) { // first direction is invalid, try the other
			SETBIT(*railbit, 3); // reverse the direction
			trdx = -trdx;
			trdy = -trdy;
		} else // other direction is invalid too, invalid drag
			return CMD_ERROR;
	}

	// (for diagonal tracks, this is already made sure of by above test), but:
	// for non-diagonal tracks, check if the start and end tile are on 1 line
	if (*railbit & 0x6) {
		trdx = _railbit.xinc[*railbit];
		trdy = _railbit.yinc[*railbit];
		if ((abs(dx) != abs(dy)) && (abs(dx) + abs(trdy) != abs(dy) + abs(trdx)))
			return CMD_ERROR;
	}

	return 0;
}

/** Build a stretch of railroad tracks.
 * @param x,y start tile of drag
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-3) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev)
 * - p2 = (bit 4-6) - track-orientation, valid values: 0-5
 * - p2 = (bit 7)   - 0 = build, 1 = remove tracks
 */
static int32 CmdRailTrackHelper(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	int ex, ey;
	int32 ret, total_cost = 0;
	byte railbit = (p2 >> 4) & 7;
	byte mode = HASBIT(p2, 7);

	if (!ValParamRailtype(p2 & 0x3) || !ValParamTrackOrientation(railbit)) return CMD_ERROR;
	if (p1 > MapSize()) return CMD_ERROR;

	/* unpack end point */
	ex = TileX(p1) * 16;
	ey = TileY(p1) * 16;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	if (CmdFailed(ValidateAutoDrag(&railbit, x, y, ex, ey))) return CMD_ERROR;

	if (flags & DC_EXEC) SndPlayTileFx(SND_20_SPLAT_2, TILE_FROM_XY(x,y));

	for(;;) {
		ret = DoCommand(x, y, p2 & 0x3, railbit & 7, flags, (mode == 0) ? CMD_BUILD_SINGLE_RAIL : CMD_REMOVE_SINGLE_RAIL);

		if (CmdFailed(ret)) {
			if ((_error_message != STR_1007_ALREADY_BUILT) && (mode == 0))
				break;
		} else
			total_cost += ret;

		if (x == ex && y == ey)
			break;

		x += _railbit.xinc[railbit];
		y += _railbit.yinc[railbit];

		// toggle railbit for the non-diagonal tracks
		if (railbit & 0x6) railbit ^= 1;
	}

	return (total_cost == 0) ? CMD_ERROR : total_cost;
}

/** Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @see CmdRailTrackHelper
 */
int32 CmdBuildRailroadTrack(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	return CmdRailTrackHelper(x, y, flags, p1, CLRBIT(p2, 7));
}

/** Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @see CmdRailTrackHelper
 */
int32 CmdRemoveRailroadTrack(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	return CmdRailTrackHelper(x, y, flags, p1, SETBIT(p2, 7));
}

/** Build a train depot
 * @param x,y position of the train depot
 * @param p1 rail type
 * @param p2 depot direction (0 through 3), where 0 is NW, 1 is NE, etc.
 */
int32 CmdBuildTrainDepot(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	Depot *d;
	TileIndex tile = TILE_FROM_XY(x,y);
	int32 cost, ret;
	uint tileh;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	if (!EnsureNoVehicle(tile)) return CMD_ERROR;
	/* check railtype and valid direction for depot (0 through 3), 4 in total */
	if (!ValParamRailtype(p1) || p2 > 3) return CMD_ERROR;

	tileh = GetTileSlope(tile, NULL);
	if (tileh != 0) {
		if ((!_patches.ainew_active && _is_ai_player) || !_patches.build_on_slopes || (tileh & 0x10 || !((0x4C >> p2) & tileh) ))
			return_cmd_error(STR_0007_FLAT_LAND_REQUIRED);
	}

	ret = DoCommandByTile(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (CmdFailed(ret)) return CMD_ERROR;
	cost = ret;

	d = AllocateDepot();
	if (d == NULL)
		return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (_current_player == _local_player)
			_last_built_train_depot_tile = (TileIndex)tile;

		ModifyTile(tile,
			MP_SETTYPE(MP_RAILWAY) |
			MP_MAP3LO | MP_MAPOWNER_CURRENT | MP_MAP5,
			p1, /* map3_lo */
			p2 | RAIL_TYPE_DEPOT /* map5 */
		);

		d->xy = tile;
		d->town_index = ClosestTownFromTile(tile, (uint)-1)->index;

		SetSignalsOnBothDir(tile, (p2&1) ? 2 : 1);

	}

	return cost + _price.build_train_depot;
}

/** Build signals, alternate between double/single, signal/semaphore,
 * pre/exit/combo-signals, and what-else not
 * @param x,y coordinates where signals is being built
 * @param p1 various bitstuffed elements
 * - p1 = (bit 0-2) - track-orientation, valid values: 0-5
 * - p1 = (bit 3)   - choose semaphores/signals or cycle normal/pre/exit/combo depending on context
 * @param p2 used for CmdBuildManySignals() to copy style of first signal
 */
int32 CmdBuildSingleSignal(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	TileIndex tile = TILE_FROM_XY(x, y);
	bool semaphore;
	bool pre_signal;
	uint track = p1 & 0x7;
	byte m5;
	int32 cost;

	if (!ValParamTrackOrientation(track) || !IsTileType(tile, MP_RAILWAY) || !EnsureNoVehicle(tile))
		return CMD_ERROR;

	/* Protect against invalid signal copying */
	if (p2 != 0 && (p2 & _signals_table_both[track]) == 0) return CMD_ERROR;

	m5 = _map5[tile];

	/* You can't build signals in a depot, and the selected track must exist */
	if (m5 & 0x80 || !HASBIT(m5, track)) return CMD_ERROR;

	if (!CheckTileOwnership(tile)) return CMD_ERROR;

	_error_message = STR_1005_NO_SUITABLE_RAILROAD_TRACK;

	{
		byte m = m5 & RAIL_BIT_MASK;
		if (m != RAIL_BIT_DIAG1 &&
				m != RAIL_BIT_DIAG2 &&
				m != RAIL_BIT_UPPER &&
				m != RAIL_BIT_LOWER &&
				m != RAIL_BIT_LEFT &&
				m != RAIL_BIT_RIGHT &&
				m != (RAIL_BIT_UPPER | RAIL_BIT_LOWER) &&
				m != (RAIL_BIT_LEFT | RAIL_BIT_RIGHT))
			return CMD_ERROR;
	}

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	// Same bit, used in different contexts
	semaphore = pre_signal = HASBIT(p1, 3);

	if ((_map3_lo[tile] & _signals_table_both[track]) == 0) {
		// build new signals
		cost = _price.build_signals;
	} else {
		if (p2 != 0 &&
				((semaphore && !HASBIT(_map3_hi[tile], 2)) ||
				(!semaphore && HASBIT(_map3_hi[tile], 2)))) {
			// convert signals <-> semaphores
			cost = _price.build_signals + _price.remove_signals;
		} else {
			// it is free to change orientation/pre-exit-combo signals
			cost = 0;
		}
	}

	if (flags & DC_EXEC) {
		if (!(m5 & RAIL_TYPE_SIGNALS)) {
			// there are no signals at all on this tile yet
			_map5[tile] |= RAIL_TYPE_SIGNALS; // change into signals
			_map2[tile] |= 0xF0;              // all signals are on
			_map3_lo[tile] &= ~0xF0;          // no signals built by default
			_map3_hi[tile] = semaphore ? 4 : 0;
		}

		if (p2 == 0) {
			if ((_map3_lo[tile] & _signals_table_both[track]) == 0) {
				// build new signals
				_map3_lo[tile] |= _signals_table_both[track];
			} else {
				if (pre_signal) {
					// cycle between normal -> pre -> exit -> combo -> ...
					byte type = (_map3_hi[tile] + 1) & 0x03;
					_map3_hi[tile] &= ~0x03;
					_map3_hi[tile] |= type;
				} else {
					// cycle between two-way -> one-way -> one-way -> ...
					switch (track) {
						case 3:
						case 5: {
							byte signal = (_map3_lo[tile] - 0x10) & 0x30;
							if (signal == 0) signal = 0x30;
							_map3_lo[tile] &= ~0x30;
							_map3_lo[tile] |= signal;
							break;
						}

						default: {
							byte signal = (_map3_lo[tile] - 0x40) & 0xC0;
							if (signal == 0) signal = 0xC0;
							_map3_lo[tile] &= ~0xC0;
							_map3_lo[tile] |= signal;
							break;
						}
					}
				}
			}
		} else {
			/* If CmdBuildManySignals is called with copying signals, just copy the
			 * style of the first signal given as parameter by CmdBuildManySignals */
			_map3_lo[tile] &= ~_signals_table_both[track];
			_map3_lo[tile] |= p2 & _signals_table_both[track];
			// convert between signal<->semaphores when dragging
			if (semaphore)
				SETBIT(_map3_hi[tile], 2);
			else
				CLRBIT(_map3_hi[tile], 2);
		}

		MarkTileDirtyByTile(tile);
		SetSignalsOnBothDir(tile, track);
	}

	return cost;
}

/**	Build many signals by dragging; AutoSignals
 * @param x,y start tile of drag
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0)    - 0 = build, 1 = remove signals
 * - p2 = (bit  3)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  4- 6) - track-orientation, valid values: 0-5
 * - p2 = (bit 24-31) - user defined signals_density
 */
static int32 CmdSignalTrackHelper(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	int ex, ey;
	int32 ret, total_cost, signal_ctr;
	byte m5, signals;
	TileIndex tile = TILE_FROM_XY(x, y);
	bool error = true;

	int mode = p2 & 0x1;
	byte semaphores = (HASBIT(p2, 3)) ? 8 : 0;
	byte railbit = (p2 >> 4) & 0x7;
	byte signal_density = (p2 >> 24);

	if (p1 > MapSize()) return CMD_ERROR;
	if (signal_density == 0 || signal_density > 20) return CMD_ERROR;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	/* for vertical/horizontal tracks, double the given signals density
	* since the original amount will be too dense (shorter tracks) */
	if (railbit & 0x6) signal_density *= 2;

	// unpack end tile
	ex = TileX(p1) * 16;
	ey = TileY(p1) * 16;

	if (CmdFailed(ValidateAutoDrag(&railbit, x, y, ex, ey))) return CMD_ERROR;

	// copy the signal-style of the first rail-piece if existing
	m5 = _map5[tile];
	if (!(m5 & RAIL_TYPE_SPECIAL) && (m5 & RAIL_BIT_MASK) && (m5 & RAIL_TYPE_SIGNALS)) {
		signals = _map3_lo[tile] & _signals_table_both[railbit];
		if (signals == 0) signals = _signals_table_both[railbit];

		semaphores = (_map3_hi[tile] & ~3) ? 8 : 0; // copy signal/semaphores style (independent of CTRL)
	} else // no signals exist, drag a two-way signal stretch
		signals = _signals_table_both[railbit];

	/* signal_density_ctr	- amount of tiles already processed
	 * signals_density		- patch setting to put signal on every Nth tile (double space on |, -- tracks)
	 **********
	 * railbit		- direction of autorail
	 * semaphores	- semaphores or signals
	 * signals		- is there a signal/semaphore on the first tile, copy its style (two-way/single-way)
									and convert all others to semaphore/signal
	 * mode				- 1 remove signals, 0 build signals */
	signal_ctr = total_cost = 0;
	for (;;) {
		// only build/remove signals with the specified density
		if ((signal_ctr %	signal_density) == 0 ) {
			ret = DoCommand(x, y, (railbit & 7) | semaphores, signals, flags, (mode == 1) ? CMD_REMOVE_SIGNALS : CMD_BUILD_SIGNALS);

			/* Abort placement for any other error than NOT_SUITABLE_TRACK
			 * This includes vehicles on track, competitor's tracks, etc. */
			if (CmdFailed(ret)) {
				if (_error_message != STR_1005_NO_SUITABLE_RAILROAD_TRACK && mode != 1) return CMD_ERROR;
			} else {
				error = false;
				total_cost += ret;
			}
		}

		if (ex == x && ey == y) break; // reached end of drag

		x += _railbit.xinc[railbit];
		y += _railbit.yinc[railbit];
		signal_ctr++;

		// toggle railbit for the non-diagonal tracks (|, -- tracks)
		if (railbit & 0x6) railbit ^= 1;
	}

	return (error) ? CMD_ERROR : total_cost;
}

/** Build signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @see CmdSignalTrackHelper
 */
int32 CmdBuildSignalTrack(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	return CmdSignalTrackHelper(x, y, flags, p1, p2);
}

/** Remove signals
 * @param x,y coordinates where signal is being deleted from
 * @param p1 track combination to remove signal from
 */
int32 CmdRemoveSingleSignal(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	TileIndex tile = TILE_FROM_XY(x, y);
	uint track = p1 & 0x7;

	if (!ValParamTrackOrientation(track) || !IsTileType(tile, MP_RAILWAY) || !EnsureNoVehicle(tile))
		return CMD_ERROR;

	if ((_map5[tile] & RAIL_TYPE_MASK) != RAIL_TYPE_SIGNALS ||
			(_map3_lo[tile] & _signals_table_both[track]) == 0) // signals on track?
		return CMD_ERROR;

	/* Only water can remove signals from anyone */
	if (_current_player != OWNER_WATER && !CheckTileOwnership(tile)) return CMD_ERROR;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	/* Do it? */
	if (flags & DC_EXEC) {
		_map3_lo[tile] &= ~_signals_table_both[track];

		/* removed last signal from tile? */
		if ((_map3_lo[tile] & 0xF0) == 0) {
			_map5[tile] &= ~RAIL_TYPE_SIGNALS;
			_map2[tile] &= ~0xF0;
			CLRBIT(_map3_hi[tile], 2); // remove any possible semaphores
		}

		SetSignalsOnBothDir(tile, track);

		MarkTileDirtyByTile(tile);
	}

	return _price.remove_signals;
}

/** Remove signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @see CmdSignalTrackHelper
 */
int32 CmdRemoveSignalTrack(int x, int y, uint32 flags, uint32 p1, uint32 p2)
{
	return CmdSignalTrackHelper(x, y, flags, p1, SETBIT(p2, 0));
}

typedef int32 DoConvertRailProc(uint tile, uint totype, bool exec);

static int32 DoConvertRail(uint tile, uint totype, bool exec)
{
	if (!CheckTileOwnership(tile) || !EnsureNoVehicle(tile))
		return CMD_ERROR;

	// tile is already of requested type?
	if ( (uint)(_map3_lo[tile] & 0xF) == totype)
		return CMD_ERROR;

	// change type.
	if (exec) {
		_map3_lo[tile] = (_map3_lo[tile] & 0xF0) + totype;
		MarkTileDirtyByTile(tile);
	}

	return _price.build_rail >> 1;
}

extern int32 DoConvertStationRail(uint tile, uint totype, bool exec);
extern int32 DoConvertStreetRail(uint tile, uint totype, bool exec);
extern int32 DoConvertTunnelBridgeRail(uint tile, uint totype, bool exec);

/** Convert one rail type to the other. You can convert normal rail to
 * monorail/maglev easily or vice-versa.
 * @param ex,ey end tile of rail conversion drag
 * @param p1 start tile of drag
 * @param p2 new railtype to convert to
 */
int32 CmdConvertRail(int ex, int ey, uint32 flags, uint32 p1, uint32 p2)
{
	int32 ret, cost, money;
	int sx, sy, x, y;

	SET_EXPENSES_TYPE(EXPENSES_CONSTRUCTION);

	if (!ValParamRailtype(p2)) return CMD_ERROR;
	if (p1 > MapSize()) return CMD_ERROR;

	// make sure sx,sy are smaller than ex,ey
	sx = TileX(p1) * 16;
	sy = TileY(p1) * 16;
	if (ex < sx) intswap(ex, sx);
	if (ey < sy) intswap(ey, sy);

	money = GetAvailableMoneyForCommand();
	cost = 0;

	for (x = sx; x <= ex; x += 16) {
		for (y = sy; y <= ey; y += 16) {
			TileIndex tile = TILE_FROM_XY(x,y);
			DoConvertRailProc *proc;

			if (IsTileType(tile, MP_RAILWAY)) proc = DoConvertRail;
			else if (IsTileType(tile, MP_STATION)) proc = DoConvertStationRail;
			else if (IsTileType(tile, MP_STREET)) proc = DoConvertStreetRail;
			else if (IsTileType(tile, MP_TUNNELBRIDGE)) proc = DoConvertTunnelBridgeRail;
			else continue;

			ret = proc(tile, p2, false);
			if (CmdFailed(ret)) continue;
			cost += ret;

			if (flags & DC_EXEC) {
				if ( (money -= ret) < 0) { _additional_cash_required = ret; return cost - ret; }
				proc(tile, p2, true);
			}
		}
	}

	return (cost == 0) ? CMD_ERROR : cost;
}

static int32 RemoveTrainDepot(uint tile, uint32 flags)
{
	if (!CheckTileOwnership(tile) && _current_player != OWNER_WATER)
		return CMD_ERROR;

	if (!EnsureNoVehicle(tile))
		return CMD_ERROR;

	if (flags & DC_EXEC) {
		int track = _map5[tile] & RAIL_DEPOT_TRACK_MASK;

		DoDeleteDepot(tile);
		SetSignalsOnBothDir(tile, track);
	}

	return _price.remove_train_depot;
}

static int32 ClearTile_Track(TileIndex tile, byte flags)
{
	int32 cost;
	int32 ret;
	byte m5;

	m5 = _map5[tile];

	if (flags & DC_AUTO) {
		if (m5 & RAIL_TYPE_SPECIAL)
			return_cmd_error(STR_2004_BUILDING_MUST_BE_DEMOLISHED);

		if (_map_owner[tile] != _current_player)
			return_cmd_error(STR_1024_AREA_IS_OWNED_BY_ANOTHER);

		return_cmd_error(STR_1008_MUST_REMOVE_RAILROAD_TRACK);
	}

	cost = 0;

	switch (m5 & RAIL_TYPE_MASK) {
		case RAIL_TYPE_SIGNALS:
			if (_map3_lo[tile] & _signals_table_both[0]) {
				ret = DoCommandByTile(tile, 0, 0, flags, CMD_REMOVE_SIGNALS);
				if (ret == CMD_ERROR) return CMD_ERROR;
				cost += ret;
			}
			if (_map3_lo[tile] & _signals_table_both[3]) {
				ret = DoCommandByTile(tile, 3, 0, flags, CMD_REMOVE_SIGNALS);
				if (ret == CMD_ERROR) return CMD_ERROR;
				cost += ret;
			}

			m5 &= RAIL_BIT_MASK;
			if (!(flags & DC_EXEC)) {
				for (; m5 != 0; m5 >>= 1) if (m5 & 1) cost += _price.remove_rail;
				return cost;
			}
			/* FALLTHROUGH */

		case RAIL_TYPE_NORMAL: {
			uint i;

			for (i = 0; m5 != 0; i++, m5 >>= 1) {
				if (m5 & 1) {
					ret = DoCommandByTile(tile, 0, i, flags, CMD_REMOVE_SINGLE_RAIL);
					if (ret == CMD_ERROR) return CMD_ERROR;
					cost += ret;
				}
			}
			return cost;
		}

		case RAIL_TYPE_DEPOT:
			switch (m5 & RAIL_SUBTYPE_MASK) {
				case RAIL_SUBTYPE_DEPOT:
					return RemoveTrainDepot(tile, flags);

				case RAIL_SUBTYPE_WAYPOINT:
					return RemoveTrainWaypoint(tile, flags, false);

				default:
					return CMD_ERROR;
			}

		default:
			return CMD_ERROR;
	}
}



#include "table/track_land.h"

// used for presignals
static const SpriteID _signal_base_sprites[16] = {
	0x4FB,
	0x1323,
	0x1333,
	0x1343,

	// use semaphores instead of signals?
	0x1353,
	0x1363,
	0x1373,
	0x1383,

	// mirrored versions
	0x4FB,
	0x1323,
	0x1333,
	0x1343,

	0x13C6,
	0x13D6,
	0x13E6,
	0x13F6,
};

// used to determine the side of the road for the signal
static const byte _signal_position[24] = {
	/* original: left side position */
	0x58,0x1E,0xE1,0xB9,0x01,0xA3,0x4B,0xEE,0x3B,0xD4,0x43,0xBD,
	/* patch: ride side position */
	0x1E,0xAC,0x64,0xE1,0x4A,0x10,0xEE,0xC5,0xDB,0x34,0x4D,0xB3
};

static void DrawSignalHelper(TileInfo *ti, byte condition, uint32 image_and_pos)
{
	bool otherside = _opt.road_side & _patches.signal_side;

	uint v = _signal_position[(image_and_pos & 0xF) + (otherside ? 12 : 0)];
	uint x = ti->x | (v&0xF);
	uint y = ti->y | (v>>4);
	uint sprite = _signal_base_sprites[(_map3_hi[ti->tile] & 7) + (otherside ? 8 : 0)] + (image_and_pos>>4) + ((condition != 0) ? 1 : 0);
	AddSortableSpriteToDraw(sprite, x, y, 1, 1, 10, GetSlopeZ(x,y));
}

static uint32 _drawtile_track_palette;


static void DrawTrackFence_NW(TileInfo *ti)
{
	uint32 image = 0x515;
	if (ti->tileh != 0) {
		image = 0x519;
		if (!(ti->tileh & 2)) {
			image = 0x51B;
		}
	}
	AddSortableSpriteToDraw(image | _drawtile_track_palette,
		ti->x, ti->y+1, 16, 1, 4, ti->z);
}

static void DrawTrackFence_SE(TileInfo *ti)
{
	uint32 image = 0x515;
	if (ti->tileh != 0) {
		image = 0x519;
		if (!(ti->tileh & 2)) {
			image = 0x51B;
		}
	}
	AddSortableSpriteToDraw(image | _drawtile_track_palette,
		ti->x, ti->y+15, 16, 1, 4, ti->z);
}

static void DrawTrackFence_NW_SE(TileInfo *ti)
{
	DrawTrackFence_NW(ti);
	DrawTrackFence_SE(ti);
}

static void DrawTrackFence_NE(TileInfo *ti)
{
	uint32 image = 0x516;
	if (ti->tileh != 0) {
		image = 0x51A;
		if (!(ti->tileh & 2)) {
			image = 0x51C;
		}
	}
	AddSortableSpriteToDraw(image | _drawtile_track_palette,
		ti->x+1, ti->y, 1, 16, 4, ti->z);
}

static void DrawTrackFence_SW(TileInfo *ti)
{
	uint32 image = 0x516;
	if (ti->tileh != 0) {
		image = 0x51A;
		if (!(ti->tileh & 2)) {
			image = 0x51C;
		}
	}
	AddSortableSpriteToDraw(image | _drawtile_track_palette,
		ti->x+15, ti->y, 1, 16, 4, ti->z);
}

static void DrawTrackFence_NE_SW(TileInfo *ti)
{
	DrawTrackFence_NE(ti);
	DrawTrackFence_SW(ti);
}

static void DrawTrackFence_NS_1(TileInfo *ti)
{
	int z = ti->z;
	if (ti->tileh & 1)
		z += 8;
	AddSortableSpriteToDraw(0x517 | _drawtile_track_palette,
		ti->x + 8, ti->y + 8, 1, 1, 4, z);
}

static void DrawTrackFence_NS_2(TileInfo *ti)
{
	int z = ti->z;
	if (ti->tileh & 4)
		z += 8;
	AddSortableSpriteToDraw(0x517 | _drawtile_track_palette,
		ti->x + 8, ti->y + 8, 1, 1, 4, z);
}

static void DrawTrackFence_WE_1(TileInfo *ti)
{
	int z = ti->z;
	if (ti->tileh & 8)
		z += 8;
	AddSortableSpriteToDraw(0x518 | _drawtile_track_palette,
		ti->x + 8, ti->y + 8, 1, 1, 4, z);
}

static void DrawTrackFence_WE_2(TileInfo *ti)
{
	int z = ti->z;
	if (ti->tileh & 2)
		z += 8;
	AddSortableSpriteToDraw(0x518 | _drawtile_track_palette,
		ti->x + 8, ti->y + 8, 1, 1, 4, z);
}

static void DetTrackDrawProc_Null(TileInfo *ti)
{
	/* nothing should be here */
}

typedef void DetailedTrackProc(TileInfo *ti);
DetailedTrackProc * const _detailed_track_proc[16] = {
	DetTrackDrawProc_Null,
	DetTrackDrawProc_Null,

	DrawTrackFence_NW,
	DrawTrackFence_SE,
	DrawTrackFence_NW_SE,

	DrawTrackFence_NE,
	DrawTrackFence_SW,
	DrawTrackFence_NE_SW,

	DrawTrackFence_NS_1,
	DrawTrackFence_NS_2,

	DrawTrackFence_WE_1,
	DrawTrackFence_WE_2,

	DetTrackDrawProc_Null,
	DetTrackDrawProc_Null,
	DetTrackDrawProc_Null,
	DetTrackDrawProc_Null,
};

static void DrawSpecialBuilding(uint32 image, uint32 tracktype_offs,
                                TileInfo *ti,
                                byte x, byte y, byte z,
                                byte xsize, byte ysize, byte zsize)
{
	if (image & 0x8000)
		image |= _drawtile_track_palette;
	image += tracktype_offs;
	if (_display_opt & DO_TRANS_BUILDINGS) // show transparent depots
		image = (image & 0x3FFF) | 0x3224000;
	AddSortableSpriteToDraw(image, ti->x + x, ti->y + y, xsize, ysize, zsize, ti->z + z);
}

static void DrawTile_Track(TileInfo *ti)
{
	uint32 tracktype_offs, image;
	byte m5;

	_drawtile_track_palette = SPRITE_PALETTE(PLAYER_SPRITE_COLOR(_map_owner[ti->tile]));

	tracktype_offs = (_map3_lo[ti->tile] & 0xF) * TRACKTYPE_SPRITE_PITCH;

	m5 = (byte)ti->map5;
	if (!(m5 & RAIL_TYPE_SPECIAL)) {
		bool special;

		m5 &= RAIL_BIT_MASK;

		special = false;

		// select the sprite to use based on the map5 byte.
		(image = 0x3F3, m5 == RAIL_BIT_DIAG2) ||
		(image++,				m5 == RAIL_BIT_DIAG1) ||
		(image++,				m5 == RAIL_BIT_UPPER) ||
		(image++,				m5 == RAIL_BIT_LOWER) ||
		(image++,				m5 == RAIL_BIT_RIGHT) ||
		(image++,				m5 == RAIL_BIT_LEFT) ||
		(image++,				m5 == (RAIL_BIT_DIAG1|RAIL_BIT_DIAG2)) ||

		(image = 0x40B, m5 == (RAIL_BIT_UPPER|RAIL_BIT_LOWER)) ||
		(image++,				m5 == (RAIL_BIT_LEFT|RAIL_BIT_RIGHT)) ||

		(special=true, false) ||

		(image = 0x3FA, !(m5 & (RAIL_BIT_RIGHT|RAIL_BIT_UPPER|RAIL_BIT_DIAG1))) ||
		(image++,				!(m5 & (RAIL_BIT_LEFT|RAIL_BIT_LOWER|RAIL_BIT_DIAG1))) ||
		(image++,				!(m5 & (RAIL_BIT_LEFT|RAIL_BIT_UPPER|RAIL_BIT_DIAG2))) ||
		(image++,				!(m5 & (RAIL_BIT_RIGHT|RAIL_BIT_LOWER|RAIL_BIT_DIAG2))) ||
		(image++, true);

		if (ti->tileh != 0) {
			int f = GetRailFoundation(ti->tileh, ti->map5 & 0x3F);
			if (f) DrawFoundation(ti, f);

			// default sloped sprites..
			if (ti->tileh != 0) image = _track_sloped_sprites[ti->tileh - 1] + 0x3F3;
		}

		if ((_map2[ti->tile] & RAIL_MAP2LO_GROUND_MASK)==RAIL_GROUND_BROWN)
			image = (image & 0xFFFF) | 0x3178000; // use a brown palette
		else if ((_map2[ti->tile] & RAIL_MAP2LO_GROUND_MASK)==RAIL_GROUND_ICE_DESERT)
			image += 26;

		DrawGroundSprite(image + tracktype_offs);

		if (special) {
			if (m5 & RAIL_BIT_DIAG1) DrawGroundSprite(0x3ED + tracktype_offs);
			if (m5 & RAIL_BIT_DIAG2) DrawGroundSprite(0x3EE + tracktype_offs);
			if (m5 & RAIL_BIT_UPPER) DrawGroundSprite(0x3EF + tracktype_offs);
			if (m5 & RAIL_BIT_LOWER) DrawGroundSprite(0x3F0 + tracktype_offs);
			if (m5 & RAIL_BIT_LEFT)  DrawGroundSprite(0x3F2 + tracktype_offs);
			if (m5 & RAIL_BIT_RIGHT) DrawGroundSprite(0x3F1 + tracktype_offs);
		}

		if (_display_opt & DO_FULL_DETAIL) {
			_detailed_track_proc[_map2[ti->tile] & RAIL_MAP2LO_GROUND_MASK](ti);
		}

		/* draw signals also? */
		if (!(ti->map5 & RAIL_TYPE_SIGNALS))
			return;

		{
			byte m23;

			m23 = (_map3_lo[ti->tile] >> 4) | (_map2[ti->tile] & 0xF0);

#define HAS_SIGNAL(x) (m23 & (byte)(0x1 << (x)))
#define ISON_SIGNAL(x) (m23 & (byte)(0x10 << (x)))
#define MAYBE_DRAW_SIGNAL(x,y,z) if (HAS_SIGNAL(x)) DrawSignalHelper(ti, ISON_SIGNAL(x), ((y-0x4FB) << 4)|(z))

		if (!(m5 & RAIL_BIT_DIAG2)) {
			if (!(m5 & RAIL_BIT_DIAG1)) {
				if (m5 & RAIL_BIT_LEFT) {
					MAYBE_DRAW_SIGNAL(2, 0x509, 0);
					MAYBE_DRAW_SIGNAL(3, 0x507, 1);
				}
				if (m5 & RAIL_BIT_RIGHT) {
					MAYBE_DRAW_SIGNAL(0, 0x509, 2);
					MAYBE_DRAW_SIGNAL(1, 0x507, 3);
				}
				if (m5 & RAIL_BIT_UPPER) {
					MAYBE_DRAW_SIGNAL(3, 0x505, 4);
					MAYBE_DRAW_SIGNAL(2, 0x503, 5);
				}
				if (m5 & RAIL_BIT_LOWER) {
					MAYBE_DRAW_SIGNAL(1, 0x505, 6);
					MAYBE_DRAW_SIGNAL(0, 0x503, 7);
				}
			} else {
				MAYBE_DRAW_SIGNAL(3, 0x4FB, 8);
				MAYBE_DRAW_SIGNAL(2, 0x4FD, 9);
			}
		} else {
			MAYBE_DRAW_SIGNAL(3, 0x4FF, 10);
			MAYBE_DRAW_SIGNAL(2, 0x501, 11);
		}
		}
	} else {
		/* draw depots / waypoints */
		const DrawTrackSeqStruct *drss;
		byte type = m5 & 0x3F; // 0-3: depots, 4-5: waypoints

		if (!(m5 & (RAIL_TYPE_MASK&~RAIL_TYPE_SPECIAL)))
			return;

		if (ti->tileh != 0) { DrawFoundation(ti, ti->tileh); }

		if (IsRailWaypoint(m5) && _map3_lo[ti->tile] & 16) {
			// look for customization
			StationSpec *stat = GetCustomStation(STAT_CLASS_WAYP, _map3_hi[ti->tile]);

			if (stat) {
				DrawTileSeqStruct const *seq;
				// emulate station tile - open with building
				DrawTileSprites *cust = &stat->renderdata[2 + (m5 & 0x1)];
				uint32 relocation = GetCustomStationRelocation(stat, ComposeWaypointStation(ti->tile), 0);
				int railtype=(_map3_lo[ti->tile] & 0xF);

				/* We don't touch the 0x8000 bit. In all this
				 * waypoint code, it is used to indicate that
				 * we should offset by railtype, but we always
				 * do that for custom ground sprites and never
				 * for station sprites. And in the drawing
				 * code, it is used to indicate that the sprite
				 * should be drawn in company colors, and it's
				 * up to the GRF file to decide that. */

				image = cust->ground_sprite;
				image += railtype*((image<_custom_sprites_base)?TRACKTYPE_SPRITE_PITCH:1);

				DrawGroundSprite(image);

				foreach_draw_tile_seq(seq, cust->seq) {
					uint32 image = seq->image + relocation;
					DrawSpecialBuilding(image, 0, ti,
					                    seq->delta_x, seq->delta_y, seq->delta_z,
					                    seq->width, seq->height, seq->unk);
				}
				return;
			}
		}

		drss = _track_depot_layout_table[type];

		image = drss++->image;
		if (image & 0x8000) image = (image & 0x7FFF) + tracktype_offs;

		// adjust ground tile for desert
		// (don't adjust for arctic depots, because snow in depots looks weird)
		if ((_map2[ti->tile] & RAIL_MAP2LO_GROUND_MASK)==RAIL_GROUND_ICE_DESERT && (_opt.landscape == LT_DESERT || type>=4))
		{
			if(image!=3981)
				image += 26; // tile with tracks
			else
				image = 4550; // flat ground
		}

		DrawGroundSprite(image);

		while ((image=drss->image) != 0) {
			DrawSpecialBuilding(image, type < 4 ? tracktype_offs : 0, ti,
			                    drss->subcoord_x, drss->subcoord_y, 0,
			                    drss->width, drss->height, 0x17);
			drss++;
		}
	}
}

void DrawTrainDepotSprite(int x, int y, int image, int railtype)
{
	uint32 ormod, img;
	const DrawTrackSeqStruct *dtss;

	/* baseimage */
	railtype *= TRACKTYPE_SPRITE_PITCH;

	ormod = PLAYER_SPRITE_COLOR(_local_player);

	dtss = _track_depot_layout_table[image];

	x+=33;
	y+=17;

	img = dtss++->image;
	if (img & 0x8000) img = (img & 0x7FFF) + railtype;
	DrawSprite(img, x, y);

	for (; dtss->image != 0; dtss++) {
		Point pt = RemapCoords(dtss->subcoord_x, dtss->subcoord_y, 0);
		image = dtss->image;
		if (image & 0x8000) image |= ormod;
		DrawSprite(image + railtype, x + pt.x, y + pt.y);
	}
}

#define NUM_SSD_ENTRY 256
#define NUM_SSD_STACK 32

typedef struct SetSignalsData {
	int cur;
	int cur_stack;
	bool stop;
	bool has_presignal;

	// presignal info
	int presignal_exits;
	int presignal_exits_free;

	// these are used to keep track of the signals that change.
	byte bit[NUM_SSD_ENTRY];
	TileIndex tile[NUM_SSD_ENTRY];

	// these are used to keep track of the stack that modifies presignals recursively
	TileIndex next_tile[NUM_SSD_STACK];
	byte next_dir[NUM_SSD_STACK];

} SetSignalsData;

static bool SetSignalsEnumProc(uint tile, SetSignalsData *ssd, int track, uint length, byte *state)
{
	// the tile has signals?
	if (IsTileType(tile, MP_RAILWAY)) {
		if ((_map5[tile]&RAIL_TYPE_MASK) == RAIL_TYPE_SIGNALS) {
			if ((_map3_lo[tile] & _signals_table_both[track]) != 0) {

				// is the signal pointing in to the segment existing?
				if ((_map3_lo[tile] & _signals_table[track]) != 0) {
					// yes, add the signal to the list of signals
					if (ssd->cur != NUM_SSD_ENTRY) {
						ssd->tile[ssd->cur] = tile; // remember the tile index
						ssd->bit[ssd->cur] = track; // and the controlling bit number
						ssd->cur++;
					}

					// remember if this block has a presignal.
					ssd->has_presignal |= (_map3_hi[tile]&1);
				}

				// is this an exit signal that points out from the segment?
				if ((_map3_hi[tile]&2) && _map3_lo[tile]&_signals_table_other[track]) {
					ssd->presignal_exits++;
					if ((_map2[tile]&_signals_table_other[track]) != 0)
						ssd->presignal_exits_free++;
				}

				return true;
			}
		} else if (IsRailDepot(_map5[tile]))
			return true; // don't look further if the tile is a depot
	}
	return false;
}

/* Struct to parse data from VehicleFromPos to SignalVehicleCheckProc */
typedef struct SignalVehicleCheckStruct {
	TileIndex tile;
	uint track;
} SignalVehicleCheckStruct;

static void *SignalVehicleCheckProc(Vehicle *v, void *data)
{
	SignalVehicleCheckStruct *dest = data;
	TileIndex tile;

	if (v->type != VEH_Train)
		return NULL;

	/* Find the tile outside the tunnel, for signalling */
	if (v->u.rail.track == 0x40)
		tile = GetVehicleOutOfTunnelTile(v);
	else
		tile = v->tile;

	/* Wrong tile, or no train? Not a match */
	if (tile != dest->tile)
		return NULL;

	/* Are we on the same piece of track? */
	if (dest->track & (v->u.rail.track + (v->u.rail.track<<8)))
		return v;

	return NULL;
}

/* Special check for SetSignalsAfterProc, to see if there is a vehicle on this tile */
bool SignalVehicleCheck(TileIndex tile, uint track)
{
	SignalVehicleCheckStruct dest;

	dest.tile = tile;
	dest.track = track;

	return VehicleFromPos(tile, &dest, SignalVehicleCheckProc) != NULL;
}

static void SetSignalsAfterProc(TrackPathFinder *tpf)
{
	SetSignalsData *ssd = tpf->userdata;
	TrackPathFinderLink *link;
	uint offs;
	uint i;

	ssd->stop = false;

	/* Go through all the PF tiles */
	for (i = 0; i < lengthof(tpf->hash_head); i++) {
		/* Empty hash item */
		if (tpf->hash_head[i] == 0)
			continue;

		/* If 0x8000 is not set, there is only 1 item */
		if (!(tpf->hash_head[i] & 0x8000)) {
			/* Check if there is a vehicle on this tile */
			if (SignalVehicleCheck(tpf->hash_tile[i], tpf->hash_head[i])) {
				ssd->stop = true;
				return;
			}
		} else {
			/* There are multiple items, where hash_tile points to the first item in the list */
			offs = tpf->hash_tile[i];
			do {
				/* Find the next item */
				link = PATHFIND_GET_LINK_PTR(tpf, offs);
				/* Check if there is a vehicle on this tile */
				if (SignalVehicleCheck(link->tile, link->flags)) {
					ssd->stop = true;
					return;
				}
				/* Goto the next item */
			} while ((offs=link->next) != 0xFFFF);
		}
	}
}

static const byte _dir_from_track[14] = {
	0,1,0,1,2,1, 0,0,
	2,3,3,2,3,0,
};


static void ChangeSignalStates(SetSignalsData *ssd)
{
	int i;

	// thinking about presignals...
	// the presignal is green if,
	//   if no train is in the segment AND
	//   there is at least one green exit signal OR
	//   there are no exit signals in the segment

	// then mark the signals in the segment accordingly
	for(i=0; i!=ssd->cur; i++) {
		uint tile = ssd->tile[i];
		byte bit = _signals_table[ssd->bit[i]];
		uint16 m2 = _map2[tile];

		// presignals don't turn green if there is at least one presignal exit and none are free
		if (_map3_hi[tile] & 1) {
			int ex = ssd->presignal_exits, exfree = ssd->presignal_exits_free;

			// subtract for dual combo signals so they don't count themselves
			if (_map3_hi[tile]&2 && _map3_lo[tile]&_signals_table_other[ssd->bit[i]]) {
				ex--;
				if ((_map2[tile]&_signals_table_other[ssd->bit[i]]) != 0) exfree--;
			}

			// if we have exits and none are free, make red.
			if (ex && !exfree) goto make_red;
		}

		// check if the signal is unaffected.
		if (ssd->stop) {
make_red:
			// turn red
			if ( (bit&m2) == 0 )
				continue;
		} else {
			// turn green
			if ( (bit&m2) != 0 )
				continue;
		}

		// Update signals on the other side of this exit signal, it changed.
		// If this segment has presignals, then we treat exit signals going into the segment as normal signals.
		if (_map3_hi[tile]&2 && (_map3_hi[tile]&1 || !ssd->has_presignal)) {
			if (ssd->cur_stack != NUM_SSD_STACK) {
				ssd->next_tile[ssd->cur_stack] = tile;
				ssd->next_dir[ssd->cur_stack] = _dir_from_track[ssd->bit[i]];
				ssd->cur_stack++;
			} else {
				printf("NUM_SSD_STACK too small\n");
			}
		}

		// it changed, so toggle it
		_map2[tile] = m2 ^ bit;
		MarkTileDirtyByTile(tile);
	}
}


bool UpdateSignalsOnSegment(uint tile, byte direction)
{
	SetSignalsData ssd;
	int result = -1;

	ssd.cur_stack = 0;
	direction>>=1;

	for(;;) {
		// go through one segment and update all signals pointing into that segment.
		ssd.cur = ssd.presignal_exits = ssd.presignal_exits_free = 0;
		ssd.has_presignal = false;

		FollowTrack(tile, 0xC000 | TRANSPORT_RAIL, direction, (TPFEnumProc*)SetSignalsEnumProc, SetSignalsAfterProc, &ssd);
		ChangeSignalStates(&ssd);

		// remember the result only for the first iteration.
		if (result < 0) result = ssd.stop;

		// if any exit signals were changed, we need to keep going to modify the stuff behind those.
		if(!ssd.cur_stack)
			break;

		// one or more exit signals were changed, so we need to update another segment too.
		tile = ssd.next_tile[--ssd.cur_stack];
		direction = ssd.next_dir[ssd.cur_stack];
	}

	return (bool)result;
}

void SetSignalsOnBothDir(uint tile, byte track)
{
	static const byte _search_dir_1[6] = {1, 3, 1, 3, 5, 3};
	static const byte _search_dir_2[6] = {5, 7, 7, 5, 7, 1};

	UpdateSignalsOnSegment(tile, _search_dir_1[track]);
	UpdateSignalsOnSegment(tile, _search_dir_2[track]);
}

static uint GetSlopeZ_Track(TileInfo *ti)
{
	uint z = ti->z;
	int th = ti->tileh;

	// check if it's a foundation
	if (ti->tileh != 0) {
		if ((ti->map5 & 0x80) == 0) {
			uint f = GetRailFoundation(ti->tileh, ti->map5 & 0x3F);
			if (f != 0) {
				if (f < 15) {
					// leveled foundation
					return z + 8;
				}
				// inclined foundation
				th = _inclined_tileh[f - 15];
			}
		} else if ((ti->map5 & 0xC0) == 0xC0) {
			// depot or waypoint
			return z + 8;
		}
		return GetPartialZ(ti->x&0xF, ti->y&0xF, th) + z;
	}
	return z;
}

static uint GetSlopeTileh_Track(TileInfo *ti)
{
	// check if it's a foundation
	if (ti->tileh != 0) {
		if ((ti->map5 & 0x80) == 0) {
			uint f = GetRailFoundation(ti->tileh, ti->map5 & 0x3F);
			if (f != 0) {
				if (f < 15) {
					// leveled foundation
					return 0;
				}
				// inclined foundation
				return _inclined_tileh[f - 15];
			}
		} else if ((ti->map5 & 0xC0) == 0xC0) {
			// depot or waypoint
			return 0;
		}
	}
	return ti->tileh;
}

static void GetAcceptedCargo_Track(uint tile, AcceptedCargo ac)
{
	/* not used */
}

static void AnimateTile_Track(uint tile)
{
	/* not used */
}

static void TileLoop_Track(uint tile)
{
	byte a2;
	byte rail;
	uint16 m2;
	byte owner;

	m2 = _map2[tile] & 0xF;

	/* special code for alps landscape */
	if (_opt.landscape == LT_HILLY) {
		/* convert into snow? */
		if (GetTileZ(tile) > _opt.snow_line) {
			a2 = RAIL_GROUND_ICE_DESERT;
			goto modify_me;
		}

	/* special code for desert landscape */
	} else if (_opt.landscape == LT_DESERT) {
		/* convert into desert? */
		if (GetMapExtraBits(tile) == 1) {
			a2 = RAIL_GROUND_ICE_DESERT;
			goto modify_me;
		}
	}

	// Don't continue tile loop for depots
	if (_map5[tile] & RAIL_TYPE_SPECIAL)
		return;

	a2 = RAIL_GROUND_GREEN;

	if (m2 != RAIL_GROUND_BROWN) { /* wait until bottom is green */
		/* determine direction of fence */
		rail = _map5[tile] & RAIL_BIT_MASK;

		if (rail == RAIL_BIT_UPPER) {
			a2 = RAIL_GROUND_FENCE_HORIZ1;
		} else if (rail == RAIL_BIT_LOWER) {
			a2 = RAIL_GROUND_FENCE_HORIZ2;
		} else if (rail == RAIL_BIT_LEFT) {
			a2 = RAIL_GROUND_FENCE_VERT1;
		} else if (rail == RAIL_BIT_RIGHT) {
			a2 = RAIL_GROUND_FENCE_VERT2;
		} else {
			owner = _map_owner[tile];

			if ( (!(rail&(RAIL_BIT_DIAG2|RAIL_BIT_UPPER|RAIL_BIT_LEFT)) && (rail&RAIL_BIT_DIAG1)) || rail==(RAIL_BIT_LOWER|RAIL_BIT_RIGHT)) {
				if (!IsTileType(tile + TILE_XY(0,-1), MP_RAILWAY) ||
						owner != _map_owner[tile + TILE_XY(0,-1)] ||
						(_map5[tile + TILE_XY(0,-1)]==RAIL_BIT_UPPER || _map5[tile + TILE_XY(0,-1)]==RAIL_BIT_LEFT))
							a2 = RAIL_GROUND_FENCE_NW;
			}

			if ( (!(rail&(RAIL_BIT_DIAG2|RAIL_BIT_LOWER|RAIL_BIT_RIGHT)) && (rail&RAIL_BIT_DIAG1)) || rail==(RAIL_BIT_UPPER|RAIL_BIT_LEFT)) {
				if (!IsTileType(tile + TILE_XY(0,1), MP_RAILWAY) ||
						owner != _map_owner[tile + TILE_XY(0,1)] ||
						(_map5[tile + TILE_XY(0,1)]==RAIL_BIT_LOWER || _map5[tile + TILE_XY(0,1)]==RAIL_BIT_RIGHT))
							a2 = (a2 == RAIL_GROUND_FENCE_NW) ? RAIL_GROUND_FENCE_SENW : RAIL_GROUND_FENCE_SE;
			}

			if ( (!(rail&(RAIL_BIT_DIAG1|RAIL_BIT_UPPER|RAIL_BIT_RIGHT)) && (rail&RAIL_BIT_DIAG2)) || rail==(RAIL_BIT_LOWER|RAIL_BIT_LEFT)) {
				if (!IsTileType(tile + TILE_XY(-1,0), MP_RAILWAY) ||
						owner != _map_owner[tile + TILE_XY(-1,0)] ||
						(_map5[tile + TILE_XY(-1,0)]==RAIL_BIT_UPPER || _map5[tile + TILE_XY(-1,0)]==RAIL_BIT_RIGHT))
							a2 = RAIL_GROUND_FENCE_NE;
			}

			if ( (!(rail&(RAIL_BIT_DIAG1|RAIL_BIT_LOWER|RAIL_BIT_LEFT)) && (rail&RAIL_BIT_DIAG2)) || rail==(RAIL_BIT_UPPER|RAIL_BIT_RIGHT)) {
				if (!IsTileType(tile + TILE_XY(1,0), MP_RAILWAY) ||
						owner != _map_owner[tile + TILE_XY(1,0)] ||
						(_map5[tile + TILE_XY(1,0)]==RAIL_BIT_LOWER || _map5[tile + TILE_XY(1,0)]==RAIL_BIT_LEFT))
							a2 = (a2 == RAIL_GROUND_FENCE_NE) ? RAIL_GROUND_FENCE_NESW : RAIL_GROUND_FENCE_SW;
			}
		}
	}

modify_me:;
	/* tile changed? */
	if ( m2 != a2) {
		_map2[tile] = (_map2[tile] & ~RAIL_MAP2LO_GROUND_MASK) | a2;
		MarkTileDirtyByTile(tile);
	}
}


static uint32 GetTileTrackStatus_Track(uint tile, TransportType mode) {
	byte m5, a;
	uint16 b;
	uint32 ret;

	if (mode != TRANSPORT_RAIL)
		return 0;

	m5 = _map5[tile];

	if (!(m5 & RAIL_TYPE_SPECIAL)) {
		ret = (m5 | (m5 << 8)) & 0x3F3F;
		if (!(m5 & RAIL_TYPE_SIGNALS)) {
			if ( (ret & 0xFF) == 3)
			/* Diagonal crossing? */
				ret |= 0x40;
		} else {
			/* has_signals */

			a = _map3_lo[tile];
			b = _map2[tile];

			b &= a;

			/* When signals are not present (in neither
			 * direction), we pretend them to be green. (So if
			 * signals are only one way, the other way will
			 * implicitely become `red' */
			if ((a & 0xC0) == 0) { b |= 0xC0; }
			if ((a & 0x30) == 0) { b |= 0x30; }

			if ( (b & 0x80) == 0)	ret |= 0x10070000;
			if ( (b & 0x40) == 0)	ret |= 0x7100000;
			if ( (b & 0x20) == 0)	ret |= 0x20080000;
			if ( (b & 0x10) == 0)	ret |= 0x8200000;
		}
	} else if (m5 & 0x40) {
		static const byte _train_spec_tracks[6] = {1,2,1,2,1,2};
		m5 = _train_spec_tracks[m5 & 0x3F];
		ret = (m5 << 8) + m5;
	} else
		return 0;
	return ret;
}

static void ClickTile_Track(uint tile)
{
	if (IsRailDepot(_map5[tile]))
		ShowTrainDepotWindow(tile);
	else if (IsRailWaypoint(_map5[tile]))
		ShowRenameWaypointWindow(GetWaypointByTile(tile));

}

static void GetTileDesc_Track(TileIndex tile, TileDesc *td)
{
	td->owner = _map_owner[tile];
	switch (_map5[tile] & RAIL_TYPE_MASK) {
		case RAIL_TYPE_NORMAL:
			td->str = STR_1021_RAILROAD_TRACK;
			break;

		case RAIL_TYPE_SIGNALS: {
			const StringID signal_type[] = {
				STR_RAILROAD_TRACK_WITH_NORMAL_SIGNALS,
				STR_RAILROAD_TRACK_WITH_PRESIGNALS,
				STR_RAILROAD_TRACK_WITH_EXITSIGNALS,
				STR_RAILROAD_TRACK_WITH_COMBOSIGNALS
			};

			td->str = signal_type[_map3_hi[tile] & 0x03];
			break;
		}

		case RAIL_TYPE_DEPOT:
		default:
			td->str = ((_map5[tile] & RAIL_SUBTYPE_MASK) == RAIL_SUBTYPE_DEPOT) ?
				STR_1023_RAILROAD_TRAIN_DEPOT : STR_LANDINFO_WAYPOINT;
			break;
	}
}

static void ChangeTileOwner_Track(uint tile, byte old_player, byte new_player)
{
	if (_map_owner[tile] != old_player)
		return;

	if (new_player != 255) {
		_map_owner[tile] = new_player;
	}	else {
		DoCommandByTile(tile, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR);
	}
}

static const byte _fractcoords_behind[4] = { 0x8F, 0x8, 0x80, 0xF8 };
static const byte _fractcoords_enter[4] = { 0x8A, 0x48, 0x84, 0xA8 };
static const byte _fractcoords_leave[4] = { 0x81, 0xD8, 0x8D, 0x18 };
static const byte _enter_directions[4] = {5, 7, 1, 3};
static const byte _leave_directions[4] = {1, 3, 5, 7};
static const byte _depot_track_mask[4] = {1, 2, 1, 2};

static uint32 VehicleEnter_Track(Vehicle *v, uint tile, int x, int y)
{
	byte fract_coord;
	int dir;

	// this routine applies only to trains in depot tiles
	if (v->type != VEH_Train || !IsRailDepot(_map5[tile]))
		return 0;

	/* depot direction */
	dir = _map5[tile] & RAIL_DEPOT_DIR;

	fract_coord = (x & 0xF) + ((y & 0xF) << 4);
	if (_fractcoords_behind[dir] == fract_coord) {
		/* make sure a train is not entering the tile from behind */
		return 8;
	} else if (_fractcoords_enter[dir] == fract_coord) {
		if (_enter_directions[dir] == v->direction) {
			/* enter the depot */
			v->u.rail.track = 0x80,
			v->vehstatus |= VS_HIDDEN; /* hide it */
			v->direction ^= 4;
			if (v->next == NULL)
				TrainEnterDepot(v, tile);
			v->tile = tile;
			InvalidateWindow(WC_VEHICLE_DEPOT, tile);
			return 4;
		}
	} else if (_fractcoords_leave[dir] == fract_coord) {
		if (_leave_directions[dir] == v->direction) {
			/* leave the depot? */
			if ((v=v->next) != NULL) {
				v->vehstatus &= ~VS_HIDDEN;
				v->u.rail.track = _depot_track_mask[dir];
				assert(v->u.rail.track);
			}
		}
	}

	return 0;
}

void InitializeRail(void)
{
	_last_built_train_depot_tile = 0;
}

const TileTypeProcs _tile_type_rail_procs = {
	DrawTile_Track,						/* draw_tile_proc */
	GetSlopeZ_Track,					/* get_slope_z_proc */
	ClearTile_Track,					/* clear_tile_proc */
	GetAcceptedCargo_Track,		/* get_accepted_cargo_proc */
	GetTileDesc_Track,				/* get_tile_desc_proc */
	GetTileTrackStatus_Track,	/* get_tile_track_status_proc */
	ClickTile_Track,					/* click_tile_proc */
	AnimateTile_Track,				/* animate_tile_proc */
	TileLoop_Track,						/* tile_loop_clear */
	ChangeTileOwner_Track,		/* change_tile_owner_clear */
	NULL,											/* get_produced_cargo_proc */
	VehicleEnter_Track,				/* vehicle_enter_tile_proc */
	NULL,											/* vehicle_leave_tile_proc */
	GetSlopeTileh_Track,			/* get_slope_tileh_proc */
};
