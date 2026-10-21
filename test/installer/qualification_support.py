"""Fail-closed provenance checks for mutable published release qualification."""
def fingerprint(release):
    return sorted((a['id'], a['name'], a['size'], a.get('digest'), a['updated_at'])
                  for a in release['assets'])


def verify_release_unchanged(before, after, expected_sha, before_sha=None, after_sha=None):
    # API-resolved tag commits support releases whose target_commitish is a branch.
    before_sha = before['target_commitish'] if before_sha is None else before_sha
    after_sha = after['target_commitish'] if after_sha is None else after_sha
    if before_sha != expected_sha or after_sha != expected_sha:
        raise RuntimeError('Rolling release source changed during qualification')
    if before['id'] != after['id'] or fingerprint(before) != fingerprint(after):
        raise RuntimeError('Rolling release assets changed during qualification')
