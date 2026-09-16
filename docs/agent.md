# Music Agent

`Agent` is a top-level workspace in the navigation tree. Open it, describe the
music you want, and press Enter. The result list merges matches from the full
MPD library and saved `Streams`; rows are labelled `[Local]` or `[Stream]`.
Select a result and press Enter again to play the ranked result snapshot.

Requests with an explicit listening verb (`播放`, `我想听`, `来一首`, or
`play`) automatically start the best-ranked result. Neutral requests such as
`search jazz` only show results. Add `本地` / `local only` or `流媒体` /
`stream only` to constrain the source; without a constraint both are searched.

Press `/` from the Agent result list to start another request. Requests may be
plain keywords or short commands such as `play jazz`, `find Alice`,
`播放夜曲`, or `来点摇滚音乐`.

## Architecture

The deterministic catalog merger and ranker lives in `src/agent`. It has no
terminal, MPD, network, or model dependency. `Controller::queryAgent` supplies
local MPD results and streaming results, and the UI only presents its response.
This boundary lets a future LLM planner translate richer conversation into a
query without giving the model direct access to MPD commands or unvalidated
playback URLs.

Ranking prefers title matches, then artist, album, genre, and URI. Source does
not decide relevance; a streaming title match can outrank a weak local match.
Duplicate source identities are removed before the result is returned.

Network catalog providers feed the same module through
`streaming::Service::searchAll`. Registered searchable providers are queried on
a cancellable background worker, their playable results are merged with local
and saved-stream candidates, and a failure from one provider is reported
without discarding healthy results. Playback URLs are resolved and validated by
the provider layer before MPD receives them.
