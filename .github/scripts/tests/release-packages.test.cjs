const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { ARTIFACTS, PACKAGE_JOBS, plan, selectRun, packageStatus, waitForPackages } = require('../release-packages.cjs');

function fixture() {
  const state = { time: 0, outputs: {}, sleeps: 0, jobs: PACKAGE_JOBS.map(name =>
    ({ name, status: 'completed', conclusion: 'success' })),
    artifacts: ARTIFACTS.map(name => ({ name, expired: false, size_in_bytes: 123 })),
    runs: [], currentAttempt: 1, trees: { head: 'tree', merge: 'tree' } };
  const pr = { number: 1042, head: { sha: 'head', ref: 'release/v3.10.0', repo: { full_name: 'libretro/vj' } },
    merge_commit_sha: 'merge' };
  const run = { id: 42, head_sha: 'head', head_branch: pr.head.ref, event: 'push',
    head_repository: { full_name: 'libretro/vj' }, run_attempt: 1, status: 'in_progress', html_url: 'https://example/run/42' };
  state.runs = [run];
  const context = { eventName: 'pull_request', payload: { pull_request: pr }, sha: 'merge',
    repo: { owner: 'libretro', repo: 'vj' } };
  const actions = {
    listWorkflowRuns: async args => {
      assert.equal(args.workflow_id, 'release.yml'); assert.equal(args.head_sha, pr.head.sha);
      return { data: { workflow_runs: state.runs } };
    },
    listJobsForWorkflowRun: 'jobs', listWorkflowRunArtifacts: 'artifacts',
    getWorkflowRun: async () => {
      if (state.onVerify) state.onVerify();
      return { data: { ...run, run_attempt: state.currentAttempt } };
    },
  };
  const github = { rest: { actions,
    pulls: { get: async () => ({ data: state.live || pr }) },
    git: { getCommit: async ({ commit_sha }) => ({ data: { tree: { sha: state.trees[commit_sha] } } }) },
  }, paginate: async (method, args) => {
    assert.equal(args.run_id, 42);
    if (method === 'jobs') { assert.equal(args.filter, 'latest'); return state.jobs; }
    assert.equal(method, 'artifacts'); return state.artifacts;
  } };
  const core = { setOutput: (k, v) => { state.outputs[k] = v; }, info: () => {},
    summary: { addRaw: () => ({ write: async () => {} }) } };
  const options = { github, context, core, now: () => state.time, timeoutMs: 120000,
    sleep: async ms => { state.time += ms; state.sleeps++; if (state.onSleep) state.onSleep(); } };
  return { state, pr, run, context, options };
}

test('same-repository release PR with an identical merge tree shares packages', async () => {
  const f = fixture(); await plan(f.options); assert.equal(f.state.outputs.reuse, 'true');
});
test('forks, feature PRs, push and dispatch keep independent package handling', async () => {
  for (const change of [f => { f.pr.head.repo.full_name = 'fork/vj'; },
    f => { f.pr.head.ref = 'feature/test'; }, f => { f.context.eventName = 'push'; },
    f => { f.context.eventName = 'workflow_dispatch'; }]) {
    const f = fixture(); change(f); await plan(f.options); assert.equal(f.state.outputs.reuse, 'false');
  }
});
test('different merge content keeps the full PR package build', async () => {
  const f = fixture(); f.state.trees.merge = 'another-tree'; await plan(f.options);
  assert.equal(f.state.outputs.reuse, 'false');
});
test('select only the newest exact-head push from this branch and repository', () => {
  const f = fixture();
  const runs = [f.run, ...[
    { head_sha: 'old' }, { head_branch: 'release/other' }, { event: 'workflow_dispatch' },
    { head_repository: { full_name: 'fork/vj' } },
  ].map((change, i) => ({ ...f.run, id: 100 + i, ...change })), { ...f.run, id: 43 }];
  assert.equal(selectRun(runs, f.pr, 'libretro/vj').id, 43);
});
test('all twelve package/runtime checks and all six artifacts pass', async () => {
  const f = fixture(); assert.equal(PACKAGE_JOBS.length, 12); assert.equal(ARTIFACTS.length, 6);
  await waitForPackages(f.options); assert.equal(f.state.outputs.run_id, '42'); assert.equal(f.state.sleeps, 0);
});
test('a queued package check does not approve artifacts already uploaded', async () => {
  const f = fixture(); f.state.jobs[0].status = 'queued'; f.state.jobs[0].conclusion = null;
  f.state.onSleep = () => { f.state.jobs[0] = { ...f.state.jobs[0], status: 'completed', conclusion: 'success' }; };
  await waitForPackages(f.options); assert.equal(f.state.sleeps, 1);
});
test('failed, cancelled, skipped or timed-out package jobs fail even with all artifacts', () => {
  for (const conclusion of ['failure', 'cancelled', 'skipped', 'timed_out']) {
    const f = fixture(); f.state.jobs[0].conclusion = conclusion;
    assert.throws(() => packageStatus(f.state.jobs, f.state.artifacts), /did not pass/);
  }
});
test('missing, expired and empty artifacts cannot yield green', () => {
  for (const change of [f => { f.state.artifacts.pop(); }, f => { f.state.artifacts[0].expired = true; },
    f => { f.state.artifacts[0].size_in_bytes = 0; }]) {
    const f = fixture(); change(f);
    assert.throws(() => packageStatus(f.state.jobs, f.state.artifacts), /missing, expired or empty/);
  }
});
test('late-added package jobs also must pass', () => {
  const f = fixture(); f.state.jobs.push({ name: 'App packages / New platform', status: 'queued' });
  assert.equal(packageStatus(f.state.jobs, f.state.artifacts), false);
});
test('a completed workflow with absent required jobs fails instead of silently skipping coverage', async () => {
  const f = fixture(); f.state.jobs.pop(); f.run.status = 'completed';
  await assert.rejects(waitForPackages(f.options), /ended without all required/);
});
test('a missing Release run waits for discovery and never creates another run', async () => {
  const f = fixture(); f.state.runs = []; f.state.onSleep = () => { f.state.runs = [f.run]; };
  await waitForPackages(f.options); assert.equal(f.state.sleeps, 1);
});
test('missing Release run times out red', async () => {
  const f = fixture(); f.state.runs = [];
  await assert.rejects(waitForPackages(f.options), /Timed out/); assert.equal(f.state.sleeps, 2);
});
test('superseded PR head or changed merge tree fails before accepting old evidence', async () => {
  for (const change of [f => { f.state.live = { ...f.pr, head: { ...f.pr.head, sha: 'new-head' } }; },
    f => { f.state.trees.merge = 'new-base-tree'; }]) {
    const f = fixture(); change(f); await assert.rejects(waitForPackages(f.options), /PR head changed|merge tree differs/);
  }
});
test('a new rerun attempt invalidates the sampled job evidence', async () => {
  const f = fixture(); f.state.currentAttempt = 2;
  f.state.onSleep = () => { f.run.run_attempt = 2; };
  await waitForPackages(f.options); assert.equal(f.state.sleeps, 1);
});
test('the verifier independently rejects foreign PRs', async () => {
  const f = fixture(); f.pr.head.repo.full_name = 'fork/vj';
  await assert.rejects(waitForPackages(f.options), /Only same-repository/);
});
test('workflow keeps native tests, has both exclusive package paths and points comments at the canonical run', () => {
  const workflow = fs.readFileSync(`${__dirname}/../../workflows/app.yml`, 'utf8');
  assert.match(workflow, /name: App \$\{\{ matrix.config.name \}\}/);
  assert.match(workflow, /name: ThreadSanitizer/);
  assert.match(workflow, /if: needs.package-plan.outputs.reuse == 'true'/);
  assert.match(workflow, /if: github.event_name != 'push' && needs.package-plan.outputs.reuse != 'true'/);
  assert.match(workflow, /PACKAGE_RUN_ID: \$\{\{ needs.package-reuse.outputs.run_id \}\}/);
  assert.match(workflow, /runUrl\}\/artifacts\/\$\{a.id/);
});

test('a head change while reading job evidence cannot be approved', async () => {
  const f = fixture();
  f.state.onVerify = () => { f.state.live = { ...f.pr, head: { ...f.pr.head, sha: 'new-head' } }; };
  await assert.rejects(waitForPackages(f.options), /PR head changed/);
});
