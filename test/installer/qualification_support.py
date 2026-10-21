"""Fail-closed provenance checks for mutable published release qualification."""
def fingerprint(release):
    return sorted((a['id'], a['name'], a['size'], a.get('digest'), a['updated_at'])
                  for a in release['assets'])


def verify_release_unchanged(before, after, expected_sha):
    if before['target_commitish'] != expected_sha or after['target_commitish'] != expected_sha:
        raise RuntimeError('Rolling release source changed during qualification')
    if before['id'] != after['id'] or fingerprint(before) != fingerprint(after):
        raise RuntimeError('Rolling release assets changed during qualification')
