// SPDX-License-Identifier: GPL-3.0-only
(() => {
  "use strict";

  const PRAYER_KEYS = ["imsak", "subuh", "syuruq", "dzuhur", "ashar", "maghrib", "isya"];
  const PRAYER_NAMES = ["Imsak", "Subuh", "Syuruq", "Dzuhur", "Ashar", "Maghrib", "Isya"];
  // Hanya tampilkan 6 waktu sholat (keluarkan Syuruq / indeks 2)
  const DISPLAYED_PRAYER_INDICES = [0, 1, 3, 4, 5, 6];
  const IQOMAH_PRAYER_INDICES = [1, 3, 4, 5, 6]; // Subuh, Dzuhur, Ashar, Maghrib, Isya
  const IQOMAH_KEYS = ["subuh", "dzuhur", "ashar", "maghrib", "isya"];

  // Bitmask 32x32 Asli dari Twinkle (https://github.com/karbyt/twinkle — images/jws.png)
  const TWINKLE_FRAME_32 = [
    0xfffdbfff, 0xfffbdfff, 0xc0066003, 0xc00c3003, 0xc0181803, 0xc0300c03, 0xc0600603, 0xc0c00303,
    0xc1800183, 0xc30000c3, 0xc6000063, 0xcc000033, 0xd800001b, 0xb000000d, 0x60000006, 0xc0000003,
    0xc0000003, 0x60000006, 0xb000000d, 0xd800001b, 0xcc000033, 0xc6000063, 0xc30000c3, 0xc1800183,
    0xc0c00303, 0xc0600603, 0xc0300c03, 0xc0181803, 0xc00c3003, 0xc0066003, 0xfffbdfff, 0xfffdbfff
  ];
  const TWINKLE_CORNER_32 = [
    0x00000000, 0x00000000, 0x00000000, 0x1fe007f8, 0x10000008, 0x10000008, 0x10000008, 0x10000008,
    0x10000008, 0x10000008, 0x10000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000008,
    0x10000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000008, 0x10000008, 0x10000008,
    0x10000008, 0x10000008, 0x10000008, 0x10000008, 0x1fe007f8, 0x00000000, 0x00000000, 0x00000000
  ];
  const TWINKLE_MUHAMMAD_32 = [
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00200c00, 0x00301e00, 0x00301300, 0x00181e00, 0x00187000, 0x000cc1c0, 0x000dffe0,
    0x000c07f0, 0x01061e00, 0x01067800, 0x018fe000, 0x00fdc000, 0x0070e000, 0x00007c00, 0x00003800,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
  ];
  const TWINKLE_ALLAH_32 = [
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00008000,
    0x00018000, 0x00018000, 0x00018800, 0x00118d00, 0x00718d80, 0x01f18d80, 0x03f18d80, 0x073184c0,
    0x062186c0, 0x07e186c0, 0x03e186c0, 0x00618680, 0x0073ce00, 0x003e7c00, 0x001c3800, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
  ];

  const HIJRI_MONTHS = [
    "Muharram", "Safar", "Rabiul Awal", "Rabiul Akhir",
    "Jumadil Awal", "Jumadil Akhir", "Rajab", "Syaban",
    "Ramadhan", "Syawal", "Dzulqaidah", "Dzulhijjah"
  ];
  const PASARAN_NAMES = ["Legi", "Pahing", "Pon", "Wage", "Kliwon"];
  const MOON_PHASES = [
    "NEW MOON", "WAXING CRESC", "FIRST QUARTER", "WAXING GIBB",
    "FULL MOON", "WANING GIBB", "LAST QUARTER", "WANING CRESC"
  ];

  // Jadwal dasar Kota Kotamobagu (WITA / UTC+8) — November 2026
  const KOTAMOBAGU_NOV_2026 = {
    "2026-11-01": [237, 247, 320, 690, 889, 1052, 1122],
    "2026-11-02": [237, 247, 320, 690, 889, 1052, 1123],
    "2026-11-03": [237, 247, 320, 690, 889, 1052, 1123],
    "2026-11-04": [237, 247, 320, 690, 890, 1052, 1123],
    "2026-11-05": [236, 246, 320, 690, 890, 1052, 1123],
    "2026-11-06": [236, 246, 321, 690, 890, 1052, 1123],
    "2026-11-07": [236, 246, 321, 690, 890, 1052, 1123],
    "2026-11-08": [236, 246, 321, 690, 891, 1052, 1123],
    "2026-11-09": [236, 246, 321, 690, 891, 1052, 1123],
    "2026-11-10": [236, 246, 321, 690, 891, 1052, 1124],
    "2026-11-11": [236, 246, 321, 690, 892, 1053, 1124],
    "2026-11-12": [236, 246, 321, 690, 892, 1053, 1124],
    "2026-11-13": [236, 246, 321, 690, 892, 1053, 1124],
    "2026-11-14": [236, 246, 321, 691, 892, 1053, 1125],
    "2026-11-15": [236, 246, 321, 691, 893, 1053, 1125]
  };

  const params = new URLSearchParams(window.location.search);
  const forcedDemo = params.get("demo") === "1" || window.location.protocol === "file:";

  function defaultCfg() {
    return {
      mosqueName: "Nama Masjid",
      runningText: "Selamat datang. Mari menjaga ketenangan dan kebersihan masjid.",
      runningTextEnabled: true,
      scrollIntervalMs: 80,
      panelCount: 2,
      panelRows: 1,
      calligraphyEnabled: true,
      brightness: 60,
      hijriOffsetDays: 0,
      adzanDurationMinutes: 4,
      iqomahMinutes: [12, 10, 10, 7, 10],
      khatibName: "Ust. H. Ahmad Fauzi",
      imamName: "Ust. Abdurrahman",
      muadzinName: "Sdr. Rizky Pratama",
      bilalName: "Sdr. Hidayatullah",
      khotbahEnabled: true,
      qrEnabled: true,
      qrText: "https://masjid-kotamobagu.id/infaq",
      autoSleepEnabled: false,
      sleepHour: 23,
      wakeHour: 3,
      audioVolume: 20,
      buzzerEnabled: true,
      tarhimOffsetMinutes: -6,
      corrections: [0, 0, 0, 0, 0, 0, 0]
    };
  }

  const state = {
    connected: false,
    csrfToken: "",
    revision: 1,
    activeScene: "prayer",
    savedConfig: defaultCfg(),
    draftConfig: defaultCfg(),
    baseMinutes: [236, 246, 321, 690, 890, 1052, 1123],
    clock: {
      valid: true,
      date: "2026-11-07",
      time: "12:23:28"
    },
    singlePanelPage: 0,
    iqomahDemoSec: 585,
    tickerX: 320,
    lastTickerFrameMs: performance.now()
  };

  function cloneConfig(c) {
    return {
      ...c,
      iqomahMinutes: [...(c.iqomahMinutes || [12, 10, 10, 7, 10])],
      corrections: [...(c.corrections || [0, 0, 0, 0, 0, 0, 0])]
    };
  }

  function pad2(n) {
    return String(n).padStart(2, "0");
  }

  function formatMinute(m) {
    if (typeof m !== "number" || m < 0 || m >= 1440) return "--:--";
    const hh24 = Math.floor(m / 60);
    const mm = m % 60;
    const hh12 = (hh24 % 12 === 0) ? 12 : (hh24 % 12);
    const ampm = hh24 >= 12 ? "PM" : "AM";
    return `${pad2(hh12)}:${pad2(mm)} ${ampm}`;
  }

  function formatClock12(timeStr) {
    const parts = (timeStr || "").split(":").map(Number);
    if (parts.length !== 3 || parts.some(isNaN)) return timeStr || "--:--:--";
    const hh24 = parts[0];
    const hh12 = (hh24 % 12 === 0) ? 12 : (hh24 % 12);
    const ampm = hh24 >= 12 ? "PM" : "AM";
    return `${pad2(hh12)}:${pad2(parts[1])}:${pad2(parts[2])} ${ampm}`;
  }

  function julianDayFromDateStr(dateStr) {
    const parts = (dateStr || "").split("-").map(Number);
    if (parts.length !== 3 || parts.some(isNaN)) return 2461352;
    let [y, m, d] = parts;
    if (m <= 2) {
      y -= 1;
      m += 12;
    }
    const a = Math.floor(y / 100);
    const b = 2 - a + Math.floor(a / 4);
    return Math.floor(365.25 * (y + 4716)) + Math.floor(30.6001 * (m + 1)) + d + b - 1524;
  }

  function computeHijriPasaranMoon(dateStr, offsetDays) {
    const jd = julianDayFromDateStr(dateStr) + (Number(offsetDays) || 0);
    let l = jd - 1948440 + 10632;
    const n = Math.floor((l - 1) / 10631);
    l = l - 10631 * n + 354;
    const j =
      Math.floor((10985 - l) / 5316) * Math.floor((50 * l) / 17719) +
      Math.floor(l / 5670) * Math.floor((43 * l) / 15238);
    l =
      l -
      Math.floor((30 - j) / 15) * Math.floor((17719 * j) / 50) -
      Math.floor(j / 16) * Math.floor((15238 * j) / 43) +
      29;
    const hMonth = Math.floor((24 * l) / 709);
    const hDay = l - Math.floor((709 * hMonth) / 24);
    const hYear = 30 * n + j - 30;
    const hijriStr = `${hDay} ${HIJRI_MONTHS[Math.max(0, Math.min(11, hMonth - 1))]} ${hYear}H`;

    const baseJd = julianDayFromDateStr(dateStr);
    const pasaranIdx = ((baseJd - 2461351) % 5 + 5) % 5;
    const pasaran = PASARAN_NAMES[pasaranIdx];

    const synodic = 29.530588853;
    const rawAge = (baseJd - 2451550.1) % synodic;
    const age = rawAge < 0 ? rawAge + synodic : rawAge;
    const moonIdx = Math.floor((age / synodic) * 8 + 0.5) % 8;
    const moonPhase = MOON_PHASES[moonIdx];

    return { hijriStr, pasaran, moonPhase };
  }

  function getWitaNow() {
    const now = new Date();
    const utcMs = now.getTime() + now.getTimezoneOffset() * 60000;
    const wita = new Date(utcMs + 8 * 3600000);
    const yyyy = wita.getFullYear();
    const mm = pad2(wita.getMonth() + 1);
    const dd = pad2(wita.getDate());
    return {
      dateStr: `${yyyy}-${mm}-${dd}`,
      timeStr: `${pad2(wita.getHours())}:${pad2(wita.getMinutes())}:${pad2(wita.getSeconds())}`,
      totalSeconds: wita.getHours() * 3600 + wita.getMinutes() * 60 + wita.getSeconds()
    };
  }

  function formatIndonesianDate(dateStr) {
    const parts = (dateStr || "").split("-").map(Number);
    if (parts.length !== 3 || parts.some(isNaN)) return dateStr || "--";
    const dt = new Date(Date.UTC(parts[0], parts[1] - 1, parts[2]));
    return dt.toLocaleDateString("id-ID", {
      weekday: "long",
      day: "numeric",
      month: "long",
      year: "numeric",
      timeZone: "UTC"
    });
  }

  function getWeekdayUpper(dateStr) {
    const parts = (dateStr || "").split("-").map(Number);
    if (parts.length !== 3 || parts.some(isNaN)) return "SABTU";
    const dt = new Date(Date.UTC(parts[0], parts[1] - 1, parts[2]));
    return dt.toLocaleDateString("id-ID", { weekday: "long", timeZone: "UTC" }).toUpperCase();
  }

  function isPrintableAscii(str, allowEmpty, maxLen) {
    if (typeof str !== "string") return false;
    if (!allowEmpty && str.length === 0) return false;
    if (str.length > maxLen) return false;
    for (let i = 0; i < str.length; i++) {
      const code = str.charCodeAt(i);
      if (code < 32 || code > 126) return false;
    }
    return true;
  }

  function computeFinalMinutes(baseMinutes, corrections) {
    const finals = [];
    let validOrder = true;
    let errorMsg = "";
    for (let i = 0; i < PRAYER_KEYS.length; i++) {
      const base = baseMinutes[i];
      const corr = Number(corrections[i]) || 0;
      if (corr < -30 || corr > 30) {
        validOrder = false;
        errorMsg = `Revisi ${PRAYER_NAMES[i]} (${corr} menit) di luar batas -30..+30 menit.`;
      }
      const val = base >= 0 ? base + corr : -1;
      finals.push(val);
      if (base >= 0) {
        if (val < 0 || val >= 1440) {
          validOrder = false;
          errorMsg = `Waktu ${PRAYER_NAMES[i]} keluar dari rentang 00:00..23:59.`;
        } else if (i > 0 && val <= finals[i - 1]) {
          validOrder = false;
          errorMsg = `Urutan tidak valid: ${PRAYER_NAMES[i]} (${formatMinute(val)}) harus setelah ${PRAYER_NAMES[i - 1]} (${formatMinute(finals[i - 1])}).`;
        }
      }
    }
    return { finals, validOrder, errorMsg };
  }

  function isDirty() {
    return JSON.stringify(state.savedConfig) !== JSON.stringify(state.draftConfig);
  }

  function showAlert(message, type = "info") {
    const banner = document.getElementById("alertBanner");
    banner.textContent = message;
    banner.className = `alert alert-${type}`;
  }

  function drawTwinkleCalligraphyCanvases() {
    const pairs = [
      { id: "p5CalMuhammadCanvas", glyph: TWINKLE_MUHAMMAD_32 },
      { id: "p5CalAllahCanvas", glyph: TWINKLE_ALLAH_32 }
    ];
    for (const item of pairs) {
      const canvas = document.getElementById(item.id);
      if (!canvas) continue;
      const ctx = canvas.getContext("2d");
      ctx.fillStyle = "#030508";
      ctx.fillRect(0, 0, 32, 32);

      for (let r = 0; r < 32; r++) {
        const fRow = TWINKLE_FRAME_32[r] >>> 0;
        const cRow = TWINKLE_CORNER_32[r] >>> 0;
        const gRow = item.glyph[r] >>> 0;
        for (let col = 0; col < 32; col++) {
          const mask = (1 << (31 - col)) >>> 0;
          if ((gRow & mask) !== 0) {
            // Kaligrafi Arab Asli Twinkle -> Radiant Gold (#fde047)
            ctx.fillStyle = "#fde047";
          } else if ((cRow & mask) !== 0) {
            // Siku Sudut Dalam Twinkle -> Luminous Emerald (#10b981)
            ctx.fillStyle = "#10b981";
          } else if ((fRow & mask) !== 0) {
            // Bingkai Kotak & Belah Ketupat Twinkle -> Royal Cyan (#00e5ff)
            ctx.fillStyle = "#00e5ff";
          } else {
            // Titik Matriks LED Padam
            ctx.fillStyle = "#0b1320";
          }
          ctx.fillRect(col, r, 1, 1);
        }
      }
    }
  }

  function buildPrayerRows() {
    const tbody = document.getElementById("prayerTableBody");
    tbody.textContent = "";

    // Hanya tampilkan 6 waktu sholat (Imsak, Subuh, Dzuhur, Ashar, Maghrib, Isya — tanpa Syuruq)
    for (const i of DISPLAYED_PRAYER_INDICES) {
      const tr = document.createElement("tr");

      const tdName = document.createElement("td");
      tdName.className = "prayer-name-cell";
      tdName.textContent = PRAYER_NAMES[i];

      const tdBase = document.createElement("td");
      tdBase.className = "time-mono";
      tdBase.id = `baseTime-${i}`;
      tdBase.textContent = formatMinute(state.baseMinutes[i]);

      const tdCorr = document.createElement("td");
      const group = document.createElement("div");
      group.className = "stepper-group";

      const btnMinus = document.createElement("button");
      btnMinus.type = "button";
      btnMinus.className = "stepper-btn";
      btnMinus.textContent = "-";
      btnMinus.setAttribute("aria-label", `Kurangi 1 menit ${PRAYER_NAMES[i]}`);
      btnMinus.addEventListener("click", () => adjustCorrection(i, -1));

      const input = document.createElement("input");
      input.type = "number";
      input.className = "correction-input";
      input.id = `corrInput-${i}`;
      input.min = "-30";
      input.max = "30";
      input.step = "1";
      input.value = String(state.draftConfig.corrections[i]);
      input.setAttribute("aria-label", `Revisi menit ${PRAYER_NAMES[i]}`);
      input.addEventListener("input", (e) => {
        const parsed = parseInt(e.target.value, 10);
        state.draftConfig.corrections[i] = Number.isNaN(parsed) ? 0 : parsed;
        renderScheduleAndPreview();
      });

      const btnPlus = document.createElement("button");
      btnPlus.type = "button";
      btnPlus.className = "stepper-btn";
      btnPlus.textContent = "+";
      btnPlus.setAttribute("aria-label", `Tambah 1 menit ${PRAYER_NAMES[i]}`);
      btnPlus.addEventListener("click", () => adjustCorrection(i, 1));

      group.appendChild(btnMinus);
      group.appendChild(input);
      group.appendChild(btnPlus);
      tdCorr.appendChild(group);

      const tdFinal = document.createElement("td");
      const finalSpan = document.createElement("span");
      finalSpan.className = "time-final";
      finalSpan.id = `finalTime-${i}`;
      const deltaSpan = document.createElement("span");
      deltaSpan.className = "delta-badge delta-zero";
      deltaSpan.id = `deltaBadge-${i}`;
      tdFinal.appendChild(finalSpan);
      tdFinal.appendChild(deltaSpan);

      const tdIqomah = document.createElement("td");
      const iqIdx = IQOMAH_PRAYER_INDICES.indexOf(i);
      if (iqIdx >= 0) {
        const iqInput = document.createElement("input");
        iqInput.type = "number";
        iqInput.className = "iqomah-input";
        iqInput.id = `iqomahInput-${iqIdx}`;
        iqInput.min = "1";
        iqInput.max = "30";
        iqInput.value = String(state.draftConfig.iqomahMinutes[iqIdx] || 10);
        iqInput.setAttribute("aria-label", `Iqomah ${PRAYER_NAMES[i]} menit`);
        iqInput.addEventListener("input", (e) => {
          const v = Math.max(1, Math.min(30, parseInt(e.target.value, 10) || 10));
          state.draftConfig.iqomahMinutes[iqIdx] = v;
          renderScheduleAndPreview();
        });
        tdIqomah.appendChild(iqInput);
      } else {
        tdIqomah.className = "time-mono";
        tdIqomah.textContent = "-";
      }

      tr.appendChild(tdName);
      tr.appendChild(tdBase);
      tr.appendChild(tdCorr);
      tr.appendChild(tdFinal);
      tr.appendChild(tdIqomah);
      tbody.appendChild(tr);
    }
  }

  function buildPreviewGrids() {
    // Grid TFT 8 kotak: 0..5 = 6 Waktu Sholat (Tanpa Syuruq), 6 = HARI, 7 = MENUJU
    const tftGrid = document.getElementById("tftScheduleGrid");
    tftGrid.textContent = "";
    for (let slot = 0; slot < 8; slot++) {
      const cell = document.createElement("div");
      cell.className = `tft-cell ${slot === 0 ? "muted" : ""}`;
      const label = document.createElement("div");
      label.className = "tft-cell-label";
      label.id = `tftCellLabel-${slot}`;
      if (slot < DISPLAYED_PRAYER_INDICES.length) {
        label.textContent = PRAYER_NAMES[DISPLAYED_PRAYER_INDICES[slot]];
      } else if (slot === 6) {
        label.textContent = "HARI";
      } else {
        label.textContent = "MENUJU";
      }
      const val = document.createElement("div");
      val.className = slot === 7 ? "tft-cell-value tft-cell-countdown" : "tft-cell-value";
      val.id = `tftCellVal-${slot}`;
      val.textContent = slot < 6 ? "--:--" : slot === 6 ? "SABTU" : "--:--:--";
      cell.appendChild(label);
      cell.appendChild(val);
      tftGrid.appendChild(cell);
    }

    // Grid Panel P5 RGB: Hanya 6 Waktu Sholat (Imsak, Subuh, Dzuhur, Ashar, Maghrib, Isya — Tanpa Syuruq)
    const p5Bar = document.getElementById("p5ScheduleBar");
    p5Bar.textContent = "";
    for (let slot = 0; slot < DISPLAYED_PRAYER_INDICES.length; slot++) {
      const pIdx = DISPLAYED_PRAYER_INDICES[slot];
      const cell = document.createElement("div");
      cell.className = "p5-cell";
      cell.id = `p5CellBox-${slot}`;
      cell.setAttribute("data-prayer-idx", String(pIdx));
      const label = document.createElement("div");
      label.className = "p5-cell-label";
      label.textContent = PRAYER_NAMES[pIdx].toUpperCase();
      const val = document.createElement("div");
      val.className = "p5-cell-val";
      val.id = `p5CellVal-${slot}`;
      val.textContent = "--:--";
      cell.appendChild(label);
      cell.appendChild(val);
      p5Bar.appendChild(cell);
    }

    drawTwinkleCalligraphyCanvases();
  }

  function drawQrCanvas(text) {
    const canvas = document.getElementById("p5QrCanvas");
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const size = 21;
    const scale = 3;
    ctx.fillStyle = "#030508";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#ffffff";

    function drawFinder(fx, fy) {
      for (let r = 0; r < 7; r++) {
        for (let c = 0; c < 7; c++) {
          const border = r === 0 || r === 6 || c === 0 || c === 6;
          const inner = r >= 2 && r <= 4 && c >= 2 && c <= 4;
          if (border || inner) {
            ctx.fillRect((fx + c) * scale, (fy + r) * scale, scale, scale);
          }
        }
      }
    }
    drawFinder(0, 0);
    drawFinder(14, 0);
    drawFinder(0, 14);

    let hash = 2166136261;
    const str = text || "https://masjid-kotamobagu.id/infaq";
    for (let i = 0; i < str.length; i++) {
      hash ^= str.charCodeAt(i);
      hash = Math.imul(hash, 16777619);
    }

    for (let r = 0; r < size; r++) {
      for (let c = 0; c < size; c++) {
        const inFinder = (r < 8 && c < 8) || (r < 8 && c > 12) || (r > 12 && c < 8);
        if (inFinder) continue;
        if (r === 6 || c === 6) {
          if ((r + c) % 2 === 0) ctx.fillRect(c * scale, r * scale, scale, scale);
          continue;
        }
        const seed = (hash ^ (r * 31 + c * 17)) >>> 0;
        if (((seed ^ (seed >>> 3)) & 1) === 1) {
          ctx.fillRect(c * scale, r * scale, scale, scale);
        }
      }
    }
  }

  function adjustCorrection(index, delta) {
    const current = Number(state.draftConfig.corrections[index]) || 0;
    const next = Math.max(-30, Math.min(30, current + delta));
    state.draftConfig.corrections[index] = next;
    const input = document.getElementById(`corrInput-${index}`);
    if (input) input.value = String(next);
    renderScheduleAndPreview();
  }

  function syncInputsFromDraft() {
    const d = state.draftConfig;
    document.getElementById("mosqueNameInput").value = d.mosqueName;
    document.getElementById("runningTextInput").value = d.runningText;
    document.getElementById("runningTextEnabledInput").checked = Boolean(d.runningTextEnabled);
    document.getElementById("scrollIntervalInput").value = String(d.scrollIntervalMs);
    document.getElementById("scrollIntervalValue").textContent = `${d.scrollIntervalMs} ms`;
    document.getElementById("panelCountSelect").value = String(d.panelCount || 2);
    document.getElementById("panelRowsSelect").value = String(d.panelRows || 1);
    document.getElementById("brightnessInput").value = String(d.brightness || 60);
    document.getElementById("brightnessValue").textContent = `${d.brightness || 60}%`;
    document.getElementById("calligraphyEnabledInput").checked = Boolean(d.calligraphyEnabled);
    document.getElementById("quickCalligraphyToggle").checked = Boolean(d.calligraphyEnabled);
    document.getElementById("hijriOffsetInput").value = String(d.hijriOffsetDays || 0);
    document.getElementById("adzanDurationInput").value = String(d.adzanDurationMinutes || 4);
    document.getElementById("khatibInput").value = d.khatibName || "";
    document.getElementById("imamInput").value = d.imamName || "";
    document.getElementById("muadzinInput").value = d.muadzinName || "";
    document.getElementById("bilalInput").value = d.bilalName || "";
    const khotbahEl = document.getElementById("khotbahEnabledInput");
    if (khotbahEl) khotbahEl.checked = d.khotbahEnabled !== undefined ? Boolean(d.khotbahEnabled) : true;
    document.getElementById("qrEnabledInput").checked = Boolean(d.qrEnabled);
    document.getElementById("qrTextInput").value = d.qrText || "";
    document.getElementById("autoSleepEnabledInput").checked = Boolean(d.autoSleepEnabled);
    document.getElementById("sleepHourInput").value = String(d.sleepHour ?? 23);
    document.getElementById("wakeHourInput").value = String(d.wakeHour ?? 3);
    document.getElementById("audioVolumeInput").value = String(d.audioVolume ?? 20);
    document.getElementById("buzzerEnabledInput").checked = Boolean(d.buzzerEnabled);
    document.getElementById("tarhimOffsetInput").value = String(d.tarhimOffsetMinutes ?? -6);

    document.querySelectorAll(".pill-btn").forEach((btn) => {
      btn.classList.toggle("active", Number(btn.getAttribute("data-panels")) === Number(d.panelCount));
    });
    document.querySelectorAll(".row-btn").forEach((btn) => {
      btn.classList.toggle("active", Number(btn.getAttribute("data-rows")) === Number(d.panelRows));
    });

    for (const i of DISPLAYED_PRAYER_INDICES) {
      const el = document.getElementById(`corrInput-${i}`);
      if (el) el.value = String(d.corrections[i]);
    }
    for (let i = 0; i < 5; i++) {
      const iqEl = document.getElementById(`iqomahInput-${i}`);
      if (iqEl) iqEl.value = String(d.iqomahMinutes[i] || 10);
    }
    updateCharCounts();
  }

  function updateCharCounts() {
    document.getElementById("mosqueNameCount").textContent = `${state.draftConfig.mosqueName.length}/64`;
    document.getElementById("runningTextCount").textContent = `${state.draftConfig.runningText.length}/256`;
  }

  function renderMultiPanelAndCalligraphy() {
    const d = state.draftConfig;
    const panels = Math.max(1, Math.min(4, Number(d.panelCount) || 2));
    const rows = Math.max(1, Math.min(2, Number(d.panelRows) || 1));
    const showCal = Boolean(d.calligraphyEnabled);

    const p5Screen = document.getElementById("p5Screen");
    p5Screen.className = `p5-screen panels-${panels} rows-${rows}`;
    const bri = Math.max(5, Math.min(100, Number(d.brightness) || 60));
    p5Screen.style.opacity = String(0.45 + (bri / 100) * 0.55);

    const pxWidth = panels * 64;
    const pxHeight = rows * 32;
    const cmWidth = panels * 32;
    const cmHeight = rows * 16;
    const resInfo = document.getElementById("previewResolutionInfo");
    if (resInfo) {
      resInfo.textContent = `MODUL HD-WF2 \u2022 ${panels}\u00d7${rows} PANEL P5 (${pxWidth}\u00d7${pxHeight} PX / ${cmWidth}\u00d7${cmHeight} CM)`;
    }

    const calAllah = document.getElementById("p5CalligraphyAllah");
    const calMuhammad = document.getElementById("p5CalligraphyMuhammad");
    const tftCalAllah = document.getElementById("tftCalAllah");
    const tftCalMuhammad = document.getElementById("tftCalMuhammad");

    if (calAllah) calAllah.classList.toggle("hidden", !showCal);
    if (calMuhammad) calMuhammad.classList.toggle("hidden", !showCal);
    if (tftCalAllah) tftCalAllah.classList.toggle("hidden", !showCal);
    if (tftCalMuhammad) tftCalMuhammad.classList.toggle("hidden", !showCal);

    // Toggle scenes
    document.getElementById("p5ScenePrayer").classList.toggle("hidden", state.activeScene !== "prayer");
    document.getElementById("p5SceneIqomah").classList.toggle("hidden", state.activeScene !== "iqomah");
    document.getElementById("p5SceneKhotbah").classList.toggle("hidden", state.activeScene !== "khotbah");
    document.getElementById("p5SceneQr").classList.toggle("hidden", state.activeScene !== "qr");

    // Populate Khotbah & QR scenes
    document.getElementById("p5KhatibText").textContent = d.khatibName || "-";
    document.getElementById("p5ImamText").textContent = d.imamName || "-";
    document.getElementById("p5MuadzinText").textContent = d.muadzinName || "-";
    document.getElementById("p5BilalText").textContent = d.bilalName || "-";
    document.getElementById("p5QrMosque").textContent = d.mosqueName || "Nama Masjid";
    document.getElementById("p5QrUrlText").textContent = d.qrText || "https://masjid-kotamobagu.id/infaq";
    drawQrCanvas(d.qrText);

    // For 1 Panel (64x32), rotate pairs of the 6 prayer boxes (3 pages x 2 prayers) so text stays large & readable
    for (let slot = 0; slot < DISPLAYED_PRAYER_INDICES.length; slot++) {
      const box = document.getElementById(`p5CellBox-${slot}`);
      if (!box) continue;
      if (panels === 1) {
        const pairStart = (state.singlePanelPage % 3) * 2;
        const visible = slot === pairStart || slot === pairStart + 1;
        box.classList.toggle("hidden", !visible);
      } else {
        box.classList.remove("hidden");
      }
    }
  }

  function renderScheduleAndPreview() {
    renderMultiPanelAndCalligraphy();

    // Pastikan Syuruq (indeks 2) selalu 0 agar tidak mengganggu urutan 6 waktu sholat
    state.draftConfig.corrections[2] = 0;

    const { finals, validOrder, errorMsg } = computeFinalMinutes(
      state.baseMinutes,
      state.draftConfig.corrections
    );

    const errBox = document.getElementById("orderValidationError");
    if (!validOrder) {
      errBox.textContent = errorMsg;
      errBox.classList.remove("hidden");
    } else {
      errBox.textContent = "";
      errBox.classList.add("hidden");
    }

    // Update 6 Waktu Sholat pada Panel P5, TFT, dan Tabel Revisi
    for (let slot = 0; slot < DISPLAYED_PRAYER_INDICES.length; slot++) {
      const pIdx = DISPLAYED_PRAYER_INDICES[slot];
      const formatted = state.clock.valid && validOrder ? formatMinute(finals[pIdx]) : "--:--";

      const p5Val = document.getElementById(`p5CellVal-${slot}`);
      if (p5Val) p5Val.textContent = formatted;

      const tftVal = document.getElementById(`tftCellVal-${slot}`);
      if (tftVal) tftVal.textContent = formatted;

      const baseEl = document.getElementById(`baseTime-${pIdx}`);
      const finalEl = document.getElementById(`finalTime-${pIdx}`);
      const deltaEl = document.getElementById(`deltaBadge-${pIdx}`);
      if (baseEl) baseEl.textContent = formatMinute(state.baseMinutes[pIdx]);
      if (finalEl) finalEl.textContent = validOrder ? formatMinute(finals[pIdx]) : "--:--";

      const corr = Number(state.draftConfig.corrections[pIdx]) || 0;
      if (deltaEl) {
        if (corr === 0) {
          deltaEl.textContent = "";
          deltaEl.className = "delta-badge delta-zero";
        } else if (corr > 0) {
          deltaEl.textContent = `+${corr}m`;
          deltaEl.className = "delta-badge delta-pos";
        } else {
          deltaEl.textContent = `${corr}m`;
          deltaEl.className = "delta-badge delta-neg";
        }
      }
    }

    const rawTitle = state.draftConfig.mosqueName || "Nama Masjid";
    document.getElementById("tftMosqueTitle").textContent =
      rawTitle.length <= 40 ? rawTitle : `${rawTitle.slice(0, 37)}...`;
    document.getElementById("p5MosqueTitle").textContent = rawTitle;

    const tickerActive = state.draftConfig.runningTextEnabled && state.draftConfig.runningText.length > 0;
    const tftTicker = document.getElementById("tftTickerText");
    const p5Ticker = document.getElementById("p5TickerText");
    if (tickerActive) {
      tftTicker.textContent = state.draftConfig.runningText;
      p5Ticker.textContent = state.draftConfig.runningText;
      tftTicker.style.visibility = "visible";
      p5Ticker.style.visibility = "visible";
    } else {
      tftTicker.style.visibility = "hidden";
      p5Ticker.style.visibility = "hidden";
    }

    const dirty = isDirty();
    const dot = document.getElementById("unsavedIndicator");
    const unsavedText = document.getElementById("unsavedText");
    const btnRevert = document.getElementById("btnRevertChanges");
    const btnSave = document.getElementById("btnSaveConfig");

    if (dirty) {
      dot.className = "status-dot status-dirty";
      unsavedText.textContent = "Ada perubahan pengaturan yang belum disimpan.";
      btnRevert.disabled = false;
    } else {
      dot.className = "status-dot status-clean";
      unsavedText.textContent = "Semua perubahan sudah tersimpan.";
      btnRevert.disabled = true;
    }

    btnSave.disabled = !validOrder;
    updateNextPrayerCountdown(finals, validOrder);
  }

  function parseClockSeconds(timeStr) {
    const parts = (timeStr || "").split(":").map(Number);
    if (parts.length !== 3 || parts.some(isNaN)) return null;
    return parts[0] * 3600 + parts[1] * 60 + parts[2];
  }

  function updateNextPrayerCountdown(finals, validOrder) {
    const parsedClockSec = state.clock.valid ? parseClockSeconds(state.clock.time) : null;
    const nowSec = parsedClockSec !== null ? parsedClockSec : getWitaNow().totalSeconds;
    const nextNameEl = document.getElementById("nextPrayerName");
    const nextTimeEl = document.getElementById("nextPrayerTime");
    const countdownEl = document.getElementById("nextPrayerCountdown");
    const tftLabel7 = document.getElementById("tftCellLabel-7");
    const tftVal7 = document.getElementById("tftCellVal-7");
    const p5NextLabel = document.getElementById("p5NextLabel");
    const p5NextCd = document.getElementById("p5NextCountdown");

    if (!state.clock.valid || !validOrder || finals.some((m) => m < 0)) {
      nextNameEl.textContent = "--";
      nextTimeEl.textContent = "--:--";
      countdownEl.textContent = "--:--:--";
      if (tftLabel7) tftLabel7.textContent = "MENUJU";
      if (tftVal7) tftVal7.textContent = "--:--:--";
      if (p5NextLabel) p5NextLabel.textContent = "MENUJU";
      if (p5NextCd) p5NextCd.textContent = "--:--:--";
      return;
    }

    const obligatoryIndices = [1, 3, 4, 5, 6];
    let chosenIdx = -1;
    let iqomahSlot = 1;
    for (let s = 0; s < obligatoryIndices.length; s++) {
      const idx = obligatoryIndices[s];
      if (finals[idx] * 60 > nowSec) {
        chosenIdx = idx;
        iqomahSlot = s;
        break;
      }
    }

    let diffSec = 0;
    if (chosenIdx >= 0) {
      diffSec = finals[chosenIdx] * 60 - nowSec;
    } else {
      chosenIdx = 1;
      iqomahSlot = 0;
      diffSec = 86400 - nowSec + finals[1] * 60;
    }

    // Highlight active next prayer box on P5 panel
    for (let slot = 0; slot < DISPLAYED_PRAYER_INDICES.length; slot++) {
      const box = document.getElementById(`p5CellBox-${slot}`);
      if (box) {
        box.classList.toggle("next-active", DISPLAYED_PRAYER_INDICES[slot] === chosenIdx);
      }
    }

    const hh = Math.floor(diffSec / 3600);
    const mm = Math.floor((diffSec % 3600) / 60);
    const ss = diffSec % 60;
    const cdText = `-${pad2(hh)}:${pad2(mm)}:${pad2(ss)}`;
    const targetName = PRAYER_NAMES[chosenIdx];

    nextNameEl.textContent = targetName;
    nextTimeEl.textContent = formatMinute(finals[chosenIdx]);
    countdownEl.textContent = cdText;
    if (tftLabel7) tftLabel7.textContent = `>${targetName}`;
    if (tftVal7) tftVal7.textContent = cdText;
    if (p5NextLabel) p5NextLabel.textContent = `>${targetName}`;
    if (p5NextCd) p5NextCd.textContent = cdText;

    // Update Iqomah Scene preview timer using configured iqomahMinutes
    const iqMin = Number(state.draftConfig.iqomahMinutes[iqomahSlot]) || 10;
    const totalIqSec = iqMin * 60;
    const remIqSec = totalIqSec - (nowSec % totalIqSec);
    document.getElementById("p5IqomahPrayerName").textContent = targetName.toUpperCase();
    document.getElementById("p5IqomahTimer").textContent =
      `-${pad2(Math.floor(remIqSec / 60))}:${pad2(remIqSec % 60)}`;
  }

  function renderHeaderAndClock() {
    const connBadge = document.getElementById("connectionBadge");
    if (state.connected) {
      connBadge.textContent = "Terhubung";
      connBadge.className = "badge badge-online";
    } else {
      connBadge.textContent = "Mode Simulasi";
      connBadge.className = "badge badge-demo";
    }

    const { hijriStr, pasaran, moonPhase } = computeHijriPasaranMoon(
      state.clock.date,
      state.draftConfig.hijriOffsetDays
    );
    const dayUpper = getWeekdayUpper(state.clock.date);
    const dayTitle = dayUpper.charAt(0) + dayUpper.slice(1).toLowerCase();

    const clock12 = formatClock12(state.clock.time);
    document.getElementById("liveClockText").textContent = clock12;
    document.getElementById("currentDateText").textContent = formatIndonesianDate(state.clock.date);
    document.getElementById("currentHijriText").textContent = hijriStr;
    document.getElementById("pasaranMoonText").textContent = `${dayUpper} ${pasaran.toUpperCase()} \u2022 ${moonPhase}`;

    const dayBadge = document.getElementById("nextPrayerDayBadge");
    if (dayBadge) dayBadge.textContent = `${dayUpper} ${pasaran.toUpperCase()}`;

    document.getElementById("tftClock").textContent = clock12;
    document.getElementById("tftDate").textContent = `${dayTitle}, ${state.clock.date} \u2022 ${hijriStr}`;
    const tftDayCell = document.getElementById("tftCellVal-6");
    if (tftDayCell) tftDayCell.textContent = dayUpper;

    document.getElementById("p5Clock").textContent = clock12;
    document.getElementById("p5Date").textContent = state.clock.date;
    document.getElementById("p5DayName").textContent = dayUpper;
    const p5DayInline = document.getElementById("p5DayInline");
    if (p5DayInline) p5DayInline.textContent = dayUpper;
    document.getElementById("p5PasaranMoon").textContent = pasaran.toUpperCase();
    document.getElementById("p5HijriLine").textContent = `${hijriStr} \u2022 ${moonPhase}`;
  }

  async function fetchDeviceData() {
    state.singlePanelPage = Math.floor(Date.now() / 3000) % 4;
    if (forcedDemo) {
      tickLocalDemoClock();
      return;
    }
    try {
      const [cfgRes, statusRes] = await Promise.all([
        fetch("/api/v1/config", { cache: "no-store" }),
        fetch("/api/v1/status", { cache: "no-store" })
      ]);
      if (!cfgRes.ok || !statusRes.ok) throw new Error("HTTP error");
      const cfg = await cfgRes.json();
      const status = await statusRes.json();

      const wasDirty = isDirty();
      state.connected = true;
      state.csrfToken = cfg.csrfToken || state.csrfToken;
      state.revision = cfg.revision || 1;
      if (status.system) {
        const el = document.getElementById("sysHealthSummary");
        if (el) {
          const s = status.system;
          const upH = Math.floor((s.uptimeSec || 0) / 3600);
          const upM = Math.floor(((s.uptimeSec || 0) % 3600) / 60);
          el.textContent =
            `Internal RAM: ${s.freeHeapKb} KB (Min: ${s.minFreeHeapKb} KB) • ` +
            `PSRAM: ${s.psramFreeKb} / ${s.psramTotalKb} KB • ` +
            `Wi-Fi: ${s.wifiRssi} dBm (${s.ip}) • ` +
            `Uptime: ${upH}j ${upM}m • Reset Terakhir: ${s.resetReason}`;
        }
      }

      const loadedCorrections = PRAYER_KEYS.map((k) =>
        cfg.corrections && typeof cfg.corrections[k] === "number" ? cfg.corrections[k] : 0
      );
      const loadedIqomah = IQOMAH_KEYS.map((k, idx) =>
        cfg.iqomah && typeof cfg.iqomah[k] === "number"
          ? cfg.iqomah[k]
          : state.savedConfig.iqomahMinutes[idx]
      );

      state.savedConfig = {
        mosqueName: cfg.mosqueName || "Nama Masjid",
        runningText: cfg.runningText || "",
        runningTextEnabled: Boolean(cfg.runningTextEnabled),
        scrollIntervalMs: Number(cfg.scrollIntervalMs) || 80,
        panelCount: Number(cfg.panelCount) >= 1 && Number(cfg.panelCount) <= 4 ? Number(cfg.panelCount) : 2,
        panelRows: Number(cfg.panelRows) >= 1 && Number(cfg.panelRows) <= 2 ? Number(cfg.panelRows) : 1,
        calligraphyEnabled: cfg.calligraphyEnabled !== undefined ? Boolean(cfg.calligraphyEnabled) : true,
        brightness: Number(cfg.brightness) || 60,
        hijriOffsetDays: Number(cfg.hijriOffsetDays) || 0,
        adzanDurationMinutes: Number(cfg.adzanDurationMinutes) || 4,
        iqomahMinutes: loadedIqomah,
        khatibName: cfg.khatibName ?? state.savedConfig.khatibName,
        imamName: cfg.imamName ?? state.savedConfig.imamName,
        muadzinName: cfg.muadzinName ?? state.savedConfig.muadzinName,
        bilalName: cfg.bilalName ?? state.savedConfig.bilalName,
        khotbahEnabled: cfg.khotbahEnabled !== undefined ? Boolean(cfg.khotbahEnabled) : true,
        qrEnabled: cfg.qrEnabled !== undefined ? Boolean(cfg.qrEnabled) : true,
        qrText: cfg.qrText ?? state.savedConfig.qrText,
        autoSleepEnabled: Boolean(cfg.autoSleepEnabled),
        sleepHour: Number(cfg.sleepHour ?? 23),
        wakeHour: Number(cfg.wakeHour ?? 3),
        audioVolume: Number(cfg.audioVolume ?? 20),
        buzzerEnabled: cfg.buzzerEnabled !== undefined ? Boolean(cfg.buzzerEnabled) : true,
        tarhimOffsetMinutes: Number(cfg.tarhimOffsetMinutes ?? -6),
        corrections: loadedCorrections
      };

      if (!wasDirty) {
        state.draftConfig = cloneConfig(state.savedConfig);
        syncInputsFromDraft();
      }

      if (status.clock) {
        state.clock.valid = Boolean(status.clock.valid);
        state.clock.date = status.clock.date || "----:--:--";
        state.clock.time = status.clock.time || "--:--:--";
      }

      if (status.schedule && Array.isArray(status.schedule.prayers)) {
        state.baseMinutes = status.schedule.prayers.map((p) =>
          typeof p.baseMinute === "number" ? p.baseMinute : -1
        );
      }

      renderHeaderAndClock();
      renderScheduleAndPreview();
    } catch (_) {
      state.connected = false;
      tickLocalDemoClock();
    }
  }

  function tickLocalDemoClock() {
    const wita = getWitaNow();
    state.clock.valid = true;
    state.clock.date = wita.dateStr;
    state.clock.time = wita.timeStr;
    if (KOTAMOBAGU_NOV_2026[wita.dateStr]) {
      state.baseMinutes = [...KOTAMOBAGU_NOV_2026[wita.dateStr]];
    } else {
      state.baseMinutes = [...KOTAMOBAGU_NOV_2026["2026-11-07"]];
    }
    renderHeaderAndClock();
    renderScheduleAndPreview();
  }

  async function handleSaveConfig(event) {
    event.preventDefault();

    const d = state.draftConfig;
    const name = d.mosqueName.trim();
    if (!isPrintableAscii(name, false, 64)) {
      showAlert("Nama masjid wajib 1..64 karakter huruf Latin/angka standar.", "error");
      return;
    }
    if (!isPrintableAscii(d.runningText, true, 256)) {
      showAlert("Teks berjalan maksimal 256 karakter standar.", "error");
      return;
    }

    const { validOrder, errorMsg } = computeFinalMinutes(state.baseMinutes, d.corrections);
    if (!validOrder) {
      showAlert(errorMsg, "error");
      return;
    }

    d.mosqueName = name;

    if (!state.connected) {
      state.revision += 1;
      state.savedConfig = cloneConfig(d);
      try {
        localStorage.setItem("jws_kotamobagu_preview_cfg_v2", JSON.stringify(state.savedConfig));
      } catch (_) {}
      syncInputsFromDraft();
      renderHeaderAndClock();
      renderScheduleAndPreview();
      showAlert("Pengaturan waktu sholat, panel P5, Iqomah, & Scene berhasil disimpan.", "success");
      return;
    }

    const correctionsPayload = {};
    for (let i = 0; i < PRAYER_KEYS.length; i++) {
      correctionsPayload[PRAYER_KEYS[i]] = Number(d.corrections[i]) || 0;
    }
    const iqomahPayload = {};
    for (let i = 0; i < IQOMAH_KEYS.length; i++) {
      iqomahPayload[IQOMAH_KEYS[i]] = Number(d.iqomahMinutes[i]) || 10;
    }

    const payload = {
      revision: state.revision,
      mosqueName: d.mosqueName,
      runningText: d.runningText,
      runningTextEnabled: Boolean(d.runningTextEnabled),
      scrollIntervalMs: Number(d.scrollIntervalMs),
      panelCount: Number(d.panelCount) || 2,
      panelRows: Number(d.panelRows) || 1,
      calligraphyEnabled: Boolean(d.calligraphyEnabled),
      brightness: Number(d.brightness) || 60,
      hijriOffsetDays: Number(d.hijriOffsetDays) || 0,
      adzanDurationMinutes: Number(d.adzanDurationMinutes) || 4,
      iqomah: iqomahPayload,
      khatibName: d.khatibName,
      imamName: d.imamName,
      muadzinName: d.muadzinName,
      bilalName: d.bilalName,
      khotbahEnabled: Boolean(d.khotbahEnabled),
      qrEnabled: Boolean(d.qrEnabled),
      qrText: d.qrText,
      autoSleepEnabled: Boolean(d.autoSleepEnabled),
      sleepHour: Number(d.sleepHour),
      wakeHour: Number(d.wakeHour),
      audioVolume: Number(d.audioVolume),
      buzzerEnabled: Boolean(d.buzzerEnabled),
      tarhimOffsetMinutes: Number(d.tarhimOffsetMinutes),
      corrections: correctionsPayload
    };

    try {
      const res = await fetch("/api/v1/config", {
        method: "PUT",
        headers: {
          "Content-Type": "application/json",
          "X-JWS-Token": state.csrfToken
        },
        body: JSON.stringify(payload)
      });
      const data = await res.json();
      if (!res.ok) {
        showAlert(data.error || `Gagal menyimpan (HTTP ${res.status}).`, "error");
        if (res.status === 409) await fetchDeviceData();
        return;
      }

      state.revision = data.revision || state.revision + 1;
      state.csrfToken = data.csrfToken || state.csrfToken;
      state.savedConfig = cloneConfig(d);
      showAlert("Seluruh pengaturan berhasil disimpan ke perangkat.", "success");
      await fetchDeviceData();
    } catch (_) {
      showAlert("Gagal menghubungi perangkat JWS.", "error");
    }
  }

  function animateTicker(nowMs) {
    const tftTicker = document.getElementById("tftTickerText");
    const p5Ticker = document.getElementById("p5TickerText");
    if (state.draftConfig.runningTextEnabled && state.draftConfig.runningText.length > 0) {
      const interval = Math.max(40, Math.min(250, Number(state.draftConfig.scrollIntervalMs) || 80));
      const elapsed = nowMs - state.lastTickerFrameMs;
      if (elapsed >= interval) {
        const steps = Math.floor(elapsed / interval);
        state.lastTickerFrameMs += steps * interval;
        const textWidth = state.draftConfig.runningText.length * 6.8;
        state.tickerX -= steps * 2;
        if (state.tickerX < -textWidth) {
          state.tickerX = 420;
        }
        const tx = `translateX(${state.tickerX}px)`;
        tftTicker.style.transform = tx;
        p5Ticker.style.transform = tx;
      }
    }
    requestAnimationFrame(animateTicker);
  }

  function setupPreviewTabs() {
    const tabP5 = document.getElementById("tabP5");
    const tabTft = document.getElementById("tabTft");
    const p5Wrap = document.getElementById("p5PreviewWrapper");
    const tftWrap = document.getElementById("tftPreviewWrapper");
    const previewTitle = document.getElementById("previewTitle");

    tabP5.addEventListener("click", () => {
      tabP5.classList.add("active");
      tabP5.setAttribute("aria-selected", "true");
      tabTft.classList.remove("active");
      tabTft.setAttribute("aria-selected", "false");
      p5Wrap.classList.remove("hidden");
      tftWrap.classList.add("hidden");
      previewTitle.textContent = "Preview Panel P5 RGB (WF2)";
    });

    tabTft.addEventListener("click", () => {
      tabTft.classList.add("active");
      tabTft.setAttribute("aria-selected", "true");
      tabP5.classList.remove("active");
      tabP5.setAttribute("aria-selected", "false");
      tftWrap.classList.remove("hidden");
      p5Wrap.classList.add("hidden");
      previewTitle.textContent = "Preview Layar TFT";
    });
  }

  function init() {
    try {
      const savedLocal = localStorage.getItem("jws_kotamobagu_preview_cfg_v2");
      if (savedLocal) {
        const parsed = JSON.parse(savedLocal);
        if (parsed && Array.isArray(parsed.corrections) && parsed.corrections.length === 7) {
          state.savedConfig = { ...defaultCfg(), ...parsed };
          state.draftConfig = cloneConfig(state.savedConfig);
        }
      }
    } catch (_) {}

    buildPrayerRows();
    buildPreviewGrids();
    setupPreviewTabs();
    syncInputsFromDraft();

    document.querySelectorAll(".pill-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        state.draftConfig.panelCount = Number(btn.getAttribute("data-panels")) || 2;
        syncInputsFromDraft();
        renderScheduleAndPreview();
      });
    });

    document.querySelectorAll(".row-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        state.draftConfig.panelRows = Number(btn.getAttribute("data-rows")) || 1;
        syncInputsFromDraft();
        renderScheduleAndPreview();
      });
    });

    document.querySelectorAll(".scene-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        state.activeScene = btn.getAttribute("data-scene") || "prayer";
        document.querySelectorAll(".scene-btn").forEach((b) =>
          b.classList.toggle("active", b === btn)
        );
        renderScheduleAndPreview();
        if (state.connected) {
          fetch("/api/v1/scene", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ scene: state.activeScene })
          }).catch(() => {});
        }
      });
    });

    // Theme selection buttons
    document.querySelectorAll(".theme-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        const themeId = parseInt(btn.getAttribute("data-theme"), 10) || 0;
        document.querySelectorAll(".theme-btn").forEach((b) => b.style.outline = (b === btn) ? "2px solid #fff" : "none");
        if (state.connected) {
          fetch("/api/v1/theme", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ theme: themeId })
          }).then(r => r.json()).then(d => {
            showAlert(d.message || "Tema berhasil diubah.", "success");
          }).catch(() => {});
        }
      });
    });

    // Remote Slide navigation buttons
    document.querySelectorAll(".slide-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        const slideId = parseInt(btn.getAttribute("data-slide"), 10) || 0;
        document.querySelectorAll(".slide-btn").forEach((b) => b.classList.toggle("active", b === btn));
        if (state.connected) {
          fetch("/api/v1/slide", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ slide: slideId })
          }).then(r => r.json()).then(d => {
            showAlert(d.message || "Slide layar berhasil diubah.", "success");
          }).catch(() => {});
        }
      });
    });

    function postLiveBrightness(briVal) {
      const bri = Math.max(5, Math.min(100, Number(briVal) || 80));
      state.draftConfig.brightness = bri;
      state.savedConfig.brightness = bri;
      const bInput = document.getElementById("brightnessInput");
      const bVal = document.getElementById("brightnessValue");
      if (bInput) bInput.value = String(bri);
      if (bVal) bVal.textContent = `${bri}%`;
      document.querySelectorAll(".bri-btn").forEach((b) => {
        b.classList.toggle("active", parseInt(b.getAttribute("data-bri"), 10) === bri);
      });
      renderScheduleAndPreview();
      if (state.connected) {
        fetch("/api/v1/brightness", {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            "X-JWS-Token": state.csrfToken
          },
          body: JSON.stringify({ brightness: bri })
        }).then(r => r.json()).then(d => {
          showAlert(d.message || `Kecerahan layar diatur ke ${bri}%`, "success");
        }).catch(() => {});
      }
    }

    document.querySelectorAll(".bri-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        postLiveBrightness(parseInt(btn.getAttribute("data-bri"), 10) || 80);
      });
    });

    // Watchlist stock save button
    const btnSaveStocks = document.getElementById("btnSaveStocks");
    const stockWatchInput = document.getElementById("stockWatchlistInput");
    if (btnSaveStocks && stockWatchInput) {
      btnSaveStocks.addEventListener("click", () => {
        const syms = stockWatchInput.value.trim();
        if (state.connected) {
          fetch("/api/v1/stocks", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ symbols: syms })
          }).then(r => r.json()).then(d => {
            showAlert(d.message || "Watchlist saham berhasil disimpan.", "success");
          }).catch(() => {});
        }
      });
    }

    // MikroTik NOC settings loader & save handler
    const mtikHostInput = document.getElementById("mtikHostInput");
    const mtikPortInput = document.getElementById("mtikPortInput");
    const mtikUserInput = document.getElementById("mtikUserInput");
    const mtikPassInput = document.getElementById("mtikPassInput");
    const mtikIfacesInput = document.getElementById("mtikIfacesInput");
    const mtikStatusHint = document.getElementById("mtikStatusHint");
    const btnSaveMikrotik = document.getElementById("btnSaveMikrotik");

    async function loadMikrotikSettings() {
      try {
        const res = await fetch("/api/v1/mikrotik");
        if (!res.ok) return;
        const m = await res.json();
        if (mtikHostInput && m.host) mtikHostInput.value = m.host;
        if (mtikPortInput && m.port) mtikPortInput.value = String(m.port);
        if (mtikUserInput && m.user) mtikUserInput.value = m.user;
        if (mtikPassInput && m.hasPassword) mtikPassInput.placeholder = "(Password tersimpan)";
        if (mtikIfacesInput && m.ifaces) mtikIfacesInput.value = m.ifaces;
        if (mtikStatusHint) {
          mtikStatusHint.className = m.connected ? "badge badge-online" : "badge badge-demo";
          if (m.connected) {
            mtikStatusHint.style.color = "";
            mtikStatusHint.textContent = `\u2705 Terhubung: ${m.identity || "RouterOS"} (CPU ${Number(m.cpuLoad) || 0}%, PPPoE ${Number(m.totalPppoe) || 0}, HS ${Number(m.totalHotspot) || 0})`;
          } else {
            mtikStatusHint.style.color = "";
            mtikStatusHint.textContent = "Mode: Demo / Belum Terhubung ke RouterOS";
          }
        }
      } catch (_) {}
    }

    if (btnSaveMikrotik) {
      btnSaveMikrotik.addEventListener("click", async () => {
        const host = mtikHostInput ? mtikHostInput.value.trim() : "192.168.88.1";
        const port = mtikPortInput ? (parseInt(mtikPortInput.value, 10) || 8728) : 8728;
        const user = mtikUserInput ? mtikUserInput.value.trim() : "admin";
        const pass = mtikPassInput ? mtikPassInput.value : "";
        const ifaces = mtikIfacesInput ? mtikIfacesInput.value.trim() : "ether1,ether2,ether3,ether4";
        if (!host) {
          showAlert("IP Address Router MikroTik wajib diisi.", "error");
          return;
        }
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung (Mode Simulator).", "error");
          return;
        }
        btnSaveMikrotik.disabled = true;
        btnSaveMikrotik.textContent = "Menyimpan...";
        try {
          const res = await fetch("/api/v1/mikrotik", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ host, port, user, pass, ifaces })
          });
          const d = await res.json();
          if (res.ok) {
            showAlert(d.message || "Konfigurasi MikroTik NOC berhasil disimpan.", "success");
            if (mtikPassInput) mtikPassInput.value = "";
            setTimeout(loadMikrotikSettings, 2500);
          } else {
            showAlert(d.error || "Gagal menyimpan konfigurasi MikroTik.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          btnSaveMikrotik.disabled = false;
          btnSaveMikrotik.textContent = "Simpan MikroTik";
        }
      });
    }
    setTimeout(loadMikrotikSettings, 1200);

    // Section Navigation Tabs (Modular Dashboard)
    const secTabBtns = document.querySelectorAll(".sec-tab-btn");
    const secPanels = document.querySelectorAll(".sec-panel");
    const secForm = document.getElementById("jwsConfigForm");
    let currentSection = "jws";

    function activateSection(secId, persist) {
      if (!secForm || !secTabBtns.length) return;
      const target = secId || "jws";
      currentSection = target;
      secTabBtns.forEach((btn) => {
        const isMatch = btn.getAttribute("data-sec") === target;
        btn.classList.toggle("active", isMatch);
        btn.setAttribute("aria-selected", isMatch ? "true" : "false");
      });
      if (target === "all") {
        secForm.classList.add("show-all");
        secPanels.forEach((p) => p.classList.add("active"));
      } else {
        secForm.classList.remove("show-all");
        secPanels.forEach((p) => {
          p.classList.toggle("active", p.getAttribute("data-section") === target);
        });
      }
      if (persist) {
        try {
          localStorage.setItem("jws_active_section", target);
        } catch (_) {}
      }
      if (target === "xiaozhi" || target === "all") loadXiaozhiStatus();
      if (target === "mikrotik" || target === "all") loadMikrotikSettings();
    }

    secTabBtns.forEach((btn) => {
      btn.addEventListener("click", () => {
        activateSection(btn.getAttribute("data-sec"), true);
      });
    });

    // Xiaozhi AI: Custom Agent Builder, Live Dialog, & INMP441 Voice Capture
    const xzAgentNameInput = document.getElementById("xzAgentNameInput");
    const xzAgentPromptInput = document.getElementById("xzAgentPromptInput");
    const xzLiveQueryText = document.getElementById("xzLiveQueryText");
    const xzLiveReplyText = document.getElementById("xzLiveReplyText");
    const btnWebSpeechXiaozhi = document.getElementById("btnWebSpeechXiaozhi");
    const btnRecordVoiceXiaozhi = document.getElementById("btnRecordVoiceXiaozhi");
    const xzQuestionInput = document.getElementById("xzQuestionInput");
    const btnAskXiaozhi = document.getElementById("btnAskXiaozhi");
    const xzApiBaseInput = document.getElementById("xzApiBaseInput");
    const xzApiModelInput = document.getElementById("xzApiModelInput");
    const xzApiKeyInput = document.getElementById("xzApiKeyInput");
    const btnSaveXzKey = document.getElementById("btnSaveXzKey");
    const btnClearXzKey = document.getElementById("btnClearXzKey");
    const xzStatusHint = document.getElementById("xzStatusHint");
    const btnRebootEsp = document.getElementById("btnRebootEsp");
    const relayState = { relay1: false, relay2: false };
    function renderRelayButtons() {
      const m = [
        ["btnToggleR1", "Relay 1 (GPIO15 HIGH)", relayState.relay1],
        ["btnToggleR2", "Relay 2 (GPIO16 LOW)", relayState.relay2]
      ];
      for (const [id, label, on] of m) {
        const b = document.getElementById(id);
        if (!b) continue;
        b.textContent = `${label}: ${on ? "ON" : "OFF"}`;
        b.className = on ? "btn btn-primary" : "btn btn-ghost";
      }
    }
    async function loadRelayStatus() {
      try {
        const res = await fetch("/api/v1/relay", { cache: "no-store" });
        if (!res.ok) return;
        Object.assign(relayState, await res.json());
        renderRelayButtons();
      } catch (_) {}
    }
    async function postRelay(patch) {
      if (!state.connected) {
        showAlert("Perangkat JWS belum terhubung.", "error");
        return;
      }
      try {
        const res = await fetch("/api/v1/relay", {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
          body: JSON.stringify(patch)
        });
        if (res.ok) {
          Object.assign(relayState, await res.json());
          renderRelayButtons();
        }
      } catch (_) {}
    }
    const relayBindings = [
      ["btnToggleR1", () => postRelay({ relay1: !relayState.relay1 })],
      ["btnToggleR2", () => postRelay({ relay2: !relayState.relay2 })],
      ["btnRelayAllOn", () => postRelay({ all: true })],
      ["btnRelayAllOff", () => postRelay({ all: false })]
    ];
    for (const [id, fn] of relayBindings) {
      const b = document.getElementById(id);
      if (b) b.addEventListener("click", fn);
    }
    loadRelayStatus();

    const sinricBadge = document.getElementById("sinricStatusBadge");
    const sinricEnabledInput = document.getElementById("sinricEnabledInput");
    const sinricAppKeyInput = document.getElementById("sinricAppKeyInput");
    const sinricAppSecretInput = document.getElementById("sinricAppSecretInput");
    const sinricDev1Input = document.getElementById("sinricDev1Input");
    const sinricDev2Input = document.getElementById("sinricDev2Input");
    const btnSaveSinric = document.getElementById("btnSaveSinric");
    const btnClearSinric = document.getElementById("btnClearSinric");

    function renderSinricStatus(s) {
      if (!s) return;
      if (sinricEnabledInput) sinricEnabledInput.checked = Boolean(s.enabled);
      if (sinricDev1Input && document.activeElement !== sinricDev1Input) sinricDev1Input.value = s.deviceId1 || "";
      if (sinricDev2Input && document.activeElement !== sinricDev2Input) sinricDev2Input.value = s.deviceId2 || "";
      if (sinricAppKeyInput && s.hasAppKey) sinricAppKeyInput.placeholder = "(App Key tersimpan \u2022 kosongkan jika tidak ganti)";
      if (sinricAppSecretInput && s.hasAppSecret) sinricAppSecretInput.placeholder = "(App Secret tersimpan \u2022 kosongkan jika tidak ganti)";
      if (sinricBadge) {
        if (s.connected) {
          sinricBadge.className = "badge badge-online";
          sinricBadge.textContent = "Sinric Pro ONLINE (Google Home Siap)";
        } else if (s.enabled && s.configured) {
          sinricBadge.className = "badge badge-demo";
          sinricBadge.textContent = "Menghubungkan ke Sinric Pro...";
        } else {
          sinricBadge.className = "badge badge-demo";
          sinricBadge.textContent = "Belum dikonfigurasi";
        }
      }
    }

    async function loadSinricStatus() {
      try {
        const res = await fetch("/api/v1/sinric", { cache: "no-store" });
        if (res.ok) renderSinricStatus(await res.json());
      } catch (_) {}
    }

    if (btnSaveSinric) {
      btnSaveSinric.addEventListener("click", async () => {
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung.", "error");
          return;
        }
        btnSaveSinric.disabled = true;
        try {
          const payload = {
            enabled: sinricEnabledInput ? sinricEnabledInput.checked : true,
            deviceId1: sinricDev1Input ? sinricDev1Input.value.trim() : "",
            deviceId2: sinricDev2Input ? sinricDev2Input.value.trim() : ""
          };
          const ak = sinricAppKeyInput ? sinricAppKeyInput.value.trim() : "";
          const as = sinricAppSecretInput ? sinricAppSecretInput.value.trim() : "";
          if (ak) payload.appKey = ak;
          if (as) payload.appSecret = as;
          const res = await fetch("/api/v1/sinric", {
            method: "POST",
            headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
            body: JSON.stringify(payload)
          });
          if (res.ok) {
            renderSinricStatus(await res.json());
            if (sinricAppKeyInput) sinricAppKeyInput.value = "";
            if (sinricAppSecretInput) sinricAppSecretInput.value = "";
            showAlert("Pengaturan Sinric Pro disimpan! Menghubungkan ke Google Home...", "success");
            setTimeout(loadSinricStatus, 2500);
          } else {
            showAlert("Gagal menyimpan pengaturan Sinric Pro.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi perangkat JWS.", "error");
        } finally {
          btnSaveSinric.disabled = false;
        }
      });
    }

    if (btnClearSinric) {
      btnClearSinric.addEventListener("click", async () => {
        if (!state.connected) return;
        try {
          const res = await fetch("/api/v1/sinric", {
            method: "POST",
            headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
            body: JSON.stringify({ enabled: false, clearSecrets: true })
          });
          if (res.ok) {
            renderSinricStatus(await res.json());
            showAlert("Konfigurasi Sinric Pro dihapus.", "success");
          }
        } catch (_) {}
      });
    }
    loadSinricStatus();

    const chronosBadge = document.getElementById("chronosStatusBadge");
    const chronosNotifSummary = document.getElementById("chronosNotifSummary");
    const btnChronosFindPhone = document.getElementById("btnChronosFindPhone");
    const btnChronosMusicPlay = document.getElementById("btnChronosMusicPlay");
    const btnChronosMusicPrev = document.getElementById("btnChronosMusicPrev");
    const btnChronosMusicNext = document.getElementById("btnChronosMusicNext");
    const btnChronosCamera = document.getElementById("btnChronosCamera");
    let chronosState = { connected: false, findingPhone: false };

    function renderChronosStatus(cs) {
      if (!cs) return;
      chronosState = cs;
      if (chronosBadge) {
        if (cs.connected) {
          chronosBadge.className = "badge badge-online";
          chronosBadge.textContent = `BLE Terhubung \u2022 Baterai HP ${cs.phoneBattery || 0}%${cs.phoneCharging ? " (\u26A1)" : ""}`;
        } else {
          chronosBadge.className = "badge badge-demo";
          chronosBadge.textContent = `BLE Aktif (${cs.deviceName || "JWS-UPIN-S3"}) \u2022 Menunggu HP`;
        }
      }
      if (btnChronosFindPhone) {
        btnChronosFindPhone.textContent = cs.findingPhone
          ? "\uD83D\uDD15 Stop Cari HP"
          : "\uD83D\uDD14 Cari HP (Bunyikan)";
      }
      if (chronosNotifSummary && (cs.notifCount > 0 || cs.ringing)) {
        if (cs.ringing) {
          chronosNotifSummary.textContent = `\uD83D\uDCDE PANGGILAN MASUK di HP Anda!`;
        } else {
          chronosNotifSummary.textContent =
            `\uD83D\uDCE9 Notifikasi Terakhir [#${cs.notifCount} \u2022 ${cs.notifApp || "HP"}]: ${cs.notifTitle || ""} \u2014 ${cs.notifMsg || ""}`;
        }
      }
    }

    async function loadChronosStatus() {
      try {
        const res = await fetch("/api/v1/chronos", { cache: "no-store" });
        if (res.ok) renderChronosStatus(await res.json());
      } catch (_) {}
    }

    async function postChronos(payload, okMsg) {
      if (!state.connected) {
        showAlert("Perangkat JWS belum terhubung.", "error");
        return;
      }
      try {
        const res = await fetch("/api/v1/chronos", {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
          body: JSON.stringify(payload)
        });
        if (res.ok) {
          renderChronosStatus(await res.json());
          if (okMsg) showAlert(okMsg, "success");
        }
      } catch (_) {
        showAlert("Gagal mengirim perintah Chronos BLE.", "error");
      }
    }

    if (btnChronosFindPhone) {
      btnChronosFindPhone.addEventListener("click", () => {
        const nextFind = !chronosState.findingPhone;
        postChronos(
          { findPhone: nextFind },
          nextFind ? "Sinyal Cari HP dikirim via Chronos BLE!" : "Pencarian HP dihentikan."
        );
      });
    }
    if (btnChronosMusicPlay) {
      btnChronosMusicPlay.addEventListener("click", () => postChronos({ music: "play" }, "Perintah Play/Pause Musik dikirim ke HP!"));
    }
    if (btnChronosMusicPrev) {
      btnChronosMusicPrev.addEventListener("click", () => postChronos({ music: "prev" }, "Perintah Lagu Sebelumnya dikirim ke HP!"));
    }
    if (btnChronosMusicNext) {
      btnChronosMusicNext.addEventListener("click", () => postChronos({ music: "next" }, "Perintah Lagu Berikutnya dikirim ke HP!"));
    }
    if (btnChronosCamera) {
      btnChronosCamera.addEventListener("click", () => postChronos({ camera: true }, "Shutter Kamera HP dikirim via Chronos BLE!"));
    }
    loadChronosStatus();
    setInterval(() => {
      if (state.connected) loadChronosStatus();
    }, 3500);

    if (btnRebootEsp) {
      btnRebootEsp.addEventListener("click", async () => {
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung.", "error");
          return;
        }
        btnRebootEsp.disabled = true;
        try {
          const res = await fetch("/api/v1/system/reboot", {
            method: "POST",
            headers: { "X-JWS-Token": state.csrfToken }
          });
          if (res.ok) {
            showAlert("ESP32-S3 sedang restart... tunggu 5 detik lalu refresh.", "success");
          } else {
            showAlert("Gagal merestart ESP32-S3.", "error");
          }
        } catch (_) {
          showAlert("ESP32-S3 sedang restart...", "success");
        } finally {
          setTimeout(() => { btnRebootEsp.disabled = false; }, 5000);
        }
      });
    }
    let xzAgentDirty = false;
    let lastPolledXzQuery = "";

    if (xzAgentNameInput) {
      xzAgentNameInput.addEventListener("input", () => { xzAgentDirty = true; });
    }
    if (xzAgentPromptInput) {
      xzAgentPromptInput.addEventListener("input", () => { xzAgentDirty = true; });
    }
    if (xzApiBaseInput) {
      xzApiBaseInput.addEventListener("input", () => { xzAgentDirty = true; });
    }
    if (xzApiModelInput) {
      xzApiModelInput.addEventListener("input", () => { xzAgentDirty = true; });
    }

    async function loadXiaozhiStatus() {
      try {
        const res = await fetch("/api/v1/xiaozhi", { cache: "no-store" });
        if (!res.ok) return;
        const xz = await res.json();
        if (!xzAgentDirty) {
          if (xzAgentNameInput && xz.agentName && document.activeElement !== xzAgentNameInput) {
            xzAgentNameInput.value = xz.agentName;
          }
          if (xzAgentPromptInput && xz.agentPrompt && document.activeElement !== xzAgentPromptInput) {
            xzAgentPromptInput.value = xz.agentPrompt;
          }
          if (xzApiBaseInput && document.activeElement !== xzApiBaseInput) {
            xzApiBaseInput.value = xz.apiBaseUrl || "";
          }
          if (xzApiModelInput && document.activeElement !== xzApiModelInput) {
            xzApiModelInput.value = xz.apiModel || "";
          }
        }
        if (xzApiKeyInput && xz.hasApiKey) {
          xzApiKeyInput.placeholder = "(API Key tersimpan \u2022 kosongkan jika tidak ganti)";
        }
        if (xzLiveQueryText) {
          xzLiveQueryText.textContent = xz.query || "-";
        }
        if (xzQuestionInput && document.activeElement !== xzQuestionInput &&
            xz.query && xz.query !== lastPolledXzQuery &&
            !/^(Panggil |Merekam |Mendengarkan |Menerjemahkan |\(Suara )/.test(xz.query)) {
          lastPolledXzQuery = xz.query;
          xzQuestionInput.value = xz.query;
        }
        if (xzLiveReplyText) {
          const combinedReply = [xz.reply1, xz.reply2, xz.reply3].filter(Boolean).join(" ").trim();
          xzLiveReplyText.textContent = combinedReply || "-";
        }
        if (xzStatusHint) {
          const micLabel = xz.micReady ? `Mic INMP441 Aktif (${Number(xz.micLevel) || 0}%)` : "Mode Sentuh / Web";
          const aiLabel = xz.hasApiKey ? "Gemini + Internet AI Aktif" : "AI Internet Gratis Aktif";
          xzStatusHint.className = "badge badge-online";
          xzStatusHint.textContent = `${micLabel} \u2022 ${aiLabel}`;
        }
      } catch (_) {}
    }

    if (btnAskXiaozhi && xzQuestionInput) {
      btnAskXiaozhi.addEventListener("click", async () => {
        const question = xzQuestionInput.value.trim();
        if (!question) {
          showAlert("Tulis pertanyaan untuk Agent AI terlebih dahulu.", "error");
          return;
        }
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung (Mode Simulator).", "error");
          return;
        }
        btnAskXiaozhi.disabled = true;
        try {
          const res = await fetch("/api/v1/xiaozhi", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ question })
          });
          const d = await res.json();
          if (res.ok) {
            showAlert(d.message || "Pertanyaan dikirim ke layar Xiaozhi!", "success");
            xzQuestionInput.value = "";
            setTimeout(loadXiaozhiStatus, 1000);
            setTimeout(loadXiaozhiStatus, 3200);
          } else {
            showAlert(d.error || "Gagal mengirim pertanyaan ke Xiaozhi.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          btnAskXiaozhi.disabled = false;
        }
      });
      if (xzQuestionInput) {
        xzQuestionInput.addEventListener("keydown", (ev) => {
          if (ev.key === "Enter") {
            ev.preventDefault();
            btnAskXiaozhi.click();
          }
        });
      }
    }

    if (btnWebSpeechXiaozhi) {
      btnWebSpeechXiaozhi.addEventListener("click", () => {
        const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;
        if (!SpeechRec) {
          if (btnRecordVoiceXiaozhi && state.connected) {
            showAlert("Browser ini tidak mendukung Mic Web Speech — mengalihkan ke Mic Alat INMP441...", "success");
            btnRecordVoiceXiaozhi.click();
            return;
          }
          showAlert("Browser ini tidak mendukung Mic Web Speech. Gunakan dikte mikrofon pada keyboard HP Anda atau ketik langsung.", "error");
          return;
        }
        const origLabel = btnWebSpeechXiaozhi.textContent;
        const rec = new SpeechRec();
        rec.lang = "id-ID";
        rec.interimResults = true;
        rec.maxAlternatives = 1;
        btnWebSpeechXiaozhi.disabled = true;
        btnWebSpeechXiaozhi.textContent = "\uD83C\uDF99\uFE0F Silakan bicara di HP/Laptop Anda...";

        rec.onresult = (ev) => {
          let interim = "";
          let finalTxt = "";
          for (let i = ev.resultIndex || 0; i < ev.results.length; ++i) {
            const part = String((ev.results[i][0] && ev.results[i][0].transcript) || "").trim();
            if (ev.results[i].isFinal) {
              finalTxt += (finalTxt ? " " : "") + part;
            } else {
              interim += (interim ? " " : "") + part;
            }
          }
          const shown = (finalTxt || interim).trim();
          if (shown) {
            if (xzQuestionInput) xzQuestionInput.value = shown;
            if (xzLiveQueryText) xzLiveQueryText.textContent = shown;
          }
          if (finalTxt && xzQuestionInput && btnAskXiaozhi) {
            const lower = finalTxt.toLowerCase();
            const prefixRe = /^(halo\s+upin|hai\s+upin|alo\s+upin|halo\s+ipin|halo\s+ubin|halo\s+apin|halo\s+pin|upin|ipin)\b/i;
            const hasPrefix = prefixRe.test(lower);
            if (!hasPrefix) {
              showAlert(`Teks terbaca: "${finalTxt}". Gunakan awalan 'Halo Upin ...' atau klik Tanya Teks.`, "success");
              return;
            }
            const stripped = finalTxt.replace(/^(halo\s+upin|hai\s+upin|alo\s+upin|halo\s+ipin|halo\s+ubin|halo\s+apin|halo\s+pin|upin|ipin)[:,.!\s]*/i, "").trim();
            xzQuestionInput.value = stripped || "Halo Upin! Sapa aku dengan ceria!";
            btnAskXiaozhi.click();
          }
        };
        rec.onerror = (ev) => {
          if (ev && (ev.error === "not-allowed" || ev.error === "service-not-allowed")) {
            if (btnRecordVoiceXiaozhi && state.connected) {
              showAlert("Mic browser diblokir pada HTTP lokal — mengalihkan ke Mic Alat INMP441...", "success");
              btnRecordVoiceXiaozhi.click();
              return;
            }
            showAlert("Izin mikrofon diblokir browser (pada alamat http:// lokal, pakai ikon mic di keyboard HP untuk suara ke teks).", "error");
            return;
          }
          showAlert("Suara belum terdengar atau dibatalkan. Silakan coba lagi.", "error");
        };
        rec.onend = () => {
          btnWebSpeechXiaozhi.disabled = false;
          btnWebSpeechXiaozhi.textContent = origLabel;
        };
        try {
          rec.start();
        } catch (_) {
          btnWebSpeechXiaozhi.disabled = false;
          btnWebSpeechXiaozhi.textContent = origLabel;
          showAlert("Gagal mengaktifkan mikrofon browser. Gunakan dikte keyboard HP atau ketik langsung.", "error");
        }
      });
    }

    if (btnRecordVoiceXiaozhi) {
      btnRecordVoiceXiaozhi.addEventListener("click", async () => {
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung (Mode Simulator).", "error");
          return;
        }
        const origHtml = btnRecordVoiceXiaozhi.textContent;
        btnRecordVoiceXiaozhi.disabled = true;
        btnRecordVoiceXiaozhi.textContent = "Merekam 2 detik di INMP441...";
        try {
          const res = await fetch("/api/v1/xiaozhi", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ recordVoice: true })
          });
          const d = await res.json();
          if (res.ok) {
            showAlert(d.message || "Silakan bicara sekarang ke mic INMP441...", "success");
            setTimeout(loadXiaozhiStatus, 1500);
            setTimeout(loadXiaozhiStatus, 4200);
            setTimeout(loadXiaozhiStatus, 7200);
          } else {
            showAlert(d.error || "Gagal memulai rekaman suara.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          setTimeout(() => {
            btnRecordVoiceXiaozhi.disabled = false;
            btnRecordVoiceXiaozhi.textContent = origHtml;
          }, 2500);
        }
      });
    }

    if (btnSaveXzKey) {
      btnSaveXzKey.addEventListener("click", async () => {
        const agentName = xzAgentNameInput ? xzAgentNameInput.value.trim() : "";
        const agentPrompt = xzAgentPromptInput ? xzAgentPromptInput.value.trim() : "";
        const apiBaseUrl = xzApiBaseInput ? xzApiBaseInput.value.trim() : "";
        const apiModel = xzApiModelInput ? xzApiModelInput.value.trim() : "";
        const apiKey = xzApiKeyInput ? xzApiKeyInput.value.trim() : "";
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung (Mode Simulator).", "error");
          return;
        }
        btnSaveXzKey.disabled = true;
        try {
          const payload = { agentName, agentPrompt, apiBaseUrl, apiModel };
          if (apiKey.length > 0) payload.apiKey = apiKey;
          const res = await fetch("/api/v1/xiaozhi", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify(payload)
          });
          const d = await res.json();
          if (res.ok) {
            xzAgentDirty = false;
            showAlert(d.message || "Profil Agent & API Key berhasil disimpan.", "success");
            if (xzApiKeyInput) xzApiKeyInput.value = "";
            setTimeout(loadXiaozhiStatus, 800);
          } else {
            showAlert(d.error || "Gagal menyimpan pengaturan Xiaozhi AI.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          btnSaveXzKey.disabled = false;
        }
      });
    }

    if (btnClearXzKey) {
      btnClearXzKey.addEventListener("click", async () => {
        if (!state.connected) {
          showAlert("Perangkat JWS belum terhubung (Mode Simulator).", "error");
          return;
        }
        btnClearXzKey.disabled = true;
        try {
          const res = await fetch("/api/v1/xiaozhi", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ clearApiKey: true })
          });
          const d = await res.json();
          if (res.ok) {
            if (xzApiBaseInput) xzApiBaseInput.value = "";
            if (xzApiModelInput) xzApiModelInput.value = "";
            if (xzApiKeyInput) {
              xzApiKeyInput.value = "";
              xzApiKeyInput.placeholder = "sk-... atau AIzaSy... (Kosongkan jika tidak mengubah key)";
            }
            showAlert("Koneksi Custom/Gemini berhasil diputus. Sekarang menggunakan AI Internet Gratis!", "success");
            setTimeout(loadXiaozhiStatus, 500);
          } else {
            showAlert(d.error || "Gagal memutus koneksi Gemini.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          btnClearXzKey.disabled = false;
        }
      });
    }

    try {
      const savedSec = localStorage.getItem("jws_active_section") || "jws";
      activateSection(savedSec, false);
    } catch (_) {
      activateSection("jws", false);
    }
    setTimeout(loadXiaozhiStatus, 1400);

    document.getElementById("panelCountSelect").addEventListener("change", (e) => {
      state.draftConfig.panelCount = Number(e.target.value) || 2;
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("panelRowsSelect").addEventListener("change", (e) => {
      state.draftConfig.panelRows = Number(e.target.value) || 1;
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("brightnessInput").addEventListener("input", (e) => {
      state.draftConfig.brightness = Number(e.target.value) || 80;
      document.getElementById("brightnessValue").textContent = `${state.draftConfig.brightness}%`;
      renderScheduleAndPreview();
    });
    document.getElementById("brightnessInput").addEventListener("change", (e) => {
      postLiveBrightness(Number(e.target.value) || 80);
    });

    document.getElementById("quickCalligraphyToggle").addEventListener("change", (e) => {
      state.draftConfig.calligraphyEnabled = e.target.checked;
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("calligraphyEnabledInput").addEventListener("change", (e) => {
      state.draftConfig.calligraphyEnabled = e.target.checked;
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("hijriOffsetInput").addEventListener("change", (e) => {
      state.draftConfig.hijriOffsetDays = Number(e.target.value) || 0;
      renderHeaderAndClock();
      renderScheduleAndPreview();
    });

    document.getElementById("adzanDurationInput").addEventListener("input", (e) => {
      state.draftConfig.adzanDurationMinutes = Math.max(1, Math.min(10, Number(e.target.value) || 4));
      renderScheduleAndPreview();
    });

    ["khatib", "imam", "muadzin", "bilal"].forEach((role) => {
      document.getElementById(`${role}Input`).addEventListener("input", (e) => {
        state.draftConfig[`${role}Name`] = e.target.value;
        renderScheduleAndPreview();
      });
    });

    const khotbahInputEl = document.getElementById("khotbahEnabledInput");
    if (khotbahInputEl) {
      khotbahInputEl.addEventListener("change", (e) => {
        state.draftConfig.khotbahEnabled = e.target.checked;
        renderScheduleAndPreview();
      });
    }

    document.getElementById("qrEnabledInput").addEventListener("change", (e) => {
      state.draftConfig.qrEnabled = e.target.checked;
      renderScheduleAndPreview();
    });

    document.getElementById("qrTextInput").addEventListener("input", (e) => {
      state.draftConfig.qrText = e.target.value;
      renderScheduleAndPreview();
    });

    document.getElementById("autoSleepEnabledInput").addEventListener("change", (e) => {
      state.draftConfig.autoSleepEnabled = e.target.checked;
      renderScheduleAndPreview();
    });

    document.getElementById("sleepHourInput").addEventListener("input", (e) => {
      state.draftConfig.sleepHour = Math.max(0, Math.min(23, Number(e.target.value) || 0));
      renderScheduleAndPreview();
    });

    document.getElementById("wakeHourInput").addEventListener("input", (e) => {
      state.draftConfig.wakeHour = Math.max(0, Math.min(23, Number(e.target.value) || 0));
      renderScheduleAndPreview();
    });

    document.getElementById("buzzerEnabledInput").addEventListener("change", (e) => {
      state.draftConfig.buzzerEnabled = e.target.checked;
      renderScheduleAndPreview();
      if (state.connected) {
        fetch("/api/v1/audio", {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
          body: JSON.stringify({ enabled: e.target.checked })
        }).catch(() => {});
      }
    });

    document.getElementById("audioVolumeInput").addEventListener("input", (e) => {
      state.draftConfig.audioVolume = Math.max(0, Math.min(30, Number(e.target.value) || 0));
      renderScheduleAndPreview();
    });
    document.getElementById("audioVolumeInput").addEventListener("change", (e) => {
      const vol = Math.max(0, Math.min(30, Number(e.target.value) || 0));
      if (state.connected) {
        fetch("/api/v1/audio", {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
          body: JSON.stringify({ volume: vol })
        }).then(() => showAlert(`Volume Speaker MAX98357A diatur ke ${vol}/30`, "success")).catch(() => {});
      }
    });

    async function postAudioTest(payload, msg) {
      if (!state.connected) {
        showAlert("Perangkat JWS belum terhubung.", "error");
        return;
      }
      try {
        const res = await fetch("/api/v1/audio", {
          method: "POST",
          headers: { "Content-Type": "application/json", "X-JWS-Token": state.csrfToken },
          body: JSON.stringify(payload)
        });
        if (res.ok && msg) showAlert(msg, "success");
      } catch (_) {
        showAlert("Gagal mengirim perintah audio.", "error");
      }
    }

    const btnTestAudioChime = document.getElementById("btnTestAudioChime");
    const btnTestAudioAdzan = document.getElementById("btnTestAudioAdzan");
    const btnTestAudioTts = document.getElementById("btnTestAudioTts");
    if (btnTestAudioChime) {
      btnTestAudioChime.addEventListener("click", () => {
        postAudioTest({ chime: "boot" }, "Memutar nada chime di speaker MAX98357A!");
      });
    }
    if (btnTestAudioAdzan) {
      btnTestAudioAdzan.addEventListener("click", () => {
        postAudioTest({ chime: "adzan" }, "Memutar melodi peringatan Adzan di speaker MAX98357A!");
      });
    }
    if (btnTestAudioTts) {
      btnTestAudioTts.addEventListener("click", () => {
        postAudioTest(
          { speak: "Assalamualaikum kawan! Speaker MAX 98357A dan asisten suara Upin sudah aktif. Betul betul betul!" },
          "Memutar suara bicara UPIN AI (TTS Bahasa Indonesia) di speaker MAX98357A!"
        );
      });
    }

    document.getElementById("tarhimOffsetInput").addEventListener("input", (e) => {
      state.draftConfig.tarhimOffsetMinutes = Math.max(-15, Math.min(0, Number(e.target.value) || 0));
      renderScheduleAndPreview();
    });

    document.getElementById("mosqueNameInput").addEventListener("input", (e) => {
      state.draftConfig.mosqueName = e.target.value;
      updateCharCounts();
      renderScheduleAndPreview();
    });

    document.getElementById("runningTextInput").addEventListener("input", (e) => {
      state.draftConfig.runningText = e.target.value;
      updateCharCounts();
      renderScheduleAndPreview();
    });

    document.getElementById("runningTextEnabledInput").addEventListener("change", (e) => {
      state.draftConfig.runningTextEnabled = e.target.checked;
      renderScheduleAndPreview();
    });

    document.getElementById("scrollIntervalInput").addEventListener("input", (e) => {
      state.draftConfig.scrollIntervalMs = Number(e.target.value) || 80;
      document.getElementById("scrollIntervalValue").textContent = `${state.draftConfig.scrollIntervalMs} ms`;
      renderScheduleAndPreview();
    });

    document.getElementById("btnResetZero").addEventListener("click", () => {
      state.draftConfig.corrections = [0, 0, 0, 0, 0, 0, 0];
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("btnPresetIhtiyat").addEventListener("click", () => {
      state.draftConfig.corrections = [0, 2, 0, 2, 2, 2, 2];
      syncInputsFromDraft();
      renderScheduleAndPreview();
    });

    document.getElementById("btnRevertChanges").addEventListener("click", () => {
      state.draftConfig = cloneConfig(state.savedConfig);
      syncInputsFromDraft();
      renderHeaderAndClock();
      renderScheduleAndPreview();
    });

    const btnSyncPhone = document.getElementById("btnSyncPhoneTime");
    if (btnSyncPhone) {
      btnSyncPhone.addEventListener("click", async () => {
        const origText = btnSyncPhone.innerHTML;
        btnSyncPhone.disabled = true;
        btnSyncPhone.textContent = "Menyinkronkan...";

        const now = new Date();
        const epoch = Math.floor(now.getTime() / 1000);

        if (!state.connected) {
          tickLocalDemoClock();
          showAlert("Jam simulator lokal berhasil disinkronkan dengan HP/Laptop Anda.", "success");
          btnSyncPhone.disabled = false;
          btnSyncPhone.innerHTML = origText;
          return;
        }

        try {
          const res = await fetch("/api/v1/time", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ epoch })
          });
          const data = await res.json();
          if (res.ok) {
            showAlert(data.message || "Jam modul berhasil disinkronkan dengan HP!", "success");
            await fetchDeviceData();
          } else {
            showAlert(data.error || "Gagal menyinkronkan jam modul.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul JWS.", "error");
        } finally {
          btnSyncPhone.disabled = false;
          btnSyncPhone.innerHTML = origText;
        }
      });
    }

    const btnSaveWifi = document.getElementById("btnSaveWifi");
    const wifiStatusText = document.getElementById("wifiStatusText");
    const wifiSsidInput = document.getElementById("wifiSsidInput");
    const wifiPasswordInput = document.getElementById("wifiPasswordInput");

    async function checkWifiStatus() {
      if (!wifiStatusText || !state.connected) return;
      try {
        const res = await fetch("/api/v1/wifi");
        if (res.ok) {
          const data = await res.json();
          if (data.connected) {
            wifiStatusText.innerHTML = `<span style="color:#22c55e;">&#9989; Terhubung ke Wi-Fi: <strong>${data.ssid}</strong> (IP: ${data.ip})</span>`;
          } else {
            wifiStatusText.innerHTML = `<span>&#128246; Mode AP Aktif: <strong>JWS-KOTAMOBAGU</strong> (192.168.4.1)</span>`;
          }
        }
      } catch (_) {}
    }

    if (btnSaveWifi) {
      btnSaveWifi.addEventListener("click", async () => {
        const ssid = wifiSsidInput ? wifiSsidInput.value.trim() : "";
        const password = wifiPasswordInput ? wifiPasswordInput.value : "";
        if (!ssid) {
          showAlert("Nama Wi-Fi (SSID) tidak boleh kosong.", "error");
          return;
        }
        btnSaveWifi.disabled = true;
        btnSaveWifi.textContent = "Menyambungkan...";
        try {
          const res = await fetch("/api/v1/wifi", {
            method: "POST",
            headers: {
              "Content-Type": "application/json",
              "X-JWS-Token": state.csrfToken
            },
            body: JSON.stringify({ ssid, password })
          });
          const data = await res.json();
          if (res.ok) {
            showAlert("Konfigurasi Wi-Fi dikirim. ESP32 sedang menyambungkan ke jaringan...", "success");
            setTimeout(checkWifiStatus, 4000);
          } else {
            showAlert(data.error || "Gagal menyimpan Wi-Fi.", "error");
          }
        } catch (_) {
          showAlert("Gagal menghubungi modul.", "error");
        } finally {
          btnSaveWifi.disabled = false;
          btnSaveWifi.innerHTML = "&#128246; Sambungkan Wi-Fi";
        }
      });
    }

    document.getElementById("jwsConfigForm").addEventListener("submit", handleSaveConfig);

    fetchDeviceData();
    setInterval(fetchDeviceData, 1000);
    setInterval(() => {
      if (state.connected) {
        loadXiaozhiStatus();
      }
    }, 2500);
    setTimeout(checkWifiStatus, 1500);
    setInterval(checkWifiStatus, 10000);
    requestAnimationFrame(animateTicker);
  }

  document.addEventListener("DOMContentLoaded", init);
})();
