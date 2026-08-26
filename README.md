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

### Phase 2: minimal backend

Recommended direction: **Java backend with minimal dependencies**.

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

Add adapters behind clear interfaces:

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
cd /home/runner/work/TVTime/TVTime
python3 -m http.server 8000
```

Then open <http://localhost:8000>.

## Next backend milestones

1. move browser state into a Java service
2. add persistent media and schedule storage
3. separate scheduling rules from UI logic
4. add playback endpoints and source adapters
5. add authentication only when multi-user support is needed
