#include "sim/objects/drive_tracks.h"

#include <algorithm>
#include <utility>

namespace ra::sim::drive_tracks {

namespace {
#include "sim/objects/drive_tracks.inc"
} // namespace

const RawTrack& raw_track(int index)
{
	return kRawTracks[std::clamp(index, 0, 15)];
}

const TurnTrack& turn_track(int index)
{
	return kTurnTracks[std::clamp(index, 0, 71)];
}

Point transform(Point p, int flag)
{
	if (flag & 1)
		std::swap(p.x, p.y);
	if (flag & 2)
		p.x = -p.x;
	if (flag & 4)
		p.y = -p.y;
	return p;
}

} // namespace ra::sim::drive_tracks
