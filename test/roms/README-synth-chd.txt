# Synthetic Jaguar CD CHD fixtures (issue #322)
#
# synth_jagcd.chd            — two-session, CHSE present (pinned chdman, -c none)
# synth_jagcd_nosession.chd  — same layout, no CHSE (chdman 0.288, -c none)
#
# Regenerated from a 4+8 sector silence CUE plus an I2S-swapped ATARI boot
# header in session 2. Uncompressed so tiny FLAC/LZMA CD hunks cannot
# confuse libchdr. Do not recompress with createcd's default codecs.
#
# synth_jagcd_multi.chd      — three tracks, session 1 has TWO tracks
#                               (4+4 silent sectors), session 2 is track 3
#                               (8 sectors, ATARI boot header at +0x42).
#                               Regression fixture for issue #476: CHD's
#                               CHSE metadata is indexed among CHSE entries
#                               themselves (0 = 1st CHSE anywhere in the
#                               file), not by track position, so a session-1
#                               track count other than 1 must not flip the
#                               session boundary one track early. Built
#                               with the pinned chdman, -c none.
