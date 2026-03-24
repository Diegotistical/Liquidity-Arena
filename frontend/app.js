/**
 * app.js — Liquidity Arena Premium Dashboard v2.0
 *
 * WebSocket client + Chart.js visualizations + particle effects
 * + regime indicator + quantitative metrics + agent activity
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
const $eventRate = $('eventRate');
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

// Regime indicator
const $regimeIndicator = $('regimeIndicator');
const $regimeLabel = $('regimeLabel');

// Metrics
const $metricSharpe = $('metricSharpe');
const $metricAdverse = $('metricAdverse');
const $metricFillRatio = $('metricFillRatio');
const $metricInvVar = $('metricInvVar');
const $metricQuoteLife = $('metricQuoteLife');
const $metricOTR = $('metricOTR');
const $metricSpreadCap = $('metricSpreadCap');
const $metricInvPnl = $('metricInvPnl');
const $metricFees = $('metricFees');

// Latency
const $engineLatency = $('engineLatency');

// ── Chart.js Global Config ───────────────────────────────────────────
Chart.defaults.color = '#6b7280';
Chart.defaults.borderColor = 'rgba(99, 102, 241, 0.06)';
Chart.defaults.font.family = "'JetBrains Mono', monospace";
Chart.defaults.font.size = 10;
// Disable all animations for maximum rendering performance under heavy load
Chart.defaults.animation = false;
Chart.defaults.transitions = { active: { animation: { duration: 0 } } };

const CHART_TOOLTIP = {
    backgroundColor: 'rgba(13, 19, 33, 0.95)',
    borderColor: 'rgba(99, 102, 241, 0.3)',
    borderWidth: 1,
    padding: 10,
    titleFont: { size: 10, family: "'JetBrains Mono', monospace" },
    bodyFont: { size: 10, family: "'JetBrains Mono', monospace" },
    cornerRadius: 6,
    animation: false,
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

// ── WebSocket & Game Loop State ──────────────────────────────────────
let ws = null;
let reconnectAttempt = 0;

let pendingState = null;
let isDirty = false;
let tradeCount = 0;

// Caching to avoid layout thrashing
let tradeTapeRect = null;
function getTradeTapeRect() {
    if (!tradeTapeRect) tradeTapeRect = $tradeTape.getBoundingClientRect();
    return tradeTapeRect;
}
window.addEventListener('resize', () => { tradeTapeRect = null; });

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
            if (data.type === 'update') {
                pendingState = data;
                isDirty = true;
            }
        } catch (e) { /* ignore parse errors */ }
    };
}

// ── Render Loop (Game Loop) ──────────────────────────────────────────
let prevPnl = null;
let prevSpread = null;
let lastUpdateTime = performance.now();
let updateCounter = 0;

function renderLoop() {
    requestAnimationFrame(renderLoop);

    if (!isDirty || !pendingState) return;

    applyStateToDOM(pendingState);
    isDirty = false;
}

function formatPrice(ticks) { return (ticks * 0.01).toFixed(2); }

function flashElement(el) {
    el.classList.remove('flash');
    void el.offsetWidth; // Force reflow
    el.classList.add('flash');
}

function colorValue(value) {
    return value > 0 ? 'var(--positive)' : value < 0 ? 'var(--negative)' : 'var(--text-primary)';
}

function safeText(el, text) {
    if (el.textContent !== text) el.textContent = text;
}

function applyStateToDOM(data) {
    const book = data.book;
    const mm = data.mm;

    // ── Stats Bar ────────────────────────────────────────────────
    safeText($bestBid, `$${formatPrice(book.best_bid)}`);
    safeText($bestAsk, `$${formatPrice(book.best_ask)}`);
    safeText($spread, `${mm.spread} ticks`);
    
    // Only flash if changed
    if (prevSpread !== mm.spread) {
        flashElement($spread);
        prevSpread = mm.spread;
    }

    safeText($mmPnl, `$${mm.pnl.toFixed(2)}`);
    $mmPnl.style.color = colorValue(mm.pnl);

    safeText($pnlBadge, mm.pnl >= 0 ? `+$${mm.pnl.toFixed(0)}` : `-$${Math.abs(mm.pnl).toFixed(0)}`);
    $pnlBadge.style.color = colorValue(mm.pnl);

    safeText($mmInventory, mm.inventory > 0 ? `+${mm.inventory}` : String(mm.inventory));
    $mmInventory.style.color = mm.inventory > 0 ? 'var(--bid-color)' : mm.inventory < 0 ? 'var(--ask-color)' : 'var(--text-primary)';

    safeText($mmFills, mm.fills.toLocaleString());

    // Events/sec calculation
    updateCounter++;
    const now = performance.now();
    if (now - lastUpdateTime > 1000) {
        const rate = updateCounter / ((now - lastUpdateTime) / 1000);
        safeText($eventRate, `${rate.toFixed(0)}`);
        updateCounter = 0;
        lastUpdateTime = now;
    }

    // ── Regime Indicator ─────────────────────────────────────────
    if (data.regime) {
        const regimeMap = { 0: 'CALM', 1: 'VOLATILE', 2: 'NEWS' };
        const regimeClass = { 0: '', 1: 'volatile', 2: 'news' };
        const regimeLabelCurrent = regimeMap[data.regime] || 'CALM';
        if ($regimeLabel.textContent !== regimeLabelCurrent) {
            $regimeIndicator.className = `regime-indicator ${regimeClass[data.regime] || ''}`;
            safeText($regimeLabel, regimeLabelCurrent);
        }
    }

    // ── Engine Latency ───────────────────────────────────────────
    if (data.latency_us) {
        safeText($engineLatency, `${data.latency_us.toFixed(0)}µs`);
    }

    // ── Inventory Gauge ──────────────────────────────────────────
    updateGauge(mm.inventory);

    // ── A-S Model Params ─────────────────────────────────────────
    if (data.mm_model) {
        safeText($reservationPrice, `$${formatPrice(data.mm_model.reservation)}`);
        safeText($sigmaEstimate, data.mm_model.sigma_sq.toFixed(2));
        safeText($optimalSpread, `${data.mm_model.spread.toFixed(1)}`);
    }

    // ── Quantitative Metrics ─────────────────────────────────────
    if (data.metrics) {
        const m = data.metrics;
        updateMetric($metricSharpe, m.sharpe_ratio, v => v.toFixed(2));
        updateMetric($metricAdverse, m.adverse_selection, v => `${v.toFixed(1)} ticks`);
        updateMetric($metricFillRatio, m.fill_ratio, v => (v * 100).toFixed(1) + '%');
        updateMetric($metricInvVar, m.inventory_variance, v => v.toFixed(1));
        updateMetric($metricQuoteLife, m.quote_lifetime_avg, v => v.toFixed(1));
        updateMetric($metricOTR, m.order_to_trade_ratio, v => v.toFixed(1));

        // PnL decomposition
        if (m.spread_capture !== undefined) {
            updateMetric($metricSpreadCap, m.spread_capture, v => `$${v.toFixed(0)}`);
            updateMetric($metricInvPnl, m.inventory_pnl, v => `$${v.toFixed(0)}`);
            updateMetric($metricFees, m.net_fees, v => `$${v.toFixed(0)}`);
        }
    }

    // ── Agent Activity ───────────────────────────────────────────
    if (data.agents) {
        for (const [id, stats] of Object.entries(data.agents)) {
            // Player UI
            if (id === 'HUMAN') {
                const playerPnl = $('playerPnlBadge');
                if (playerPnl) {
                    safeText(playerPnl, stats.pnl >= 0 ? `+$${stats.pnl.toFixed(0)}` : `-$${Math.abs(stats.pnl).toFixed(0)}`);
                    playerPnl.style.color = colorValue(stats.pnl);
                }
                const playerInv = $('playerInv');
                if (playerInv) {
                    safeText(playerInv, stats.inventory > 0 ? `+${stats.inventory}` : String(stats.inventory));
                    playerInv.style.color = stats.inventory > 0 ? 'var(--bid-color)' : stats.inventory < 0 ? 'var(--ask-color)' : 'var(--text-primary)';
                }
                const playerFills = $('playerFills');
                if (playerFills) safeText(playerFills, String(stats.fills));
                continue;
            }

            const pnlEl = $(`agent${id}_pnl`);
            const invEl = $(`agent${id}_inv`) || $(`agent${id}_fills`);
            if (pnlEl) {
                safeText(pnlEl, `$${stats.pnl.toFixed(0)}`);
                pnlEl.style.color = colorValue(stats.pnl);
            }
            if (invEl) {
                if (stats.inventory !== undefined) {
                    safeText(invEl, `inv: ${stats.inventory}`);
                } else {
                    safeText(invEl, `fills: ${stats.fills}`);
                }
            }
        }
    }

    // ── Depth Chart ──────────────────────────────────────────────
    updateDepthChart(book);

    // ── PnL Chart ────────────────────────────────────────────────
    addChartPoint(pnlChart, data.step, mm.pnl);
    pnlChart.data.datasets[0].borderColor = mm.pnl >= 0 ? '#10b981' : '#ef4444';
    pnlChart.data.datasets[0].backgroundColor = mm.pnl >= 0 ? 'rgba(16, 185, 129, 0.06)' : 'rgba(239, 68, 68, 0.06)';
    pnlChart.update('none');

    // ── Spread Chart ─────────────────────────────────────────────
    addChartPoint(spreadChart, data.step, mm.spread);
    spreadChart.update('none');

    // ── Particle Effects ─────────────────────────────────────────
    if (prevPnl !== null && mm.pnl !== prevPnl) {
        const pnlDiff = mm.pnl - prevPnl;
        const color = pnlDiff > 0 ? '#10b981' : '#ef4444';
        const pnlEl = $mmPnl.getBoundingClientRect();
        spawnParticles(pnlEl.left + pnlEl.width / 2, pnlEl.top + pnlEl.height / 2, color, 5);
    }
    prevPnl = mm.pnl;

    // ── Trade Tape ───────────────────────────────────────────────
    // ── Trade Tape ───────────────────────────────────────────────
    if (mm.fills > tradeCount && book.best_bid > 0) {
        const newFills = mm.fills - tradeCount;
        const tradesToSpawn = [];
        for (let i = 0; i < Math.min(newFills, 5); i++) {
            tradesToSpawn.push({
                price: book.best_bid + Math.floor(Math.random() * mm.spread),
                qty: 20 + Math.floor(Math.random() * 80),
                side: Math.random() > 0.5 ? 'buy' : 'sell'
            });
        }
        addTradesBatch(tradesToSpawn);
        tradeCount = mm.fills;
    }
}

function updateMetric(el, value, formatFn) {
    if (el && value !== undefined && value !== null) {
        el.textContent = formatFn(value);
        // Color positive/negative metrics
        if (typeof value === 'number') {
            if (el.id === 'metricSharpe') {
                el.style.color = value > 1 ? 'var(--positive)' : value < 0 ? 'var(--negative)' : 'var(--text-primary)';
            } else if (el.id === 'metricAdverse') {
                el.style.color = value > 0 ? 'var(--negative)' : value < 0 ? 'var(--positive)' : 'var(--text-primary)';
            } else if (el.id === 'metricSpreadCap' || el.id === 'metricInvPnl' || el.id === 'metricFees') {
                el.style.color = colorValue(value);
            }
        }
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
    const maxInv = 500;
    const pct = Math.min(Math.abs(inventory) / maxInv, 1);
    const markerPct = 50 + (inventory / maxInv) * 50;

    $gaugeMarker.style.left = `${Math.max(2, Math.min(98, markerPct))}%`;
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

    // Danger indicator at extreme inventory
    if (pct > 0.8) {
        $gaugeValue.style.textShadow = `0 0 12px ${inventory > 0 ? 'rgba(16, 185, 129, 0.5)' : 'rgba(239, 68, 68, 0.5)'}`;
    } else {
        $gaugeValue.style.textShadow = 'none';
    }
}

function addTradesBatch(trades) {
    if (trades.length === 0) return;

    const empty = $tradeTape.querySelector('.tape-empty');
    if (empty) empty.remove();

    const fragment = document.createDocumentFragment();
    const timeStr = new Date().toLocaleTimeString();
    
    // Spawn particles using cached container rect instead of calculating per-element
    const rect = getTradeTapeRect();
    const spawnX = rect.left + 20;
    const spawnY = rect.top + 15;

    for (const t of trades) {
        const trade = document.createElement('div');
        trade.className = `tape-trade ${t.side}`;
        trade.innerHTML = `
            <span class="tape-price ${t.side === 'buy' ? 'bid-color' : 'ask-color'}">
                $${formatPrice(t.price)}
            </span>
            <span class="tape-qty">${t.qty} × ${t.side.toUpperCase()}</span>
            <span class="tape-time">${timeStr}</span>
        `;
        fragment.appendChild(trade);

        // Particle burst
        spawnParticles(spawnX, spawnY, t.side === 'buy' ? '#10b981' : '#ef4444', 4);
    }

    $tradeTape.prepend(fragment);

    while ($tradeTape.children.length > MAX_TAPE) {
        $tradeTape.removeChild($tradeTape.lastChild);
    }
}

// ── Interactive Scenarios & Trading ──────────────────────────────────
window.sendScenario = function (name) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'scenario', name: name }));
    }
};

window.sendHumanAction = function(action) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const qty = parseInt($('tradeQty').value) || 100;
        const offset = parseInt($('tradeOffset').value) || 0;
        ws.send(JSON.stringify({ type: 'human_action', action, qty, offset }));
    }
};

// Hotkeys for trading
window.addEventListener('keydown', (e) => {
    // Ignore if typing in inputs
    if (e.target.tagName === 'INPUT') return;
    
    switch(e.key) {
        case 'ArrowUp': 
            e.preventDefault();
            window.sendHumanAction('market_buy'); 
            break;
        case 'ArrowDown': 
            e.preventDefault();
            window.sendHumanAction('market_sell'); 
            break;
        case 'ArrowLeft': 
            e.preventDefault();
            window.sendHumanAction('limit_buy'); 
            break;
        case 'ArrowRight': 
            e.preventDefault();
            window.sendHumanAction('limit_sell'); 
            break;
        case 'c':
        case 'C':
            e.preventDefault();
            window.sendHumanAction('cancel_all');
            break;
    }
});

// ── Initialize ───────────────────────────────────────────────────────
connect();
requestAnimationFrame(renderLoop);
