const STORAGE_KEY = "tvtime-state-v1";
const DAY_MINUTES = 24 * 60;

function createId() {
  if (window.crypto?.randomUUID) {
    return window.crypto.randomUUID();
  }

  return `media-${Date.now()}-${Math.random().toString(16).slice(2)}`;
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
const channelList = document.querySelector("#channel-list");
const mediaList = document.querySelector("#media-list");
const manualChannel = document.querySelector("#manual-channel");
const autoChannel = document.querySelector("#auto-channel");
const guideChannel = document.querySelector("#guide-channel");
const manualMedia = document.querySelector("#manual-media");
const guideBody = document.querySelector("#guide-body");
const nowPlaying = document.querySelector("#now-playing");
const message = document.querySelector("#message");

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

  if (toMinutes(start) + media.duration >= DAY_MINUTES) {
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
    showMessage("End time must be after start time.", "warning");
    return;
  }

  if (endMinutes >= DAY_MINUTES) {
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

function loadState() {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (!saved) {
    return createSeedState();
  }

  try {
    return JSON.parse(saved);
  } catch {
    return createSeedState();
  }
}

function persistState() {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
}

function render(selectedChannel = guideChannel.value || state.channels[0]) {
  renderChannels();
  renderMedia();
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
    row.innerHTML = `<strong>${escapeHtml(item.title)}</strong> · ${escapeHtml(
      item.genre
    )} · ${Number(item.duration)} min · ${escapeHtml(item.source)}`;
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
  const liveSlot = findLiveSlot();
  if (!liveSlot) {
    nowPlaying.textContent = "No scheduled programme is live right now.";
    return;
  }

  const media = state.media.find((item) => item.id === liveSlot.mediaId);
  if (!media) {
    nowPlaying.textContent = "A live slot exists, but its media item is missing.";
    return;
  }

  nowPlaying.innerHTML = `<strong>${escapeHtml(media.title)}</strong> is live on ${escapeHtml(
    liveSlot.channel
  )} until ${liveSlot.end}.`;
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
window.setInterval(renderNowPlaying, 30000);
