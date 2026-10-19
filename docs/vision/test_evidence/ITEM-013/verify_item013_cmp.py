#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-013 独立验证 V-CMP: fresh 复跑 vs 判据独立断言(不采信交付 acceptance 行)。"""
import json, pathlib, re, sys

base = pathlib.Path(__file__).resolve().parents[4]
oidir = base / 'docs' / 'vision' / 'test_evidence' / 'ITEM-013'
arch = base / 'docs' / 'vision' / 'test_evidence' / 'SELFTEST'
TAG = 'vision-item013-192_168_0_103-verify-20261021-item013-v1'

checks = []
def ck(name, ok):
    checks.append((name, bool(ok)))

# DET 恒等
impl_det = (arch / 'vision-selftest-192_168_0_103-20261021-item013-r2.deterministic.json').read_bytes()
fresh_det1 = (oidir / (TAG + '.app_core-evidence_item013-selftest-det1-verify-20261021-item013-v1.json')).read_bytes()
fresh_det2 = (oidir / (TAG + '.app_core-evidence_item013-selftest-det2-verify-20261021-item013-v1.json')).read_bytes()
ck('det_impl_r2_vs_fresh_det1_identical', impl_det == fresh_det1)
ck('det_fresh_det1_vs_det2_identical', fresh_det1 == fresh_det2)

# fresh run log
fresh_run = (oidir / (TAG + '.app_core-evidence_item013-selftest-verify-20261021-item013-v1.log')).read_text(encoding='utf-8', errors='replace')
ck('summary_16_pass', 'type=summary total=16 passed=16 failed=0' in fresh_run)
ck('result_pass', 'RESULT: PASS failures=0' in fresh_run)
ck('pending_lines_4', len(re.findall(r'^type=pending text=', fresh_run, re.M)) == 4)
ck('no_device_true', 'no_device=true' in fresh_run)
ck('mismatch_expected_not_ready', '[PASS] model/model.tensor_mismatch' in fresh_run)
ck('fi_ai_12_absent_in_selftest', 'FI-AI-12' not in fresh_run)
ev_lines = [l for l in fresh_run.splitlines() if l.strip().startswith('evidence ')]
ck('verbose_evidence_lines_ge25', len(ev_lines) >= 25)

# fresh json
fresh_json = (oidir / (TAG + '.app_core-evidence_item013-selftest-verify-20261021-item013-v1.json')).read_text(encoding='utf-8')
j = json.loads(fresh_json)
det = json.loads(fresh_det1.decode('utf-8'))
ck('json_summary_16', j['summary'] == {'total': 16, 'passed': 16, 'failed': 0})
ck('archive_json_has_timestamp', 'timestamp_ms' in j)
ck('det_json_no_timestamp', 'timestamp_ms' not in det)
ck('json_pending_4', len(j['pending']) == 4)
for lbl, s in (('arch', fresh_json), ('det', json.dumps(det))):
    ck('json_%s_no_addr_url_cred' % lbl,
       '192.168.' not in s and 'rtsp://' not in s and 'TRAIN_' not in s)

ev = {c['id']: c.get('evidence', {}) for c in j['cases']}
ck('box_x_exact', ev['detect.synthetic_box']['x'] == '0.498437'
   and ev['detect.synthetic_box']['expected_x'] == '0.498437')
ck('mask_positive_14400_full_size',
   ev['seg.synthetic_mask']['positive'] == '14400'
   and ev['seg.synthetic_mask']['mask_w'] == '640' and ev['seg.synthetic_mask']['mask_h'] == '640')
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

# JUnit
xml = (oidir / (TAG + '.app_core-evidence_item013-core-tests-verify-20261021-item013-v1.xml')).read_text(encoding='utf-8', errors='replace')
m = re.search(r'tests="(\d+)" failures="(\d+)"', xml)
ck('junit_1445_0', bool(m) and m.group(1) == '1445' and m.group(2) == '0')
names = re.findall(r'<testcase [^>]*\sname="([^"]+)"', xml)
names = [n.replace('&gt;', '>') for n in names]
ck('junit_selftest_domain_ge30', sum(1 for n in names if 'selftest' in n) >= 30)

# --list 与 JSON ids 恒等
lst = (oidir / (TAG + '.app_core-evidence_item013-selftest-list-verify-20261021-item013-v1.txt')).read_text(encoding='utf-8').split()
ck('list_ids_match_json', lst == [c['id'] for c in j['cases']])
ck('list_count_16', len(lst) == 16)

# media selftest fresh log
med = (oidir / (TAG + '.app_core-evidence_item013-media-selftest-verify-20261021-item013-v1.log')).read_text(encoding='utf-8', errors='replace')
ck('media_result_ok', re.search(r'^RESULT: PASS failures=0', med, re.M) is not None)

npass = sum(1 for _, ok in checks if ok)
for n, ok in checks:
    if not ok:
        print('CKFAIL', n)
print('CMP_OK %d/%d' % (npass, len(checks)))
