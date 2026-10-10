/*
 * test/tools/test_option_explain.c
 *
 * E2E test for issue #841: the core tells the player which settings it chose
 * for them, through the frontend's own UI.  Two surfaces, both checked
 * through the shared harness (which records SET_MESSAGE_EXT texts and the
 * option definitions of every SET_CORE_OPTIONS_V2 push):
 *
 *   - OSD notices (SET_MESSAGE_EXT): per-title presets applied at load,
 *     and the enhancement profile withholding enhancement presets;
 *   - option sublabels: the `info` of the affected options amended by a
 *     SET_CORE_OPTIONS_V2 re-push (option COUNT unchanged), restored on
 *     unload, re-pushed only when the amendment SET changes.
 *
 * And the rule underneath both: the user's values are never rewritten
 * (SET_VARIABLE is never called).
 *
 * One process per --case (a fresh core load each, like a frontend restart):
 *
 *   1  White Men Can't Jump (CRC $14915F20), defaults.  Its COMPATIBILITY
 *      row turns blitter bus timing on: exactly one notice naming the title,
 *      the option and "(compatibility preset)"; the blitter_timing info is
 *      amended ("This game: set to on by its per-title preset...") with the
 *      original text kept; one re-push at load, none per frame; the option
 *      count is unchanged; other options' info untouched; unload restores
 *      the original info.
 *   2  Alien vs Predator (CRC $DC187F82), enhancement_profile=performance:
 *      the suppression notice with the profile's reason, no notice claiming
 *      Internal Resolution was applied, the internal_resolution info says
 *      the preset is not applied.
 *   3  yarc.j64 (in-tree), idle_skip=enabled + risc_clock_scale=2x: no DB row
 *      so no preset notice, but the idle-skip info says it is inactive and
 *      names the clock scale.
 *   4  yarc.j64, defaults: the control.  No preset/suppression notice and
 *      NO re-push at all.
 *   6  Alien vs Predator, defaults, with risc_idle_skip=enabled passed
 *      explicitly -- what a real frontend reports for an untouched option
 *      (the harness otherwise answers "unset").  AvP's idle-skip row says
 *      "enabled", EQUAL to the default, so it changes nothing visible: the
 *      notice must name only Internal Resolution, and the idle-skip info
 *      must stay untouched.
 *   5  yarc.j64, changing risc_clock_scale 1x -> 2x -> (no change) -> 1x
 *      mid-session: a re-push exactly when the amendment set changes, the
 *      original info back at the end.
 *
 * Exit status: 0 all PASS, 1 any FAIL.
 *
 * Usage:
 *   ./test/tools/test_option_explain [core] <rom> --case N [--option K=V ...]
 *
 * Build: see the test/tools/test_option_explain rule in the Makefile.
 */

#include "../harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static harness_result mkres(int ok, const char *name, const char *detail)
{
    harness_result r;
    r.status = ok ? "PASS" : "FAIL";
    r.name   = name;
    r.detail = detail;
    return r;
}

/* How many OSD notices contain every one of up to three needles. */
static unsigned osd_matching(const char *a, const char *b, const char *c)
{
    unsigned i, n = 0;
    for (i = 0; i < harness_osd_count(); i++) {
        const char *t = harness_osd_text(i);
        if (a && !strstr(t, a)) continue;
        if (b && !strstr(t, b)) continue;
        if (c && !strstr(t, c)) continue;
        n++;
    }
    return n;
}

static int info_has(const char *key, const char *needle)
{
    const char *info = harness_options_info(key);
    return info && strstr(info, needle) != NULL;
}

/* Latest info == first-push info (i.e. nothing amended / restored). */
static int info_pristine(const char *key)
{
    const char *a = harness_options_first_info(key);
    const char *b = harness_options_info(key);
    return a && b && strcmp(a, b) == 0;
}

static void run_frames(harness_config *cfg, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        harness_step(cfg);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[10];
    unsigned nres = 0, i, before, after;
    int case_num = 0, pass = 1, scale_idx = -1;
    int count_same, one_msg;
    char detail[10][160];

    for (i = 1; (int)i < argc; i++)
        if (strcmp(argv[i], "--case") == 0 && (int)i + 1 < argc)
            case_num = atoi(argv[i + 1]);
    if (case_num < 1 || case_num > 6) {
        fprintf(stderr, "usage: test_option_explain [core] <rom> --case N[1-6] "
                        "[--option KEY=VALUE ...]\n");
        return 1;
    }

    cfg.frames = 60;
    cfg.core_options_v2 = 1;   /* register via V2 so re-pushes are visible */
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (case_num == 5) {
        /* Mutated in place mid-run; GET_VARIABLE returns the first match. */
        harness_set_option(&cfg, "virtualjaguar_risc_idle_skip", "enabled");
        scale_idx = (int)cfg.num_options;
        harness_set_option(&cfg, "virtualjaguar_risc_clock_scale", "1x");
    }

    if (!harness_load_rom(&cfg)) return 1;
    /* The core's initial registration happens in retro_set_environment;
     * everything counted from retro_load_game on is a re-push. */
    before = harness_options_pushes_before_load();
    after  = harness_options_push_count();

    if (case_num != 5)
        harness_run(&cfg);

    switch (case_num) {
    case 1: {
        unsigned late;
        int amended, kept, orig_clean, others_clean;
        const char *first = harness_options_first_info("virtualjaguar_blitter_timing");
        const char *now   = harness_options_info("virtualjaguar_blitter_timing");

        one_msg = (osd_matching("White Men Can't Jump", "Blitter Bus Timing",
                                "(compatibility preset)") == 1);
        snprintf(detail[0], sizeof(detail[0]), "%u OSD notice(s) total",
                 harness_osd_count());
        results[nres++] = mkres(one_msg, "wmcj_compat_preset_notice",
            one_msg ? "one notice naming title, option and class"
                    : detail[0]);

        amended = info_has("virtualjaguar_blitter_timing",
                           "This game: set to on by its per-title preset");
        kept    = first && now && strstr(now, first) != NULL;
        orig_clean = first && !strstr(first, "per-title preset");
        results[nres++] = mkres(amended && kept && orig_clean,
            "wmcj_blitter_timing_info_amended",
            amended && kept && orig_clean
                ? "sentence prepended, original text kept"
                : "blitter_timing info not amended as specified");

        others_clean = info_pristine("virtualjaguar_usefastblitter")
                    && info_pristine("virtualjaguar_internal_resolution");
        results[nres++] = mkres(others_clean, "wmcj_other_options_untouched",
            others_clean ? "unrelated options' info unchanged"
                         : "an unrelated option's info changed");

        count_same = harness_options_def_count_first()
                  == harness_options_def_count_latest();
        results[nres++] = mkres(count_same, "wmcj_option_count_unchanged",
            count_same ? "same number of definitions after the re-push"
                       : "definition count changed (libretro.h forbids it)");

        late = harness_options_push_count();
        snprintf(detail[1], sizeof(detail[1]),
                 "%u re-push(es) at load, %u more over 60 frames",
                 after - before, late - after);
        results[nres++] = mkres(after - before == 1 && late == after,
            "wmcj_repush_once_at_load", detail[1]);

        results[nres++] = mkres(harness_set_variable_calls() == 0,
            "wmcj_user_value_never_rewritten",
            harness_set_variable_calls() == 0
                ? "SET_VARIABLE never called"
                : "core called SET_VARIABLE");

        harness_shutdown(&cfg);
        results[nres++] = mkres(info_pristine("virtualjaguar_blitter_timing"),
            "wmcj_unload_restores_original",
            info_pristine("virtualjaguar_blitter_timing")
                ? "original info pushed back on unload"
                : "amended info still live after unload");
        break;
    }
    case 2: {
        int sup, no_claim, res_info, no_idle;

        one_msg = (osd_matching("per-title enhancement presets not applied",
                                "performance profile selected", NULL) == 1);
        results[nres++] = mkres(one_msg, "avp_suppression_notice",
            one_msg ? "one suppression notice with the profile's reason"
                    : "suppression notice missing or repeated");

        no_claim = (osd_matching("Internal Resolution 2x", NULL, NULL) == 0);
        results[nres++] = mkres(no_claim, "avp_no_false_applied_claim",
            no_claim ? "no notice claims Internal Resolution was applied"
                     : "a notice claims a withheld preset was applied");

        no_idle = (osd_matching("Idle-Loop", NULL, NULL) == 0)
               && info_pristine("virtualjaguar_risc_idle_skip");
        results[nres++] = mkres(no_idle, "avp_default_equal_preset_not_reported",
            no_idle ? "idle-skip row equal to its default is not reported"
                    : "a preset equal to the default was reported as a change");

        res_info = info_has("virtualjaguar_internal_resolution",
                            "per-title preset (2x) is not applied: "
                            "performance profile selected");
        results[nres++] = mkres(res_info, "avp_internal_resolution_info",
            res_info ? "info says the preset is not applied, and why"
                     : "internal_resolution info not amended");

        sup = (harness_options_def_count_first()
               == harness_options_def_count_latest());
        results[nres++] = mkres(sup, "avp_option_count_unchanged",
            sup ? "definition count unchanged"
                : "definition count changed");
        results[nres++] = mkres(harness_set_variable_calls() == 0,
            "avp_user_value_never_rewritten",
            harness_set_variable_calls() == 0 ? "SET_VARIABLE never called"
                                              : "core called SET_VARIABLE");
        break;
    }
    case 3: {
        int inactive, named, no_preset;

        inactive = info_has("virtualjaguar_risc_idle_skip",
                            "Inactive: suppressed by");
        named    = info_has("virtualjaguar_risc_idle_skip", "Clock Scale 2x");
        results[nres++] = mkres(inactive && named, "yarc_idle_skip_inactive_info",
            inactive && named ? "idle-skip info names the suppressing option"
                              : "idle-skip info not amended");
        no_preset = (osd_matching("preset", NULL, NULL) == 0);
        results[nres++] = mkres(no_preset, "yarc_no_preset_notice",
            no_preset ? "no preset notice for a title with no DB row"
                      : "preset notice for a title with no DB row");
        results[nres++] = mkres(after - before == 1, "yarc_one_repush",
            after - before == 1 ? "one re-push at load"
                                : "re-push count != 1");
        results[nres++] = mkres(info_pristine("virtualjaguar_risc_clock_scale"),
            "yarc_suppressor_info_untouched",
            info_pristine("virtualjaguar_risc_clock_scale")
                ? "the suppressing option's own info is unchanged"
                : "suppressing option's info changed");
        break;
    }
    case 4: {
        int none;

        none = (harness_osd_count() == 0
                || (osd_matching("preset", NULL, NULL) == 0
                    && osd_matching("not applied", NULL, NULL) == 0));
        results[nres++] = mkres(none, "yarc_control_no_notice",
            none ? "no preset or suppression notice"
                 : "unexpected notice for a title with no DB row");
        snprintf(detail[2], sizeof(detail[2]),
                 "%u re-push(es) during load", after - before);
        results[nres++] = mkres(after == before, "yarc_control_no_repush",
            after == before ? "no re-push" : detail[2]);
        results[nres++] = mkres(info_pristine("virtualjaguar_risc_idle_skip"),
            "yarc_control_info_pristine",
            info_pristine("virtualjaguar_risc_idle_skip")
                ? "idle-skip info untouched" : "idle-skip info changed");
        break;
    }
    case 6: {
        int named, quiet_idle;

        named = (osd_matching("Internal Resolution 2x", "(enhancement preset)",
                              NULL) == 1);
        quiet_idle = (osd_matching("Idle-Loop", NULL, NULL) == 0)
                  && info_pristine("virtualjaguar_risc_idle_skip");
        results[nres++] = mkres(named, "avp_default_notice_names_resolution",
            named ? "one notice naming only Internal Resolution"
                  : "notice missing or names more than Internal Resolution");
        results[nres++] = mkres(quiet_idle, "avp_default_idle_skip_silent",
            quiet_idle ? "no-op idle-skip preset: no notice, info untouched"
                       : "no-op idle-skip preset was reported");
        results[nres++] = mkres(info_has("virtualjaguar_internal_resolution",
                                         "This game: set to 2x by its "
                                         "per-title preset"),
            "avp_default_resolution_info",
            info_has("virtualjaguar_internal_resolution",
                     "This game: set to 2x by its per-title preset")
                ? "internal_resolution info amended"
                : "internal_resolution info not amended");
        break;
    }
    case 5: {
        unsigned p0, p1, p2, p3;

        run_frames(&cfg, 30);
        p0 = harness_options_push_count();

        cfg.options[scale_idx].value = "2x";
        harness_notify_variable_update();
        run_frames(&cfg, 3);
        p1 = harness_options_push_count();

        run_frames(&cfg, 30);
        harness_notify_variable_update();          /* same values again */
        run_frames(&cfg, 3);
        p2 = harness_options_push_count();

        snprintf(detail[3], sizeof(detail[3]),
                 "pushes: load %u, after 2x %u, after idle+no-change %u",
                 p0 - before, p1 - before, p2 - before);
        results[nres++] = mkres(p0 == before && p1 == before + 1
                                && p2 == p1, "mid_session_repush_on_change_only",
                                detail[3]);
        results[nres++] = mkres(
            info_has("virtualjaguar_risc_idle_skip", "Clock Scale 2x"),
            "mid_session_info_amended",
            info_has("virtualjaguar_risc_idle_skip", "Clock Scale 2x")
                ? "idle-skip info amended after the option change"
                : "idle-skip info not amended");

        cfg.options[scale_idx].value = "1x";
        harness_notify_variable_update();
        run_frames(&cfg, 3);
        p3 = harness_options_push_count();
        snprintf(detail[4], sizeof(detail[4]),
                 "%u push(es) on return to 1x", p3 - p2);
        results[nres++] = mkres(p3 == p2 + 1 && info_pristine(
                                    "virtualjaguar_risc_idle_skip"),
            "mid_session_restored_on_revert", detail[4]);
        results[nres++] = mkres(harness_set_variable_calls() == 0,
            "mid_session_user_value_never_rewritten",
            harness_set_variable_calls() == 0 ? "SET_VARIABLE never called"
                                              : "core called SET_VARIABLE");
        break;
    }
    }

    for (i = 0; i < nres; i++)
        if (strcmp(results[i].status, "PASS") != 0) pass = 0;

    harness_report(&cfg, results, nres);
    if (cfg.core_handle) harness_shutdown(&cfg);
    return pass ? 0 : 1;
}
