// Regenerate the fixture battery headless with a given oracle, exactly as its
// "⭳ BATTERIA FIXTURE 10b" button does: buildFixtureBattery() serialised with JSON.stringify.
//
//   node tools/oracle_harness/regenerate_battery.js <oracle.html> <out.json> [--reverse] [--perturb]
//
// Comparing two runs byte for byte: normalise the header first. Since oracle v1.8.0 the battery
// carries generatedAt (and userAgent), so two runs of the same oracle always differ in those bytes
// and only in those; strip them before concluding anything about the pipeline.
//
// --reverse  runs the scenarios in reverse order (the fixtures are written back in the normal order):
//            the output must not change if every scenario is self-contained.
// --perturb  moves the UI away from its defaults before generating, the way a user could have left it
//            (site latitude, epoch, moon phase, CLEAN gain, nulling, complexity, Earth-station
//            selection and sandbox array layout): the output must not change if the battery pins
//            everything that reaches the pipeline.

'use strict';

const fs = require('fs');
const path = require('path');
const { loadOracle } = require('./oracle');
const { makeStatefulSandbox } = require('./battery_dom');

function perturbUi(oracle) {
    const $ = oracle.$;
    $('slLat').value = '-43';
    $('slEpoch').value = '50';
    oracle.setEpochFromSlider();     // what the slider's input listener does
    $('slMph').value = '120';
    oracle.setMoonPhaseDeg(120);     // what the slider's input listener does
    $('slGain').value = '25';
    $('ckNull').checked = true;
    $('selCx').value = 'complex';
    // Earth-station selection (the GW checkboxes) and the sandbox array layout (preset buttons).
    oracle.GW.forEach((station, i) => { station.on = i % 2 === 1; });
    oracle.presets('ring');
}

function main() {
    const [htmlArg, outPath, ...flags] = process.argv.slice(2);
    if (!htmlArg || !outPath) {
        console.error('usage: regenerate_battery.js <oracle.html> <out.json> [--reverse] [--perturb]');
        process.exit(2);
    }

    const oracle = loadOracle(path.resolve(htmlArg), { sandbox: makeStatefulSandbox });
    if (flags.includes('--perturb')) perturbUi(oracle);

    const reverse = flags.includes('--reverse');
    if (reverse) oracle.FIXTURE_SCENARIOS.reverse();
    const battery = oracle.buildFixtureBattery();
    if (reverse) {
        oracle.FIXTURE_SCENARIOS.reverse();
        battery.fixtures.reverse();
    }

    fs.writeFileSync(outPath, JSON.stringify(battery));
    console.log(`${path.basename(htmlArg)}${reverse ? ' (reversed)' : ''}${flags.includes('--perturb') ? ' (perturbed UI)' : ''}: `
        + `${battery.fixtures.length} fixtures, version ${battery.version} -> ${outPath}`);
}

main();
