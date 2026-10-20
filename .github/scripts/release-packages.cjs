// Same-repository release PRs can share the push workflow's packages only for an identical merge tree.
// No artifacts are copied or relabeled: the PR links the canonical run's six complete package groups.
const ARTIFACTS = [
  'app-package-macos-universal', 'app-package-windows-x64', 'app-package-windows-arm64',
  'app-package-flatpak', 'app-package-linux-x86_64', 'app-package-linux-aarch64',
];
const PACKAGE_JOBS = [
  'Package metadata', 'Package-manager templates', 'macOS universal', 'Windows x64', 'Windows arm64',
  'Flatpak x86_64', 'Linux x86_64', 'Linux aarch64',
  ...['x86_64-apple-darwin', 'aarch64-apple-darwin', 'x86_64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu']
    .map(target => `librashader runtime / ${target}`),
].map(name => `App packages / ${name}`);

async function sameTree(github, repo, head, merge) {
  if (!merge) return false;
  const commits = await Promise.all([head, merge].map(ref => github.rest.git.getCommit({ ...repo, commit_sha: ref })));
  return commits[0].data.tree.sha === commits[1].data.tree.sha;
}

async function plan({ github, context, core }) {
  core.setOutput('reuse', 'false');
  const pr = context.payload.pull_request;
  const fullName = `${context.repo.owner}/${context.repo.repo}`;
  if (context.eventName !== 'pull_request' || pr?.head.repo?.full_name !== fullName ||
      !pr.head.ref.startsWith('release/')) return;
  if (!await sameTree(github, context.repo, pr.head.sha, context.sha)) {
    core.info('The PR merge tree differs from the release head: keep independent PR package builds.');
    return;
  }
  core.setOutput('reuse', 'true');
  core.info(`Share Release push packages for ${pr.head.ref}@${pr.head.sha}; the PR merge tree is identical.`);
}

function selectRun(runs, pr, repo) {
  return runs.filter(run => run.event === 'push' && run.head_sha === pr.head.sha &&
    run.head_branch === pr.head.ref && run.head_repository?.full_name === repo)
    .sort((a, b) => b.id - a.id)[0];
}

function packageStatus(jobs, artifacts) {
  const packages = jobs.filter(job => job.name.startsWith('App packages / '));
  const failed = packages.find(job => job.status === 'completed' && job.conclusion !== 'success');
  if (failed) throw new Error(`Release package check did not pass: ${failed.name} (${failed.conclusion}).`);
  if (PACKAGE_JOBS.some(name => !packages.some(job => job.name === name && job.status === 'completed'))) return false;
  if (packages.some(job => job.status !== 'completed')) return false;
  const missing = ARTIFACTS.filter(name => !artifacts.some(a => a.name === name && !a.expired && a.size_in_bytes > 0));
  if (missing.length) throw new Error(`Release package artifacts are missing, expired or empty: ${missing.join(', ')}.`);
  return true;
}

async function assertCurrentMerge(github, repo, pr) {
  const { data: live } = await github.rest.pulls.get({ ...repo, pull_number: pr.number });
  const fullName = `${repo.owner}/${repo.repo}`;
  if (live.head.sha !== pr.head.sha || live.head.ref !== pr.head.ref || live.head.repo?.full_name !== fullName)
    throw new Error('The PR head changed; wait for the new head\'s checks.');
  if (!await sameTree(github, repo, pr.head.sha, live.merge_commit_sha))
    throw new Error('The live PR merge tree differs from the release head; fresh independent PR validation is needed.');
}

async function waitForPackages({ github, context, core, sleep = ms => new Promise(resolve => setTimeout(resolve, ms)),
                                  now = Date.now, timeoutMs = 345 * 60000 }) {
  const pr = context.payload.pull_request;
  const repo = context.repo, fullName = `${repo.owner}/${repo.repo}`;
  // Recheck independently: the workflow must never turn a fork or another branch into trusted Release artifacts.
  if (context.eventName !== 'pull_request' || pr?.head.repo?.full_name !== fullName || !pr.head.ref.startsWith('release/'))
    throw new Error('Only same-repository release PRs can share Release packages.');
  const deadline = now() + timeoutMs;
  let previous = '';
  while (now() < deadline) {
    await assertCurrentMerge(github, repo, pr);
    const { data } = await github.rest.actions.listWorkflowRuns({ ...repo, workflow_id: 'release.yml',
      branch: pr.head.ref, event: 'push', head_sha: pr.head.sha, per_page: 100 });
    const run = selectRun(data.workflow_runs, pr, fullName);
    if (run) {
      core.setOutput('run_id', String(run.id));
      const jobs = await github.paginate(github.rest.actions.listJobsForWorkflowRun,
        { ...repo, run_id: run.id, filter: 'latest', per_page: 100 });
      const artifacts = await github.paginate(github.rest.actions.listWorkflowRunArtifacts,
        { ...repo, run_id: run.id, per_page: 100 });
      if (packageStatus(jobs, artifacts)) {
        // A rerun can replace jobs while the API pages are being read. Never approve an older attempt's evidence.
        const { data: current } = await github.rest.actions.getWorkflowRun({ ...repo, run_id: run.id });
        if (current.run_attempt === run.run_attempt && current.head_sha === pr.head.sha) {
          await assertCurrentMerge(github, repo, pr);
          core.info(`All package checks and six artifact groups verified: ${run.html_url}`);
          await core.summary.addRaw(`Shared Release packages: [run ${run.id}](${run.html_url}), head \`${pr.head.sha}\`.\n`).write();
          return;
        }
      } else if (run.status === 'completed') {
        throw new Error(`Release run ${run.id} ended without all required package checks/artifacts.`);
      }
    }
    const status = run ? `Waiting for package checks in Release run ${run.id} (attempt ${run.run_attempt}).` :
      `Waiting for the Release push run for ${pr.head.sha}.`;
    if (status !== previous) core.info(status);
    previous = status;
    await sleep(60000);
  }
  throw new Error('Timed out waiting for this head\'s Release packages; no package check was bypassed.');
}

module.exports = { ARTIFACTS, PACKAGE_JOBS, sameTree, plan, selectRun, packageStatus, waitForPackages };
