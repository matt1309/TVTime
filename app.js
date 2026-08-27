const STORAGE_KEY = "tvtime-state-v1";
const DAY_MINUTES = 24 * 60;

function createId() {
  if (window.crypto?.randomUUID) {
    return window.crypto.randomUUID();
  }

  return `id-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function createSeedState() {
  return {
    channels: ["Kids 1", "Family Movies"],
    media: [
      {
        id: createId(),
        title: "Spider-Man",
        genre: "superhero",
        duration: 120,
        source: "local"
      },
      {
        id: createId(),
        title: "Bluey",
        genre: "kids",
        duration: 10,
        source: "stream"
      },
      {
        id: createId(),
        title: "Teen Titans",
        genre: "superhero",
        duration: 25,
        source: "local"
      },
      {
        id: createId(),
        title: "Nature Break",
        genre: "filler",
        duration: 15,
        source: "http"
      }
    ],
    schedule: []
  };
}

let state = loadState();

const channelForm = document.querySelector("#channel-form");
const mediaForm = document.querySelector("#media-form");
const manualScheduleForm = document.querySelector("#manual-schedule-form");
const autoScheduleForm = document.querySelector("#auto-schedule-form");
const iptvForm = document.querySelector("#iptv-form");
const channelList = document.querySelector("#channel-list");
const mediaList = document.querySelector("#media-list");
const manualChannel = document.querySelector("#manual-channel");
const autoChannel = document.querySelector("#auto-channel");
const guideChannel = document.querySelector("#guide-channel");
const manualMedia = document.querySelector("#manual-media");
const guideBody = document.querySelector("#guide-body");
const nowPlaying = document.querySelector("#now-playing");
const message = document.querySelector("#message");
const libraryStatus = document.querySelector("#library-status");
const syncLibrary = document.querySelector("#sync-library");
const videoPlayerContainer = document.querySelector("#video-player-container");
const videoPlayer = document.querySelector("#video-player");
const playCurrentBtn = document.querySelector("#play-current-btn");
const iptvList = document.querySelector("#iptv-list");
const tunerStatus = document.querySelector("#tuner-status");
const tunerDeviceId = document.querySelector("#tuner-device-id");
const tunerBaseUrl = document.querySelector("#tuner-base-url");
const tunerChannelCount = document.querySelector("#tuner-channel-count");
const refreshTunerBtn = document.querySelector("#refresh-tuner");

channelForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const name = document.querySelector("#channel-name").value.trim();

  if (!name) {
    showMessage("Channel name is required.", "warning");
    return;
  }

  if (state.channels.includes(name)) {
    showMessage("That channel already exists.", "warning");
    return;
  }

  state.channels.push(name);
  persistState();
  channelForm.reset();
  render();
  showMessage(`Added channel "${name}".`, "success");
});

mediaForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const title = document.querySelector("#media-title").value.trim();
  const genre = document.querySelector("#media-genre").value.trim().toLowerCase();
  const duration = Number.parseInt(
    document.querySelector("#media-duration").value,
    10
  );
  const source = document.querySelector("#media-source").value.trim() || "local";

  if (!title || !genre || !Number.isInteger(duration) || duration < 1) {
    showMessage("Enter a title, genre and valid duration.", "warning");
    return;
  }

  state.media.push({
    id: createId(),
    title,
    genre,
    duration,
    source
  });
  persistState();
  mediaForm.reset();
  render();
  showMessage(`Added "${title}" to the media library.`, "success");
});

manualScheduleForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const channel = manualChannel.value;
  const mediaId = manualMedia.value;
  const start = document.querySelector("#manual-start").value;
  const media = state.media.find((item) => item.id === mediaId);

  if (!channel || !media || !start) {
    showMessage("Choose a channel, programme and start time.", "warning");
    return;
  }

  if (toMinutes(start) + media.duration > DAY_MINUTES) {
    showMessage("This prototype only supports schedules that finish before midnight.", "warning");
    return;
  }

  const slot = createSlot(channel, media, start);
  if (hasOverlap(slot)) {
    showMessage("That slot overlaps an existing programme.", "warning");
    return;
  }

  state.schedule.push(slot);
  sortSchedule();
  persistState();
  manualScheduleForm.reset();
  render(channel);
  showMessage(`Scheduled "${media.title}" on ${channel}.`, "success");
});

autoScheduleForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const channel = autoChannel.value;
  const genre = document.querySelector("#auto-genre").value.trim().toLowerCase();
  const start = document.querySelector("#auto-start").value;
  const end = document.querySelector("#auto-end").value;

  if (!channel || !genre || !start || !end) {
    showMessage("Complete all generation fields first.", "warning");
    return;
  }

  const startMinutes = toMinutes(start);
  const endMinutes = toMinutes(end);
  if (endMinutes <= startMinutes) {
    showMessage("End time must be later than the start time in the same day.", "warning");
    return;
  }

  if (endMinutes > DAY_MINUTES) {
    showMessage("This prototype only supports schedules that finish before midnight.", "warning");
    return;
  }

  const matchingMedia = state.media.filter((item) => item.genre === genre);
  if (matchingMedia.length === 0) {
    showMessage(`No media found for genre "${genre}".`, "warning");
    return;
  }

  const generatedSlots = [];
  let currentStart = startMinutes;
  let nextIndex = 0;

  while (currentStart < endMinutes) {
    const media = matchingMedia[nextIndex % matchingMedia.length];
    const slotEnd = currentStart + media.duration;

    if (slotEnd > endMinutes) {
      break;
    }

    generatedSlots.push({
      channel,
      mediaId: media.id,
      start: fromMinutes(currentStart),
      end: fromMinutes(slotEnd)
    });
    currentStart = slotEnd;
    nextIndex += 1;
  }

  if (generatedSlots.length === 0) {
    showMessage("The selected window is too small for the chosen media.", "warning");
    return;
  }

  if (generatedSlots.some((slot) => hasOverlap(slot))) {
    showMessage("Generated slots would overlap the existing schedule.", "warning");
    return;
  }

  state.schedule.push(...generatedSlots);
  sortSchedule();
  persistState();
  autoScheduleForm.reset();
  render(channel);
  showMessage(`Generated ${generatedSlots.length} programme slots on ${channel}.`, "success");
});

guideChannel.addEventListener("change", () => {
  renderGuide(guideChannel.value);
  renderNowPlaying();
});

syncLibrary.addEventListener("click", () => {
  syncBackendMedia({ showSuccess: true });
});

iptvForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const playlistText = document.querySelector("#iptv-playlist").value;
  const entries = parseM3u(playlistText);

  if (entries.length === 0) {
    showMessage("Paste a valid M3U playlist with at least one channel.", "warning");
    return;
  }

  let addedChannels = 0;
  entries.forEach((entry) => {
    if (!state.channels.includes(entry.title)) {
      state.channels.push(entry.title);
      addedChannels += 1;
    }

    const existing = state.media.find((item) => item.uri === entry.url);
    const media = existing || {
      id: createId(),
      title: entry.title,
      genre: entry.genre,
      duration: 0,
      source: "iptv",
      uri: entry.url
    };

    if (!existing) {
      state.media.push(media);
    }

    // Live IPTV channels run all day; replace any previous all-day slot for
    // this channel so re-importing a playlist keeps the guide in sync.
    state.schedule = state.schedule.filter((slot) => slot.channel !== entry.title);
    state.schedule.push({
      channel: entry.title,
      mediaId: media.id,
      start: "00:00",
      end: "23:59"
    });
  });

  sortSchedule();
  persistState();
  iptvForm.reset();
  renderIptvList();
  render();
  showMessage(
    `Imported ${entries.length} IPTV channel${entries.length === 1 ? "" : "s"}` +
      (addedChannels > 0 ? ` (${addedChannels} new).` : "."),
    "success"
  );
});

refreshTunerBtn.addEventListener("click", () => {
  refreshTunerInfo();
});

playCurrentBtn.addEventListener("click", () => {
  const liveSlot = findLiveSlot();
  if (!liveSlot) {
    showMessage("No programme is currently live to play.", "warning");
    return;
  }

  const media = state.media.find((item) => item.id === liveSlot.mediaId);
  if (!media) {
    showMessage("Media item not found for current programme.", "warning");
    return;
  }

  if (!media.uri && !media.source) {
    showMessage("No playback source available for this media.", "warning");
    return;
  }

  const videoUrl = media.uri || `/api/stream/${encodeURIComponent(media.id)}`;
  videoPlayer.src = videoUrl;
  videoPlayerContainer.style.display = "block";
  videoPlayer.play().catch((error) => {
    showMessage(`Failed to play video: ${error.message}`, "warning");
    window.console.error("Video playback error:", error);
  });
});

async function syncBackendMedia({ showSuccess = false } = {}) {
  try {
    const response = await fetch("/api/videos", {
      headers: {
        Accept: "application/json"
      }
    });

    if (!response.ok) {
      throw new Error(`backend returned ${response.status}`);
    }

    const videos = await response.json();
    if (!Array.isArray(videos)) {
      throw new Error("backend response was not a video list");
    }

    const existingIds = new Set(state.media.map((item) => item.id));
    const imported = videos
      .filter((video) => video?.id && !existingIds.has(video.id))
      .map((video) => ({
        id: video.id,
        title: video.title || "Untitled video",
        genre: video.genre || "unknown",
        duration: normaliseDuration(video.durationMinutes),
        source: video.source || "backend",
        uri: video.uri || ""
      }));

    if (imported.length > 0) {
      state.media.push(...imported);
      persistState();
      render();
    }

    libraryStatus.textContent = `Backend connected. ${state.media.length} media item${
      state.media.length === 1 ? "" : "s"
    } available.`;
    if (showSuccess) {
      showMessage(
        `Imported ${imported.length} new backend media item${
          imported.length === 1 ? "" : "s"
        }.`,
        "success"
      );
    }
  } catch (error) {
    libraryStatus.textContent =
      "Backend media sync is unavailable. You can keep using the browser-only library.";
    window.console.info("TVTime backend media sync skipped:", error);
  }
}

function parseM3u(text) {
  const lines = text.split(/\r?\n/).map((line) => line.trim());
  const entries = [];
  let pendingTitle = "";
  let pendingGenre = "iptv";

  lines.forEach((line) => {
    if (!line) {
      return;
    }

    if (line.startsWith("#EXTINF:")) {
      const groupMatch = line.match(/group-title="([^"]*)"/i);
      pendingGenre = groupMatch && groupMatch[1] ? groupMatch[1] : "iptv";
      const commaIndex = line.lastIndexOf(",");
      pendingTitle = commaIndex >= 0 ? line.slice(commaIndex + 1).trim() : "";
      return;
    }

    if (line.startsWith("#")) {
      return;
    }

    entries.push({
      title: pendingTitle || line,
      genre: pendingGenre,
      url: line
    });
    pendingTitle = "";
    pendingGenre = "iptv";
  });

  return entries;
}

function renderIptvList() {
  iptvList.innerHTML = "";
  const iptvChannels = state.media.filter((item) => item.source === "iptv");

  if (iptvChannels.length === 0) {
    const empty = document.createElement("li");
    empty.textContent = "No IPTV channels imported yet.";
    iptvList.appendChild(empty);
    return;
  }

  iptvChannels.forEach((item) => {
    const row = document.createElement("li");
    row.innerHTML = `<strong>${escapeHtml(item.title)}</strong> · ${escapeHtml(
      item.genre
    )} · Live`;
    iptvList.appendChild(row);
  });
}

async function refreshTunerInfo() {
  tunerStatus.textContent = "Checking backend...";
  try {
    const [discoverResponse, lineupResponse] = await Promise.all([
      fetch("/discover.json", { headers: { Accept: "application/json" } }),
      fetch("/lineup.json", { headers: { Accept: "application/json" } })
    ]);

    if (!discoverResponse.ok || !lineupResponse.ok) {
      throw new Error("tuner endpoints unavailable");
    }

    const discover = await discoverResponse.json();
    const lineup = await lineupResponse.json();

    tunerStatus.textContent = "Online — ready for Plex/Emby/Jellyfin";
    tunerDeviceId.textContent = discover.DeviceID || "unknown";
    tunerBaseUrl.textContent = discover.BaseURL || "unknown";
    tunerChannelCount.textContent = Array.isArray(lineup) ? String(lineup.length) : "0";
  } catch (error) {
    tunerStatus.textContent = "Offline (start the C++ backend to enable tuner emulation).";
    tunerDeviceId.textContent = "—";
    tunerBaseUrl.textContent = "—";
    tunerChannelCount.textContent = "—";
    window.console.info("TVTime tuner info unavailable:", error);
  }
}

function loadState() {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (!saved) {
    return createSeedState();
  }

  try {
    return JSON.parse(saved);
  } catch {
    window.console.warn("TVTime saved state was invalid and has been reset.");
    return createSeedState();
  }
}

function persistState() {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
}

function render(selectedChannel = guideChannel.value || state.channels[0]) {
  renderChannels();
  renderMedia();
  renderIptvList();
  renderSelects(selectedChannel);
  renderGuide(selectedChannel);
  renderNowPlaying();
}

function renderChannels() {
  channelList.innerHTML = "";
  state.channels.forEach((channel) => {
    const item = document.createElement("li");
    item.textContent = channel;
    channelList.appendChild(item);
  });
}

function renderMedia() {
  mediaList.innerHTML = "";
  state.media.forEach((item) => {
    const row = document.createElement("li");
    const durationLabel =
      item.source === "iptv" ? "Live" : `${Number(item.duration)} min`;
    row.innerHTML = `<strong>${escapeHtml(item.title)}</strong> · ${escapeHtml(
      item.genre
    )} · ${durationLabel} · ${escapeHtml(item.source)}`;
    mediaList.appendChild(row);
  });
}

function renderSelects(selectedChannel) {
  fillSelect(manualChannel, state.channels, selectedChannel);
  fillSelect(autoChannel, state.channels, selectedChannel);
  fillSelect(guideChannel, state.channels, selectedChannel);

  manualMedia.innerHTML = "";
  state.media.forEach((item) => {
    const option = document.createElement("option");
    option.value = item.id;
    option.textContent = `${item.title} (${item.duration} min)`;
    manualMedia.appendChild(option);
  });
}

function renderGuide(channel) {
  guideBody.innerHTML = "";
  const channelSchedule = state.schedule.filter((slot) => slot.channel === channel);

  if (channelSchedule.length === 0) {
    const emptyRow = document.createElement("tr");
    emptyRow.innerHTML =
      "<td colspan='5'>No programmes scheduled for this channel yet.</td>";
    guideBody.appendChild(emptyRow);
    return;
  }

  channelSchedule.forEach((slot) => {
    const media = state.media.find((item) => item.id === slot.mediaId);
    if (!media) {
      return;
    }

    const row = document.createElement("tr");
    row.innerHTML = `
      <td>${slot.start}</td>
      <td>${slot.end}</td>
      <td><strong>${escapeHtml(media.title)}</strong></td>
      <td>${escapeHtml(media.genre)}</td>
      <td>${escapeHtml(media.source)}</td>
    `;
    guideBody.appendChild(row);
  });
}

function renderNowPlaying() {
  const selectedChannel = guideChannel.value || state.channels[0] || "the selected channel";
  const liveSlot = findLiveSlot();
  if (!liveSlot) {
    nowPlaying.textContent = `No scheduled programme is live on ${selectedChannel} right now.`;
    videoPlayerContainer.style.display = "none";
    return;
  }

  const media = state.media.find((item) => item.id === liveSlot.mediaId);
  if (!media) {
    nowPlaying.textContent = `A live slot exists on ${selectedChannel}, but its media item is missing.`;
    videoPlayerContainer.style.display = "none";
    return;
  }

  nowPlaying.innerHTML = `<strong>${escapeHtml(media.title)}</strong> is live on ${escapeHtml(
    liveSlot.channel
  )} until ${liveSlot.end}.`;
  
  if (media.uri || media.source) {
    videoPlayerContainer.style.display = "block";
  } else {
    videoPlayerContainer.style.display = "none";
  }
}

function fillSelect(select, values, selectedValue) {
  select.innerHTML = "";
  values.forEach((value) => {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = value;
    option.selected = value === selectedValue;
    select.appendChild(option);
  });
}

function createSlot(channel, media, start) {
  const startMinutes = toMinutes(start);
  return {
    channel,
    mediaId: media.id,
    start,
    end: fromMinutes(startMinutes + media.duration)
  };
}

function hasOverlap(candidate) {
  const candidateStart = toMinutes(candidate.start);
  const candidateEnd = toMinutes(candidate.end);

  return state.schedule.some((slot) => {
    if (slot.channel !== candidate.channel) {
      return false;
    }

    const slotStart = toMinutes(slot.start);
    const slotEnd = toMinutes(slot.end);
    return candidateStart < slotEnd && candidateEnd > slotStart;
  });
}

function sortSchedule() {
  state.schedule.sort((left, right) => {
    if (left.channel !== right.channel) {
      return left.channel.localeCompare(right.channel);
    }

    return toMinutes(left.start) - toMinutes(right.start);
  });
}

function findLiveSlot() {
  const now = new Date();
  const nowMinutes = now.getHours() * 60 + now.getMinutes();
  const selectedChannel = guideChannel.value || state.channels[0] || "";

  return state.schedule.find((slot) => {
    if (selectedChannel && slot.channel !== selectedChannel) {
      return false;
    }

    const start = toMinutes(slot.start);
    const end = toMinutes(slot.end);
    return nowMinutes >= start && nowMinutes < end;
  });
}

function toMinutes(value) {
  const [hours, minutes] = value.split(":").map(Number);
  return hours * 60 + minutes;
}

function fromMinutes(value) {
  const hours = Math.floor(value / 60);
  const minutes = value % 60;
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}`;
}

function normaliseDuration(value) {
  const duration = Number.parseInt(value, 10);
  return Number.isInteger(duration) && duration > 0 ? duration : 30;
}

function showMessage(text, tone) {
  message.textContent = text;
  message.className = `message visible ${tone}`;
}

function escapeHtml(value) {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

render();
syncBackendMedia();
refreshTunerInfo();
window.setInterval(renderNowPlaying, 30000);
