#pragma once

// 部署私有凭据文件的启动加载（现场需求：NVR 账号/密码固定，页面不再让用户输入）。
//
// 凭据不写入源码/YAML/Git，只放在部署目录的 0600 私有 .env 文件里：
//   <configDir>/camera-credentials.env    TRAIN_FRONT_RTSP_CREDENTIAL / TRAIN_SIDE_RTSP_CREDENTIAL
//   <configDir>/nvr-credentials.env       TRAIN_NVR_CREDENTIAL
// systemd 的 EnvironmentFile 是第一条路径，这里补上手工启动 / 非 systemd 场景。
// 已存在的环境变量优先，绝不用文件覆盖现场显式注入的值；返回值只报数量，不记录任何值。
#include <QString>

int loadPrivateCredentialFiles(const QString& configDir);
