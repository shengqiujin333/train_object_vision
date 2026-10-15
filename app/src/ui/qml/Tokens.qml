// 由 app/tools/gen_tokens.py 从 docs/hmi/design_tokens.yaml 生成；请勿手工编辑。
pragma Singleton
import QtQuick 2.15

QtObject {
    id: tokens
    property QtObject meta: QtObject {
        property string theme: "dark"
        property var reference_resolution: [1920, 1080]
        property var min_resolution: [1280, 720]
        property int grid: 8
        property string scale_policy: "proportional"
        property string density: "comfortable"
        property string interaction_design: "v2"
        property string spectrum: "visible_only"
    }
    property QtObject color: QtObject {
        property color bg_base: "#0E1116"
        property color bg_sunken: "#0A0D11"
        property color surface: "#161B22"
        property color surface_elev: "#1F2630"
        property color surface_hover: "#232B36"
        property color border: "#2D3644"
        property color border_strong: "#3A4552"
        property color overlay_scrim: Qt.rgba(0.023529411764705882, 0.03529411764705882, 0.047058823529411764, 0.72)
        property color text_primary: "#E6EDF3"
        property color text_secondary: "#9BA7B4"
        property color text_muted: "#6E7A88"
        property color text_disabled: "#4C5764"
        property color text_on_solid: "#FFFFFF"
        property color accent: "#3B82F6"
        property color accent_hover: "#2F6FE0"
        property color accent_pressed: "#2563C9"
        property color focus_ring: "#58A6FF"
        property color link: "#79B8FF"
        property color ok: "#3FB950"
        property color ok_bg: "#12261A"
        property color info: "#4C9AFF"
        property color info_bg: "#12203A"
        property color degraded: "#E3B341"
        property color degraded_bg: "#2A2312"
        property color error: "#E5484D"
        property color error_fg: "#FF6B6B"
        property color error_bg: "#331114"
        property color neutral: "#8B949E"
        property color neutral_bg: "#1B2027"
    }
    property QtObject alarm: QtObject {
        property QtObject L1: QtObject {
            property string label: "一级"
            property string glyph: "triangle"
            property color accent: "#FF6B6B"
            property color solid_bg: "#C42B1C"
            property color tint_bg: "#3A1518"
            property color border: "#E5484D"
            property color text_on_solid: "#FFFFFF"
            property string sound: "required"
            property real pulse_hz: 1.5
            property string persist: "until_ack"
        }
        property QtObject L2: QtObject {
            property string label: "二级"
            property string glyph: "circle"
            property color accent: "#F0883E"
            property color solid_bg: "#B45309"
            property color tint_bg: "#3A2612"
            property color border: "#F0883E"
            property color text_on_solid: "#FFFFFF"
            property string sound: "short"
            property int pulse_hz: 0
            property string persist: "until_dismiss_or_ack"
        }
        property QtObject L3: QtObject {
            property string label: "三级"
            property string glyph: "diamond"
            property color accent: "#E3B341"
            property color solid_bg: "#8A6D00"
            property color tint_bg: "#332A12"
            property color border: "#E3B341"
            property color text_on_solid: "#FFFFFF"
            property string sound: "none"
            property int pulse_hz: 0
            property string persist: "auto_dismiss_10s"
        }
    }
    property QtObject stream_state: QtObject {
        property QtObject live: QtObject {
            property string label: "在线"
            property color fg: "#3FB950"
            property color bg: "#12261A"
            property string glyph: "dot"
            property bool placeholder: false
        }
        property QtObject connecting: QtObject {
            property string label: "连接中"
            property color fg: "#4C9AFF"
            property color bg: "#12203A"
            property string glyph: "spinner"
            property bool placeholder: true
        }
        property QtObject reconnecting: QtObject {
            property string label: "重连中"
            property color fg: "#4C9AFF"
            property color bg: "#12203A"
            property string glyph: "spinner"
            property bool placeholder: true
        }
        property QtObject frozen: QtObject {
            property string label: "画面冻结"
            property color fg: "#E3B341"
            property color bg: "#2A2312"
            property string glyph: "snowflake"
            property bool placeholder: true
        }
        property QtObject degraded: QtObject {
            property string label: "画面降级"
            property color fg: "#E3B341"
            property color bg: "#2A2312"
            property string glyph: "warning"
            property bool placeholder: false
        }
        property QtObject offline: QtObject {
            property string label: "离线"
            property color fg: "#FF6B6B"
            property color bg: "#331114"
            property string glyph: "slash"
            property bool placeholder: true
        }
    }
    property QtObject mode: QtObject {
        property QtObject visible: QtObject {
            property string label: "可见光"
            property color fg: "#8B949E"
            property color bg: "#1B2027"
            property string glyph: "dot"
            property bool interactive: false
        }
        property QtObject ir: QtObject {
            property string label: "红外未接入"
            property color fg: "#4C5764"
            property string bg: "transparent"
            property string glyph: "slash"
            property bool interactive: false
        }
    }
    property QtObject integration_state: QtObject {
        property QtObject probing: QtObject {
            property string label: "探测中"
            property color fg: "#4C9AFF"
            property color bg: "#12203A"
            property string glyph: "spinner"
        }
        property QtObject reachable: QtObject {
            property string label: "可达"
            property color fg: "#3FB950"
            property color bg: "#12261A"
            property string glyph: "dot"
        }
        property QtObject unreachable: QtObject {
            property string label: "不可达"
            property color fg: "#FF6B6B"
            property color bg: "#331114"
            property string glyph: "slash"
        }
        property QtObject unconfigured: QtObject {
            property string label: "未配置"
            property color fg: "#8B949E"
            property color bg: "#1B2027"
            property string glyph: "minus"
        }
        property QtObject unverified: QtObject {
            property string label: "未核验"
            property color fg: "#8B949E"
            property color bg: "#1B2027"
            property string glyph: "help"
        }
    }
    property QtObject detection_badge: QtObject {
        property QtObject base: QtObject {
            property color fg: "#9BA7B4"
            property color bg: "#1B2027"
        }
        property QtObject none: QtObject {
            property color fg: "#6E7A88"
            property color bg: "#1B2027"
            property string label: "广角·无检测"
        }
        property var profiles: ["前方障碍/人员", "受电弓三状态", "轮轨", "车门", "广角·无检测"]
    }
    property QtObject typography: QtObject {
        property QtObject family: QtObject {
            property string ui: "\"Noto Sans CJK SC\",\"Source Han Sans SC\",\"WenQuanYi Micro Hei\",\"Noto Sans\",sans-serif"
            property string mono: "\"Noto Sans Mono\",\"DejaVu Sans Mono\",monospace"
        }
        property QtObject scale: QtObject {
            property QtObject display: QtObject {
                property int size: 32
                property int line: 40
                property int weight: 700
            }
            property QtObject h1: QtObject {
                property int size: 24
                property int line: 32
                property int weight: 700
            }
            property QtObject h2: QtObject {
                property int size: 20
                property int line: 28
                property int weight: 500
            }
            property QtObject body_lg: QtObject {
                property int size: 18
                property int line: 26
                property int weight: 400
            }
            property QtObject body: QtObject {
                property int size: 16
                property int line: 24
                property int weight: 400
            }
            property QtObject label: QtObject {
                property int size: 14
                property int line: 20
                property int weight: 500
            }
            property QtObject caption: QtObject {
                property int size: 12
                property int line: 16
                property int weight: 400
            }
        }
        property int min_driver_text: 16
        property int min_alarm_title: 24
        property string numeral: "mono"
    }
    property QtObject spacing: QtObject {
        property int s0: 0
        property int s1: 4
        property int s2: 8
        property int s3: 12
        property int s4: 16
        property int s5: 24
        property int s6: 32
        property int s7: 48
        property int s8: 64
        property QtObject safe_area: QtObject {
            property int _1920x1080: 24
            property int _1280x720: 16
        }
    }
    property QtObject radius: QtObject {
        property int chip: 4
        property int button: 6
        property int card: 8
        property int panel: 8
        property int modal: 12
        property int pill: 999
    }
    property QtObject border: QtObject {
        property int width: 1
        property int width_emphasis: 2
        property int alarm_width: 3
    }
    property QtObject elevation: QtObject {
        property string _0: "none"
        property string _1: "0 1px 2px rgba(0,0,0,0.4)"
        property string _2: "0 4px 12px rgba(0,0,0,0.5)"
        property string _3: "0 12px 32px rgba(0,0,0,0.6)"
    }
    property string radius_note: "border+表面层级优先于阴影"
    property QtObject size: QtObject {
        property int top_bar_height: 48
        property int nav_width: 88
        property int status_strip_height: 64
        property int alarm_bar_height: 64
        property int touch_target_min: 44
        property QtObject button: QtObject {
            property int lg: 44
            property int md: 36
            property int sm: 32
        }
        property QtObject camera_tile: QtObject {
            property real front_min_area_ratio: 0.34
            property var side_area_ratio: [0.22, 0.3]
        }
    }
    property QtObject motion: QtObject {
        property string fast: "120ms"
        property string normal: "200ms"
        property string slow: "320ms"
        property string easing: "cubic-bezier(0.2,0,0,1)"
        property real pulse_min_hz: 1.0
        property real pulse_max_hz: 3.0
        property string reduce_motion: "respect_os_setting"
    }
    property QtObject z_index: QtObject {
        property int base: 0
        property int camera_overlay: 10
        property int top_bar: 100
        property int nav: 100
        property int auto_popup: 300
        property int alarm_banner: 400
        property int modal: 500
        property int toast: 600
    }
    property QtObject icon: QtObject {
        property int grid: 24
        property int stroke: 2
        property string style: "outline"
        property bool status_filled: true
        property string set_rule: "使用开源许可图标集（如 Lucide ISC / Material Symbols Apache-2.0）或项目自绘 SVG；须登记来源与许可，禁止未授权素材"
        property var required_glyphs: ["camera", "camera-off", "bell", "replay-clock", "grid-6", "cpu-ai", "wrench", "check", "warning-triangle", "circle", "diamond", "play", "pause", "speed", "export", "lock", "user", "clock", "thermometer", "disk", "network", "server", "minus-circle", "chevron-left", "chevron-right", "close", "search", "filter"]
    }
    property var contrast_verified_pairs: [{ fg: "#E6EDF3", bg: "#0E1116", ratio: 16.0 }, { fg: "#9BA7B4", bg: "#0E1116", ratio: 7.73 }, { fg: "#E6EDF3", bg: "#161B22", ratio: 14.64 }, { fg: "#E6EDF3", bg: "#1F2630", ratio: 12.9 }, { fg: "#9BA7B4", bg: "#1F2630", ratio: 6.23 }, { fg: "#FFFFFF", bg: "#C42B1C", ratio: 5.66 }, { fg: "#FFFFFF", bg: "#B45309", ratio: 5.02 }, { fg: "#FFFFFF", bg: "#8A6D00", ratio: 4.92 }, { fg: "#3FB950", bg: "#12261A", ratio: 6.27 }, { fg: "#FF6B6B", bg: "#331114", ratio: 6.15 }, { fg: "#FF6B6B", bg: "#0E1116", ratio: 6.81 }, { fg: "#4C9AFF", bg: "#12203A", ratio: 5.7 }, { fg: "#E3B341", bg: "#2A2312", ratio: 8.01 }, { fg: "#0E1116", bg: "#58A6FF", ratio: 7.49 }]
}
