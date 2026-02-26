/**
 * app.js — Liquidity Arena Premium Dashboard
 *
 * WebSocket client + Chart.js visualizations + particle effects
 */

// ── Configuration ────────────────────────────────────────────────────
const WS_URL = `ws://${window.location.hostname || 'localhost'}:8765`;
const MAX_POINTS = 400;
const MAX_TAPE = 20;

// ── DOM Elements ─────────────────────────────────────────────────────
const $ = (id) => document.getElementById(id);
const $bestBid = $('bestBid');
const $bestAsk = $('bestAsk');
const $spread = $('spread');
const $mmPnl = $('mmPnl');
const $mmInventory = $('mmInventory');
const $mmFills = $('mmFills');
const $simStep = $('simStep');
const $statusDot = document.querySelector('.status-dot');
const $statusText = document.querySelector('.status-text');
const $pnlBadge = $('pnlBadge');
const $gaugeFill = $('gaugeFill');
const $gaugeMarker = $('gaugeMarker');
const $gaugeValue = $('gaugeValue');
const $tradeTape = $('tradeTape');
const $reservationPrice = $('reservationPrice');
const $sigmaEstimate = $('sigmaEstimate');
const $optimalSpread = $('optimalSpread');

// ── Chart.js Global Config ───────────────────────────────────────────
Chart.defaults.color = '#6b7280';
Chart.defaults.borderColor = 'rgba(99, 102, 241, 0.06)';
Chart.defaults.font.family = "'JetBrains Mono', monospace";
Chart.defaults.font.size = 10;

const CHART_TOOLTIP = {
    backgroundColor: 'rgba(13, 19, 33, 0.95)',
    borderColor: 'rgba(99, 102, 241, 0.3)',
    borderWidth: 1,
    padding: 10,
    titleFont: { size: 10, family: "'JetBrains Mono', monospace" },
    bodyFont: { size: 10, family: "'JetBrains Mono', monospace" },
    cornerRadius: 6,
};

// ── Depth Chart ──────────────────────────────────────────────────────
const depthChart = new Chart($('depthChart').getContext('2d'), {
    type: 'bar',
    data: {
        labels: [],
        datasets: [
            {
                label: 'Bids',
                data: [],
                backgroundColor: 'rgba(16, 185, 129, 0.55)',
                borderColor: 'rgba(16, 185, 129, 0.85)',
                borderWidth: 1,
                borderRadius: 4,
                borderSkipped: false,
            },
            {
                label: 'Asks',
                data: [],
                backgroundColor: 'rgba(239, 68, 68, 0.55)',
                borderColor: 'rgba(239, 68, 68, 0.85)',
                borderWidth: 1,
                borderRadius: 4,
                borderSkipped: false,
            }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 120, easing: 'easeOutQuart' },
        plugins: {
            legend: { display: true, position: 'top', labels: { usePointStyle: true, pointStyle: 'rectRounded', padding: 14, font: { size: 10 } } },
            tooltip: CHART_TOOLTIP
        },
        scales: {
            x: { title: { display: true, text: 'Price (ticks)', color: '#4b5563', font: { size: 9 } }, grid: { display: false } },
            y: { title: { display: true, text: 'Qty', color: '#4b5563', font: { size: 9 } }, beginAtZero: true, grid: { color: 'rgba(99, 102, 241, 0.04)' } }
        }
    }
});

// ── PnL Chart ────────────────────────────────────────────────────────
const pnlChart = new Chart($('pnlChart').getContext('2d'), {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Cumulative PnL ($)',
            data: [],
            borderColor: '#6366f1',
            backgroundColor: 'rgba(99, 102, 241, 0.06)',
            borderWidth: 1.5,
            fill: true,
            tension: 0.35,
            pointRadius: 0,
            pointHitRadius: 6,
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 80 },
        plugins: {
            legend: { display: false },
            tooltip: { ...CHART_TOOLTIP, callbacks: { label: (ctx) => `PnL: $${ctx.parsed.y.toFixed(2)}` } }
        },
        scales: {
            x: { display: true, grid: { display: false }, ticks: { maxTicksLimit: 8, font: { size: 9 } } },
            y: { grid: { color: 'rgba(99, 102, 241, 0.04)' }, ticks: { callback: (v) => `$${v.toFixed(0)}`, font: { size: 9 } } }
        }
    }
});

// ── Spread Chart ─────────────────────────────────────────────────────
const spreadChart = new Chart($('spreadChart').getContext('2d'), {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Spread (ticks)',
            data: [],
            borderColor: '#f59e0b',
            backgroundColor: 'rgba(245, 158, 11, 0.06)',
            borderWidth: 1.5,
            fill: true,
            tension: 0.3,
            pointRadius: 0,
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 80 },
        plugins: {
            legend: { display: false },
            tooltip: { ...CHART_TOOLTIP, callbacks: { label: (ctx) => `Spread: ${ctx.parsed.y} ticks` } }
        },
        scales: {
            x: { display: true, grid: { display: false }, ticks: { maxTicksLimit: 8, font: { size: 9 } } },
            y: { beginAtZero: true, grid: { color: 'rgba(245, 158, 11, 0.04)' }, ticks: { font: { size: 9 } } }
        }
    }
});

// ── Particle System ──────────────────────────────────────────────────
const particleCanvas = $('particleCanvas');
const pCtx = particleCanvas.getContext('2d');
let particles = [];

function resizeParticleCanvas() {
    particleCanvas.width = window.innerWidth;
    particleCanvas.height = window.innerHeight;
}
resizeParticleCanvas();
window.addEventListener('resize', resizeParticleCanvas);

function spawnParticles(x, y, color, count = 8) {
    for (let i = 0; i < count; i++) {
        particles.push({
            x, y,
            vx: (Math.random() - 0.5) * 4,
            vy: (Math.random() - 0.5) * 4 - 1,
            life: 1,
            decay: 0.02 + Math.random() * 0.03,
            size: 2 + Math.random() * 3,
            color,
        });
    }
}

function updateParticles() {
    pCtx.clearRect(0, 0, particleCanvas.width, particleCanvas.height);
    particles = particles.filter(p => p.life > 0);
    for (const p of particles) {
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.05;
        p.life -= p.decay;
        pCtx.globalAlpha = p.life;
        pCtx.fillStyle = p.color;
        pCtx.beginPath();
        pCtx.arc(p.x, p.y, p.size * p.life, 0, Math.PI * 2);
        pCtx.fill();
    }
    pCtx.globalAlpha = 1;
    requestAnimationFrame(updateParticles);
}
updateParticles();

// ── WebSocket Connection ─────────────────────────────────────────────
let ws = null;
let reconnectAttempt = 0;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        $statusDot.classList.add('connected');
        $statusText.textContent = 'Connected';
        reconnectAttempt = 0;
    };

    ws.onclose = () => {
        $statusDot.classList.remove('connected');
        reconnectAttempt++;
        $statusText.textContent = `Reconnecting (${reconnectAttempt})...`;
        setTimeout(connect, Math.min(2000 * reconnectAttempt, 10000));
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'update') updateDashboard(data);
        } catch (e) { /* ignore parse errors */ }
    };
}

// ── Dashboard Update ─────────────────────────────────────────────────
let prevPnl = null;
let prevSpread = null;
let tradeCount = 0;

function formatPrice(ticks) { return (ticks * 0.01).toFixed(2); }

function flashElement(el) {
    el.classList.remove('flash');
    void el.offsetWidth; // Force reflow
    el.classList.add('flash');
}

function updateDashboard(data) {
    const book = data.book;
    const mm = data.mm;

    // Stats bar
    $bestBid.textContent = `$${formatPrice(book.best_bid)}`;
    $bestAsk.textContent = `$${formatPrice(book.best_ask)}`;
    $spread.textContent = `${mm.spread} ticks`;
    flashElement($spread);

    $mmPnl.textContent = `$${mm.pnl.toFixed(2)}`;
    $mmPnl.style.color = mm.pnl >= 0 ? 'var(--positive)' : 'var(--negative)';
    flashElement($mmPnl);

    $pnlBadge.textContent = mm.pnl >= 0 ? `+$${mm.pnl.toFixed(0)}` : `-$${Math.abs(mm.pnl).toFixed(0)}`;
    $pnlBadge.style.color = mm.pnl >= 0 ? 'var(--positive)' : 'var(--negative)';

    $mmInventory.textContent = mm.inventory > 0 ? `+${mm.inventory}` : mm.inventory;
    $mmInventory.style.color = mm.inventory > 0 ? 'var(--bid-color)' : mm.inventory < 0 ? 'var(--ask-color)' : 'var(--text-primary)';

    $mmFills.textContent = mm.fills.toLocaleString();
    $simStep.textContent = data.step.toLocaleString();

    // Inventory gauge
    updateGauge(mm.inventory);

    // A-S model params (if available)
    if (data.mm_model) {
        $reservationPrice.textContent = `$${formatPrice(data.mm_model.reservation)}`;
        $sigmaEstimate.textContent = data.mm_model.sigma_sq.toFixed(2);
        $optimalSpread.textContent = `${data.mm_model.spread.toFixed(1)}`;
    }

    // Depth chart
    updateDepthChart(book);

    // PnL chart
    addChartPoint(pnlChart, data.step, mm.pnl);
    // Dynamic PnL line color
    pnlChart.data.datasets[0].borderColor = mm.pnl >= 0 ? '#10b981' : '#ef4444';
    pnlChart.data.datasets[0].backgroundColor = mm.pnl >= 0 ? 'rgba(16, 185, 129, 0.06)' : 'rgba(239, 68, 68, 0.06)';
    pnlChart.update('none');

    // Spread chart
    addChartPoint(spreadChart, data.step, mm.spread);
    spreadChart.update('none');

    // Particle effect on PnL change
    if (prevPnl !== null && mm.pnl !== prevPnl) {
        const pnlDiff = mm.pnl - prevPnl;
        const color = pnlDiff > 0 ? '#10b981' : '#ef4444';
        const pnlEl = $mmPnl.getBoundingClientRect();
        spawnParticles(pnlEl.left + pnlEl.width / 2, pnlEl.top + pnlEl.height / 2, color, 5);
    }
    prevPnl = mm.pnl;

    // Trade tape (simulate trades from fills)
    if (mm.fills > tradeCount && book.best_bid > 0) {
        const newFills = mm.fills - tradeCount;
        for (let i = 0; i < Math.min(newFills, 3); i++) {
            addTradeToTape(book.best_bid + Math.floor(Math.random() * mm.spread),
                20 + Math.floor(Math.random() * 80),
                Math.random() > 0.5 ? 'buy' : 'sell');
        }
        tradeCount = mm.fills;
    }
}

function addChartPoint(chart, label, value) {
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(value);
    if (chart.data.labels.length > MAX_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }
}

function updateDepthChart(book) {
    const bidData = [], askData = [], labels = [];

    for (let i = book.bid_prices.length - 1; i >= 0; i--) {
        if (book.bid_prices[i] > 0) {
            labels.push(formatPrice(book.bid_prices[i]));
            bidData.push(book.bid_quantities[i]);
            askData.push(0);
        }
    }
    for (let i = 0; i < book.ask_prices.length; i++) {
        if (book.ask_prices[i] > 0) {
            labels.push(formatPrice(book.ask_prices[i]));
            bidData.push(0);
            askData.push(book.ask_quantities[i]);
        }
    }

    depthChart.data.labels = labels;
    depthChart.data.datasets[0].data = bidData;
    depthChart.data.datasets[1].data = askData;
    depthChart.update('none');
}

function updateGauge(inventory) {
    const maxInv = 100;
    const pct = Math.min(Math.abs(inventory) / maxInv, 1);
    const markerPct = 50 + (inventory / maxInv) * 50;

    $gaugeMarker.style.left = `${markerPct}%`;
    $gaugeValue.textContent = inventory > 0 ? `+${inventory}` : inventory;

    if (inventory > 0) {
        $gaugeFill.style.left = '50%';
        $gaugeFill.style.width = `${pct * 50}%`;
        $gaugeFill.classList.remove('negative');
        $gaugeValue.style.color = 'var(--bid-color)';
    } else if (inventory < 0) {
        $gaugeFill.style.left = `${50 - pct * 50}%`;
        $gaugeFill.style.width = `${pct * 50}%`;
        $gaugeFill.classList.add('negative');
        $gaugeValue.style.color = 'var(--ask-color)';
    } else {
        $gaugeFill.style.width = '0';
        $gaugeValue.style.color = 'var(--text-primary)';
    }
}

function addTradeToTape(price, qty, side) {
    // Remove empty placeholder
    const empty = $tradeTape.querySelector('.tape-empty');
    if (empty) empty.remove();

    const trade = document.createElement('div');
    trade.className = `tape-trade ${side}`;
    trade.innerHTML = `
        <span class="tape-price ${side === 'buy' ? 'bid-color' : 'ask-color'}">
            $${formatPrice(price)}
        </span>
        <span class="tape-qty">${qty} × ${side.toUpperCase()}</span>
        <span class="tape-time">${new Date().toLocaleTimeString()}</span>
    `;

    $tradeTape.prepend(trade);

    // Limit tape length
    while ($tradeTape.children.length > MAX_TAPE) {
        $tradeTape.removeChild($tradeTape.lastChild);
    }

    // Particle burst for trades
    const rect = trade.getBoundingClientRect();
    spawnParticles(rect.left + 20, rect.top + 15, side === 'buy' ? '#10b981' : '#ef4444', 4);
}

// ── Initialize ───────────────────────────────────────────────────────
connect();
