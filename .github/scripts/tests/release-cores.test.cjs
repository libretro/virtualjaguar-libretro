const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync, spawnSync } = require('node:child_process');
const { MATRIX, CORES, planCores, coreStatus, verifyDownloads } = require('../release-cores.cjs');
const root = path.resolve(__dirname, '../../..');
const head = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim();
function complete() {
  return { jobs: CORES.map(c => ({ name: `build-${c.platform}`, status: 'completed', conclusion: 'success' })),
    artifacts: CORES.flatMap(c => [`ci-core-${c.platform}`, `virtualjaguar_libretro-${c.platform}`]
      .map(name => ({ name, expired: false, size_in_bytes: 123 }))) };
}
async function route(same = true, fork = false) {
  const out = {};
  await planCores({ context: { repo: { owner: 'o', repo: 'r' }, eventName: 'pull_request', sha: 'merge',
    payload: { pull_request: { head: { sha: 'head', ref: 'release/v1', repo: { full_name: fork ? 'fork/r' : 'o/r' } } } } },
    github: { rest: { git: { getCommit: async ({ commit_sha }) => ({ data: { tree: {
      sha: same || commit_sha === 'head' ? 'same' : 'different' } } }) } } },
    core: { setOutput: (k,v) => { out[k] = v; }, info: () => {} } });
  return { ...out, matrix: JSON.parse(out.matrix) };
}
test('matching release PR retains all seven native compiler/test rows and shares only eight matrix rows plus consoles', async () => {
  const r = await route(); assert.equal(r.reuse, 'true'); assert.equal(r.matrix.length, 7);
  assert.equal(MATRIX.length, 15); assert.equal(CORES.length, 10);
  assert.deepEqual(r.matrix, MATRIX.filter(c => !c.release_platform));
  for (const name of ['Linux x86_64 (GCC)', 'Linux x86_64 (Clang)', 'Linux aarch64', 'Linux i686',
    'macOS arm64 (Clang)', 'Windows x86_64 (MSYS2)', 'Windows i686 (MSYS2)'])
    assert(r.matrix.some(c => c.displayTargetName === name));
});
test('different merge trees and forks keep all fifteen original matrix configurations', async () => {
  for (const r of [await route(false), await route(true, true)]) {
    assert.equal(r.reuse, 'false'); assert.deepEqual(r.matrix, MATRIX);
  }
});
test('all ten production jobs and BOTH raw and distributable artifact groups are required', () => {
  const f = complete(); assert(coreStatus(f.jobs, f.artifacts));
  for (const c of CORES) {
    const fewer = f.jobs.filter(j => j.name !== `build-${c.platform}`);
    assert.equal(coreStatus(fewer, f.artifacts), false);
    for (const name of [`ci-core-${c.platform}`, `virtualjaguar_libretro-${c.platform}`])
      assert.throws(() => coreStatus(f.jobs, f.artifacts.filter(a => a.name !== name)), /Missing/);
  }
});
test('failures, cancelled jobs, skipped jobs, stale and empty artifacts cannot approve a core', () => {
  for (const conclusion of ['failure', 'cancelled', 'skipped', 'timed_out']) {
    const f = complete(); f.jobs[0].conclusion = conclusion;
    assert.throws(() => coreStatus(f.jobs, f.artifacts), /did not pass/);
  }
  for (const change of [a => { a.expired = true; }, a => { a.size_in_bytes = 0; }]) {
    const f = complete(); change(f.artifacts[0]); assert.throws(() => coreStatus(f.jobs, f.artifacts), /expired/);
  }
});
function staged() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-core-contract-'));
  for (const c of CORES) {
    const src = path.join(dir, c.platform, c.file); fs.mkdirSync(path.dirname(src), { recursive: true });
    fs.writeFileSync(src, `fixture production bytes for ${c.platform}`);
    execFileSync('bash', [path.join(root, 'scripts/stage-ci-core.sh'), c.platform, src, path.join(dir, `ci-core-${c.platform}`)],
      { cwd: root, env: { ...process.env, GITHUB_SHA: head, TEST_EXPORTS: '0', RELEASE_DEBUG_INFO: '1' } });
  }
  return dir;
}
test('real staging shell and download verifier preserve production bytes and exact provenance', () => {
  const dir = staged(); verifyDownloads(dir, head);
  for (const c of CORES)
    assert.deepEqual(fs.readFileSync(path.join(dir, c.platform, c.file)), fs.readFileSync(path.join(dir, `ci-core-${c.platform}`, c.file)));
});
test('staging verifies the exact commit in a container checkout with different ownership', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-core-owner-'));
  const src = path.join(dir, 'virtualjaguar_libretro_vita.a');
  fs.writeFileSync(src, 'fixture production archive');
  const env = { ...process.env, GIT_TEST_ASSUME_DIFFERENT_OWNER: '1',
    GIT_CONFIG_COUNT: '1', GIT_CONFIG_KEY_0: 'safe.directory', GIT_CONFIG_VALUE_0: '',
    GITHUB_SHA: head, TEST_EXPORTS: '0', RELEASE_DEBUG_INFO: '1' };
  const baseline = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: root, env });
  assert.notEqual(baseline.status, 0);
  assert.match(baseline.stderr.toString(), /dubious ownership/);
  execFileSync('bash', [path.join(root, 'scripts/stage-ci-core.sh'), 'vita', src, path.join(dir, 'out')],
    { cwd: root, env });
  assert.equal(JSON.parse(fs.readFileSync(path.join(dir, 'out/BUILD.json'))).head_sha, head);
  const wrong = spawnSync('bash', [path.join(root, 'scripts/stage-ci-core.sh'), 'vita', src, path.join(dir, 'wrong')],
    { cwd: root, env: { ...env, GITHUB_SHA: 'wrong' } });
  assert.notEqual(wrong.status, 0);
});
test('wrong head, platform, filename, test ABI and build flags are rejected after download', () => {
  const dir = staged(), c = CORES[0], file = path.join(dir, `ci-core-${c.platform}`, 'BUILD.json');
  const meta = JSON.parse(fs.readFileSync(file));
  for (const delta of [{ head_sha: 'old' }, { platform: 'wrong' }, { file: 'other.so' }, { test_exports: 1 }, { release_debug_info: 0 }]) {
    fs.writeFileSync(file, JSON.stringify({ ...meta, ...delta }));
    assert.throws(() => verifyDownloads(dir, head), /Wrong commit/);
  }
  fs.writeFileSync(file, JSON.stringify(meta)); verifyDownloads(dir, head);
});
test('tampered or empty production bytes fail checksum verification', () => {
  const dir = staged(), c = CORES[0], file = path.join(dir, `ci-core-${c.platform}`, c.file);
  for (const bytes of ['changed', '']) {
    fs.writeFileSync(file, bytes); assert.throws(() => verifyDownloads(dir, head), /checksum mismatch/);
  }
});
test('staging rejects the wide test ABI and a wrong workflow commit', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-core-stage-')), c = CORES[0], src = path.join(dir, c.file);
  fs.writeFileSync(src, 'binary fixture');
  for (const delta of [{ TEST_EXPORTS: '1' }, { GITHUB_SHA: 'old' }, { RELEASE_DEBUG_INFO: '0' }]) {
    const r = spawnSync('bash', [path.join(root, 'scripts/stage-ci-core.sh'), c.platform, src, path.join(dir, 'out')],
      { cwd: root, env: { ...process.env, GITHUB_SHA: head, TEST_EXPORTS: '0', RELEASE_DEBUG_INFO: '1', ...delta } });
    assert.notEqual(r.status, 0);
  }
});
test('all original compile-only artifact aliases remain in C/C++ CI, with native tests and ABI transitions', () => {
  const ci = fs.readFileSync(path.join(root, '.github/workflows/c-cpp.yml'), 'utf8');
  for (const c of CORES) { assert(ci.includes(`name: '${c.name}'`)); assert(ci.includes(`shared-cores/ci-core-${c.platform}/${c.file}`)); }
  for (const gate of ['Run cheat engine unit tests', 'Verify TEST_EXPORTS -> production mode switch relinks',
    'MSVC ${{ matrix.arch }} compilation check', 'AddressSanitizer + UndefinedBehaviorSanitizer',
    'Code coverage (gcov + codecov)', 'clang-tidy on changed files']) assert(ci.includes(gate), gate);
});
test('both Release publishers exclude raw intermediates; shipped core and debug artifact upload stays intact', () => {
  const release = fs.readFileSync(path.join(root, '.github/workflows/release.yml'), 'utf8');
  assert.equal((release.match(/-not -path 'artifacts\/ci-core-\*'/g) || []).length, 2);
  assert(release.includes('name: virtualjaguar_libretro-${{ matrix.config.platform }}'));
  assert(release.includes('path: dist/'));
  assert(release.includes('Verify compile-only Mach-O production exports'));
  assert(release.includes('bash scripts/build-ndk-ci.sh'));
});
function ndkCase(mode) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-ndk-check-'));
  fs.mkdirSync(path.join(dir, 'scripts')); fs.writeFileSync(path.join(dir, 'scripts/gen-version-h.sh'), '#!/bin/sh\nexit 0\n');
  const sdk = path.join(dir, 'sdk'); fs.mkdirSync(sdk);
  fs.writeFileSync(path.join(sdk, 'ndk-build'), `#!/bin/sh
case "$*" in
  *-n*) echo 'clang -c src/tom/blitter.c ${mode === 'missing-define' ? '' : '-DBLITTER_SIMD_NEON'}' ;;
  *) mkdir -p obj/local/arm64-v8a; touch obj/local/arm64-v8a/blitter_simd_${mode === 'wrong-object' ? 'scalar' : 'neon'}.o ;;
esac
`, { mode: 0o755 });
  return spawnSync('bash', [path.join(root, 'scripts/build-ndk-ci.sh'), sdk, 'arm64-v8a', 'neon'], { cwd: dir, encoding: 'utf8' });
}
test('shared NDK builder enforces compile definition and selected SIMD object', () => {
  assert.equal(ndkCase('good').status, 0);
  assert.notEqual(ndkCase('missing-define').status, 0);
  assert.notEqual(ndkCase('wrong-object').status, 0);
});
test('the shared wait loop requires core evidence, not the independent app package set', async () => {
  const { waitForCores } = require('../release-cores.cjs');
  const f = complete(); let approved = false;
  const pr = { number: 1, head: { sha: 'head', ref: 'release/v1', repo: { full_name: 'o/r' } }, merge_commit_sha: 'merge' };
  const run = { id: 42, head_sha: 'head', head_branch: 'release/v1', head_repository: { full_name: 'o/r' },
    event: 'push', run_attempt: 1, status: 'in_progress', html_url: 'https://example/42' };
  const github = { rest: {
    git: { getCommit: async () => ({ data: { tree: { sha: 'tree' } } }) },
    pulls: { get: async () => ({ data: pr }) },
    actions: { listWorkflowRuns: async () => ({ data: { workflow_runs: [run] } }),
      listJobsForWorkflowRun: 'jobs', listWorkflowRunArtifacts: 'artifacts',
      getWorkflowRun: async () => ({ data: run }) },
  }, paginate: async method => method === 'jobs' ? f.jobs : f.artifacts };
  const options = { github, context: { eventName: 'pull_request', repo: { owner: 'o', repo: 'r' }, payload: { pull_request: pr } },
    core: { setOutput: () => {}, info: () => {}, summary: { addRaw: () => ({ write: async () => { approved = true; } }) } } };
  await waitForCores(options); assert(approved);
  approved = false; f.jobs[0].conclusion = 'failure';
  await assert.rejects(waitForCores(options), /did not pass/); assert.equal(approved, false);
});
test('release staging excludes raw CI cores and runtimes while keeping shipped binaries and debug archives', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-release-stage-'));
  for (const name of ['ci-core-vita/private-raw.a', 'librashader-aarch64/runtime.so',
    'virtualjaguar_libretro-vita/shipped.a', 'virtualjaguar_libretro-vita/shipped-debug.tar.gz']) {
    const p = path.join(dir, 'artifacts', name); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, 'fixture');
  }
  const out = execFileSync('find', ['artifacts/', '-mindepth', '2', '-type', 'f', '-not', '-path', 'artifacts/librashader-*',
    '-not', '-path', 'artifacts/ci-core-*', '-print'], { cwd: dir, encoding: 'utf8' });
  assert(out.includes('shipped.a')); assert(out.includes('shipped-debug.tar.gz'));
  assert(!out.includes('private-raw')); assert(!out.includes('runtime.so'));
});
