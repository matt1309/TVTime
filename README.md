# TVTime

TVTime is a low-dependency web app prototype for building "old school TV" schedules from your own media library.

## What is in this repository today?

This first cut keeps dependencies at zero:

- plain HTML, CSS and JavaScript
- no framework
- no build step
- no package manager setup

The prototype lets you:

- create multiple channels
- add media entries with a title, genre, duration and source
- build a schedule from scratch
- generate a schedule window from a chosen genre
- view a TV guide and "now playing" summary in the browser

All data is stored locally in `localStorage`, which keeps the initial project easy to understand and easy to evolve.

## Recommended architecture

Because the long-term product needs scheduling, media metadata, local streams and eventually external integrations, a backend-first architecture is a good fit.

### Phase 1: zero-dependency prototype

- **Frontend:** stock HTML/CSS/JS
- **Backend:** none yet
- **Storage:** browser `localStorage`
- **Goal:** validate the guide UX, channel model and scheduling workflow

### Phase 2: minimal C++ backend

Recommended direction: **C++ backend with minimal dependencies**.

- serve the static frontend directly from the backend
- expose a small JSON API for channels, media, schedules and playback state
- start with file-based persistence or SQLite
- keep the frontend framework-free unless the UI complexity proves otherwise

Suggested backend responsibilities:

- media library indexing
- schedule generation rules
- guide publishing
- stream URL validation
- user/account configuration
- parental and channel restrictions

### Phase 3: integrations

Add source plugins behind clear interfaces:

- local files
- DLNA discovery
- HTTP/RTSP streams
- later: provider/import integrations where legally and technically appropriate

### Phase 4: playback targets

- web player
- lightweight device apps that reuse the same guide API
- future experimental output layers such as DVB-T hardware tooling

## Why this approach?

It matches the project goals:

- **few dependencies**
- **backend-friendly design**
- **simple frontend**
- **incremental path from prototype to real scheduler**

## Running the prototype

From the repository root:

```bash
python3 -m http.server 8000
```

Then open <http://localhost:8000>.

## Running the C++ backend

The backend scaffold builds with CMake and serves the current static frontend from
the repository root. It also exposes early JSON endpoints for health, source
plugins and discovered videos.

```bash
cmake -S . -B build
cmake --build build
./build/tvtime_server . ./media
```

Then open <http://127.0.0.1:8080>. Set `TVTIME_PORT` to choose another port.

Current API endpoints:

- `GET /api/health`
- `GET /api/sources`
- `GET /api/videos`

## Next backend milestones

1. move browser state into the C++ service
2. add persistent media and schedule storage
3. separate scheduling rules from UI logic
4. add playback endpoints and source adapters
5. add authentication only when multi-user support is needed
