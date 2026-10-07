// Oven Controller Prototype
// Dashboard + recipe editor + thermal simulator.
// In the ESP32 version, the simulator will be replaced by real sensor/API data.

const defaultRecipes = [
  {
    id: "carbon-standard",
    name: "Carbon Standard",
    steps: [
      { type: "ramp", target: 80, rate: 2.0 },
      { type: "hold", target: 80, duration: 30 },
      { type: "ramp", target: 150, rate: 1.5 },
      { type: "hold", target: 150, duration: 60 },
      { type: "cooldown", target: 40, rate: 1.0 }
    ]
  },
  {
    id: "resin-low-temp",
    name: "Resin Low Temp",
    steps: [
      { type: "ramp", target: 60, rate: 1.5 },
      { type: "hold", target: 60, duration: 45 },
      { type: "ramp", target: 100, rate: 1.0 },
      { type: "hold", target: 100, duration: 30 },
      { type: "cooldown", target: 40, rate: 1.0 }
    ]
  },
  {
    id: "high-temp-demo",
    name: "High Temp Demo",
    steps: [
      { type: "ramp", target: 120, rate: 3.0 },
      { type: "hold", target: 120, duration: 20 },
      { type: "ramp", target: 190, rate: 2.0 },
      { type: "hold", target: 190, duration: 30 },
      { type: "cooldown", target: 50, rate: 1.5 }
    ]
  }
];

let recipes = loadRecipes();
let selectedRecipeId = recipes[0]?.id ?? "";
let state = createInitialState();

const $ = (id) => document.getElementById(id);

function createInitialState() {
  return {
    running: false,
    paused: false,
    elapsedSec: 0,
    currentStep: 0,
    stepElapsedSec: 0,
    target: 25,
    actualCore: 25,
    sensors: [25, 25, 25],
    power: 0,
    history: [],
    stepStartTemp: 25,
    completed: false
  };
}

function loadRecipes() {
  try {
    const saved = JSON.parse(localStorage.getItem("oven_recipes"));
    if (Array.isArray(saved) && saved.length) return saved;
  } catch (_) {}
  return structuredClone(defaultRecipes);
}

function persistRecipes() {
  localStorage.setItem("oven_recipes", JSON.stringify(recipes));
}

function getRecipe() {
  return recipes.find(r => r.id === selectedRecipeId) || recipes[0];
}

function uid() {
  return "recipe-" + Date.now().toString(36);
}

function populateRecipeSelect() {
  const select = $("recipeSelect");
  select.innerHTML = "";
  recipes.forEach(r => {
    const opt = document.createElement("option");
    opt.value = r.id;
    opt.textContent = r.name;
    select.appendChild(opt);
  });
  select.value = selectedRecipeId;
}

function renderRecipeEditor() {
  const recipe = getRecipe();
  if (!recipe) return;
  $("recipeName").value = recipe.name;
  const box = $("stepsEditor");
  box.innerHTML = "";

  recipe.steps.forEach((step, index) => {
    const row = document.createElement("div");
    row.className = "step-row";
    row.innerHTML = `
      <div class="step-row-head">
        <strong>Step ${index + 1}</strong>
        <button class="step-remove" data-index="${index}">Rimuovi</button>
      </div>
      <div class="step-fields">
        <label class="wide">
          <span>Tipo</span>
          <select data-field="type" data-index="${index}">
            <option value="ramp" ${step.type === "ramp" ? "selected" : ""}>Ramp</option>
            <option value="hold" ${step.type === "hold" ? "selected" : ""}>Hold</option>
            <option value="cooldown" ${step.type === "cooldown" ? "selected" : ""}>Cooldown</option>
          </select>
        </label>

        <label>
          <span>Target °C</span>
          <input type="number" step="0.1" min="0" max="220"
                 value="${step.target ?? 25}" data-field="target" data-index="${index}" />
        </label>

        ${step.type === "hold" ? `
        <label>
          <span>Durata min</span>
          <input type="number" step="1" min="1" value="${step.duration ?? 30}"
                 data-field="duration" data-index="${index}" />
        </label>` : `
        <label>
          <span>${step.type === "cooldown" ? "Raffreddamento °C/min" : "Ramping °C/min"}</span>
          <input type="number" step="0.1" min="0.1" value="${step.rate ?? 1}"
                 data-field="rate" data-index="${index}" />
        </label>`}
      </div>`;
    box.appendChild(row);
  });

  box.querySelectorAll("[data-field]").forEach(el => {
    el.addEventListener("change", () => {
      const idx = Number(el.dataset.index);
      const field = el.dataset.field;
      const val = field === "type" ? el.value : Number(el.value);
      recipe.steps[idx][field] = val;

      if (field === "type") {
        if (val === "hold") {
          delete recipe.steps[idx].rate;
          recipe.steps[idx].duration ??= 30;
        } else {
          delete recipe.steps[idx].duration;
          recipe.steps[idx].rate ??= 1.0;
        }
        renderRecipeEditor();
      }
    });
  });

  box.querySelectorAll(".step-remove").forEach(btn => {
    btn.addEventListener("click", () => {
      recipe.steps.splice(Number(btn.dataset.index), 1);
      renderRecipeEditor();
    });
  });
}

function addStep() {
  getRecipe().steps.push({ type: "hold", target: 100, duration: 30 });
  renderRecipeEditor();
}

function saveRecipe() {
  const recipe = getRecipe();
  recipe.name = $("recipeName").value.trim() || "Untitled Recipe";
  persistRecipes();
  populateRecipeSelect();
  renderRecipeEditor();
}

function newRecipe() {
  const r = {
    id: uid(),
    name: "Nuova ricetta",
    steps: [
      { type: "ramp", target: 80, rate: 2 },
      { type: "hold", target: 80, duration: 30 }
    ]
  };
  recipes.push(r);
  selectedRecipeId = r.id;
  persistRecipes();
  populateRecipeSelect();
  renderRecipeEditor();
}

function exportRecipes() {
  const blob = new Blob([JSON.stringify(recipes, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "oven-recipes.json";
  a.click();
  URL.revokeObjectURL(url);
}

function importRecipes(file) {
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const data = JSON.parse(reader.result);
      if (!Array.isArray(data) || !data.length) throw new Error();
      recipes = data;
      selectedRecipeId = recipes[0].id;
      persistRecipes();
      populateRecipeSelect();
      renderRecipeEditor();
      stopCycle();
    } catch {
      alert("JSON ricette non valido.");
    }
  };
  reader.readAsText(file);
}

function startCycle() {
  if (state.completed || !state.running) {
    state = createInitialState();
    state.running = true;
    state.stepStartTemp = state.actualCore;
  } else if (state.paused) {
    state.paused = false;
  }
  updateStatus();
}

function pauseCycle() {
  if (!state.running) return;
  state.paused = !state.paused;
  updateStatus();
}

function stopCycle() {
  state = createInitialState();
  updateStatus();
  updateUI();
}

function currentStep() {
  return getRecipe()?.steps[state.currentStep];
}

function advanceStep() {
  state.currentStep++;
  state.stepElapsedSec = 0;
  state.stepStartTemp = state.target;

  const recipe = getRecipe();
  if (!recipe || state.currentStep >= recipe.steps.length) {
    state.running = false;
    state.completed = true;
    state.power = 0;
    updateStatus();
  }
}

function calculateTargetAndStepDone(dt) {
  const step = currentStep();
  if (!step) return { target: state.target, done: true };

  const elapsedMin = state.stepElapsedSec / 60;
  const start = state.stepStartTemp;

  if (step.type === "hold") {
    return {
      target: step.target,
      done: state.stepElapsedSec >= step.duration * 60
    };
  }

  const rate = Math.max(0.01, step.rate || 1);
  const direction = step.target >= start ? 1 : -1;
  const next = start + direction * rate * elapsedMin;
  const reached = direction > 0 ? next >= step.target : next <= step.target;

  return {
    target: reached ? step.target : next,
    done: reached
  };
}

function simulateThermalPlant(dt) {
  const ambient = 25;
  const error = state.target - state.actualCore;

  // Simplified controller: proportional + feed-forward.
  let power = 0;
  if (error > 0) {
    power = Math.min(100, Math.max(0, error * 5.5 + 12));
  }

  // During cooldown we do not actively heat.
  if (currentStep()?.type === "cooldown") power = 0;

  state.power = power;

  // First-order oven model.
  const heatingRateAt100 = 0.075;  // °C/s
  const coolingCoeff = 0.00065;    // heat loss coefficient
  const heatIn = heatingRateAt100 * (power / 100);
  const heatOut = coolingCoeff * Math.max(0, state.actualCore - ambient);

  state.actualCore += (heatIn - heatOut) * dt;

  // Three sensors: spatial offsets + small deterministic pseudo-noise.
  const t = state.elapsedSec;
  const dynamicSpread = Math.min(3.5, Math.abs(error) * 0.025);
  state.sensors = [
    state.actualCore + 0.55 + dynamicSpread * 0.55 + Math.sin(t * 0.07) * 0.08,
    state.actualCore + Math.sin(t * 0.05 + 1.2) * 0.07,
    state.actualCore - 0.45 - dynamicSpread * 0.45 + Math.sin(t * 0.08 + 2.5) * 0.08
  ];
}

function simTick(realDtSec) {
  if (!state.running || state.paused) {
    updateUI();
    return;
  }

  const speed = Number($("speedSelect").value);
  const simDt = realDtSec * speed;

  // Break long accelerated intervals into small substeps.
  const sub = Math.max(1, Math.ceil(simDt / 0.5));
  const dt = simDt / sub;

  for (let i = 0; i < sub; i++) {
    state.elapsedSec += dt;
    state.stepElapsedSec += dt;

    const profile = calculateTargetAndStepDone(dt);
    state.target = Math.min(200, Math.max(0, profile.target));
    simulateThermalPlant(dt);

    if (profile.done) {
      advanceStep();
      if (!state.running) break;
    }
  }

  if (state.history.length === 0 ||
      state.elapsedSec - state.history[state.history.length - 1].t >= 10) {
    state.history.push({
      t: state.elapsedSec,
      target: state.target,
      actual: average(state.sensors)
    });
    if (state.history.length > 900) state.history.shift();
  }

  updateUI();
}

function average(arr) {
  return arr.reduce((a, b) => a + b, 0) / arr.length;
}

function formatTime(sec) {
  if (!Number.isFinite(sec)) return "—";
  sec = Math.max(0, Math.round(sec));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return [h, m, s].map(x => String(x).padStart(2, "0")).join(":");
}

function stepRemainingSeconds() {
  const step = currentStep();
  if (!step) return null;

  if (step.type === "hold") {
    return Math.max(0, step.duration * 60 - state.stepElapsedSec);
  }

  const distance = Math.abs(step.target - state.stepStartTemp);
  const total = distance / Math.max(0.01, step.rate) * 60;
  return Math.max(0, total - state.stepElapsedSec);
}

function stepLabel(step) {
  if (!step) return "—";
  if (step.type === "hold") return `Hold ${step.target}°C`;
  if (step.type === "cooldown") return `Cooldown → ${step.target}°C (${step.rate ?? 1} °C/min)`;
  return `Ramp → ${step.target}°C (${step.rate ?? 1} °C/min)`;
}

function updateStatus() {
  const dot = $("statusDot");
  dot.className = "status-dot";
  let text = "IDLE";
  if (state.completed) text = "COMPLETE";
  else if (state.running && state.paused) {
    text = "PAUSED"; dot.classList.add("paused");
  } else if (state.running) {
    text = "RUNNING"; dot.classList.add("running");
  }
  $("statusText").textContent = text;
  $("pauseBtn").textContent = state.paused ? "RIPRENDI" : "PAUSA";
}

function updateUI() {
  const avg = average(state.sensors);
  const minT = Math.min(...state.sensors);
  const maxT = Math.max(...state.sensors);
  const dT = maxT - minT;

  $("avgTemp").textContent = avg.toFixed(1);
  $("targetTemp").textContent = state.target.toFixed(1);
  $("power").textContent = Math.round(state.power);
  $("powerBar").style.width = `${Math.round(state.power)}%`;
  $("deltaTemp").textContent = dT.toFixed(1);
  $("t1").textContent = state.sensors[0].toFixed(1);
  $("t2").textContent = state.sensors[1].toFixed(1);
  $("t3").textContent = state.sensors[2].toFixed(1);
  $("elapsed").textContent = formatTime(state.elapsedSec);

  const recipe = getRecipe();
  $("stepCounter").textContent = recipe ? `${Math.min(state.currentStep + 1, recipe.steps.length)} / ${recipe.steps.length}` : "0 / 0";
  $("stepName").textContent = state.completed ? "Completato" : stepLabel(currentStep());
  $("stepRemaining").textContent = state.running ? formatTime(stepRemainingSeconds()) : "—";

  const alarm = $("alarmBox");
  if (dT > 10) {
    alarm.classList.remove("hidden");
    $("alarmText").textContent = `Differenza tra sensori elevata: ${dT.toFixed(1)} °C`;
    $("uniformityText").textContent = "Verificare uniformità";
  } else {
    alarm.classList.add("hidden");
    $("uniformityText").textContent = "Uniformità OK";
  }

  drawChart();
}

function drawChart() {
  const canvas = $("tempChart");
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = "#0d141c";
  ctx.fillRect(0, 0, W, H);

  const pad = { l: 60, r: 18, t: 18, b: 38 };
  const plotW = W - pad.l - pad.r;
  const plotH = H - pad.t - pad.b;
  const yMin = 20, yMax = 205;

  ctx.font = "18px system-ui";
  ctx.lineWidth = 1;

  // Grid
  for (let temp = 25; temp <= 200; temp += 25) {
    const y = pad.t + plotH - ((temp - yMin) / (yMax - yMin)) * plotH;
    ctx.strokeStyle = "#20303e";
    ctx.beginPath();
    ctx.moveTo(pad.l, y);
    ctx.lineTo(W - pad.r, y);
    ctx.stroke();
    ctx.fillStyle = "#74879a";
    ctx.fillText(`${temp}°`, 12, y + 6);
  }

  if (state.history.length < 2) return;

  const tMin = state.history[0].t;
  const tMax = Math.max(tMin + 60, state.history[state.history.length - 1].t);

  const x = t => pad.l + ((t - tMin) / (tMax - tMin)) * plotW;
  const y = temp => pad.t + plotH - ((temp - yMin) / (yMax - yMin)) * plotH;

  function line(key, color) {
    ctx.strokeStyle = color;
    ctx.lineWidth = 4;
    ctx.beginPath();
    state.history.forEach((p, i) => {
      const px = x(p.t), py = y(p[key]);
      if (i === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    });
    ctx.stroke();
  }

  line("target", "#4ba3ff");
  line("actual", "#36c58c");

  ctx.fillStyle = "#74879a";
  ctx.fillText(`${Math.round(tMin/60)} min`, pad.l, H - 10);
  const label = `${Math.round(tMax/60)} min`;
  ctx.fillText(label, W - pad.r - ctx.measureText(label).width, H - 10);
}

$("recipeSelect").addEventListener("change", e => {
  selectedRecipeId = e.target.value;
  renderRecipeEditor();
  stopCycle();
});

$("startBtn").addEventListener("click", startCycle);
$("pauseBtn").addEventListener("click", pauseCycle);
$("stopBtn").addEventListener("click", stopCycle);
$("addStepBtn").addEventListener("click", addStep);
$("saveRecipeBtn").addEventListener("click", saveRecipe);
$("newRecipeBtn").addEventListener("click", newRecipe);
$("exportBtn").addEventListener("click", exportRecipes);
$("importInput").addEventListener("change", e => {
  if (e.target.files[0]) importRecipes(e.target.files[0]);
});

populateRecipeSelect();
renderRecipeEditor();
updateStatus();
updateUI();

let last = performance.now();
setInterval(() => {
  const now = performance.now();
  const dt = (now - last) / 1000;
  last = now;
  simTick(Math.min(dt, 0.25));
}, 100);
