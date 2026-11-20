// Salin file ini menjadi secrets.h, isi nilainya. secrets.h sudah ada di .gitignore.
#pragma once

#define WIFI_SSID  "3"
#define WIFI_PASS  "3339999999999"

#define TB_HOST    "thingsboard.cloud"   // sesuaikan dengan server ThingsBoard kalian
#define DEVICE_ID  "jemuran-esp32-01"
#define TB_TOKEN   "ACCESS_TOKEN_DEVICE"  // unik per perangkat, dari ThingsBoard

// Root CA server ThingsBoard (format PEM). Ambil dengan:
//   openssl s_client -showcerts -connect thingsboard.cloud:8883
// lalu salin sertifikat root/penerbitnya ke bawah ini.
const char* TB_ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
ISI_SERTIFIKAT_ROOT_CA_DI_SINI
-----END CERTIFICATE-----
)EOF";
