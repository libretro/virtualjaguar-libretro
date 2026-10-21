// Compile-only C/C++ rows share production artifacts; native/Clang/MSVC/sanitizer/test-ABI builds stay independent.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { plan, waitForPackages } = require('./release-packages.cjs');
const MATRIX = require('../ci/core-matrix.json');
const CORES = [
  ...MATRIX.filter(c => c.release_platform).map(c => ({ platform: c.release_platform,
    name: c.displayTargetName, file: path.basename(c.artifact) })),
  { platform: 'vita', name: 'PS Vita', file: 'virtualjaguar_libretro_vita.a' },
  { platform: 'switch', name: 'Nintendo Switch', file: 'virtualjaguar_libretro_libnx.a' },
];

async function planCores(options) {
  const reuse = await plan(options);
  options.core.setOutput('matrix', JSON.stringify(MATRIX.filter(c => !reuse || !c.release_platform)));
}

function coreStatus(jobs, artifacts) {
  const failed = jobs.find(job => CORES.some(c => job.name === `build-${c.platform}`) &&
    job.status === 'completed' && job.conclusion !== 'success');
  if (failed) throw new Error(`Release production build did not pass: ${failed.name} (${failed.conclusion}).`);
  for (const core of CORES) {
    const name = `build-${core.platform}`;
    const job = jobs.find(j => j.name === name);
    if (!job || job.status !== 'completed') return false;
    for (const name of [`ci-core-${core.platform}`, `virtualjaguar_libretro-${core.platform}`]) {
      if (!artifacts.some(a => a.name === name && !a.expired && a.size_in_bytes > 0))
        throw new Error(`Missing, empty or expired production artifact: ${name}.`);
    }
  }
  return true;
}

async function waitForCores(options) {
  return waitForPackages({ ...options, checkStatus: coreStatus, subject: 'production core' });
}

function verifyDownloads(root, head) {
  for (const core of CORES) {
    const dir = path.join(root, `ci-core-${core.platform}`);
    const meta = JSON.parse(fs.readFileSync(path.join(dir, 'BUILD.json'), 'utf8'));
    if (meta.head_sha !== head || meta.platform !== core.platform || meta.file !== core.file ||
        meta.test_exports !== 0 || meta.release_debug_info !== 1)
      throw new Error(`Wrong commit, platform, filename or production ABI for ${core.platform}.`);
    const data = fs.readFileSync(path.join(dir, core.file));
    if (!data.length || crypto.createHash('sha256').update(data).digest('hex') !== meta.sha256)
      throw new Error(`Production binary checksum mismatch for ${core.platform}.`);
  }
}

module.exports = { MATRIX, CORES, planCores, coreStatus, waitForCores, verifyDownloads };
