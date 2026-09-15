// Regenerate the fixture battery headless with a given oracle, exactly as its
// "⭳ BATTERIA FIXTURE 10b" button does: buildFixtureBattery() serialised with JSON.stringify.
//
//   node tools/oracle_harness/regenerate_battery.js <oracle.html> <out.json> [--reverse] [--perturb]
//
// --reverse  runs the scenarios in reverse order (the fixtures are written back in the normal order):
//            the output must not change if every scenario is self-contained.
// --perturb  moves UI controls away from their defaults before generating, the way a user could have
//            left them (site latitude, epoch, moon phase, CLEAN gain, nulling, complexity): the output
//            must not change if the battery pins everything that reaches the pipeline.

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
