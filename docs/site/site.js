// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PS2Native website's script (docs/index.html). No dependencies.
//
// Everything here is an addition: without it the page shows all of its
// content and nothing moves. Motion is on when html[data-motion] is "on",
// which the page's first script sets from the reader's "reduce motion"
// preference and from the switch in the footer; every part below starts and
// stops with it, and nothing is drawn for a part that is not on screen.

(function () {
	'use strict';

	const doc = document;
	const root = doc.documentElement;
	const $ = (sel, el) => (el || doc).querySelector(sel);
	const $$ = (sel, el) => Array.prototype.slice.call((el || doc).querySelectorAll(sel));
	const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

	// The page's first script waits for this before it gives up on motion.
	window.ps2nSite = true;

	// ---- Motion: on or off, and who wants to know -----------------------------

	const Motion = (function () {
		const subs = [];
		let on = root.getAttribute('data-motion') !== 'off';
		return {
			get on() { return on; },
			set(value, remember) {
				on = !!value;
				root.setAttribute('data-motion', on ? 'on' : 'off');
				if (remember) {
					try { localStorage.setItem('ps2n-motion', on ? 'on' : 'off'); } catch (e) { /* private mode */ }
				}
				subs.forEach((fn) => safely(() => fn(on)));
			},
			sub(fn) { subs.push(fn); },
		};
	})();

	function safely(fn) {
		try { fn(); } catch (e) { if (window.console) console.error('[ps2n]', e); }
	}

	// ---- One animation frame loop for everything that draws --------------------

	const Loop = (function () {
		const fns = new Set();
		let id = 0;
		let last = 0;
		function frame(now) {
			const dt = Math.min(64, now - last);
			last = now;
			fns.forEach((fn) => {
				try { fn(now, dt); } catch (e) { fns.delete(fn); if (window.console) console.error('[ps2n]', e); }
			});
			id = fns.size ? requestAnimationFrame(frame) : 0;
		}
		return {
			add(fn) {
				if (fns.has(fn)) return;
				fns.add(fn);
				if (!id) { last = performance.now(); id = requestAnimationFrame(frame); }
			},
			remove(fn) { fns.delete(fn); },
		};
	})();

	// ---- Helpers ---------------------------------------------------------------

	const hasIO = 'IntersectionObserver' in window;

	// Calls back with true and false as the element comes on and goes off screen.
	function watch(el, onChange, margin) {
		if (!hasIO) { onChange(true); return; }
		new IntersectionObserver((entries) => {
			entries.forEach((entry) => onChange(entry.isIntersecting));
		}, { rootMargin: margin || '60px' }).observe(el);
	}

	// Calls back once, the first time that much of the element is on screen.
	function once(el, fn, threshold) {
		if (!hasIO) { fn(); return; }
		const io = new IntersectionObserver((entries) => {
			entries.forEach((entry) => {
				if (!entry.isIntersecting) return;
				io.disconnect();
				fn();
			});
		}, { threshold: threshold || 0.2 });
		io.observe(el);
	}

	const resizeFns = [];
	let resizeTimer = 0;
	window.addEventListener('resize', () => {
		clearTimeout(resizeTimer);
		resizeTimer = setTimeout(() => resizeFns.forEach(safely), 140);
	});
	function onResize(fn) { resizeFns.push(fn); }

	// Sizes a canvas for the screen's pixel density; drawing is in CSS pixels.
	function fit(canvas, width, height) {
		const dpr = Math.min(window.devicePixelRatio || 1, 2);
		const w = Math.max(1, Math.round(width * dpr));
		const h = Math.max(1, Math.round(height * dpr));
		if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
		const ctx = canvas.getContext('2d');
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		return ctx;
	}

	// A small repeatable random number generator, so that a pattern is the same
	// on every visit.
	function lcg(seed) {
		let s = seed >>> 0;
		return () => {
			s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
			return s / 4294967296;
		};
	}

	// ---- Navigation and the progress line ---------------------------------------

	let updateTimeline = null;

	function initNav() {
		const nav = $('#nav');
		const bar = $('.progress');
		if (!nav) return;
		const toggle = $('.nav__toggle', nav);
		const links = $$('.nav__links a', nav);

		let queued = false;
		function update() {
			queued = false;
			const y = window.scrollY || root.scrollTop || 0;
			nav.classList.toggle('is-stuck', y > 12);
			const max = root.scrollHeight - window.innerHeight;
			if (bar) bar.style.transform = 'scaleX(' + (max > 0 ? clamp(y / max, 0, 1) : 0).toFixed(4) + ')';
			if (updateTimeline) updateTimeline();
		}
		function request() {
			if (queued) return;
			queued = true;
			requestAnimationFrame(update);
		}
		window.addEventListener('scroll', request, { passive: true });
		onResize(update);
		update();

		function close() {
			nav.classList.remove('is-open');
			if (toggle) toggle.setAttribute('aria-expanded', 'false');
		}
		if (toggle) {
			toggle.addEventListener('click', () => {
				const open = nav.classList.toggle('is-open');
				toggle.setAttribute('aria-expanded', open ? 'true' : 'false');
			});
			doc.addEventListener('keydown', (e) => {
				if (e.key === 'Escape' && nav.classList.contains('is-open')) { close(); toggle.focus(); }
			});
		}
		links.forEach((a) => a.addEventListener('click', close));

		// Which section is being read. A section without a link of its own lights
		// the link named in its data-nav, or none.
		if (!hasIO) return;
		const byId = {};
		links.forEach((a) => { byId[a.getAttribute('href').slice(1)] = a; });
		const io = new IntersectionObserver((entries) => {
			entries.forEach((entry) => {
				if (!entry.isIntersecting) return;
				const id = entry.target.getAttribute('data-nav') || entry.target.id;
				links.forEach((a) => {
					const active = a === byId[id];
					a.classList.toggle('is-active', active);
					if (active) a.setAttribute('aria-current', 'true'); else a.removeAttribute('aria-current');
				});
			});
		}, { rootMargin: '-46% 0px -52% 0px' });
		$$('main > section').forEach((section) => io.observe(section));
	}

	// ---- Things that come in when they are scrolled to --------------------------

	function initReveal() {
		const els = $$('[data-reveal]');
		if (!hasIO) { els.forEach((el) => el.classList.add('is-in')); return; }
		const io = new IntersectionObserver((entries) => {
			entries.forEach((entry) => {
				if (!entry.isIntersecting) return;
				entry.target.classList.add('is-in');
				io.unobserve(entry.target);
			});
		}, { rootMargin: '0px 0px -9% 0px', threshold: 0.06 });
		els.forEach((el) => io.observe(el));
	}

	// ---- Labels that decode themselves out of hexadecimal ------------------------

	function initScramble() {
		const GLYPHS = '0123456789ABCDEF';
		$$('[data-scramble]').forEach((el) => {
			const text = el.textContent;
			// The words stay for a screen reader; what is scrambled is a copy.
			const spoken = doc.createElement('span');
			spoken.className = 'sr-only';
			spoken.textContent = text;
			const shown = doc.createElement('span');
			shown.setAttribute('aria-hidden', 'true');
			shown.textContent = text;
			el.textContent = '';
			el.appendChild(spoken);
			el.appendChild(shown);

			let raf = 0;
			function run() {
				if (!Motion.on) return;
				const start = performance.now();
				const duration = 380 + text.length * 24;
				let lastDraw = 0;
				cancelAnimationFrame(raf);
				const tick = (now) => {
					const p = clamp((now - start) / duration, 0, 1);
					if (p >= 1) { shown.textContent = text; return; }
					if (now - lastDraw > 38) {
						lastDraw = now;
						const settled = Math.floor(p * text.length);
						let out = text.slice(0, settled);
						for (let i = settled; i < text.length; i++) {
							const c = text.charAt(i);
							out += /[A-Za-z0-9]/.test(c) ? GLYPHS.charAt((Math.random() * 16) | 0) : c;
						}
						shown.textContent = out;
					}
					raf = requestAnimationFrame(tick);
				};
				raf = requestAnimationFrame(tick);
			}
			once(el, run, 0.6);
			Motion.sub((on) => { if (!on) { cancelAnimationFrame(raf); shown.textContent = text; } });
		});
	}

	// ---- Numbers that count up to what was measured ------------------------------

	function initCounters() {
		$$('[data-count]').forEach((el) => {
			const final = el.textContent;
			const to = parseFloat(el.getAttribute('data-count'));
			const from = parseFloat(el.getAttribute('data-count-from') || '0');
			const decimals = parseInt(el.getAttribute('data-decimals') || '0', 10);
			const duration = parseInt(el.getAttribute('data-dur') || '1700', 10);
			if (!isFinite(to)) return;
			const format = (v) => v.toLocaleString('en-US', { minimumFractionDigits: decimals, maximumFractionDigits: decimals });
			const box = el.parentElement;
			let raf = 0;

			function finish() {
				cancelAnimationFrame(raf);
				el.textContent = final;
				box.classList.remove('is-counting');
			}
			function run() {
				if (!Motion.on) return;
				box.classList.add('is-counting');
				const start = performance.now();
				const tick = (now) => {
					const p = clamp((now - start) / duration, 0, 1);
					if (p >= 1) { finish(); return; }
					const eased = 1 - Math.pow(1 - p, 4);
					el.textContent = format(from + (to - from) * eased);
					raf = requestAnimationFrame(tick);
				};
				el.textContent = format(from);
				raf = requestAnimationFrame(tick);
			}
			once(el, run, 0.6);
			Motion.sub((on) => { if (!on) finish(); });
		});
	}

	// ---- A light that follows the pointer over a card ----------------------------

	function initSpot() {
		$$('[data-spot]').forEach((group) => {
			group.addEventListener('pointermove', (e) => {
				const card = e.target.closest ? e.target.closest('.card, .tile') : null;
				if (!card) return;
				const r = card.getBoundingClientRect();
				card.style.setProperty('--mx', (e.clientX - r.left).toFixed(1) + 'px');
				card.style.setProperty('--my', (e.clientY - r.top).toFixed(1) + 'px');
			});
		});
	}

	// ---- An edge that dissolves into squares -------------------------------------

	function initDissolve() {
		$$('[data-dissolve]').forEach((box) => {
			const canvas = $('canvas', box);
			const colour = box.getAttribute('data-dissolve');
			if (!canvas) return;
			function draw() {
				const w = box.clientWidth;
				const h = box.clientHeight;
				if (!w || !h) return;
				const ctx = fit(canvas, w, h);
				ctx.clearRect(0, 0, w, h);
				const cell = w < 700 ? 11 : 14;
				const cols = Math.ceil(w / cell);
				const rows = Math.ceil(h / cell);
				const rnd = lcg(20261111);
				ctx.fillStyle = colour;
				for (let y = 0; y < rows; y++) {
					const p = Math.pow((y + 1) / rows, 1.9);
					const top = h - (rows - y) * cell;
					for (let x = 0; x < cols; x++) {
						if (rnd() < p) ctx.fillRect(x * cell, top, cell + 0.5, cell + 0.5);
					}
				}
			}
			draw();
			onResize(draw);
		});
	}

	// ---- The hero's field of squares ---------------------------------------------

	function initField() {
		const canvas = $('.hero__field');
		if (!canvas) return;
		const hero = canvas.parentElement;
		let ctx = null;
		let W = 0, H = 0, cell = 26, cols = 0, rows = 0;
		let seeds = new Float32Array(0);
		let colWave = new Float32Array(0);
		let rowWave = new Float32Array(0);
		let pointerX = 0, pointerY = 0, pointerOn = 0, pointerWant = 0;
		let sparks = [];
		let nextSpark = 0;
		let lastDraw = 0;
		let visible = false;

		function resize() {
			W = hero.clientWidth;
			H = hero.clientHeight;
			if (!W || !H) return;
			cell = W < 700 ? 20 : 26;
			ctx = fit(canvas, W, H);
			cols = Math.ceil(W / cell) + 1;
			rows = Math.ceil(H / cell) + 1;
			seeds = new Float32Array(cols * rows);
			colWave = new Float32Array(cols);
			rowWave = new Float32Array(rows);
			const rnd = lcg(52004);
			for (let i = 0; i < seeds.length; i++) seeds[i] = rnd();
			sparks = [];
			draw(Motion.on ? performance.now() : 0);
		}

		function draw(now) {
			if (!ctx) return;
			const t = now / 1000;
			const size = cell - 3;
			const cx = W / 2;
			const cy = H * 0.42;
			ctx.clearRect(0, 0, W, H);
			ctx.fillStyle = '#0060FC';
			for (let x = 0; x < cols; x++) colWave[x] = Math.sin(x * 0.31 + t * 0.33);
			for (let y = 0; y < rows; y++) rowWave[y] = Math.sin(y * 0.43 - t * 0.26);
			for (let y = 0; y < rows; y++) {
				const py = y * cell;
				const dy = (py - cy) / (H * 0.5);
				for (let x = 0; x < cols; x++) {
					const px = x * cell;
					const seed = seeds[y * cols + x];
					const n = (colWave[x] + rowWave[y] + Math.sin((x + y) * 0.19 + t * 0.2 + seed * 6.2832)) / 3;
					let a = n > 0.16 ? (n - 0.16) * 0.31 : 0;
					if (seed > 0.988) a += 0.05;
					// Calm where the words are.
					const dx = (px - cx) / (W * 0.5);
					const d = Math.sqrt(dx * dx * 0.9 + dy * dy * 0.75);
					a *= clamp((d - 0.3) * 1.6, 0.08, 1);
					if (pointerOn > 0.01) {
						const ex = px + size / 2 - pointerX;
						const ey = py + size / 2 - pointerY;
						const dist2 = ex * ex + ey * ey;
						if (dist2 < 36100) {
							const f = 1 - Math.sqrt(dist2) / 190;
							a += f * f * 0.34 * pointerOn;
						}
					}
					if (a > 0.012) {
						ctx.globalAlpha = a > 0.62 ? 0.62 : a;
						ctx.fillRect(px, py, size, size);
					}
				}
			}
			// Now and then one square lights up and fades.
			if (sparks.length) {
				ctx.fillStyle = '#6BA6FF';
				sparks = sparks.filter((s) => now - s.t < 1500);
				sparks.forEach((s) => {
					const k = 1 - (now - s.t) / 1500;
					ctx.globalAlpha = k * k * 0.5;
					ctx.fillRect(s.x * cell, s.y * cell, size, size);
				});
			}
			ctx.globalAlpha = 1;
		}

		function tick(now) {
			if (now - lastDraw < 30) return;
			lastDraw = now;
			pointerOn += (pointerWant - pointerOn) * 0.12;
			if (now > nextSpark) {
				nextSpark = now + 180 + Math.random() * 420;
				const x = (Math.random() * cols) | 0;
				const y = (Math.random() * rows) | 0;
				// Not behind the words.
				const dx = (x * cell - W / 2) / (W * 0.5);
				const dy = (y * cell - H * 0.42) / (H * 0.5);
				if (dx * dx * 0.9 + dy * dy * 0.75 > 0.3) sparks.push({ x: x, y: y, t: now });
			}
			draw(now);
		}

		function sync() {
			if (visible && Motion.on) Loop.add(tick);
			else Loop.remove(tick);
			if (!Motion.on) { pointerOn = 0; sparks = []; draw(0); }
		}

		hero.addEventListener('pointermove', (e) => {
			const r = hero.getBoundingClientRect();
			pointerX = e.clientX - r.left;
			pointerY = e.clientY - r.top;
			pointerWant = 1;
		});
		hero.addEventListener('pointerleave', () => { pointerWant = 0; });

		resize();
		onResize(resize);
		watch(hero, (v) => { visible = v; sync(); });
		Motion.sub(sync);
	}

	// ---- The logo, put together out of squares ------------------------------------
	//
	// The squares are sampled from the logo's own picture, so what they make is
	// the logo and not a drawing of it. When they have landed the picture itself
	// takes their place.

	function initLogo() {
		const box = $('[data-logo]');
		if (!box) return;
		const img = $('.hero__logo-img', box);
		const canvas = $('.hero__logo-fx', box);
		if (!img || !canvas) return;

		let ctx = null;
		let CW = 0, CH = 0;
		let groups = [];
		let started = 0;
		let length = 0;
		let running = false;

		function resolve() {
			running = false;
			Loop.remove(tick);
			box.classList.remove('is-flying');
			box.classList.add('is-resolved');
		}

		function build() {
			const w = img.clientWidth;
			const h = img.clientHeight;
			CW = canvas.clientWidth;
			CH = canvas.clientHeight;
			if (!w || !h || !CW || !CH) return false;
			ctx = fit(canvas, CW, CH);
			ctx.clearRect(0, 0, CW, CH);

			const cell = w >= 440 ? 4 : 3;
			const cols = Math.round(w / cell);
			const rows = Math.round(h / cell);
			const cw = w / cols;
			const ch = h / rows;
			const small = doc.createElement('canvas');
			small.width = cols;
			small.height = rows;
			let data;
			try {
				const sctx = small.getContext('2d', { willReadFrequently: true });
				sctx.imageSmoothingEnabled = true;
				sctx.imageSmoothingQuality = 'high';
				sctx.drawImage(img, 0, 0, cols, rows);
				data = sctx.getImageData(0, 0, cols, rows).data;
			} catch (e) {
				return false;
			}

			const ox = (CW - w) / 2;
			const oy = (CH - h) / 2;
			const blue = { rgb: [0, 0, 0], weight: 0, parts: [] };
			const white = { rgb: [0, 0, 0], weight: 0, parts: [] };
			length = 0;
			for (let y = 0; y < rows; y++) {
				for (let x = 0; x < cols; x++) {
					const i = (y * cols + x) * 4;
					const alpha = data[i + 3] / 255;
					if (alpha < 0.14) continue;
					const r = data[i], g = data[i + 1], b = data[i + 2];
					const group = b - r > 60 ? blue : white;
					group.rgb[0] += r * alpha;
					group.rgb[1] += g * alpha;
					group.rgb[2] += b * alpha;
					group.weight += alpha;
					const across = x / cols;
					const delay = across * 520 + Math.random() * 380;
					const time = 720 + Math.random() * 460;
					const tx = ox + x * cw;
					const ty = oy + y * ch;
					group.parts.push({
						tx: tx,
						ty: ty,
						sx: tx - (150 + Math.random() * 300),
						sy: ty + (Math.random() - 0.5) * 230,
						bend: (Math.random() - 0.5) * 70,
						alpha: alpha,
						delay: delay,
						time: time,
					});
					if (delay + time > length) length = delay + time;
				}
			}
			groups = [blue, white].filter((g) => g.parts.length);
			groups.forEach((g) => {
				g.colour = 'rgb(' + g.rgb.map((v) => Math.round(v / g.weight)).join(',') + ')';
				g.cw = cw;
				g.ch = ch;
			});
			return groups.length > 0;
		}

		function tick(now) {
			const t = now - started;
			// Fade what was drawn before, so that moving squares leave a trail.
			ctx.globalCompositeOperation = 'destination-out';
			ctx.globalAlpha = 0.42;
			ctx.fillRect(0, 0, CW, CH);
			ctx.globalCompositeOperation = 'source-over';
			for (let k = 0; k < groups.length; k++) {
				const g = groups[k];
				ctx.fillStyle = g.colour;
				const parts = g.parts;
				for (let i = 0; i < parts.length; i++) {
					const p = parts[i];
					const u = (t - p.delay) / p.time;
					if (u <= 0) continue;
					if (u >= 1) {
						ctx.globalAlpha = p.alpha;
						ctx.fillRect(p.tx, p.ty, g.cw + 0.3, g.ch + 0.3);
						continue;
					}
					const e = 1 - Math.pow(1 - u, 3);
					const grow = 1 + (1 - e) * 1.5;
					const x = p.sx + (p.tx - p.sx) * e;
					const y = p.sy + (p.ty - p.sy) * e + Math.sin(e * Math.PI) * p.bend;
					ctx.globalAlpha = p.alpha * Math.min(1, u * 4);
					ctx.fillRect(x, y, g.cw * grow, g.ch * grow);
				}
			}
			ctx.globalAlpha = 1;
			if (t > length + 120) resolve();
		}

		function play() {
			if (!Motion.on || running) return;
			if (!build()) { resolve(); return; }
			running = true;
			box.classList.remove('is-resolved');
			box.classList.add('is-flying');
			started = performance.now();
			Loop.add(tick);
		}

		function begin() {
			if (!Motion.on) { resolve(); return; }
			const go = () => safely(play);
			if (img.decode) img.decode().then(go, go);
			else if (img.complete) go();
			else img.addEventListener('load', go, { once: true });
		}

		img.addEventListener('error', resolve);
		box.addEventListener('click', () => { if (!running && Motion.on && img.complete) play(); });
		onResize(() => { if (running) resolve(); });
		Motion.sub((on) => { if (!on && running) resolve(); });
		begin();
	}

	// ---- One loop, both sides ------------------------------------------------------

	function initTranslate() {
		const box = $('[data-xl8]');
		if (!box) return;
		const src = $$('.code--src li[data-i]', box);
		const dst = $$('.code--dst li', box);
		const accEl = $('[data-acc]', box);
		const totalEl = $('[data-total]', box);
		const token = $('.xl8__token', box);
		const replay = $('[data-xl8-replay]');
		const notes = $$('[data-note]');
		if (!src.length || !dst.length || !accEl) return;

		const cost = (li) => parseInt(li.getAttribute('data-cost'), 10) || 0;
		const total = src.reduce((sum, li) => sum + cost(li), 0);
		const steps = ['p'].concat(src.map((li, i) => String(i)), ['e']);
		let timers = [];
		let playing = false;

		const later = (fn, ms) => { timers.push(setTimeout(fn, ms)); };
		function stopTimers() { timers.forEach(clearTimeout); timers = []; }
		function cool() {
			src.forEach((li) => li.classList.remove('is-hot'));
			dst.forEach((li) => li.classList.remove('is-hot'));
			notes.forEach((n) => n.classList.remove('is-hot'));
		}

		// Everything shown, nothing lit: how the part looks when it is not playing.
		function settle() {
			stopTimers();
			playing = false;
			box.classList.remove('is-armed', 'is-playing');
			dst.forEach((li) => li.classList.add('is-on'));
			src.forEach((li) => li.classList.remove('is-done'));
			cool();
			accEl.textContent = String(total);
			if (totalEl) totalEl.classList.remove('is-lit');
		}

		// A square that carries an instruction across to its line of C++.
		function fly(fromEl, toEl) {
			if (!token || !token.animate) return;
			const b = box.getBoundingClientRect();
			const f = fromEl.getBoundingClientRect();
			const t = toEl.getBoundingClientRect();
			const pane = fromEl.closest('.pane').getBoundingClientRect();
			const stacked = t.top >= pane.bottom;
			const x0 = (stacked ? Math.min(f.left + 150, pane.right - 24) : pane.right - 14) - b.left;
			const y0 = f.top + f.height / 2 - b.top;
			const x1 = t.left + 12 - b.left;
			const y1 = t.top + t.height / 2 - b.top;
			const at = (k) => 'translate(' + (x0 + (x1 - x0) * k).toFixed(1) + 'px,' + (y0 + (y1 - y0) * k).toFixed(1) + 'px)';
			token.animate([
				{ transform: at(0) + ' scale(.5)', opacity: 0 },
				{ transform: at(0.12) + ' scale(1)', opacity: 1, offset: 0.18 },
				{ transform: at(1) + ' scale(1)', opacity: 1, offset: 0.86 },
				{ transform: at(1) + ' scale(.3)', opacity: 0 },
			], { duration: 470, easing: 'cubic-bezier(.45,0,.2,1)' });
		}

		function play() {
			if (!Motion.on) { settle(); return; }
			stopTimers();
			playing = true;
			cool();
			box.classList.add('is-armed', 'is-playing');
			dst.forEach((li) => li.classList.remove('is-on'));
			src.forEach((li) => li.classList.remove('is-done'));
			accEl.textContent = '0';
			if (totalEl) totalEl.classList.remove('is-lit');

			let at = 450;
			let acc = 0;
			steps.forEach((step) => {
				const lines = dst.filter((li) => li.getAttribute('data-step') === step);
				const i = step === 'p' || step === 'e' ? -1 : parseInt(step, 10);
				const begin = at;
				const lead = i >= 0 ? 310 : 0;
				later(() => {
					src.forEach((li, k) => li.classList.toggle('is-hot', k === i));
					if (i >= 0 && lines[0]) fly(src[i], lines[0]);
				}, begin);
				lines.forEach((li, k) => later(() => li.classList.add('is-on', 'is-hot'), begin + lead + k * 85));
				if (i >= 0) {
					acc += cost(src[i]);
					const sofar = acc;
					later(() => { accEl.textContent = String(sofar); }, begin + lead);
				}
				if (step === 'e' && totalEl) later(() => totalEl.classList.add('is-lit'), begin + 160);
				const hold = lead + (i >= 0 ? 330 : 300) + lines.length * 85;
				later(() => {
					lines.forEach((li) => li.classList.remove('is-hot'));
					if (i >= 0) src[i].classList.add('is-done');
				}, begin + hold);
				at = begin + hold + 40;
			});
			later(settle, at + 1500);
		}

		// Pointing at an instruction lights the C++ made from it, and the other
		// way round; pointing at a note lights the lines it is about.
		function linked(li) {
			if (li.hasAttribute('data-i')) return li.getAttribute('data-i');
			return li.getAttribute('data-link');
		}
		box.addEventListener('pointerover', (e) => {
			if (playing) return;
			const li = e.target.closest ? e.target.closest('.code li') : null;
			cool();
			if (!li) return;
			const key = linked(li);
			if (key === null || key === '') return;
			if (src[+key]) src[+key].classList.add('is-hot');
			dst.forEach((d) => { if (d.getAttribute('data-link') === key) d.classList.add('is-hot'); });
		});
		box.addEventListener('pointerleave', () => { if (!playing) cool(); });
		notes.forEach((note) => {
			const n = note.getAttribute('data-note');
			note.addEventListener('pointerenter', () => {
				if (playing) return;
				cool();
				note.classList.add('is-hot');
				dst.forEach((d) => {
					if ((d.getAttribute('data-notes') || '').split(' ').indexOf(n) >= 0) d.classList.add('is-hot');
				});
				if (n === '3') src.forEach((s) => s.classList.add('is-hot'));
			});
			note.addEventListener('pointerleave', () => { if (!playing) cool(); });
		});

		if (replay) replay.addEventListener('click', play);
		Motion.sub((on) => { if (!on) settle(); });

		if (Motion.on) {
			// Hidden until it is scrolled to, then played once.
			box.classList.add('is-armed');
			dst.forEach((li) => li.classList.remove('is-on'));
			once(box, play, 0.3);
		} else {
			settle();
		}
	}

	// ---- Two runs, compared moment by moment ----------------------------------------
	//
	// An illustration of the comparison, not a recording of one: each column is a
	// moment at which two runs can be compared, drawn as the same nine-bit
	// pattern in both lanes.

	function initSync() {
		const fig = $('[data-sync]');
		if (!fig) return;
		const view = $('.lanes__view', fig);
		const canvas = view ? $('canvas', view) : null;
		if (!canvas) return;

		const PX = 5;        // one square of a pattern
		const STEP = 6.5;    // from one square to the next
		const GLYPH = STEP * 2 + PX;
		const PITCH = 31;    // from one moment to the next
		const SPEED = 42;    // pixels a second
		let ctx = null;
		let W = 0, H = 0;
		let offset = 0;
		let visible = false;

		function pattern(n) {
			let h = Math.imul(n ^ 0x9E3779B9, 2654435761) >>> 0;
			h ^= h >>> 15;
			h = Math.imul(h, 2246822519) >>> 0;
			h ^= h >>> 13;
			const bits = h & 511;
			return bits || 186;
		}

		function glyph(x, y, bits) {
			for (let r = 0; r < 3; r++) {
				for (let c = 0; c < 3; c++) {
					if (bits & (1 << (r * 3 + c))) ctx.fillRect(x + c * STEP, y + r * STEP, PX, PX);
				}
			}
		}

		function resize() {
			W = view.clientWidth;
			H = view.clientHeight;
			if (!W || !H) return;
			ctx = fit(canvas, W, H);
			draw();
		}

		function draw() {
			if (!ctx) return;
			ctx.clearRect(0, 0, W, H);
			const gate = Math.round(W * 0.58);
			const ya = Math.round(H * 0.33 - GLYPH / 2);
			const yb = Math.round(H * 0.67 - GLYPH / 2);
			const mid = Math.round(H * 0.5);

			const first = Math.floor((offset - GLYPH) / PITCH);
			const last = Math.ceil((offset + W) / PITCH);
			let pulse = 0;
			for (let n = first; n <= last; n++) {
				const x = n * PITCH - offset;
				const centre = x + GLYPH / 2;
				const bits = pattern(n);
				const done = centre < gate;
				const flash = done ? clamp(1 - (gate - centre) / 64, 0, 1) : 0;
				if (flash > pulse) pulse = flash;

				// Stock PCSX2 above, PS2Native below: the same pattern.
				ctx.globalAlpha = done ? 0.95 : 0.4;
				ctx.fillStyle = flash > 0 ? mix('#8A94A8', flash) : '#8A94A8';
				glyph(x, ya, bits);
				ctx.fillStyle = flash > 0 ? mix('#2F7DFF', flash) : (done ? '#2F7DFF' : '#1D56C4');
				glyph(x, yb, bits);

				if (done) {
					// Compared, and equal.
					const fade = clamp(0.3 + flash * 0.7, 0, 1);
					ctx.globalAlpha = fade;
					ctx.fillStyle = '#B9D4FF';
					ctx.fillRect(centre - 5, mid - 4, 10, 2);
					ctx.fillRect(centre - 5, mid + 2, 10, 2);
				}
			}

			// Where the two are compared.
			ctx.globalAlpha = 0.55 + pulse * 0.45;
			ctx.fillStyle = '#6BA6FF';
			ctx.fillRect(gate - 1, 10, 2, H - 20);
			ctx.globalAlpha = 0.2 + pulse * 0.5;
			ctx.fillRect(gate - 7 - pulse * 3, 10, 14 + pulse * 6, H - 20);
			ctx.globalAlpha = 1;
		}

		// A colour on its way to white, for the moment of the comparison.
		function mix(hex, k) {
			const v = parseInt(hex.slice(1), 16);
			const r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
			const f = (c) => Math.round(c + (255 - c) * k * 0.85);
			return 'rgb(' + f(r) + ',' + f(g) + ',' + f(b) + ')';
		}

		function tick(now, dt) {
			offset += SPEED * dt / 1000;
			draw();
		}
		function sync() {
			if (visible && Motion.on) Loop.add(tick);
			else Loop.remove(tick);
			if (!Motion.on) draw();
		}

		resize();
		onResize(resize);
		watch(fig, (v) => { visible = v; sync(); });
		Motion.sub(sync);
	}

	// ---- The roadmap's line, drawn as far as the work has come -----------------------

	function initTimeline() {
		const list = $('[data-timeline]');
		if (!list) return;
		const now = $('.phase.is-now', list);
		updateTimeline = function () {
			const r = list.getBoundingClientRect();
			if (!r.height) return;
			// The line stops at the phase that is in progress.
			const reach = now ? clamp((now.offsetTop + 14) / r.height, 0, 1) : 1;
			let p = reach;
			if (Motion.on) p = Math.min(reach, clamp((window.innerHeight * 0.66 - r.top) / r.height, 0, 1));
			list.style.setProperty('--p', p.toFixed(4));
		};
		updateTimeline();
		Motion.sub(updateTimeline);
		onResize(updateTimeline);
	}

	// ---- The switch in the footer ------------------------------------------------------

	function initMotionSwitch() {
		const button = $('[data-motion-switch]');
		if (button) {
			const show = () => button.setAttribute('aria-pressed', Motion.on ? 'true' : 'false');
			button.addEventListener('click', () => Motion.set(!Motion.on, true));
			Motion.sub(show);
			show();
		}
		// Until the reader has chosen, the system's setting decides.
		if (!window.matchMedia) return;
		const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
		const follow = (e) => {
			let chosen = null;
			try { chosen = localStorage.getItem('ps2n-motion'); } catch (err) { /* private mode */ }
			if (chosen !== 'on' && chosen !== 'off') Motion.set(!e.matches, false);
		};
		if (mq.addEventListener) mq.addEventListener('change', follow);
		else if (mq.addListener) mq.addListener(follow);
	}

	[
		initTimeline,
		initNav,
		initReveal,
		initScramble,
		initCounters,
		initSpot,
		initDissolve,
		initField,
		initLogo,
		initTranslate,
		initSync,
		initMotionSwitch,
	].forEach(safely);
})();
