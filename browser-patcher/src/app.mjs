const elements = {
  applicationInput: document.querySelector("#application-input"),
  audioInput: document.querySelector("#audio-input"),
  applicationDrop: document.querySelector("#application-drop"),
  audioDrop: document.querySelector("#audio-drop"),
  bankExportButton: document.querySelector("#bank-export-button"),
  dolExportButton: document.querySelector("#dol-export-button"),
  status: document.querySelector("#status"),
  budgetLabel: document.querySelector("#budget-label"),
  budgetFill: document.querySelector("#budget-fill"),
  dolCapacity: document.querySelector("#dol-capacity"),
  sampleCount: document.querySelector("#sample-count"),
  poolList: document.querySelector("#pool-list"),
  moveUp: document.querySelector("#move-up"),
  moveDown: document.querySelector("#move-down"),
  remove: document.querySelector("#remove"),
  clearAll: document.querySelector("#clear-all"),
  play: document.querySelector("#play"),
  stop: document.querySelector("#stop"),
  waveform: document.querySelector("#waveform"),
  name: document.querySelector("#sample-name"),
  gain: document.querySelector("#sample-gain"),
  trimStart: document.querySelector("#trim-start"),
  trimEnd: document.querySelector("#trim-end"),
  sourceRate: document.querySelector("#source-rate"),
  preparedDuration: document.querySelector("#prepared-duration"),
  pcmSize: document.querySelector("#pcm-size"),
  origin: document.querySelector("#sample-origin"),
};

let samples = [];
let selectedId = null;
let applicationBytes = null;
let applicationName = "";
let applicationBankCapacity = null;
let audioContext = null;
let playingSource = null;
let draggingBoundary = null;

function selectedSample() {
  return samples.find(sample => sample.id === selectedId) ?? null;
}

function context() {
  if (!audioContext)
    audioContext = new (window.AudioContext || window.webkitAudioContext)();
  return audioContext;
}

function setStatus(message, kind = "") {
  elements.status.textContent = message;
  elements.status.className = `status ${kind}`;
}

function stopPreview() {
  if (playingSource) {
    try { playingSource.stop(); } catch { /* already stopped */ }
  }
  playingSource = null;
  elements.stop.disabled = true;
}

function renderBudget() {
  const estimated = samples.length ? estimatedBankBytes(samples) : DATA_OFFSET;
  const usage = Math.min(100, estimated / MAX_BANK_BYTES * 100);
  const over = estimated > MAX_BANK_BYTES || samples.length > MAX_ENTRIES;
  const dolOver = applicationBankCapacity !== null
    && estimated > applicationBankCapacity;
  elements.budgetLabel.textContent = `${formatBytes(estimated)} / ${formatBytes(MAX_BANK_BYTES)} · ${samples.length}/${MAX_ENTRIES} slots`;
  elements.budgetFill.style.width = `${usage}%`;
  elements.budgetFill.classList.toggle("over", over);
  if (applicationBankCapacity === null) {
    elements.dolCapacity.textContent = "DOL SLOT · OPEN A DOL FOR PATCHED-DOL EXPORT";
  } else {
    const suffix = dolOver ? ` · OVER BY ${formatBytes(estimated - applicationBankCapacity)}` : "";
    elements.dolCapacity.textContent = `${formatBytes(estimated)} / ${formatBytes(applicationBankCapacity)} DOL SLOT${suffix}`;
  }
  elements.dolCapacity.classList.toggle("over", dolOver);
  elements.bankExportButton.disabled = over || samples.length === 0;
  elements.dolExportButton.disabled = over || dolOver || samples.length === 0
    || applicationBytes === null;
}

function renderPool() {
  elements.poolList.replaceChildren();
  if (!samples.length) {
    const empty = document.createElement("p");
    empty.className = "empty";
    empty.textContent = "No samples in the bank.";
    elements.poolList.append(empty);
  }
  samples.forEach((sample, index) => {
    const row = document.createElement("button");
    row.className = `pool-row ${sample.id === selectedId ? "selected" : ""}`;
    row.type = "button";
    const number = document.createElement("span");
    number.textContent = String(index + 1).padStart(2, "0");
    const name = document.createElement("span");
    name.className = "pool-name";
    name.textContent = sample.name;
    const duration = document.createElement("span");
    duration.textContent = `${((sample.trimEnd - sample.trimStart) / sample.sourceRate).toFixed(2)}s`;
    row.append(number, name, duration);
    row.addEventListener("click", () => {
      stopPreview();
      selectedId = sample.id;
      render();
    });
    elements.poolList.append(row);
  });
  elements.sampleCount.textContent = String(samples.length);

  const selected = selectedSample();
  const index = selected ? samples.findIndex(sample => sample.id === selected.id) : -1;
  elements.moveUp.disabled = index <= 0;
  elements.moveDown.disabled = index < 0 || index >= samples.length - 1;
  elements.remove.disabled = !selected;
  elements.clearAll.disabled = samples.length === 0;
}

function drawWaveform() {
  const canvas = elements.waveform;
  const graphics = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;
  graphics.fillStyle = "#000";
  graphics.fillRect(0, 0, width, height);
  graphics.strokeStyle = "#202020";
  graphics.lineWidth = 1;
  for (let division = 0; division <= 8; division += 1) {
    const x = division * width / 8;
    graphics.beginPath();
    graphics.moveTo(x, 0);
    graphics.lineTo(x, height);
    graphics.stroke();
  }
  graphics.strokeStyle = "#555";
  graphics.beginPath();
  graphics.moveTo(0, height / 2);
  graphics.lineTo(width, height / 2);
  graphics.stroke();

  const sample = selectedSample();
  if (!sample) return;
  const startX = sample.trimStart / sample.data.length * width;
  const endX = sample.trimEnd / sample.data.length * width;
  const samplesPerPixel = sample.data.length / width;
  const displayGain = Math.pow(10, sample.gainDb / 20);
  for (let x = 0; x < width; x += 1) {
    const first = Math.floor(x * samplesPerPixel);
    const last = Math.min(sample.data.length,
      Math.max(first + 1, Math.ceil((x + 1) * samplesPerPixel)));
    let minimum = 1;
    let maximum = -1;
    for (let frame = first; frame < last; frame += 1) {
      const value = Math.max(-1, Math.min(1, sample.data[frame] * displayGain));
      minimum = Math.min(minimum, value);
      maximum = Math.max(maximum, value);
    }
    graphics.strokeStyle = x < startX || x > endX ? "#444" : "#fff";
    graphics.beginPath();
    graphics.moveTo(x + .5, height / 2 - maximum * height * .43);
    graphics.lineTo(x + .5, height / 2 - minimum * height * .43);
    graphics.stroke();
  }

  for (const [x, label] of [[startX, "IN"], [endX, "OUT"]]) {
    graphics.strokeStyle = "#fff";
    graphics.lineWidth = 2;
    graphics.beginPath();
    graphics.moveTo(x, 0);
    graphics.lineTo(x, height);
    graphics.stroke();
    graphics.fillStyle = "#fff";
    graphics.fillRect(label === "IN" ? x : x - 34, 0, 34, 18);
    graphics.fillStyle = "#000";
    graphics.font = "12px ui-monospace, monospace";
    graphics.fillText(label, label === "IN" ? x + 7 : x - 29, 13);
  }
}

function renderEditor() {
  const sample = selectedSample();
  const enabled = Boolean(sample);
  for (const control of [elements.name, elements.gain, elements.trimStart,
    elements.trimEnd, elements.play]) control.disabled = !enabled;
  if (!sample) {
    elements.name.value = "";
    elements.gain.value = "0";
    elements.trimStart.value = "0";
    elements.trimEnd.value = "0";
    elements.sourceRate.textContent = "—";
    elements.preparedDuration.textContent = "—";
    elements.pcmSize.textContent = "—";
    elements.origin.textContent = "—";
    drawWaveform();
    return;
  }

  const preparedFrames = Math.max(1,
    Math.round((sample.trimEnd - sample.trimStart) * TARGET_RATE / sample.sourceRate));
  elements.name.value = sample.name;
  elements.gain.value = String(sample.gainDb);
  elements.trimStart.value = (sample.trimStart / sample.sourceRate * 1000).toFixed(1);
  elements.trimEnd.value = (sample.trimEnd / sample.sourceRate * 1000).toFixed(1);
  elements.sourceRate.textContent = `${sample.sourceRate} HZ`;
  elements.preparedDuration.textContent = `${(preparedFrames / TARGET_RATE).toFixed(3)} S`;
  elements.pcmSize.textContent = formatBytes(preparedFrames * 2);
  elements.origin.textContent = sample.origin;
  drawWaveform();
}

function render() {
  renderPool();
  renderBudget();
  renderEditor();
}

async function openSource(file) {
  if (!file) return;
  try {
    stopPreview();
    const input = new Uint8Array(await file.arrayBuffer());
    const isDol = /\.dol$/i.test(file.name);
    let decoded;
    let detail;
    if (isDol) {
      const embedded = extractDolBank(input);
      decoded = decodeBank(embedded.bank);
      applicationBytes = input;
      applicationName = file.name;
      applicationBankCapacity = embedded.bankCapacity;
      detail = `${decoded.samples.length} embedded samples, ${formatBytes(decoded.used)} used in a ${formatBytes(embedded.bankCapacity)} DOL slot`;
    } else {
      decoded = decodeBank(input);
      applicationBytes = null;
      applicationName = "";
      applicationBankCapacity = null;
      detail = `${decoded.samples.length} samples, ${formatBytes(decoded.used)} external bank`;
    }
    samples = decoded.samples;
    for (const sample of samples)
      sample.origin = isDol ? `EMBEDDED · ${file.name}` : file.name;
    selectedId = samples[0]?.id ?? null;
    render();
    const conversion = decoded.sampleRate === TARGET_RATE ? ""
      : ` Legacy ${decoded.sampleRate} Hz audio will be upgraded on export.`;
    setStatus(`Opened ${file.name}: ${detail}, ${decoded.sampleRate} Hz.${conversion}`, "success");
  } catch (error) {
    setStatus(error instanceof Error ? error.message : String(error), "error");
  }
}

async function addAudio(files) {
  const incoming = Array.from(files).filter(file =>
    /\.(wav|aif|aiff|mp3|m4a|ogg|flac)$/i.test(file.name));
  if (!incoming.length) {
    setStatus("No supported audio files were selected.", "error");
    return;
  }
  if (samples.length + incoming.length > MAX_ENTRIES) {
    setStatus(`Only ${MAX_ENTRIES - samples.length} sample slots remain.`, "error");
    return;
  }
  try {
    const added = [];
    for (const file of incoming) {
      const encoded = await file.arrayBuffer();
      let data;
      let sourceRate;
      if (/\.wav$/i.test(file.name)) {
        try {
          const decoded = decodePcmWav(new Uint8Array(encoded));
          data = decoded.data;
          sourceRate = decoded.sourceRate;
        } catch {
          // Let the browser handle valid WAV encodings beyond integer PCM.
        }
      }
      if (!data) {
        const buffer = await context().decodeAudioData(encoded.slice(0));
        data = new Float32Array(buffer.length);
        sourceRate = buffer.sampleRate;
        for (let channel = 0; channel < buffer.numberOfChannels; channel += 1) {
          const channelData = buffer.getChannelData(channel);
          for (let frame = 0; frame < data.length; frame += 1)
            data[frame] += channelData[frame] / buffer.numberOfChannels;
        }
      }
      added.push(makeSample({
        name: file.name.replace(/\.[^.]+$/, ""),
        sourceRate,
        data,
        origin: file.name,
      }));
    }
    samples.push(...added);
    selectedId = added[0]?.id ?? selectedId;
    render();
    setStatus(`Added ${added.length} sample${added.length === 1 ? "" : "s"}. Export is 48 kHz mono PCM16.`, "success");
  } catch (error) {
    setStatus(`Audio decode failed: ${error instanceof Error ? error.message : String(error)}`, "error");
  }
}

function downloadBytes(bytes, filename) {
  const blob = new Blob([bytes], { type: "application/octet-stream" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

function exportBank() {
  try {
    const bank = buildBank(samples);
    downloadBytes(bank, "sample_bank.bin");
    setStatus(`Built sample_bank.bin: ${samples.length} samples, ${formatBytes(bank.length)}. Copy it to /gamecube-ambient-granulator/ on the SD card.`, "success");
  } catch (error) {
    setStatus(error instanceof Error ? error.message : String(error), "error");
  }
}

function exportDol() {
  try {
    if (applicationBytes === null || applicationBankCapacity === null)
      throw new Error("Open the GameCube DOL before exporting a patched DOL.");
    const bank = buildBank(samples, applicationBankCapacity);
    const output = patchDolBank(applicationBytes, bank);
    const stem = applicationName.replace(/\.dol$/i, "")
      || "gamecube_ambient_granulator";
    const filename = `${stem}-patched.dol`;
    downloadBytes(output, filename);
    setStatus(`Built ${filename}: ${samples.length} embedded samples, ${formatBytes(bank.length)} bank, original ${formatBytes(output.length)} DOL size preserved. Remove any external sample_bank.bin when testing it.`, "success");
  } catch (error) {
    setStatus(error instanceof Error ? error.message : String(error), "error");
  }
}

function preview() {
  const sample = selectedSample();
  if (!sample) return;
  stopPreview();
  const data = resampleSample(sample);
  const audio = context();
  const buffer = audio.createBuffer(1, data.length, TARGET_RATE);
  buffer.copyToChannel(data, 0);
  const source = audio.createBufferSource();
  source.buffer = buffer;
  source.connect(audio.destination);
  source.onended = () => {
    if (playingSource === source) {
      playingSource = null;
      elements.stop.disabled = true;
    }
  };
  source.start();
  playingSource = source;
  elements.stop.disabled = false;
}

function moveSelected(direction) {
  const sample = selectedSample();
  if (!sample) return;
  const index = samples.findIndex(item => item.id === sample.id);
  const target = index + direction;
  if (target < 0 || target >= samples.length) return;
  [samples[index], samples[target]] = [samples[target], samples[index]];
  render();
}

function removeSelected() {
  const sample = selectedSample();
  if (!sample) return;
  stopPreview();
  const index = samples.findIndex(item => item.id === sample.id);
  samples.splice(index, 1);
  selectedId = samples[Math.min(index, samples.length - 1)]?.id ?? null;
  render();
}

function clearAllSamples() {
  stopPreview();
  samples = [];
  selectedId = null;
  render();
  setStatus("Sample pool cleared. Add audio to start a new bank.");
}

function updateSelected(patch) {
  const sample = selectedSample();
  if (!sample) return;
  Object.assign(sample, patch);
}

function trimFromPointer(event) {
  const sample = selectedSample();
  if (!sample) return 0;
  const rect = elements.waveform.getBoundingClientRect();
  const fraction = Math.max(0, Math.min(1,
    (event.clientX - rect.left) / rect.width));
  return Math.round(fraction * sample.data.length);
}

function updateDraggedBoundary(event) {
  const sample = selectedSample();
  if (!sample || !draggingBoundary) return;
  const position = trimFromPointer(event);
  if (draggingBoundary === "start")
    sample.trimStart = Math.min(position, sample.trimEnd - 1);
  else
    sample.trimEnd = Math.max(position, sample.trimStart + 1);
  renderBudget();
  renderPool();
  renderEditor();
}

function installDropZone(element, handler) {
  element.addEventListener("dragover", event => {
    event.preventDefault();
    element.classList.add("armed");
  });
  element.addEventListener("dragleave", () => element.classList.remove("armed"));
  element.addEventListener("drop", event => {
    event.preventDefault();
    element.classList.remove("armed");
    handler(event.dataTransfer.files);
  });
}

elements.applicationInput.addEventListener("change", () =>
  openSource(elements.applicationInput.files?.[0]));
elements.audioInput.addEventListener("change", () => addAudio(elements.audioInput.files ?? []));
installDropZone(elements.applicationDrop, files => openSource(files[0]));
installDropZone(elements.audioDrop, addAudio);
elements.bankExportButton.addEventListener("click", exportBank);
elements.dolExportButton.addEventListener("click", exportDol);
elements.moveUp.addEventListener("click", () => moveSelected(-1));
elements.moveDown.addEventListener("click", () => moveSelected(1));
elements.remove.addEventListener("click", removeSelected);
elements.clearAll.addEventListener("click", clearAllSamples);
elements.play.addEventListener("click", preview);
elements.stop.addEventListener("click", stopPreview);

elements.name.addEventListener("input", () => {
  updateSelected({ name: elements.name.value.slice(0, 31) });
  renderPool();
});
elements.gain.addEventListener("input", () => {
  const gainDb = Math.max(-48, Math.min(18, Number(elements.gain.value) || 0));
  updateSelected({ gainDb });
  drawWaveform();
});
elements.trimStart.addEventListener("change", () => {
  const sample = selectedSample();
  if (!sample) return;
  sample.trimStart = Math.max(0, Math.min(sample.trimEnd - 1,
    Math.round((Number(elements.trimStart.value) || 0) * sample.sourceRate / 1000)));
  render();
});
elements.trimEnd.addEventListener("change", () => {
  const sample = selectedSample();
  if (!sample) return;
  sample.trimEnd = Math.min(sample.data.length, Math.max(sample.trimStart + 1,
    Math.round((Number(elements.trimEnd.value) || 0) * sample.sourceRate / 1000)));
  render();
});

elements.waveform.addEventListener("pointerdown", event => {
  const sample = selectedSample();
  if (!sample) return;
  elements.waveform.setPointerCapture(event.pointerId);
  const position = trimFromPointer(event);
  draggingBoundary = Math.abs(position - sample.trimStart)
    <= Math.abs(position - sample.trimEnd) ? "start" : "end";
  updateDraggedBoundary(event);
});
elements.waveform.addEventListener("pointermove", updateDraggedBoundary);
elements.waveform.addEventListener("pointerup", () => { draggingBoundary = null; });
elements.waveform.addEventListener("pointercancel", () => { draggingBoundary = null; });

render();
