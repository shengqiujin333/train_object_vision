#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-013 V-CMP v2: fresh 复跑 vs impl r3 归档/独立断言(不采信交付 acceptance 行)。

默认裁判 fresh verify v2 归档;--dry-run 时以 impl r3 归档当 fresh 预演(上板前零成本抓构造错)。
"""
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
OIDIR = HERE
ARCH = REPO / 'docs' / 'vision' / 'test_evidence' / 'SELFTEST'

V2 = 'verify-20261021-item013-v2'
TAG_FRESH = 'vision-item013-192_168_0_103-' + V2
IMPL_R3 = '20261021-item013-r3'

checks = []
def ck(name, ok):
    checks.append((name, bool(ok)))

def pick(mode, key):
    if mode == 'fresh':
        return HERE / (TAG_FRESH + '.' + key)
    return HERE / ('vision-item013-192_168_0_103-' + IMPL_R3 + '.' + key)

def run(mode):
    checks.clear()

    # ---- DET 恒等(同版本两次运行确定 + 与 impl r3 跨轮恒等) ----
    impl_det = (ARCH / ('vision-selftest-192_168_0_103-%s.deterministic.json' % IMPL_R3)).read_bytes()
    if mode == 'fresh':
        f1 = (HERE / (TAG_FRESH + '.app_core-evidence_item013-selftest-det1-%s.json' % V2)).read_bytes()
        f2 = (HERE / (TAG_FRESH + '.app_core-evidence_item013-selftest-det2-%s.json' % V2)).read_bytes()
        ck('det_fresh_det1_vs_impl_r3_identical', f1 == impl_det)
        ck('det_fresh_det1_vs_det2_identical', f1 == f2)

    # ---- fresh run log ----
    suffix = ('%s.app_core-evidence_item013-selftest-%s.log' % (TAG_FRESH, V2)
              if mode == 'fresh' else
              'vision-item013-192_168_0_103-%s.app_core-evidence_item013-selftest-%s.log'
              % (IMPL_R3, IMPL_R3))
    fresh_run = (OIDIR / suffix).read_text(encoding='utf-8', errors='replace')
    ck('summary_17_pass', 'type=summary total=17 passed=17 failed=0' in fresh_run)
    ck('result_pass', 'RESULT: PASS failures=0' in fresh_run)
    ck('pending_lines_4', len(re.findall(r'^type=pending text=', fresh_run, re.M)) == 4)
    ck('no_device_true', 'no_device=true' in fresh_run)
    # D-01 修复反转: FI-AI-12 现在必须在归档运行日志中可见(v1 时缺席)
    ck('case_unknown_label_pass',
       '[PASS] semantic/semantic.unknown_label_intercepted' in fresh_run)
    ck('fi_ai_12_present_in_selftest', 'FI-AI-12' in fresh_run)
    ev_lines = [l for l in fresh_run.splitlines() if l.strip().startswith('evidence ')]
    ck('verbose_evidence_lines_ge30', len(ev_lines) >= 30)

    # ---- fresh json ----
    jsuf = ('%s.app_core-evidence_item013-selftest-%s.json' % (TAG_FRESH, V2)
            if mode == 'fresh' else
            'vision-item013-192_168_0_103-%s.app_core-evidence_item013-selftest-%s.json'
            % (IMPL_R3, IMPL_R3))
    fresh_json = (OIDIR / jsuf).read_text(encoding='utf-8')
    j = json.loads(fresh_json)
    det = json.loads(impl_det.decode('utf-8'))
    ck('json_summary_17', j['summary'] == {'total': 17, 'passed': 17, 'failed': 0})
    ck('det_json_summary_matches', det['summary'] == j['summary'])
    ck('archive_json_has_timestamp', 'timestamp_ms' in j)
    ck('det_json_no_timestamp', 'timestamp_ms' not in det)
    ck('json_pending_4', len(j['pending']) == 4)
    for lbl, s in (('arch', fresh_json), ('det', json.dumps(det))):
        ck('json_%s_no_addr_url_cred' % lbl,
           '192.168.' not in s and 'rtsp://' not in s and 'TRAIN_' not in s)
    ck('json_has_fi_ai_12', 'FI-AI-12' in fresh_json)
    ck('json_cases_17', len(j['cases']) == 17)

    ev = {c['id']: c.get('evidence', {}) for c in j['cases']}
    sem = ev['semantic.unknown_label_intercepted']
    ck('sem_blocked_1', sem['blocked_unknown_label'] == '1')
    ck('sem_mapped_1', sem['mapped'] == '1')
    ck('sem_detections_only_person',
       sem['detections'] == '1' and sem['event_type'] == 'person_on_rail'
       and sem['events'] == '1' and sem['input_boxes'] == '2')
    ck('box_x_exact', ev['detect.synthetic_box']['x'] == '0.498437'
       and ev['detect.synthetic_box']['expected_x'] == '0.498437')
    ck('mask_positive_14400_full_size',
       ev['seg.synthetic_mask']['positive'] == '14400'
       and ev['seg.synthetic_mask']['mask_w'] == '640'
       and ev['seg.synthetic_mask']['mask_h'] == '640')
    ck('pad_y_140_scale_0p5',
       ev['preprocess.synthetic_letterbox']['pad_y'] == '140'
       and ev['preprocess.synthetic_letterbox']['scale'].startswith('0.5'))
    ck('bytes_640x640x3', ev['preprocess.synthetic_letterbox']['bytes'] == str(640 * 640 * 3))
    ck('two_stage_hits', ev['two_stage.person_on_mask']['alarm_hits'] == '1'
       and ev['two_stage.person_on_mask']['mask_hit'] == '1')
    ck('envelope_counters',
       ev['envelope.stale_duplicate_out_of_order']['accepted'] == '2'
       and all(ev['envelope.stale_duplicate_out_of_order'][k] == '1'
               for k in ('stale_generation', 'duplicate_seq', 'out_of_order_seq')))
    ck('ai_stopped_text', ev['availability.ai_stopped']['status_text'] == 'AI 不可用')
    ck('mismatched_items_0_structure_identify_early_return',
       ev['model.tensor_mismatch'].get('mismatched_items') == '0')

    # requirement 矩阵(v2): 六个 FI 全覆盖
    req = {c['id']: c.get('requirement', '') for c in j['cases']}
    for fi in ('FI-AI-01', 'FI-AI-02', 'FI-AI-04', 'FI-AI-07', 'FI-AI-10', 'FI-AI-12'):
        ck('requirement_covers_%s' % fi, any(fi in v for v in req.values()))

    # ---- JUnit ----
    xsuf = ('%s.app_core-evidence_item013-core-tests-%s.xml' % (TAG_FRESH, V2)
            if mode == 'fresh' else
            'vision-item013-192_168_0_103-%s.app_core-evidence_item013-core-tests-%s.xml'
            % (IMPL_R3, IMPL_R3))
    xml = (OIDIR / xsuf).read_text(encoding='utf-8', errors='replace')
    m = re.search(r'tests="(\d+)" failures="(\d+)"', xml)
    ck('junit_1452_0', bool(m) and m.group(1) == '1452' and m.group(2) == '0')
    names = re.findall(r'<testcase [^>]*\sname="([^"]+)"', xml)
    names = [n.replace('&gt;', '>') for n in names]
    ck('junit_selftest_domain_ge35', sum(1 for n in names if 'selftest' in n) >= 35)
    ck('junit_fi_ai_12_coverage_assert',
       any('selftest_covers_FI-AI-12' in n for n in names))

    # ---- --list 与 JSON ids 恒等 ----
    lsuf = ('%s.app_core-evidence_item013-selftest-list-%s.txt' % (TAG_FRESH, V2)
            if mode == 'fresh' else
            'vision-item013-192_168_0_103-%s.app_core-evidence_item013-selftest-list-%s.txt'
            % (IMPL_R3, IMPL_R3))
    lst = (OIDIR / lsuf).read_text(encoding='utf-8').split()
    ck('list_ids_match_json', lst == [c['id'] for c in j['cases']])
    ck('list_count_17', len(lst) == 17)

    # ---- media selftest ----
    msuf = ('%s.app_core-evidence_item013-media-selftest-%s.log' % (TAG_FRESH, V2)
            if mode == 'fresh' else
            'vision-item013-192_168_0_103-%s.app_core-evidence_item013-media-selftest-%s.log'
            % (IMPL_R3, IMPL_R3))
    med = (OIDIR / msuf).read_text(encoding='utf-8', errors='replace')
    ck('media_result_ok', re.search(r'^RESULT: PASS failures=0', med, re.M) is not None)

    npass = sum(1 for _, ok in checks if ok)
    for n, ok in checks:
        if not ok:
            print('CKFAIL[%s]' % mode, n)
    print('CMP[%s] %d/%d' % (mode, npass, len(checks)))
    return npass == len(checks)

if __name__ == '__main__':
    mode = 'dry' if (len(sys.argv) > 1 and sys.argv[1] == '--dry-run') else 'fresh'
    ok = run(mode)
    raise SystemExit(0 if ok else 1)
