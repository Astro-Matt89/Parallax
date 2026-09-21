// Stateful DOM emulation for running the oracle's UI-driven code headless — in particular
// buildFixtureBattery(), which configures each scenario by writing into the page controls.
//
// Every <input> and <select> with an id starts from its HTML default and keeps the values written
// to it, with the browser rules that matter for the battery:
//   - range inputs clamp to [min, max] and snap to `step` (default 1); changing min/max re-sanitises
//     the current value (so slDur = 300 on a max-24 slider reads back as 24, as in a browser);
//   - a select keeps a value only if one of its options has it, otherwise it reads back as "";
//   - checkboxes keep `checked`.
// Everything else (canvas, labels, audio, events) is inert. Event listeners are never fired:
// writing .value does not trigger them, exactly as in a browser.

'use strict';

const { makeSandbox, makeStub } = require('./oracle');

function parseAttributes(tag) {
    const attrs = {};
    const body = tag.replace(/^<\w+/, '').replace(/\/?>$/, '');
    const re = /([a-zA-Z_:][-a-zA-Z0-9_:.]*)(?:\s*=\s*"([^"]*)")?/g;
    let m;
    while ((m = re.exec(body))) attrs[m[1].toLowerCase()] = m[2] === undefined ? '' : m[2];
    return attrs;
}

function parseControls(html) {
    const controls = new Map();
    for (const m of html.matchAll(/<input\b[^>]*>/gi)) {
        const attrs = parseAttributes(m[0]);
        if (attrs.id) controls.set(attrs.id, { tag: 'input', attrs });
    }
    for (const m of html.matchAll(/<select\b([^>]*)>([\s\S]*?)<\/select>/gi)) {
        const attrs = parseAttributes('<select' + m[1] + '>');
        if (!attrs.id) continue;
        const options = [...m[2].matchAll(/<option\b([^>]*)>([\s\S]*?)<\/option>/gi)].map((o) => {
            const oa = parseAttributes('<option' + o[1] + '>');
            return { value: oa.value !== undefined ? oa.value : o[2].trim(), selected: 'selected' in oa };
        });
        controls.set(attrs.id, { tag: 'select', attrs, options });
    }
    return controls;
}

function rangeSanitizer(spec) {
    const bounds = {
        min: spec.attrs.min !== undefined ? parseFloat(spec.attrs.min) : 0,
        max: spec.attrs.max !== undefined ? parseFloat(spec.attrs.max) : 100,
        step: spec.attrs.step === 'any' ? null : (spec.attrs.step !== undefined ? parseFloat(spec.attrs.step) : 1),
    };
    const sanitize = (raw) => {
        const { min, step } = bounds;
        const max = bounds.max < min ? min : bounds.max;
        let v = parseFloat(raw);
        if (!Number.isFinite(v)) v = min + (max - min) / 2;
        if (step) v = min + Math.round((v - min) / step) * step;
        if (v > max) v = step ? min + Math.floor((max - min) / step) * step : max;
        if (v < min) v = min;
        return String(v);
    };
    return { bounds, sanitize };
}

function makeElement(id, spec, inert) {
    const state = { id, textContent: '', innerHTML: '', className: '', disabled: false, style: {}, dataset: {},
        width: 800, height: 600 };
    const kind = !spec ? 'generic' : (spec.tag === 'select' ? 'select' : (spec.attrs.type || 'text').toLowerCase());
    state.type = kind === 'select' ? 'select-one' : kind;

    const range = kind === 'range' ? rangeSanitizer(spec) : null;
    const options = (spec && spec.options) || [];
    let value = '';
    let checked = false;
    let selectedIndex = -1;
    if (range) value = range.sanitize(spec.attrs.value);
    else if (kind === 'checkbox') { checked = 'checked' in spec.attrs; value = spec.attrs.value ?? 'on'; }
    else if (kind === 'select') { selectedIndex = options.findIndex((o) => o.selected); if (selectedIndex < 0 && options.length) selectedIndex = 0; }
    else if (spec) value = spec.attrs.value ?? '';

    const noop = () => {};
    const handler = {
        get(_t, p) {
            switch (p) {
                case 'value': return kind === 'select' ? (selectedIndex >= 0 ? options[selectedIndex].value : '') : value;
                case 'checked': return checked;
                case 'min': return range ? String(range.bounds.min) : '';
                case 'max': return range ? String(range.bounds.max) : '';
                case 'selectedIndex': return selectedIndex;
                case 'options': return options;
                case 'classList': return { toggle: () => false, add: noop, remove: noop, contains: () => false };
                case 'addEventListener': case 'removeEventListener': case 'appendChild': case 'removeChild':
                case 'setAttribute': case 'focus': case 'blur': case 'click': return noop;
                case 'querySelectorAll': return () => [];
                case 'querySelector': return () => null;
                case 'getBoundingClientRect': return () => ({ left: 0, top: 0, right: 800, bottom: 600, width: 800, height: 600 });
                case 'getContext': return () => inert;
                case 'then': return undefined;
                case Symbol.toPrimitive: return (hint) => (hint === 'number' ? 0 : '');
                default: return p in state ? state[p] : inert;
            }
        },
        set(_t, p, v) {
            switch (p) {
                case 'value':
                    if (kind === 'select') selectedIndex = options.findIndex((o) => o.value === String(v));
                    else value = range ? range.sanitize(v) : String(v);
                    return true;
                case 'checked': checked = Boolean(v); return true;
                case 'min': case 'max':
                    if (range) { range.bounds[p] = parseFloat(v); value = range.sanitize(value); }
                    return true;
                case 'selectedIndex': selectedIndex = v; return true;
                default: state[p] = v; return true;
            }
        },
    };
    return new Proxy({}, handler);
}

/// Sandbox factory for loadOracle(html, { sandbox: makeStatefulSandbox }).
function makeStatefulSandbox(html) {
    const inert = makeStub();
    const controls = parseControls(html);
    const registry = new Map();
    const byId = (id) => {
        if (!registry.has(id)) registry.set(id, makeElement(id, controls.get(id), inert));
        return registry.get(id);
    };
    const sandbox = makeSandbox();
    sandbox.document = {
        getElementById: byId,
        querySelectorAll: () => [],
        querySelector: () => null,
        createElement: () => makeElement(null, null, inert),
        createTextNode: () => ({}),
        addEventListener: () => {},
        body: makeElement('body', null, inert),
        documentElement: makeElement('html', null, inert),
        hidden: false,
    };
    return sandbox;
}

module.exports = { makeStatefulSandbox, parseControls };
