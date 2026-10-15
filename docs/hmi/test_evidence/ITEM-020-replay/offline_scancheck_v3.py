# -*- coding: utf-8 -*-
"""Offline dry-run v3 part 2: host-side python re-execution of the v3 wiring-scan
script against the DELIVERED SOURCES (same logic the board will run), to catch
expectation-construction errors before spending a board round."""
import re
base = r'app/src/ui'
main = open(base + r'/qml/main.qml', encoding='utf-8').read()
p3 = open(base + r'/qml/pages/P3Replay.qml', encoding='utf-8').read()
p2 = open(base + r'/qml/pages/P2AlarmCenter.qml', encoding='utf-8').read()
mc = open(base + r'/src/main.cpp', encoding='utf-8').read()
chip = open(base + r'/qml/components/FilterChip.qml', encoding='utf-8').read()
bridge = open(base + r'/src/replay_model.cpp', encoding='utf-8').read()


def fbody(text, name):
    m = re.search(r'function\s+%s\s*\([^)]*\)\s*\{' % re.escape(name), text)
    if not m:
        return ''
    i = m.end(); depth = 1; out = []
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                break
        out.append(ch); i += 1
    return ''.join(out)


print('W1_emit_declaration=', bool(re.search(r'signal replayRequested\(string cameraId, double tsWallMs\)', p2)))
print('W1_emission=', 'replayRequested(modelData.cameraId, modelData.tsWallMs)' in p2)
mlocal = re.search(r'function onReplayRequest\([^)]*\)[^{]*\{([^}]*)\}', main)
body = mlocal.group(1) if mlocal else ''
print('W1_handler_params_used_camera=', 'cameraId' in body)
print('W1_handler_params_used_ts=', 'tsWallMs' in body)
print('W1_handler_calls_selectAlarm=', 'selectAlarm' in body)
print('W1_startup_selectAlarm=', 'selectAlarm("CAM-01"' in mc)
print('W1_seedAlarms=', 'seedAlarms' in mc)
print('W1_offsets_injected=', 'setCameraOffsets' in mc)
print('W2_cursor_positions_literal=', '[cursor, cursor, cursor]' in p3)
print('W2_syncState_consumed=', 'syncState' in p3)
print('W2_unavailable_state=', ('同步状态不可用' in p3))
print('V2_objectnames=', all(o in p3 for o in ('timelineTrack', 'timelineCursor', 'alarmSegment', 'timelineGap', 'alarmMarker', 'keyframeStrip')))
print('V2_jump_chips=', all(c in p3 for c in ('-30s', '报警时刻', '+30s', '+60s')))
print('V2_detail_fields=', all(c in p3 for c in ('时间：', '类型：', '级别：', '距离：', '置信度：', '处理状态：', '备注：')))
print('V2_source_note_ui=', 'sourceStatus' in p3)
print('V2_selection_consumed=', all(x in p3 for x in ('selectedCameraId', 'selectedAlarmTs', 'relatedCameras', 'selectedDetail')))
print('V3_bridge_keyframes=', '"keyframes"' in bridge)
print('V3_p3_thumb_repeater=', 'keyframeThumb' in p3)
print('V3_thumb_ts_positioned=', re.search(r'x:\s*page\.xOf\(modelData\.ts\)', p3) is not None)
print('V3_thumb_strip_64=', 'status_strip_height' in p3)
print('V3_sample_keyframes=', 'sampleKeyframes' in p3)
print('V3_gap_hatch_canvas=', bool(re.search(r'objectName:\s*"timelineGap"', p3)) and 'onPaint' in p3 and ('strokeStyle' in p3))
print('V3_bridge_playback=', ('playback()' in bridge or 'QVariantMap RailhmiReplayModel::playback' in bridge))
print('V3_bridge_setplaying=', 'setPlaying' in bridge)
print('V3_bridge_setspeed=', 'setSpeed' in bridge)
print('V3_p3_playback_toggle=', 'playbackToggle' in p3)
print('V3_p3_speed_chips=', all(("speed-%s" % s) in p3 for s in ("0.5", "1", "2")) and 'setSpeed(' in p3 and 'togglePlay' in p3)
print('V3_p3_playback_state=', 'onPlaybackChanged' in p3 and 'playState' in p3)
print('V3_bridge_seg_level=', 'sm["level"]' in bridge)
print('V3_bridge_marker_level=', 'am["level"]' in bridge)
print('V3_p3_level_color_fn=', 'levelColorOf' in p3 and 'levelTintOf' in p3)
print('V3_p3_level_theme=', 'Theme.alarm.L1.accent' in p3 and 'Theme.alarm.L2.accent' in p3 and 'Theme.alarm.L3.accent' in p3)
print('V3_p3_seg_uses_level=', re.search(r'levelColorOf\(modelData\.level\)', p3) is not None)
print('V3_p3_marker_uses_level=', re.search(r'objectName:\s*"alarmMarker"', p3) is not None and re.search(r'color:\s*page\.levelColorOf\(modelData\.level\)', p3) is not None)
print('V3_seg_click=', bool(re.search(r'objectName:.*alarmSegment', p3)) and 'seekTo(modelData.t0)' in p3)
print('V3_gap_click=', 'acceptedButtons: Qt.LeftButton' in p3)
print('V3_marker_click=', 'selectAlarmAt' in p3)
print('V3_full_camera_list=', 'railhmiCameras' in p3 and 'allCams' in p3)
print('V3_detail_record_bar=', 'detailRecordBar' in p3 and 'recordLabel' in p3 and 'recordOk' in p3)
print('V3_record_bar_degraded=', 'degraded_bg' in p3)
print('V3_date_error_path=', 'dateError' in p3 and 'valid === false' in p3)
print('V3_chip_touch_target=', 'touch_target_min' in chip)
print('V3_chip_keyboard=', 'activeFocusOnTab' in chip and ('Keys.onReturnPressed' in chip or 'Keys.onEnterPressed' in chip))
print('V3_track_32=', re.search(r'height:\s*Theme\.size\.button\.sm', p3) is not None and re.search(r'objectName:\s*"timelineTrack"', p3) is not None)
print('V3_cursor_top=', 'cursorTime' in p3 and re.search(r'family\.mono', p3) is not None)
mre = fbody(p3, 'reload')
print('V3_reload_handles_invalid=', 'valid === false' in mre and 'dateError' in mre)
