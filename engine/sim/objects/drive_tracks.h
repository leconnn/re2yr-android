#pragma once

// The tracks of the Drive locomotor (U section 10.2, tables in re/data/drive_tracks.json).
//
// A unit drives from cell to cell along a track: a list of points, 7 "speed units" apart, relative to
// the centre of the cell it is driving to. TurnTrack[next + facing * 8] says which track to use for a
// turn from one of 8 facings toward one of 8 directions (index cur * 9 is straight ahead).

#include <cstddef>

namespace ra::sim::drive_tracks {

struct Point {
	int x, y, facing;
};

struct RawTrack {
	int jump;  // the index at which a following straight track may be chained (-1 none)
	int entry; // the index to continue from on the new track
	int cell;  // the index at which the unit's occupation switches to the next cell (-1 never)
	const Point* points;
	int count;
};

struct TurnTrack {
	int track;       // RawTrack index, 0 = none
	int start_track; // RawTrack to use when starting on the short track
	int facing;      // body facing, 0..255
	int flag;        // 1 transpose x/y, 2 negate x, 4 negate y, 8 the track consumes the next cell
};

const RawTrack& raw_track(int index);   // 0..15
const TurnTrack& turn_track(int index); // 0..71

// Applies a turn track's flags to a point (transpose first, then negate).
Point transform(Point p, int flag);

} // namespace ra::sim::drive_tracks
