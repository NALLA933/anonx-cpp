// AnonXMusic C++ port — Phase 4 TEST SCAFFOLDING (not for production)
//
// A drop-in, signature-compatible stand-in for TDLib's
// <td/telegram/td_json_client.h>. Placing this directory on the include path
// lets the whole Telegram layer be compiled and unit-tested WITHOUT a real
// TDLib installation and WITHOUT touching the network. The real TDLib header
// declares exactly these four functions with these signatures.
//
// For a production build, do NOT use this header — link against the real
// Td::TdJson library instead (see CMakeLists.txt, ANONX_WITH_TDLIB).

#ifndef TD_TELEGRAM_TD_JSON_CLIENT_H
#define TD_TELEGRAM_TD_JSON_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

int td_create_client_id(void);
void td_send(int client_id, const char *request);
const char *td_receive(double timeout);
const char *td_execute(const char *request);

#ifdef __cplusplus
}
#endif

#endif  // TD_TELEGRAM_TD_JSON_CLIENT_H
